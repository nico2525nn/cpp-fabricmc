package com.mojang.brigadier.exceptions;

public final class SimpleCommandExceptionType implements CommandExceptionType {
    private final String message;
    public SimpleCommandExceptionType(String message) { this.message = message == null ? "" : message; }
    public CommandSyntaxException create() { return new CommandSyntaxException(this, message, null, -1); }
    public CommandSyntaxException createWithContext(com.mojang.brigadier.StringReader reader) { return new CommandSyntaxException(this, message, reader == null ? null : reader.getString(), reader == null ? -1 : reader.getCursor()); }
}
