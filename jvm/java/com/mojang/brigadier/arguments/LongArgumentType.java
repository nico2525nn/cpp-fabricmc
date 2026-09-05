package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public final class LongArgumentType implements ArgumentType<Long> {
    private final long minimum, maximum;
    private LongArgumentType(long minimum, long maximum) { this.minimum = minimum; this.maximum = maximum; }
    public static LongArgumentType longArg() { return new LongArgumentType(Long.MIN_VALUE, Long.MAX_VALUE); }
    public static LongArgumentType longArg(long min) { return new LongArgumentType(min, Long.MAX_VALUE); }
    public static LongArgumentType longArg(long min, long max) { return new LongArgumentType(min, max); }
    public static long getLong(CommandContext<?> context, String name) { Long value = context.getArgument(name, Long.class); return value == null ? 0L : value; }
    public long getMinimum() { return minimum; }
    public long getMaximum() { return maximum; }
    @Override public Long parse(StringReader reader) throws CommandSyntaxException { long value = reader.readLong(); if (value < minimum || value > maximum) throw new CommandSyntaxException("long outside range"); return value; }
}
