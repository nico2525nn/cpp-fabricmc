package net.minecraft.network;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.UUID;
import net.minecraft.item.ItemStack;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;

/** Bounded, deterministic payload buffer used by custom-channel adapters. */
public class PacketByteBuf {
    private final ByteArrayOutputStream output;
    private byte[] input;
    private int readerIndex;
    private int markedReaderIndex;
    public PacketByteBuf() { output = new ByteArrayOutputStream(); input = null; }
    public PacketByteBuf(byte[] bytes) { output = new ByteArrayOutputStream(); if (bytes != null) output.writeBytes(bytes); input = null; }
    private byte[] data() { return input == null ? output.toByteArray() : input; }
    public PacketByteBuf writeByte(int value) { output.write(value); input = null; return this; }
    public PacketByteBuf writeShort(int value) { writeByte(value >>> 8); return writeByte(value); }
    public PacketByteBuf writeChar(int value) { return writeShort(value); }
    public PacketByteBuf writeBoolean(boolean value) { return writeByte(value ? 1 : 0); }
    public PacketByteBuf writeInt(int value) { writeByte(value >>> 24); writeByte(value >>> 16); writeByte(value >>> 8); return writeByte(value); }
    public PacketByteBuf writeLong(long value) { for (int shift = 56; shift >= 0; shift -= 8) writeByte((int) (value >>> shift)); return this; }
    public PacketByteBuf writeFloat(float value) { return writeInt(Float.floatToIntBits(value)); }
    public PacketByteBuf writeDouble(double value) { return writeLong(Double.doubleToLongBits(value)); }
    public PacketByteBuf writeVarInt(int value) { while ((value & ~0x7F) != 0) { writeByte((value & 0x7F) | 0x80); value >>>= 7; } return writeByte(value); }
    public PacketByteBuf writeVarLong(long value) { while ((value & ~0x7FL) != 0) { writeByte((int) (value & 0x7F) | 0x80); value >>>= 7; } return writeByte((int) value); }
    public PacketByteBuf writeBytes(byte[] bytes) { if (bytes != null) output.writeBytes(bytes); input = null; return this; }
    public PacketByteBuf writeByteArray(byte[] bytes) { byte[] value = bytes == null ? new byte[0] : bytes; writeVarInt(value.length); return writeBytes(value); }
    public PacketByteBuf writeString(String value) { byte[] bytes = (value == null ? "" : value).getBytes(StandardCharsets.UTF_8); if (bytes.length > 32767 * 4) throw new IllegalArgumentException("string too long"); writeVarInt(bytes.length); return writeBytes(bytes); }
    public PacketByteBuf writeUuid(UUID value) { UUID uuid = value == null ? new UUID(0L, 0L) : value; return writeLong(uuid.getMostSignificantBits()).writeLong(uuid.getLeastSignificantBits()); }
    public PacketByteBuf writeIdentifier(Identifier value) { return writeString(value == null ? "minecraft:air" : value.toString()); }
    public PacketByteBuf writeBlockPos(BlockPos value) { return writeLong(value == null ? 0L : value.asLong()); }
    public PacketByteBuf writeItemStack(ItemStack value) { ItemStack stack = value == null ? ItemStack.EMPTY : value; writeBoolean(!stack.isEmpty()); if (!stack.isEmpty()) { writeIdentifier(stack.getItem().getId()); writeByte(stack.getCount()); } return this; }
    public PacketByteBuf writeEnumConstant(Enum<?> value) { return writeVarInt(value == null ? 0 : value.ordinal()); }
    public byte readByte() { return readRaw(); }
    public int readUnsignedByte() { return readRaw() & 0xFF; }
    public short readShort() { return (short) ((readUnsignedByte() << 8) | readUnsignedByte()); }
    public char readChar() { return (char) (readShort() & 0xFFFF); }
    public boolean readBoolean() { return readUnsignedByte() != 0; }
    public int readInt() { return (readUnsignedByte() << 24) | (readUnsignedByte() << 16) | (readUnsignedByte() << 8) | readUnsignedByte(); }
    public long readLong() { long value = 0; for (int i = 0; i < 8; i++) value = (value << 8) | readUnsignedByte(); return value; }
    public float readFloat() { return Float.intBitsToFloat(readInt()); }
    public double readDouble() { return Double.longBitsToDouble(readLong()); }
    public int readVarInt() { int value = 0; for (int shift = 0; shift < 35; shift += 7) { int part = readUnsignedByte(); value |= (part & 0x7F) << shift; if ((part & 0x80) == 0) return value; } throw new IllegalArgumentException("varint too long"); }
    public long readVarLong() { long value = 0; for (int shift = 0; shift < 70; shift += 7) { int part = readUnsignedByte(); value |= (long) (part & 0x7F) << shift; if ((part & 0x80) == 0) return value; } throw new IllegalArgumentException("varlong too long"); }
    public byte[] readByteArray() { int length = readVarInt(); if (length < 0 || length > readableBytes()) throw new IllegalArgumentException("invalid byte array length"); return readRawBytes(length); }
    public String readString() { return readString(32767); }
    public String readString(int maxLength) { int length = readVarInt(); if (length < 0 || length > maxLength * 4 || length > readableBytes()) throw new IllegalArgumentException("invalid string length"); String value = new String(readRawBytes(length), StandardCharsets.UTF_8); if (value.length() > maxLength) throw new IllegalArgumentException("string too long"); return value; }
    public UUID readUuid() { return new UUID(readLong(), readLong()); }
    public Identifier readIdentifier() { Identifier id = Identifier.tryParse(readString()); if (id == null) throw new IllegalArgumentException("invalid identifier"); return id; }
    public BlockPos readBlockPos() { return BlockPos.fromLong(readLong()); }
    public ItemStack readItemStack() { return readBoolean() ? new ItemStack(net.minecraft.item.Item.fromRaw(0, readIdentifier().toString()), readUnsignedByte()) : ItemStack.EMPTY; }
    public int readableBytes() { return data().length - readerIndex; }
    public int readerIndex() { return readerIndex; }
    public PacketByteBuf readerIndex(int index) { if (index < 0 || index > data().length) throw new IndexOutOfBoundsException(index); readerIndex = index; return this; }
    public void markReaderIndex() { markedReaderIndex = readerIndex; }
    public void resetReaderIndex() { readerIndex(markedReaderIndex); }
    public boolean isReadable() { return readableBytes() > 0; }
    public byte[] toByteArray() { return data().clone(); }
    public PacketByteBuf copy() { PacketByteBuf copy = new PacketByteBuf(data()); copy.readerIndex = readerIndex; return copy; }
    private byte readRaw() { if (readerIndex >= data().length) throw new IndexOutOfBoundsException("buffer underflow"); return data()[readerIndex++]; }
    private byte[] readRawBytes(int length) { byte[] value = Arrays.copyOfRange(data(), readerIndex, readerIndex + length); readerIndex += length; return value; }
}
