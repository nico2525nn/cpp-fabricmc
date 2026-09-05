package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public final class StringArgumentType implements ArgumentType<String> {
    public enum StringType { SINGLE_WORD, QUOTABLE_PHRASE, GREEDY_PHRASE }
    private final StringType type;

    private StringArgumentType(StringType type) { this.type = type; }

    public static StringArgumentType word() {
        return new StringArgumentType(StringType.SINGLE_WORD);
    }
    public static StringArgumentType string() {
        return new StringArgumentType(StringType.QUOTABLE_PHRASE);
    }
    public static StringArgumentType greedyString() {
        return new StringArgumentType(StringType.GREEDY_PHRASE);
    }
    public static String getString(CommandContext<?> context, String name) {
        return context.getArgument(name, String.class);
    }
    public StringType getType() { return type; }
    public boolean isGreedy() { return type == StringType.GREEDY_PHRASE; }

    @Override public String parse(StringReader reader) throws CommandSyntaxException {
        reader.skipWhitespace();
        if (type == StringType.GREEDY_PHRASE) { String value = reader.getRemaining(); reader.setCursor(reader.getString().length()); return value; }
        return type == StringType.QUOTABLE_PHRASE ? reader.readString() : reader.readUnquotedString();
    }
    @Override public java.util.Collection<String> getExamples() { return java.util.List.of("word", "\"quoted phrase\""); }
}
