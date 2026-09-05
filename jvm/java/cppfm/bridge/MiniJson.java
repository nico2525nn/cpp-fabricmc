package cppfm.bridge;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Small dependency-free JSON reader for fabric.mod.json metadata. */
final class MiniJson {
    private MiniJson() {}

    static Object parse(String text) {
        Parser p = new Parser(text);
        Object value = p.value();
        p.space();
        if (!p.atEnd()) throw new IllegalArgumentException("trailing JSON data");
        return value;
    }

    private static final class Parser {
        private final String text;
        private int index;

        Parser(String text) { this.text = text; }
        boolean atEnd() { return index >= text.length(); }
        void space() {
            while (!atEnd() && Character.isWhitespace(text.charAt(index))) index++;
        }
        char take() {
            if (atEnd()) throw new IllegalArgumentException("unexpected end of JSON");
            return text.charAt(index++);
        }
        void expect(char expected) {
            if (take() != expected) throw new IllegalArgumentException("expected '" + expected + "'");
        }
        Object value() {
            space();
            if (atEnd()) throw new IllegalArgumentException("missing JSON value");
            return switch (text.charAt(index)) {
                case '{' -> object();
                case '[' -> array();
                case '"' -> string();
                case 't' -> literal("true", Boolean.TRUE);
                case 'f' -> literal("false", Boolean.FALSE);
                case 'n' -> literal("null", null);
                default -> number();
            };
        }
        Object literal(String word, Object result) {
            if (!text.startsWith(word, index)) throw new IllegalArgumentException("invalid JSON literal");
            index += word.length();
            return result;
        }
        Map<String, Object> object() {
            expect('{');
            Map<String, Object> result = new LinkedHashMap<>();
            space();
            if (!atEnd() && text.charAt(index) == '}') { index++; return result; }
            while (true) {
                space();
                String key = string();
                space();
                expect(':');
                result.put(key, value());
                space();
                char delimiter = take();
                if (delimiter == '}') return result;
                if (delimiter != ',') throw new IllegalArgumentException("expected ',' or '}'");
            }
        }
        List<Object> array() {
            expect('[');
            List<Object> result = new ArrayList<>();
            space();
            if (!atEnd() && text.charAt(index) == ']') { index++; return result; }
            while (true) {
                result.add(value());
                space();
                char delimiter = take();
                if (delimiter == ']') return result;
                if (delimiter != ',') throw new IllegalArgumentException("expected ',' or ']'");
            }
        }
        String string() {
            expect('"');
            StringBuilder result = new StringBuilder();
            while (true) {
                char c = take();
                if (c == '"') return result.toString();
                if (c != '\\') { result.append(c); continue; }
                char escaped = take();
                switch (escaped) {
                    case '"' -> result.append('"');
                    case '\\' -> result.append('\\');
                    case '/' -> result.append('/');
                    case 'b' -> result.append('\b');
                    case 'f' -> result.append('\f');
                    case 'n' -> result.append('\n');
                    case 'r' -> result.append('\r');
                    case 't' -> result.append('\t');
                    case 'u' -> {
                        if (index + 4 > text.length()) throw new IllegalArgumentException("bad unicode escape");
                        result.append((char) Integer.parseInt(text.substring(index, index + 4), 16));
                        index += 4;
                    }
                    default -> throw new IllegalArgumentException("bad escape sequence");
                }
            }
        }
        Number number() {
            int start = index;
            if (text.charAt(index) == '-') index++;
            while (!atEnd() && Character.isDigit(text.charAt(index))) index++;
            if (!atEnd() && text.charAt(index) == '.') {
                index++;
                while (!atEnd() && Character.isDigit(text.charAt(index))) index++;
            }
            if (!atEnd() && (text.charAt(index) == 'e' || text.charAt(index) == 'E')) {
                index++;
                if (!atEnd() && (text.charAt(index) == '+' || text.charAt(index) == '-')) index++;
                while (!atEnd() && Character.isDigit(text.charAt(index))) index++;
            }
            String raw = text.substring(start, index);
            try { return raw.indexOf('.') >= 0 || raw.indexOf('e') >= 0 || raw.indexOf('E') >= 0
                    ? Double.parseDouble(raw) : Long.parseLong(raw); }
            catch (NumberFormatException e) { throw new IllegalArgumentException("bad JSON number", e); }
        }
    }
}
