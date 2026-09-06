package net.minecraft.nbt;

/** Immutable NBT 64-bit integer value. */
public final class NbtLong extends NbtElement {
    public static final NbtLong ZERO = new NbtLong(0L);
    private final long value;

    public NbtLong(long value) { this.value = value; }
    public static NbtLong of(long value) { return value == 0L ? ZERO : new NbtLong(value); }
    public long longValue() { return value; }
    public int intValue() { return (int) value; }
    public short shortValue() { return (short) value; }
    public byte byteValue() { return (byte) value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String asString() { return Long.toString(value); }
    @Override public byte getType() { return LONG_TYPE; }
    @Override public NbtLong copy() { return this; }
    @Override public boolean equals(Object other) { return other instanceof NbtLong nbt && value == nbt.value; }
    @Override public int hashCode() { return Long.hashCode(value); }
    @Override public String toString() { return asString() + 'L'; }
}
