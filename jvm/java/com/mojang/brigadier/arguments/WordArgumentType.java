package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public final class WordArgumentType implements ArgumentType<String> {
    private WordArgumentType() {}
    public static WordArgumentType word() { return new WordArgumentType(); }
    public static String getString(CommandContext<?> context, String name) { return context.getArgument(name, String.class); }
    @Override public String parse(StringReader reader) throws CommandSyntaxException { String value = reader.readUnquotedString(); if (value.isEmpty()) throw new CommandSyntaxException("expected word"); return value; }
}
