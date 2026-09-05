package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public final class BoolArgumentType implements ArgumentType<Boolean> {
    private BoolArgumentType() {}
    public static BoolArgumentType bool() { return new BoolArgumentType(); }
    public static boolean getBool(CommandContext<?> context, String name) { return Boolean.TRUE.equals(context.getArgument(name, Boolean.class)); }
    @Override public Boolean parse(StringReader reader) throws CommandSyntaxException { return reader.readBoolean(); }
    @Override public java.util.Collection<String> getExamples() { return java.util.List.of("true", "false"); }
}
