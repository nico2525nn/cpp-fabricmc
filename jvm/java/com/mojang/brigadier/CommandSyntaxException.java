package com.mojang.brigadier;

/** Backwards-compatible alias for the real Brigadier exception package. */
@Deprecated
public class CommandSyntaxException
        extends com.mojang.brigadier.exceptions.CommandSyntaxException {
    public CommandSyntaxException(String message) { super(message); }
}
