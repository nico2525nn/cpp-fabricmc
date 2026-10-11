package cppfm.loader;

import cppfm.transform.AccessWidener;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Converts Fabric access-widener targets with mod bytecode into cppfm's named namespace. */
public final class AccessWidenerNamespaceRemapper {
    private static final String HEADER = "accessWidener";

    private AccessWidenerNamespaceRemapper() { }

    public static byte[] remap(byte[] source, IntermediaryNamedMappings mappings)
        throws IOException {
        if (source == null || mappings == null)
            throw new NullPointerException("access widener bytes and mappings are required");
        String text = decode(source);
        String[] lines = text.split("\\n", -1);
        if (lines.length == 0) throw new IOException("access widener is empty");

        String headerLine = stripCarriageReturn(lines[0]);
        String[] header = headerLine.split("\\s+");
        if (header.length != 3 || !HEADER.equals(header[0]) ||
            !("v1".equals(header[1]) || "v2".equals(header[1]))) {
            throw new IOException("invalid access widener header");
        }
        if ("named".equals(header[2])) return source.clone();
        if (!IntermediaryNamedMappings.SOURCE_NAMESPACE.equals(header[2])) {
            throw new IOException("unsupported access widener namespace: " + header[2]);
        }
        if (mappings.isIdentity())
            throw new IOException("cannot remap intermediary access widener without mappings");

        List<String> output = new ArrayList<>(lines.length);
        output.add(HEADER + "\t" + header[1] + "\t" +
            IntermediaryNamedMappings.TARGET_NAMESPACE);
        for (int index = 1; index < lines.length; ++index) {
            String line = stripCarriageReturn(lines[index]);
            int commentIndex = line.indexOf('#');
            int commentStart = commentIndex;
            while (commentStart > 0 && Character.isWhitespace(
                    line.charAt(commentStart - 1))) --commentStart;
            String comment = commentStart < 0 ? "" : line.substring(commentStart);
            String directive = commentStart < 0 ? line : line.substring(0, commentStart);
            if ("v1".equals(header[1])) directive = directive.trim();
            if (directive.isEmpty()) {
                output.add(line);
                continue;
            }
            output.add(remapEntry(directive, comment, header[1], mappings,
                index + 1));
        }

        String result = String.join("\n", output);
        byte[] remapped = result.getBytes(StandardCharsets.UTF_8);
        try {
            AccessWidener.parse(remapped);
        } catch (RuntimeException invalid) {
            throw new IOException("remapped access widener is invalid: " +
                invalid.getMessage(), invalid);
        }
        return remapped;
    }

    private static String remapEntry(String directive, String comment, String version,
                                     IntermediaryNamedMappings mappings,
                                     int lineNumber) throws IOException {
        if ("v2".equals(version) && Character.isWhitespace(directive.codePointAt(0)))
            throw invalidLine(lineNumber, "leading whitespace is not allowed in v2");
        String[] parts = "v2".equals(version)
            ? directive.split("[ \\t]+") : directive.trim().split("\\s+");
        if (parts.length < 3)
            throw invalidLine(lineNumber, "expected an access widener target");
        String access = parts[0];
        String kind = parts[1];
        if (!isAccess(access))
            throw invalidLine(lineNumber, "unknown access modifier " + access);
        if (access.startsWith("transitive-") && !"v2".equals(version))
            throw invalidLine(lineNumber, "transitive access requires v2");

        if ("class".equals(kind)) {
            if (parts.length != 3)
                throw invalidLine(lineNumber, "class target must have three columns");
            parts[2] = mappings.mapClassName(parts[2]);
        } else if ("field".equals(kind) || "method".equals(kind)) {
            if (parts.length != 5)
                throw invalidLine(lineNumber, kind + " target must have five columns");
            String owner = parts[2];
            String descriptor = parts[4];
            parts[2] = mappings.mapClassName(owner);
            parts[3] = "field".equals(kind)
                ? mappings.mapFieldName(owner, parts[3], descriptor)
                : mappings.mapMethodName(owner, parts[3], descriptor);
            parts[4] = mappings.mapDescriptor(descriptor);
        } else {
            throw invalidLine(lineNumber, "unknown target type " + kind);
        }
        return String.join("\t", parts) + comment;
    }

    private static boolean isAccess(String value) {
        return switch (value) {
            case "accessible", "extendable", "mutable",
                 "transitive-accessible", "transitive-extendable",
                 "transitive-mutable" -> true;
            default -> false;
        };
    }

    private static IOException invalidLine(int line, String reason) {
        return new IOException("invalid access widener line " + line + ": " + reason);
    }

    private static String stripCarriageReturn(String line) {
        return line.endsWith("\r") ? line.substring(0, line.length() - 1) : line;
    }

    private static String decode(byte[] source) throws IOException {
        try {
            return StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(source)).toString();
        } catch (CharacterCodingException failure) {
            throw new IOException("access widener is not valid UTF-8", failure);
        }
    }
}
