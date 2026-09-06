package net.minecraft.nbt;

/** Immutable NBT 64-bit floating-point value. */
public final class NbtDouble extends NbtElement {
    public static final NbtDouble ZERO = new NbtDouble(0.0D);
    private final double value;

    public NbtDouble(double value) { this.value = value; }
    public static NbtDouble of(double value) { return value == 0.0D ? ZERO : new NbtDouble(value); }
    public double doubleValue() { return value; }
    public float floatValue() { return (float) value; }
    public long longValue() { return (long) value; }
    public int intValue() { return (int) value; }
    public short shortValue() { return (short) value; }
    public byte byteValue() { return (byte) value; }
    public String asString() { return Double.toString(value); }
    @Override public byte getType() { return DOUBLE_TYPE; }
    @Override public NbtDouble copy() { return this; }
    @Override public boolean equals(Object other) { return other instanceof NbtDouble nbt && Double.compare(value, nbt.value) == 0; }
    @Override public int hashCode() { return Double.hashCode(value); }
    @Override public String toString() { return asString() + 'D'; }
}
