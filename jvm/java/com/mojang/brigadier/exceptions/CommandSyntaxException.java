package com.mojang.brigadier.exceptions;

import com.mojang.brigadier.StringReader;

/** Checked command error carrying the source cursor when available. */
public class CommandSyntaxException extends Exception {
    private final CommandExceptionType type;
    private final String input;
    private final int cursor;
    public CommandSyntaxException(String message) { this(null, message, null, -1, null); }
    public CommandSyntaxException(String message, Throwable cause) { this(null, message, null, -1, cause); }
    public CommandSyntaxException(String message, String input, int cursor) { this(null, message, input, cursor, null); }
    public CommandSyntaxException(String message, Throwable cause, String input, int cursor) { this(null, message, input, cursor, cause); }
    public CommandSyntaxException(CommandExceptionType type, String message, String input, int cursor) { this(type, message, input, cursor, null); }
    public CommandSyntaxException(String message, StringReader reader, int cursor) { this(null, message, reader == null ? null : reader.getString(), cursor, null); }
    public CommandSyntaxException(String message, Throwable cause, StringReader reader, int cursor) { this(null, message, reader == null ? null : reader.getString(), cursor, cause); }
    private CommandSyntaxException(CommandExceptionType type, String message, String input, int cursor, Throwable cause) { super(message, cause); this.type = type; this.input = input; this.cursor = cursor; }
    public CommandExceptionType getType() { return type; }
    public String getInput() { return input; }
    public int getCursor() { return cursor; }
    public String getContext() { return input == null || cursor < 0 ? null : input.substring(Math.max(0, cursor - 10), Math.min(input.length(), cursor + 10)); }
}
