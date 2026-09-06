package net.minecraft.nbt;

/** Immutable NBT 16-bit integer value. */
public final class NbtShort extends NbtElement {
    public static final NbtShort ZERO = new NbtShort((short) 0);
    private final short value;

    public NbtShort(short value) { this.value = value; }
    public static NbtShort of(short value) { return value == 0 ? ZERO : new NbtShort(value); }
    public short shortValue() { return value; }
    public byte byteValue() { return (byte) value; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String asString() { return Short.toString(value); }
    @Override public byte getType() { return SHORT_TYPE; }
    @Override public NbtShort copy() { return this; }
    @Override public boolean equals(Object other) { return other instanceof NbtShort nbt && value == nbt.value; }
    @Override public int hashCode() { return Short.hashCode(value); }
    @Override public String toString() { return asString() + 's'; }
}
