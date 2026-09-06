package net.minecraft.world.chunk;

/** Compact 4-bit light array compatible with the vanilla coordinate order. */
public class ChunkNibbleArray {
    public static final int BYTES_LENGTH = 2048;
    private final byte[] bytes;
    private final int defaultValue;

    public ChunkNibbleArray() { this(0); }
    public ChunkNibbleArray(int defaultValue) {
        this.bytes = new byte[BYTES_LENGTH];
        this.defaultValue = defaultValue & 15;
        if (this.defaultValue != 0) clear(this.defaultValue);
    }
    public ChunkNibbleArray(byte[] bytes) {
        this.bytes = bytes == null ? new byte[BYTES_LENGTH] : bytes.clone();
        this.defaultValue = 0;
    }
    public int get(int x, int y, int z) { return get(getIndex(x, y, z)); }
    public int get(int index) {
        if (index < 0 || index >= 4096) return defaultValue;
        int shift = (index & 1) << 2;
        return (bytes[index >> 1] >> shift) & 15;
    }
    public void set(int x, int y, int z, int value) { set(getIndex(x, y, z), value); }
    public void set(int index, int value) {
        if (index < 0 || index >= 4096) return;
        int offset = index >> 1;
        int shift = (index & 1) << 2;
        bytes[offset] = (byte) ((bytes[offset] & ~(15 << shift)) | ((value & 15) << shift));
    }
    public byte[] asByteArray() { return bytes.clone(); }
    public void clear(int value) {
        byte packed = (byte) ((value & 15) | ((value & 15) << 4));
        java.util.Arrays.fill(bytes, packed);
    }
    public boolean isUninitialized(int expectedDefaultValue) {
        for (int value : bytes) if ((value & 0xff) != ((expectedDefaultValue & 15) * 17)) return false;
        return true;
    }
    public ChunkNibbleArray copy() { return new ChunkNibbleArray(bytes); }
    private static int getIndex(int x, int y, int z) { return (y & 15) << 8 | (z & 15) << 4 | (x & 15); }
}
