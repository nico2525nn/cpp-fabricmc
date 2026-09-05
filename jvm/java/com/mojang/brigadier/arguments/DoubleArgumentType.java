package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public final class DoubleArgumentType implements ArgumentType<Double> {
    private final double minimum, maximum;
    private DoubleArgumentType(double minimum, double maximum) { this.minimum = minimum; this.maximum = maximum; }
    public static DoubleArgumentType doubleArg() { return new DoubleArgumentType(-Double.MAX_VALUE, Double.MAX_VALUE); }
    public static DoubleArgumentType doubleArg(double min) { return new DoubleArgumentType(min, Double.MAX_VALUE); }
    public static DoubleArgumentType doubleArg(double min, double max) { return new DoubleArgumentType(min, max); }
    public static double getDouble(CommandContext<?> context, String name) { Double value = context.getArgument(name, Double.class); return value == null ? 0.0 : value; }
    public double getMinimum() { return minimum; }
    public double getMaximum() { return maximum; }
    @Override public Double parse(StringReader reader) throws CommandSyntaxException { double value = reader.readDouble(); if (value < minimum || value > maximum) throw new CommandSyntaxException("double outside range"); return value; }
}
