package net.minecraft.nbt;

public final class NbtInt extends NbtElement {
    private final int value;
    private NbtInt(int value) { this.value = value; }
    public static NbtInt of(int value) { return new NbtInt(value); }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public double doubleValue() { return value; }
    @Override public byte getType() { return INT_TYPE; }
    @Override public NbtInt copy() { return this; }
    @Override public String toString() { return Integer.toString(value); }
}
