package net.minecraft.text;

import java.util.Arrays;
import java.util.Date;
import java.util.List;

/** The immutable text view used by the 1.21.4 server API. */
public interface Text {
    static MutableText literal(String value) {
        return new MutableText(new PlainTextContent(value), Style.EMPTY);
    }

    static MutableText keybind(String value) { return literal(value); }

    static MutableText translatable(String key, Object... args) {
        String value = args == null || args.length == 0 ? key : key + " " + Arrays.toString(args);
        return literal(value);
    }

    static MutableText empty() { return literal(""); }
    static Text of(String value) { return literal(value); }
    static Text of(Date value) { return literal(String.valueOf(value)); }
    static Text of(java.net.URI value) { return literal(String.valueOf(value)); }
    static Text of(net.minecraft.util.Identifier value) { return literal(String.valueOf(value)); }
    static Text of(com.mojang.brigadier.Message value) {
        return value instanceof Text text ? text : literal(value == null ? "" : value.getString());
    }

    String getString();
    default String getLiteralString() { return getString(); }
    TextContent getContent();
    List<Text> getSiblings();
    Style getStyle();
    MutableText copy();
    MutableText copyContentOnly();

    default boolean contains(Text text) { return this == text || getSiblings().stream().anyMatch(s -> s.contains(text)); }
    default String asTruncatedString(int length) {
        String value = getString();
        return value.length() <= length ? value : value.substring(0, Math.max(0, length));
    }
}
