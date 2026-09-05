package cppfm.transform;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Small JSON model for a Fabric/Sponge Mixin configuration.
 *
 * <p>It intentionally accepts the metadata needed before class definition
 * (package, common/mixins/server/client lists, required/minVersion and
 * compatibilityLevel) without requiring a JSON dependency.  Unknown keys are
 * retained as scalar/list values so a launcher can inspect them later.</p>
 */
public final class MixinConfiguration {
    private final Map<String, Object> values;

    private MixinConfiguration(Map<String, Object> values) {
        this.values = Collections.unmodifiableMap(new LinkedHashMap<>(values));
    }

    /** Parse one complete JSON object. */
    public static MixinConfiguration parse(String json) {
        if (json == null) throw new NullPointerException("json");
        Object parsed = new JsonParser(json).parse();
        if (!(parsed instanceof Map<?, ?> map)) throw new TransformException("Mixin config root is not an object");
        LinkedHashMap<String, Object> values = new LinkedHashMap<>();
        for (Map.Entry<?, ?> entry : map.entrySet()) {
            if (!(entry.getKey() instanceof String key))
                throw new TransformException("Mixin config contains a non-string key");
            values.put(key, entry.getValue());
        }
        return new MixinConfiguration(values);
    }

    public Map<String, Object> values() {
        return values;
    }

    public String getPackage() {
        Object value = values.get("package");
        return value instanceof String string ? string : "";
    }

    public boolean isRequired() {
        Object value = values.get("required");
        return value instanceof Boolean booleanValue && booleanValue;
    }

    /** Return all mixin class names for a server-side environment. */
    public List<String> serverMixins() {
        ArrayList<String> result = new ArrayList<>();
        addNames(result, values.get("mixins"));
        addNames(result, values.get("common"));
        addNames(result, values.get("server"));
        return Collections.unmodifiableList(result);
    }

    /** Return all mixin class names for a client-side environment. */
    public List<String> clientMixins() {
        ArrayList<String> result = new ArrayList<>();
        addNames(result, values.get("mixins"));
        addNames(result, values.get("common"));
        addNames(result, values.get("client"));
        return Collections.unmodifiableList(result);
    }

    /** Return the declared mixins for an arbitrary environment key. */
    public List<String> mixinsFor(String environment) {
        if ("client".equalsIgnoreCase(environment)) return clientMixins();
        return serverMixins();
    }

    private void addNames(List<String> output, Object value) {
        if (!(value instanceof List<?> list)) return;
        for (Object item : list) {
            if (!(item instanceof String name) || name.isEmpty()) continue;
            String qualified = name.replace('/', '.');
            if (!qualified.contains(".") && !getPackage().isEmpty()) qualified = getPackage() + "." + qualified;
            if (!output.contains(qualified)) output.add(qualified);
        }
    }

    private static final class JsonParser {
        private final String input;
        private int position;

        private JsonParser(String input) {
            this.input = input;
        }

        private Object parse() {
            skipSpace();
            Object value = value();
            skipSpace();
            if (position != input.length()) error("trailing JSON data");
            return value;
        }

        private Object value() {
            skipSpace();
            if (position >= input.length()) error("unexpected end of JSON");
            return switch (input.charAt(position)) {
                case '{' -> object();
                case '[' -> array();
                case '"' -> string();
                case 't' -> literal("true", Boolean.TRUE);
                case 'f' -> literal("false", Boolean.FALSE);
                case 'n' -> literal("null", null);
                default -> number();
            };
        }

        private Map<String, Object> object() {
            expect('{');
            LinkedHashMap<String, Object> result = new LinkedHashMap<>();
            skipSpace();
            if (take('}')) return result;
            while (true) {
                skipSpace();
                if (position >= input.length() || input.charAt(position) != '"') error("object key is not a string");
                String key = string();
                skipSpace();
                expect(':');
                result.put(key, value());
                skipSpace();
                if (take('}')) return result;
                expect(',');
            }
        }

        private List<Object> array() {
            expect('[');
            ArrayList<Object> result = new ArrayList<>();
            skipSpace();
            if (take(']')) return result;
            while (true) {
                result.add(value());
                skipSpace();
                if (take(']')) return result;
                expect(',');
            }
        }

        private String string() {
            expect('"');
            StringBuilder result = new StringBuilder();
            while (position < input.length()) {
                char c = input.charAt(position++);
                if (c == '"') return result.toString();
                if (c != '\\') {
                    if (c < 0x20) error("control character in string");
                    result.append(c);
                    continue;
                }
                if (position >= input.length()) error("unterminated escape");
                char escape = input.charAt(position++);
                switch (escape) {
                    case '"' -> result.append('"');
                    case '\\' -> result.append('\\');
                    case '/' -> result.append('/');
                    case 'b' -> result.append('\b');
                    case 'f' -> result.append('\f');
                    case 'n' -> result.append('\n');
                    case 'r' -> result.append('\r');
                    case 't' -> result.append('\t');
                    case 'u' -> {
                        if (position + 4 > input.length()) error("short unicode escape");
                        try {
                            result.append((char) Integer.parseInt(input.substring(position, position + 4), 16));
                        } catch (NumberFormatException failure) {
                            error("invalid unicode escape");
                        }
                        position += 4;
                    }
                    default -> error("invalid string escape");
                }
            }
            error("unterminated string");
            return "";
        }

        private Object number() {
            int start = position;
            if (take('-')) { }
            digits();
            boolean floating = false;
            if (take('.')) {
                floating = true;
                digits();
            }
            if (position < input.length() && (input.charAt(position) == 'e' || input.charAt(position) == 'E')) {
                floating = true;
                position++;
                if (take('+') || take('-')) { }
                digits();
            }
            String text = input.substring(start, position);
            try {
                return floating ? Double.valueOf(text) : Long.valueOf(text);
            } catch (NumberFormatException failure) {
                error("invalid number: " + text);
                return 0L;
            }
        }

        private void digits() {
            int start = position;
            while (position < input.length() && Character.isDigit(input.charAt(position))) position++;
            if (position == start) error("expected digits");
        }

        private Object literal(String literal, Object value) {
            if (!input.startsWith(literal, position)) error("invalid literal");
            position += literal.length();
            return value;
        }

        private boolean take(char expected) {
            if (position < input.length() && input.charAt(position) == expected) {
                position++;
                return true;
            }
            return false;
        }

        private void expect(char expected) {
            if (!take(expected)) error("expected '" + expected + "'");
        }

        private void skipSpace() {
            while (position < input.length() && Character.isWhitespace(input.charAt(position))) position++;
        }

        private void error(String message) {
            throw new TransformException("invalid Mixin JSON at " + position + ": " + message);
        }
    }
}
