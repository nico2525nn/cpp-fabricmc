package cppfm.transform;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * Immutable parser/model for the Fabric Access Widener v2 file format.
 *
 * <p>The reader intentionally keeps the namespace in the model.  Mapping a
 * widener from {@code intermediary} to the runtime namespace is a separate
 * concern; silently treating one namespace as another would widen the wrong
 * class or member.  v1 is accepted as a compatibility input, while v2 keeps
 * the stricter whitespace rules used by Fabric's reader.</p>
 */
public final class AccessWidener {
    public enum Access {
        ACCESSIBLE("accessible"),
        EXTENDABLE("extendable"),
        MUTABLE("mutable");

        private final String name;

        Access(String name) {
            this.name = name;
        }

        public String getName() {
            return name;
        }

        static Access parse(String value, int line) {
            return switch (value.toLowerCase(Locale.ROOT)) {
                case "accessible" -> ACCESSIBLE;
                case "extendable" -> EXTENDABLE;
                case "mutable" -> MUTABLE;
                default -> throw error(line, "unknown access type: " + value);
            };
        }
    }

    public enum TargetKind {
        CLASS("class"),
        FIELD("field"),
        METHOD("method");

        private final String name;

        TargetKind(String name) {
            this.name = name;
        }

        static TargetKind parse(String value, int line) {
            return switch (value) {
                case "class" -> CLASS;
                case "field" -> FIELD;
                case "method" -> METHOD;
                default -> throw error(line, "unsupported target kind: " + value);
            };
        }

        public String getName() {
            return name;
        }
    }

    public record Header(int version, String namespace) {
        public Header {
            if (version != 1 && version != 2) {
                throw new IllegalArgumentException("unsupported access widener version: " + version);
            }
            if (namespace == null || namespace.isEmpty()) {
                throw new IllegalArgumentException("access widener namespace is empty");
            }
        }
    }

    /** One immutable class, field, or method directive. */
    public record Target(Access access, TargetKind kind, String owner, String name,
                         String descriptor, boolean transitive) {
        public Target {
            if (access == null || kind == null || owner == null || name == null || descriptor == null) {
                throw new IllegalArgumentException("access widener target contains null data");
            }
        }

        public boolean isClass() {
            return kind == TargetKind.CLASS;
        }
    }

    private final Header header;
    private final List<Target> targets;

    private AccessWidener(Header header, List<Target> targets) {
        this.header = header;
        this.targets = Collections.unmodifiableList(new ArrayList<>(targets));
    }

    public static AccessWidener parse(byte[] content) {
        if (content == null) throw new TransformException("null access widener content");
        return parse(new String(content, StandardCharsets.UTF_8));
    }

    public static AccessWidener parse(String content) {
        if (content == null) throw new TransformException("null access widener content");
        try {
            return parse(new BufferedReader(new StringReader(content)));
        } catch (IOException failure) {
            throw new TransformException("cannot read access widener", failure);
        }
    }

    public static AccessWidener read(InputStream input) throws IOException {
        if (input == null) throw new TransformException("null access widener stream");
        return parse(new BufferedReader(new InputStreamReader(input, StandardCharsets.UTF_8)));
    }

    private static AccessWidener parse(BufferedReader reader) throws IOException {
        String headerLine = reader.readLine();
        if (headerLine == null) throw error(1, "missing access widener header");
        String[] headerTokens = headerLine.split("\\s+");
        if (headerTokens.length != 3 || !"accessWidener".equals(headerTokens[0])) {
            throw error(1, "invalid header; expected 'accessWidener v2 <namespace>'");
        }
        int version = switch (headerTokens[1]) {
            case "v1" -> 1;
            case "v2" -> 2;
            default -> throw error(1, "unsupported access widener format: " + headerTokens[1]);
        };
        if (headerTokens[2].isEmpty()) throw error(1, "access widener namespace is empty");

        Header header = new Header(version, headerTokens[2]);
        ArrayList<Target> targets = new ArrayList<>();
        String line;
        int lineNumber = 1;
        String delimiter = version < 2 ? "\\s+" : "[ \\t]+";
        while ((line = reader.readLine()) != null) {
            ++lineNumber;
            line = stripComment(line, version);
            if (line.isEmpty()) continue;
            if (version >= 2 && Character.isWhitespace(line.codePointAt(0))) {
                throw error(lineNumber, "leading whitespace is not allowed in v2");
            }
            String[] tokens = line.split(delimiter);
            String accessToken = tokens.length == 0 ? "" : tokens[0];
            boolean transitive = false;
            if (version >= 2 && accessToken.startsWith("transitive-")) {
                accessToken = accessToken.substring("transitive-".length());
                transitive = true;
            }
            Access access = Access.parse(accessToken, lineNumber);
            if (tokens.length < 2) throw error(lineNumber, "missing target kind");
            TargetKind kind = TargetKind.parse(tokens[1], lineNumber);
            if (kind == TargetKind.CLASS) {
                if (tokens.length != 3) {
                    throw error(lineNumber, "class target requires exactly one class name");
                }
                String owner = validateInternalName(tokens[2], lineNumber);
                targets.add(new Target(access, kind, owner, "", "", transitive));
            } else {
                if (tokens.length != 5) {
                    throw error(lineNumber, kind.getName()
                        + " target requires owner, name, and descriptor");
                }
                String owner = validateInternalName(tokens[2], lineNumber);
                String name = validateMemberName(tokens[3], kind, lineNumber);
                String descriptor = validateDescriptor(tokens[4], kind, name, lineNumber);
                if (kind == TargetKind.FIELD && access == Access.EXTENDABLE) {
                    throw error(lineNumber, "fields cannot be extendable");
                }
                if (kind != TargetKind.FIELD && access == Access.MUTABLE) {
                    throw error(lineNumber, kind.getName() + " targets cannot be mutable");
                }
                targets.add(new Target(access, kind, owner, name, descriptor, transitive));
            }
        }
        return new AccessWidener(header, targets);
    }

