package net.minecraft.resource.metadata;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import net.minecraft.resource.InputSupplier;

/**
 * Lazily decoded metadata attached to a resource pack entry.
 *
 * <p>The real server delegates value decoding to DFU codecs.  The shadow
 * runtime keeps the same serializer-keyed model and passes a small Gson tree
 * to the local codec surface, which is sufficient for Fabric resource
 * conditions and custom server reload listeners.</p>
 */
public interface ResourceMetadata {
    ResourceMetadata NONE = new Simple(Collections.emptyMap());
    InputSupplier<ResourceMetadata> NONE_SUPPLIER = () -> NONE;

    static ResourceMetadata create(InputStream stream) throws IOException {
        Objects.requireNonNull(stream, "stream");
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        stream.transferTo(bytes);
        String json = bytes.toString(StandardCharsets.UTF_8);
        JsonElement root = Parser.parse(json);
        if (!(root instanceof JsonObject object)) return NONE;
        Map<String, Object> values = new LinkedHashMap<>();
        for (Map.Entry<String, JsonElement> entry : object.entrySet())
            values.put(entry.getKey(), entry.getValue());
        return new Simple(values);
    }

    <T> Optional<T> decode(ResourceMetadataSerializer<T> serializer);

    default ResourceMetadata copy(Collection<ResourceMetadataSerializer<?>> serializers) {
        if (serializers == null || serializers.isEmpty()) return NONE;
        Builder builder = new Builder();
        for (ResourceMetadataSerializer<?> serializer : serializers)
            copyValue(builder, serializer);
        return builder.build();
    }

    private <T> void copyValue(Builder builder, ResourceMetadataSerializer<T> serializer) {
        decode(serializer).ifPresent(value -> builder.add(serializer, value));
    }

    /** Mutable assembly helper used by vanilla metadata loaders. */
    final class Builder {
        private final Map<String, Object> values = new LinkedHashMap<>();

        public <T> Builder add(ResourceMetadataSerializer<T> serializer, T value) {
            if (serializer != null) values.put(serializer.name(), value);
            return this;
        }

        public ResourceMetadata build() { return new Simple(values); }
    }

    final class Simple implements ResourceMetadata {
        private final Map<String, Object> values;

        private Simple(Map<String, Object> values) {
            this.values = Collections.unmodifiableMap(new LinkedHashMap<>(values));
        }

        @Override @SuppressWarnings("unchecked")
        public <T> Optional<T> decode(ResourceMetadataSerializer<T> serializer) {
            if (serializer == null || !values.containsKey(serializer.name())) return Optional.empty();
            Object raw = values.get(serializer.name());
            if (raw == null) return Optional.empty();
            try {
                T decoded = serializer.codec() == null ? (T) raw : serializer.codec().decode(raw);
                return Optional.ofNullable(decoded == null ? (T) raw : decoded);
            } catch (RuntimeException ignored) {
                return Optional.empty();
            }
        }
    }

    /** Small JSON reader for pack metadata; it intentionally accepts only JSON values. */
    final class Parser {
        private final String source;
        private int index;

        private Parser(String source) { this.source = source == null ? "" : source; }

        static JsonElement parse(String source) throws IOException {
            Parser parser = new Parser(source);
            JsonElement result = parser.value();
            parser.skipWhitespace();
            if (parser.index != parser.source.length()) throw new IOException("Trailing metadata data");
            return result;
        }

        private JsonElement value() throws IOException {
            skipWhitespace();
            if (index >= source.length()) throw new IOException("Unexpected end of metadata");
            return switch (source.charAt(index)) {
                case '{' -> object();
                case '[' -> array();
                case '"' -> new JsonPrimitive(string());
                case 't' -> literal("true", Boolean.TRUE);
                case 'f' -> literal("false", Boolean.FALSE);
                case 'n' -> literal("null", null);
                default -> number();
            };
        }

        private JsonObject object() throws IOException {
            expect('{');
            JsonObject result = new JsonObject();
            skipWhitespace();
            if (consume('}')) return result;
            while (true) {
                skipWhitespace();
                if (index >= source.length() || source.charAt(index) != '"')
                    throw new IOException("Expected metadata object key");
                String key = string();
                skipWhitespace();
                expect(':');
                result.add(key, value());
                skipWhitespace();
                if (consume('}')) return result;
                expect(',');
            }
        }

        private JsonArray array() throws IOException {
            expect('[');
            JsonArray result = new JsonArray();
            skipWhitespace();
            if (consume(']')) return result;
            while (true) {
                result.add(value());
                skipWhitespace();
                if (consume(']')) return result;
                expect(',');
            }
        }

        private JsonElement literal(String expected, Object value) throws IOException {
            if (!source.startsWith(expected, index)) throw new IOException("Invalid metadata literal");
            index += expected.length();
            return new JsonPrimitive(value);
        }

        private JsonElement number() throws IOException {
            int start = index;
            while (index < source.length() && "-+0123456789.eE".indexOf(source.charAt(index)) >= 0) index++;
            if (start == index) throw new IOException("Invalid metadata value");
            String value = source.substring(start, index);
            try {
                return new JsonPrimitive(value.contains(".") || value.contains("e") || value.contains("E")
                        ? Double.parseDouble(value) : Long.parseLong(value));
            } catch (NumberFormatException error) {
                throw new IOException("Invalid metadata number", error);
            }
        }

        private String string() throws IOException {
            expect('"');
            StringBuilder result = new StringBuilder();
            while (index < source.length()) {
                char c = source.charAt(index++);
                if (c == '"') return result.toString();
                if (c != '\\') { result.append(c); continue; }
                if (index >= source.length()) throw new IOException("Invalid metadata escape");
                char escaped = source.charAt(index++);
                switch (escaped) {
                    case '"', '\\', '/' -> result.append(escaped);
                    case 'b' -> result.append('\b');
                    case 'f' -> result.append('\f');
                    case 'n' -> result.append('\n');
                    case 'r' -> result.append('\r');
                    case 't' -> result.append('\t');
                    case 'u' -> {
                        if (index + 4 > source.length()) throw new IOException("Invalid unicode escape");
                        try { result.append((char) Integer.parseInt(source.substring(index, index + 4), 16)); }
                        catch (NumberFormatException error) { throw new IOException("Invalid unicode escape", error); }
                        index += 4;
                    }
                    default -> throw new IOException("Invalid metadata escape");
                }
            }
            throw new IOException("Unterminated metadata string");
        }

        private void skipWhitespace() {
            while (index < source.length() && Character.isWhitespace(source.charAt(index))) index++;
        }

        private void expect(char expected) throws IOException {
            skipWhitespace();
            if (index >= source.length() || source.charAt(index++) != expected)
                throw new IOException("Expected '" + expected + "' in metadata");
        }

        private boolean consume(char expected) {
            skipWhitespace();
            if (index < source.length() && source.charAt(index) == expected) { index++; return true; }
            return false;
        }
    }
}
