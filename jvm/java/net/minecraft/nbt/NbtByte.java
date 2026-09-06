package net.minecraft.nbt;

/** Immutable NBT 8-bit integer value. */
public final class NbtByte extends NbtElement {
    public static final NbtByte ZERO = new NbtByte((byte) 0);
    public static final NbtByte ONE = new NbtByte((byte) 1);
    private final byte value;

    public NbtByte(byte value) { this.value = value; }
    public static NbtByte of(byte value) { return value == 0 ? ZERO : value == 1 ? ONE : new NbtByte(value); }
    public byte byteValue() { return value; }
    public short shortValue() { return value; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public boolean booleanValue() { return value != 0; }
    public String asString() { return Byte.toString(value); }
    @Override public byte getType() { return BYTE_TYPE; }
    @Override public NbtByte copy() { return this; }
    @Override public boolean equals(Object other) { return other instanceof NbtByte nbt && value == nbt.value; }
    @Override public int hashCode() { return Byte.hashCode(value); }
    @Override public String toString() { return asString() + 'b'; }
}
