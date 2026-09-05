package com.mojang.brigadier.arguments;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;

public final class FloatArgumentType implements ArgumentType<Float> {
    private final float minimum, maximum;
    private FloatArgumentType(float minimum, float maximum) { this.minimum = minimum; this.maximum = maximum; }
    public static FloatArgumentType floatArg() { return new FloatArgumentType(-Float.MAX_VALUE, Float.MAX_VALUE); }
    public static FloatArgumentType floatArg(float min) { return new FloatArgumentType(min, Float.MAX_VALUE); }
    public static FloatArgumentType floatArg(float min, float max) { return new FloatArgumentType(min, max); }
    public static float getFloat(CommandContext<?> context, String name) { Float value = context.getArgument(name, Float.class); return value == null ? 0.0f : value; }
    public float getMinimum() { return minimum; }
    public float getMaximum() { return maximum; }
    @Override public Float parse(StringReader reader) throws CommandSyntaxException { float value = reader.readFloat(); if (value < minimum || value > maximum) throw new CommandSyntaxException("float outside range"); return value; }
}