    private static String stripComment(String line, int version) {
        int comment = line.indexOf('#');
        if (comment >= 0) line = line.substring(0, comment);
        // Fabric v1 historically tolerated leading whitespace after comments;
        // v2 deliberately leaves it visible so it can be rejected.
        return version < 2 ? line.trim() : line;
    }

    private static String validateInternalName(String value, int line) {
        if (value == null || value.isEmpty()) throw error(line, "empty class name");
        if (value.indexOf('.') >= 0) {
            throw error(line, "class names must use '/' separators: " + value);
        }
        if (value.indexOf(';') >= 0 || value.indexOf('[') >= 0
            || value.indexOf('\0') >= 0 || value.startsWith("/")
            || value.endsWith("/") || value.contains("//")) {
            throw error(line, "invalid internal class name: " + value);
        }
        for (int i = 0; i < value.length(); ++i) {
            if (Character.isWhitespace(value.charAt(i))) {
                throw error(line, "whitespace in class name: " + value);
            }
        }
        return value;
    }

    private static String validateMemberName(String value, TargetKind kind, int line) {
        if (value == null || value.isEmpty()) throw error(line, "empty member name");
        if (kind == TargetKind.FIELD && (value.equals("<init>") || value.equals("<clinit>"))) {
            throw error(line, "special JVM names are only valid for methods");
        }
        for (int i = 0; i < value.length(); ++i) {
            if (Character.isWhitespace(value.charAt(i))) {
                throw error(line, "whitespace in member name: " + value);
            }
        }
        if (kind == TargetKind.METHOD && value.startsWith("<")
            && !value.equals("<init>") && !value.equals("<clinit>")) {
            throw error(line, "invalid special method name: " + value);
        }
        return value;
    }

    private static String validateDescriptor(String value, TargetKind kind,
                                             String name, int line) {
        if (value == null || value.isEmpty()) throw error(line, "empty descriptor");
        try {
            if (kind == TargetKind.FIELD) {
                Descriptor.Type type = Descriptor.type(value);
                if (type.voidType) throw error(line, "field descriptor cannot be void");
            } else {
                Descriptor.MethodDesc method = Descriptor.method(value);
                if (name.equals("<init>") && !method.returnType.voidType) {
                    throw error(line, "constructor descriptor must return void");
                }
                if (name.equals("<clinit>")
                    && (!method.arguments.isEmpty() || !method.returnType.voidType)) {
                    throw error(line, "class initializer descriptor must be ()V");
                }
            }
            validateDescriptorNames(value, line);
        } catch (TransformException failure) {
            throw error(line, failure.getMessage());
        }
        return value;
    }

    private static void validateDescriptorNames(String descriptor, int line) {
        for (int position = 0; position < descriptor.length(); ++position) {
            if (descriptor.charAt(position) != 'L') continue;
            int end = descriptor.indexOf(';', position + 1);
            if (end < 0) throw error(line, "unterminated object descriptor");
            validateInternalName(descriptor.substring(position + 1, end), line);
            position = end;
        }
    }

    static String validateResolvedName(String value, String what) {
        if (value == null || value.isEmpty()) throw new TransformException("resolver returned empty " + what);
        if (value.indexOf('.') >= 0 || value.indexOf(';') >= 0 || value.indexOf('[') >= 0
            || value.indexOf('\0') >= 0 || value.startsWith("/") || value.endsWith("/")
            || value.contains("//")) {
            throw new TransformException("resolver returned invalid " + what + ": " + value);
        }
        for (int i = 0; i < value.length(); ++i) {
            if (Character.isWhitespace(value.charAt(i))) {
                throw new TransformException("resolver returned invalid " + what + ": " + value);
            }
        }
        return value;
    }

    static String validateResolvedMemberName(String value, TargetKind kind, String what) {
        if (value == null || value.isEmpty()) {
            throw new TransformException("resolver returned empty " + what);
        }
        if (value.indexOf('.') >= 0 || value.indexOf(';') >= 0 || value.indexOf('[') >= 0
            || value.indexOf('/') >= 0 || value.indexOf('\0') >= 0) {
            throw new TransformException("resolver returned invalid " + what + ": " + value);
        }
        for (int i = 0; i < value.length(); ++i) {
            if (Character.isWhitespace(value.charAt(i))) {
                throw new TransformException("resolver returned invalid " + what + ": " + value);
            }
        }
        if (kind == TargetKind.FIELD && (value.equals("<init>") || value.equals("<clinit>"))) {
            throw new TransformException("resolver returned special field name: " + value);
        }
        if (kind == TargetKind.METHOD && value.startsWith("<")
            && !value.equals("<init>") && !value.equals("<clinit>")) {
            throw new TransformException("resolver returned invalid special method name: " + value);
        }
        return value;
    }

    static String validateResolvedDescriptor(String value, TargetKind kind, String name) {
        return validateDescriptor(value, kind, name, 0);
    }

    public int getVersion() {
        return header.version();
    }

    public String getNamespace() {
        return header.namespace();
    }

    public Header getHeader() {
        return header;
    }

    public List<Target> getTargets() {
        return targets;
    }

    private static TransformException error(int line, String message) {
        return new TransformException("access widener line " + line + ": " + message);
    }
}
