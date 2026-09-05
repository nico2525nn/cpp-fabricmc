package net.minecraft.nbt;

public abstract class NbtElement {
    public static final byte END_TYPE = 0;
    public static final byte BYTE_TYPE = 1;
    public static final byte SHORT_TYPE = 2;
    public static final byte INT_TYPE = 3;
    public static final byte LONG_TYPE = 4;
    public static final byte FLOAT_TYPE = 5;
    public static final byte DOUBLE_TYPE = 6;
    public static final byte BYTE_ARRAY_TYPE = 7;
    public static final byte STRING_TYPE = 8;
    public static final byte LIST_TYPE = 9;
    public static final byte COMPOUND_TYPE = 10;
    public static final byte INT_ARRAY_TYPE = 11;
    public static final byte LONG_ARRAY_TYPE = 12;
    public abstract byte getType();
    public abstract NbtElement copy();
}
