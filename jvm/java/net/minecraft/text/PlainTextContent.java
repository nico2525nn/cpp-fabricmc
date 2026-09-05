package net.minecraft.text;

public final class PlainTextContent implements TextContent {
    private final String value;
    public PlainTextContent(String value) { this.value = value == null ? "" : value; }
    @Override public String asString() { return value; }
    @Override public String toString() { return value; }
}
