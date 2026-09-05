package com.mojang.brigadier;

import com.mojang.brigadier.exceptions.CommandSyntaxException;

/** Cursor and token primitives matching the public Brigadier reader surface. */
public final class StringReader {
    public static final char SYNTAX_ESCAPE = '\\';
    public static final char SYNTAX_DOUBLE_QUOTE = '"';
    public static final char SYNTAX_SINGLE_QUOTE = '\'';
    private final String string;
    private int cursor;
    public StringReader(String string) { this.string = string == null ? "" : string; }
    public StringReader(StringReader other) { this.string = other == null ? "" : other.string; this.cursor = other == null ? 0 : other.cursor; }
    public String getString() { return string; }
    public int getCursor() { return cursor; }
    public void setCursor(int cursor) { this.cursor = Math.max(0, Math.min(cursor, string.length())); }
    public int getRemainingLength() { return string.length() - cursor; }
    public int getTotalLength() { return string.length(); }
    public boolean canRead() { return canRead(1); }
    public boolean canRead(int length) { return length >= 0 && cursor + length <= string.length(); }
    public char peek() { return peek(0); }
    public char peek(int offset) { return canRead(offset + 1) ? string.charAt(cursor + offset) : '\0'; }
    public char read() { return canRead() ? string.charAt(cursor++) : '\0'; }
    public void skip() { if (canRead()) ++cursor; }
    public void skipWhitespace() { while (canRead() && Character.isWhitespace(peek())) skip(); }
    public String getRemaining() { return string.substring(cursor); }
    public String getRead() { return string.substring(0, cursor); }
    public static boolean isAllowedNumber(char character) { return character >= '0' && character <= '9' || character == '.' || character == '-' || character == '+' || character == 'e' || character == 'E'; }
    public static boolean isQuotedStringStart(char character) { return character == SYNTAX_DOUBLE_QUOTE || character == SYNTAX_SINGLE_QUOTE; }
    public String readUnquotedString() { int start = cursor; while (canRead() && !Character.isWhitespace(peek())) skip(); return string.substring(start, cursor); }
    public String readString() throws CommandSyntaxException { skipWhitespace(); return isQuotedStringStart(peek()) ? readQuotedString() : readUnquotedString(); }
    public String readStringUntil(char terminator) { int start = cursor; while (canRead() && peek() != terminator) skip(); return string.substring(start, cursor); }
    public String readQuotedString() throws CommandSyntaxException {
        if (!canRead() || !isQuotedStringStart(peek())) throw new CommandSyntaxException("Expected quote", this, cursor);
        char quote = read(); StringBuilder result = new StringBuilder(); boolean escaped = false;
        while (canRead()) {
            char value = read();
            if (escaped) { if (value != quote && value != SYNTAX_ESCAPE) throw new CommandSyntaxException("Invalid escape sequence", this, cursor - 1); result.append(value); escaped = false; continue; }
            if (value == SYNTAX_ESCAPE) { escaped = true; continue; }
            if (value == quote) return result.toString();
            result.append(value);
        }
        throw new CommandSyntaxException("Unclosed quoted string", this, cursor);
    }
    public int readInt() throws CommandSyntaxException { String token = readNumber(); try { return Integer.parseInt(token); } catch (RuntimeException e) { throw new CommandSyntaxException("Invalid integer", e, this, cursor); } }
    public long readLong() throws CommandSyntaxException { String token = readNumber(); try { return Long.parseLong(token); } catch (RuntimeException e) { throw new CommandSyntaxException("Invalid long", e, this, cursor); } }
    public double readDouble() throws CommandSyntaxException { String token = readNumber(); try { return Double.parseDouble(token); } catch (RuntimeException e) { throw new CommandSyntaxException("Invalid double", e, this, cursor); } }
    public float readFloat() throws CommandSyntaxException { String token = readNumber(); try { return Float.parseFloat(token); } catch (RuntimeException e) { throw new CommandSyntaxException("Invalid float", e, this, cursor); } }
    public boolean readBoolean() throws CommandSyntaxException { String value = readString(); if ("true".equals(value)) return true; if ("false".equals(value)) return false; throw new CommandSyntaxException("Expected boolean", this, cursor); }
    private String readNumber() { skipWhitespace(); int start = cursor; while (canRead() && isAllowedNumber(peek())) skip(); return string.substring(start, cursor); }
}
