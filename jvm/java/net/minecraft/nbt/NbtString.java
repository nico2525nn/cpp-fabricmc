package net.minecraft.nbt;

public final class NbtString extends NbtElement {
    private final String value;
    private NbtString(String value) { this.value = value == null ? "" : value; }
    public static NbtString of(String value) { return new NbtString(value); }
    public String asString() { return value; }
    @Override public byte getType() { return STRING_TYPE; }
    @Override public NbtString copy() { return this; }
    @Override public String toString() { return '"' + value.replace("\"", "\\\"") + '"'; }
}
