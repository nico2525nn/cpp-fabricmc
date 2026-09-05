package com.mojang.brigadier;

/** Small cursor used by the argument-type compatibility surface. */
public final class StringReader {
    private final String string;
    private int cursor;

    public StringReader(String string) { this.string = string == null ? "" : string; }
    public String getString() { return string; }
    public int getCursor() { return cursor; }
    public void setCursor(int cursor) { this.cursor = Math.max(0, Math.min(cursor, string.length())); }
    public boolean canRead() { return canRead(1); }
    public boolean canRead(int length) { return cursor + length <= string.length(); }
    public char peek() { return canRead() ? string.charAt(cursor) : '\0'; }
    public char read() { return canRead() ? string.charAt(cursor++) : '\0'; }
    public void skip() { if (canRead()) ++cursor; }
    public String getRemaining() { return string.substring(cursor); }
    public String getRead() { return string.substring(0, cursor); }
}
