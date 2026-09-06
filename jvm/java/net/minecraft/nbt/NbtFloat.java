package net.minecraft.nbt;

/** Immutable NBT 32-bit floating-point value. */
public final class NbtFloat extends NbtElement {
    public static final NbtFloat ZERO = new NbtFloat(0.0F);
    private final float value;

    public NbtFloat(float value) { this.value = value; }
    public static NbtFloat of(float value) { return value == 0.0F ? ZERO : new NbtFloat(value); }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public long longValue() { return (long) value; }
    public int intValue() { return (int) value; }
    public short shortValue() { return (short) value; }
    public byte byteValue() { return (byte) value; }
    public String asString() { return Float.toString(value); }
    @Override public byte getType() { return FLOAT_TYPE; }
    @Override public NbtFloat copy() { return this; }
    @Override public boolean equals(Object other) { return other instanceof NbtFloat nbt && Float.compare(value, nbt.value) == 0; }
    @Override public int hashCode() { return Float.hashCode(value); }
    @Override public String toString() { return asString() + 'f'; }
}
