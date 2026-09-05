package com.mojang.brigadier.exceptions;

import java.util.function.BiFunction;

public final class Dynamic2CommandExceptionType implements CommandExceptionType {
    private final BiFunction<Object, Object, String> function;
    public Dynamic2CommandExceptionType(BiFunction<Object, Object, String> function) { this.function = function == null ? (a, b) -> String.valueOf(a) + ": " + b : function; }
    public CommandSyntaxException create(Object first, Object second) { return new CommandSyntaxException(this, function.apply(first, second), null, -1); }
    public CommandSyntaxException createWithContext(com.mojang.brigadier.StringReader reader, Object first, Object second) { return new CommandSyntaxException(this, function.apply(first, second), reader == null ? null : reader.getString(), reader == null ? -1 : reader.getCursor()); }
}
