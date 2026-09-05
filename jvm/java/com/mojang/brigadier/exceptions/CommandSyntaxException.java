package com.mojang.brigadier.exceptions;

/** Minimal Brigadier-compatible checked command error. */
public class CommandSyntaxException extends Exception {
    public CommandSyntaxException(String message) { super(message); }
    public CommandSyntaxException(String message, Throwable cause) { super(message, cause); }
}
