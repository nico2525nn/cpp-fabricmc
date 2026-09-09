package net.minecraft.nbt;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import com.mojang.serialization.Codec;

/**
 * Small stringified-NBT reader covering the compound forms used by Fabric's
 * custom-data ingredient.  The class also preserves the public codec and
 * constructor ABI of Yarn 1.21.4.
 */
public class StringNbtReader {
    public static final Codec<NbtCompound> STRINGIFIED_CODEC = new Codec<>() { };
    public static final Codec<NbtCompound> NBT_COMPOUND_CODEC = new Codec<>() { };

    public static final char COMMA = ',';
    public static final char COLON = ':';
    private final StringReader reader;

    public StringNbtReader(StringReader reader) {
        this.reader = reader == null ? new StringReader("") : reader;
    }

    public static NbtCompound parse(String string) throws CommandSyntaxException {
        StringNbtReader parser = new StringNbtReader(new StringReader(string));
        NbtCompound result = parser.readCompound();
        parser.reader.skipWhitespace();
        if (parser.reader.canRead())
            throw new CommandSyntaxException("Trailing data in NBT", parser.reader, parser.reader.getCursor());
        return result;
    }

    public NbtCompound parseCompound() throws CommandSyntaxException { return readCompound(); }

    NbtCompound readCompound() throws CommandSyntaxException {
        reader.skipWhitespace();
        if (!reader.canRead() || reader.read() != '{')
            throw new CommandSyntaxException("Expected '{'", reader, reader.getCursor());
        NbtCompound result = new NbtCompound();
        reader.skipWhitespace();
        if (reader.canRead() && reader.peek() == '}') {
            reader.skip();
            return result;
        }
        while (reader.canRead()) {
            String key = readKey();
            reader.skipWhitespace();
            if (!reader.canRead() || reader.read() != ':')
                throw new CommandSyntaxException("Expected ':'", reader, reader.getCursor());
            NbtElement value = readPrimitiveOrCompound();
            result.put(key, value);
            reader.skipWhitespace();
            if (reader.canRead() && reader.peek() == '}') {
                reader.skip();
                return result;
            }
            if (!reader.canRead() || reader.read() != ',')
                throw new CommandSyntaxException("Expected ',' or '}'", reader, reader.getCursor());
            reader.skipWhitespace();
        }
        throw new CommandSyntaxException("Unclosed NBT compound", reader, reader.getCursor());
    }

    private String readKey() throws CommandSyntaxException {
        reader.skipWhitespace();
        if (!reader.canRead()) throw new CommandSyntaxException("Expected key", reader, reader.getCursor());
        return StringReader.isQuotedStringStart(reader.peek())
            ? reader.readQuotedString() : reader.readUnquotedString();
    }

    private NbtElement readPrimitiveOrCompound() throws CommandSyntaxException {
        reader.skipWhitespace();
        if (reader.canRead() && reader.peek() == '{') return readCompound();
        if (reader.canRead() && StringReader.isQuotedStringStart(reader.peek()))
            return NbtString.of(reader.readQuotedString());
        String token = reader.readUnquotedString();
        if (token.isEmpty()) throw new CommandSyntaxException("Expected value", reader, reader.getCursor());
        try {
            if (token.endsWith("b") || token.endsWith("B"))
                return NbtByte.of(Byte.parseByte(token.substring(0, token.length() - 1)));
            if (token.endsWith("l") || token.endsWith("L"))
                return NbtLong.of(Long.parseLong(token.substring(0, token.length() - 1)));
            if (token.endsWith("f") || token.endsWith("F"))
                return NbtFloat.of(Float.parseFloat(token.substring(0, token.length() - 1)));
            if (token.contains(".") || token.contains("e") || token.contains("E"))
                return NbtDouble.of(Double.parseDouble(token));
            return NbtInt.of(Integer.parseInt(token));
        } catch (NumberFormatException ignored) {
            return NbtString.of(token);
        }
    }

    protected NbtElement parseElement() throws CommandSyntaxException { return readPrimitiveOrCompound(); }
    protected NbtElement parseElementPrimitive() throws CommandSyntaxException { return readPrimitiveOrCompound(); }
    protected NbtElement parseArray() throws CommandSyntaxException { return readPrimitiveOrCompound(); }
    protected String readString() throws CommandSyntaxException { return reader.readString(); }
}
