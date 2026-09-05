package com.mojang.brigadier.exceptions;

import java.util.function.Function;

public final class DynamicCommandExceptionType implements CommandExceptionType {
    private final Function<Object, String> function;
    public DynamicCommandExceptionType(Function<Object, String> function) { this.function = function == null ? Object::toString : function; }
    public CommandSyntaxException create(Object value) { return new CommandSyntaxException(this, function.apply(value), null, -1); }
    public CommandSyntaxException createWithContext(com.mojang.brigadier.StringReader reader, Object value) { return new CommandSyntaxException(this, function.apply(value), reader == null ? null : reader.getString(), reader == null ? -1 : reader.getCursor()); }
}
