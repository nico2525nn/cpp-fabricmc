package io.netty.buffer;

/**
 * Small heap-backed subset of Netty's ByteBuf ABI.
 *
 * <p>The production runtime supplies Netty itself.  This implementation is
 * intentionally kept in the compile-stub output, where the JVM API fixture
 * can execute without downloading a second runtime dependency.</p>
 */
public class ByteBuf {
    private byte[] data;
    private int readerIndex;
    private int writerIndex;

    public ByteBuf() { this(256); }
    protected ByteBuf(int initialCapacity) {
        data = new byte[Math.max(1, initialCapacity)];
    }

    private void ensureWritable(int length) {
        if (length < 0 || writerIndex > Integer.MAX_VALUE - length)
            throw new IndexOutOfBoundsException("buffer length");
        int required = writerIndex + length;
        if (required <= data.length) return;
        int capacity = data.length;
        while (capacity < required) {
            int next = capacity << 1;
            if (next <= capacity) { capacity = required; break; }
            capacity = next;
        }
        byte[] expanded = new byte[capacity];
        System.arraycopy(data, 0, expanded, 0, writerIndex);
        data = expanded;
    }

    private void checkReadable(int length) {
        if (length < 0 || readerIndex > writerIndex - length)
            throw new IndexOutOfBoundsException("not enough readable bytes");
    }

    public int readerIndex() { return readerIndex; }
    public ByteBuf readerIndex(int index) {
        if (index < 0 || index > writerIndex) throw new IndexOutOfBoundsException("reader index");
        readerIndex = index;
        return this;
    }
    public int writerIndex() { return writerIndex; }
    public ByteBuf writerIndex(int index) {
        if (index < readerIndex || index > data.length) throw new IndexOutOfBoundsException("writer index");
        writerIndex = index;
        return this;
    }
    public int readableBytes() { return writerIndex - readerIndex; }

    public ByteBuf writeByte(int value) {
        ensureWritable(1); data[writerIndex++] = (byte) value; return this;
    }
    public ByteBuf writeShort(int value) {
        ensureWritable(2); data[writerIndex++] = (byte) (value >>> 8);
        data[writerIndex++] = (byte) value; return this;
    }
    public ByteBuf writeChar(int value) { return writeShort(value); }
    public ByteBuf writeBoolean(boolean value) { return writeByte(value ? 1 : 0); }
    public ByteBuf writeInt(int value) {
        ensureWritable(4); data[writerIndex++] = (byte) (value >>> 24);
        data[writerIndex++] = (byte) (value >>> 16); data[writerIndex++] = (byte) (value >>> 8);
        data[writerIndex++] = (byte) value; return this;
    }
    public ByteBuf writeLong(long value) {
        ensureWritable(8); for (int shift = 56; shift >= 0; shift -= 8)
            data[writerIndex++] = (byte) (value >>> shift);
        return this;
    }
    public ByteBuf writeFloat(float value) { return writeInt(Float.floatToRawIntBits(value)); }
    public ByteBuf writeDouble(double value) { return writeLong(Double.doubleToRawLongBits(value)); }
    public ByteBuf writeBytes(byte[] bytes) {
        return bytes == null ? this : writeBytes(bytes, 0, bytes.length);
    }
    public ByteBuf writeBytes(byte[] bytes, int offset, int length) {
        if (bytes == null) {
            if (offset != 0 || length != 0) throw new NullPointerException("bytes");
            return this;
        }
        if (offset < 0 || length < 0 || offset > bytes.length - length)
            throw new IndexOutOfBoundsException("source range");
        ensureWritable(length); System.arraycopy(bytes, offset, data, writerIndex, length);
        writerIndex += length; return this;
    }

    public byte readByte() { checkReadable(1); return data[readerIndex++]; }
    public boolean readBoolean() { return readByte() != 0; }
    public short readShort() {
        checkReadable(2); return (short) (((data[readerIndex++] & 0xff) << 8)
            | (data[readerIndex++] & 0xff));
    }
    public int readInt() {
        checkReadable(4); return ((data[readerIndex++] & 0xff) << 24)
            | ((data[readerIndex++] & 0xff) << 16) | ((data[readerIndex++] & 0xff) << 8)
            | (data[readerIndex++] & 0xff);
    }
    public long readLong() {
        checkReadable(8); long result = 0;
        for (int shift = 56; shift >= 0; shift -= 8)
            result |= (long) (data[readerIndex++] & 0xff) << shift;
        return result;
    }
    public float readFloat() { return Float.intBitsToFloat(readInt()); }
    public double readDouble() { return Double.longBitsToDouble(readLong()); }
    public short readUnsignedByte() { return (short) (readByte() & 0xff); }
    public ByteBuf readBytes(byte[] destination) {
        if (destination == null) throw new NullPointerException("destination");
        checkReadable(destination.length); System.arraycopy(data, readerIndex, destination, 0, destination.length);
        readerIndex += destination.length; return this;
    }
    public ByteBuf skipBytes(int length) { checkReadable(length); readerIndex += length; return this; }
    public ByteBuf getBytes(int index, byte[] destination) {
        if (destination == null) throw new NullPointerException("destination");
        if (index < 0 || index > writerIndex - destination.length)
            throw new IndexOutOfBoundsException("source range");
        System.arraycopy(data, index, destination, 0, destination.length); return this;
    }
    public ByteBuf copy() {
        ByteBuf copy = new ByteBuf(readableBytes());
        copy.writeBytes(data, readerIndex, readableBytes());
        return copy;
    }
}
