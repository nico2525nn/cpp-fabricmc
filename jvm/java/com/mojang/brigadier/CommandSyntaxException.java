package com.mojang.brigadier;

/** Backwards-compatible alias for the real Brigadier exception package. */
@Deprecated
public class CommandSyntaxException
        extends com.mojang.brigadier.exceptions.CommandSyntaxException {
    public CommandSyntaxException(String message) { super(message); }
    public CommandSyntaxException(String message, Throwable cause) { super(message, cause); }
    public CommandSyntaxException(String message, StringReader reader, int cursor) { super(message, reader, cursor); }
}
