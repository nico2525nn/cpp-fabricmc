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
        String remaining = reader.getRemaining();
        if (type == StringType.GREEDY_PHRASE) {
            reader.setCursor(reader.getString().length());
            return remaining;
        }
        int end = 0;
        while (end < remaining.length() && !Character.isWhitespace(remaining.charAt(end))) ++end;
        if (end == 0) throw new CommandSyntaxException("expected string");
        reader.setCursor(reader.getCursor() + end);
        return remaining.substring(0, end);
    }
}
