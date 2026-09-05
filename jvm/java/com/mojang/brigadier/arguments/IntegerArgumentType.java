package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public final class IntegerArgumentType implements ArgumentType<Integer> {
    private final int minimum;
    private final int maximum;

    private IntegerArgumentType(int minimum, int maximum) {
        this.minimum = minimum;
        this.maximum = maximum;
    }
    public static IntegerArgumentType integer() {
        return new IntegerArgumentType(Integer.MIN_VALUE, Integer.MAX_VALUE);
    }
    public static IntegerArgumentType integer(int min) {
        return new IntegerArgumentType(min, Integer.MAX_VALUE);
    }
    public static IntegerArgumentType integer(int min, int max) {
        return new IntegerArgumentType(min, max);
    }
    public static int getInteger(CommandContext<?> context, String name) {
        Integer value = context.getArgument(name, Integer.class);
        return value == null ? 0 : value;
    }
    public int getMinimum() { return minimum; }
    public int getMaximum() { return maximum; }

    @Override public Integer parse(StringReader reader) throws CommandSyntaxException {
        String remaining = reader.getRemaining();
        int end = 0;
        while (end < remaining.length() && !Character.isWhitespace(remaining.charAt(end))) ++end;
        if (end == 0) throw new CommandSyntaxException("expected integer");
        final int value;
        try { value = Integer.parseInt(remaining.substring(0, end)); }
        catch (NumberFormatException failure) {
            throw new CommandSyntaxException("invalid integer", failure);
        }
        if (value < minimum || value > maximum)
            throw new CommandSyntaxException("integer outside range");
        reader.setCursor(reader.getCursor() + end);
        return value;
    }
}
