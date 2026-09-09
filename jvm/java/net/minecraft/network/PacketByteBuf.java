package net.minecraft.network;

import java.nio.charset.StandardCharsets;
import java.util.UUID;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NbtByte;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.nbt.NbtDouble;
import net.minecraft.nbt.NbtElement;
import net.minecraft.nbt.NbtFloat;
import net.minecraft.nbt.NbtInt;
import net.minecraft.nbt.NbtLong;
import net.minecraft.nbt.NbtShort;
import net.minecraft.nbt.NbtString;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import io.netty.buffer.UnpooledByteBufAllocator;
import io.netty.buffer.UnpooledHeapByteBuf;

/**
 * 1.21.4 packet buffer backed by Netty's heap buffer implementation.
 *
 * <p>The real Fabric/Minecraft ABI requires this type to be assignable to
 * {@code io.netty.buffer.ByteBuf}. The native transport still owns packet
 * framing; this class provides the Java-side buffer operations that Fabric
 * codecs and payloads use on top of that transport.</p>
 */
public class PacketByteBuf extends UnpooledHeapByteBuf {
    private static final int INITIAL_CAPACITY = 256;

    public PacketByteBuf() {
        super(UnpooledByteBufAllocator.DEFAULT, INITIAL_CAPACITY, Integer.MAX_VALUE);
    }

    public PacketByteBuf(byte[] bytes) {
        super(UnpooledByteBufAllocator.DEFAULT,
            Math.max(INITIAL_CAPACITY, bytes == null ? 0 : bytes.length), Integer.MAX_VALUE);
        if (bytes != null && bytes.length > 0) writeBytes(bytes);
    }

    @Override public PacketByteBuf writeByte(int value) { super.writeByte(value); return this; }
    @Override public PacketByteBuf writeShort(int value) { super.writeShort(value); return this; }
    @Override public PacketByteBuf writeChar(int value) { super.writeChar(value); return this; }
    @Override public PacketByteBuf writeBoolean(boolean value) { super.writeBoolean(value); return this; }
    @Override public PacketByteBuf writeInt(int value) { super.writeInt(value); return this; }
    @Override public PacketByteBuf writeLong(long value) { super.writeLong(value); return this; }
    @Override public PacketByteBuf writeFloat(float value) { super.writeFloat(value); return this; }
    @Override public PacketByteBuf writeDouble(double value) { super.writeDouble(value); return this; }
    @Override public PacketByteBuf writeBytes(byte[] bytes) {
        if (bytes != null) super.writeBytes(bytes);
        return this;
    }
    @Override public PacketByteBuf writeBytes(byte[] bytes, int offset, int length) {
        if (bytes == null) {
            if (offset != 0 || length != 0) throw new NullPointerException("bytes");
            return this;
        }
        super.writeBytes(bytes, offset, length);
        return this;
    }
    public PacketByteBuf writeBytes(PacketByteBuf value) {
        if (value == null) return this;
        int length = value.readableBytes();
        if (length > 0) {
            byte[] bytes = new byte[length];
            value.getBytes(value.readerIndex(), bytes);
            writeBytes(bytes);
        }
        return this;
    }

    public PacketByteBuf writeByteArray(byte[] bytes) {
        byte[] value = bytes == null ? new byte[0] : bytes;
        writeVarInt(value.length);
        return writeBytes(value);
    }

    /**
     * Writes the network NBT representation used by Minecraft: a root type,
     * an empty root name, and the tag payload.  The implementation covers all
     * scalar tags and compounds represented by the embedded NBT model; an
     * unsupported tag fails explicitly instead of emitting a misleading
     * packet.
     */
    public PacketByteBuf writeNbt(NbtElement value) {
        if (value == null) return writeByte(NbtElement.END_TYPE);
        writeByte(value.getType());
        if (value.getType() == NbtElement.END_TYPE) return this;
        writeNbtString("");
        writeNbtPayload(value);
        return this;
    }

    private void writeNbtPayload(NbtElement value) {
        switch (value.getType()) {
            case NbtElement.BYTE_TYPE -> writeByte(((NbtByte) value).byteValue());
            case NbtElement.SHORT_TYPE -> writeShort(((NbtShort) value).shortValue());
            case NbtElement.INT_TYPE -> writeInt(((NbtInt) value).intValue());
            case NbtElement.LONG_TYPE -> writeLong(((NbtLong) value).longValue());
            case NbtElement.FLOAT_TYPE -> writeFloat(((NbtFloat) value).floatValue());
            case NbtElement.DOUBLE_TYPE -> writeDouble(((NbtDouble) value).doubleValue());
            case NbtElement.STRING_TYPE -> writeNbtString(((NbtString) value).asString());
            case NbtElement.COMPOUND_TYPE -> {
                NbtCompound compound = (NbtCompound) value;
                for (java.util.Map.Entry<String, NbtElement> entry : compound.entrySet()) {
                    NbtElement element = entry.getValue();
                    if (element == null || element.getType() == NbtElement.END_TYPE) continue;
                    writeByte(element.getType());
                    writeNbtString(entry.getKey());
                    writeNbtPayload(element);
                }
                writeByte(NbtElement.END_TYPE);
            }
            default -> throw new IllegalArgumentException("Unsupported NBT tag type: " + value.getType());
        }
    }

    private PacketByteBuf writeNbtString(String value) {
        byte[] bytes = (value == null ? "" : value).getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 0xFFFF) throw new IllegalArgumentException("NBT string too long");
        writeShort(bytes.length);
        return writeBytes(bytes);
    }

    public PacketByteBuf writeString(String value) {
        byte[] bytes = (value == null ? "" : value).getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 32767 * 4) throw new IllegalArgumentException("string too long");
        writeVarInt(bytes.length);
        return writeBytes(bytes);
    }

    /** Alias used by the 1.21.4 packet codec API. */
    public PacketByteBuf writeUtf(String value) { return writeString(value); }

    public PacketByteBuf writeUuid(UUID value) {
        UUID uuid = value == null ? new UUID(0L, 0L) : value;
        return writeLong(uuid.getMostSignificantBits()).writeLong(uuid.getLeastSignificantBits());
    }

    public PacketByteBuf writeIdentifier(Identifier value) {
        return writeString(value == null ? "minecraft:air" : value.toString());
    }

    public PacketByteBuf writeBlockPos(BlockPos value) {
        return writeLong(value == null ? 0L : value.asLong());
    }

    public PacketByteBuf writeItemStack(ItemStack value) {
        ItemStack stack = value == null ? ItemStack.EMPTY : value;
        writeBoolean(!stack.isEmpty());
        if (!stack.isEmpty()) writeIdentifier(stack.getItem().getId()).writeByte(stack.getCount());
        return this;
    }

    public PacketByteBuf writeEnumConstant(Enum<?> value) {
        return writeVarInt(value == null ? 0 : value.ordinal());
    }

    public PacketByteBuf writeVarInt(int value) {
        while ((value & ~0x7F) != 0) {
            writeByte((value & 0x7F) | 0x80);
            value >>>= 7;
        }
        return writeByte(value);
    }

    public PacketByteBuf writeVarLong(long value) {
        while ((value & ~0x7FL) != 0) {
            writeByte((int) (value & 0x7F) | 0x80);
            value >>>= 7;
        }
        return writeByte((int) value);
    }

    @Override public PacketByteBuf readerIndex(int index) { super.readerIndex(index); return this; }
    @Override public PacketByteBuf writerIndex(int index) { super.writerIndex(index); return this; }
    @Override public PacketByteBuf skipBytes(int length) { super.skipBytes(length); return this; }

    public byte[] readByteArray() {
        int length = readVarInt();
        if (length < 0 || length > readableBytes()) throw new IllegalArgumentException("invalid byte array length");
        return readRawBytes(length);
    }

    /** Reads a root NBT element, returning {@code null} for the END marker. */
    public NbtElement readNbtElement() {
        int type = readUnsignedByte();
        if (type == NbtElement.END_TYPE) return null;
        readNbtString(); // root name; network NBT uses an empty name
        return readNbtPayload(type);
    }

    /** Reads a compound root, matching the common PacketByteBuf API. */
    public NbtCompound readNbt() {
        NbtElement value = readNbtElement();
        if (value == null) return null;
        if (!(value instanceof NbtCompound compound))
            throw new IllegalArgumentException("Expected NBT compound root");
        return compound;
    }

    private NbtElement readNbtPayload(int type) {
        return switch (type) {
            case NbtElement.BYTE_TYPE -> NbtByte.of(readByte());
            case NbtElement.SHORT_TYPE -> NbtShort.of(readShort());
            case NbtElement.INT_TYPE -> NbtInt.of(readInt());
            case NbtElement.LONG_TYPE -> NbtLong.of(readLong());
            case NbtElement.FLOAT_TYPE -> NbtFloat.of(readFloat());
            case NbtElement.DOUBLE_TYPE -> NbtDouble.of(readDouble());
            case NbtElement.STRING_TYPE -> NbtString.of(readNbtString());
            case NbtElement.COMPOUND_TYPE -> {
                NbtCompound compound = new NbtCompound();
                while (true) {
                    int childType = readUnsignedByte();
                    if (childType == NbtElement.END_TYPE) break;
                    String key = readNbtString();
                    compound.put(key, readNbtPayload(childType));
                }
                yield compound;
            }
            default -> throw new IllegalArgumentException("Unsupported NBT tag type: " + type);
        };
    }

    private String readNbtString() {
        int length = readShort() & 0xFFFF;
        if (length > readableBytes()) throw new IllegalArgumentException("invalid NBT string length");
        return new String(readRawBytes(length), StandardCharsets.UTF_8);
    }

    public String readString() { return readString(32767); }

    public String readString(int maxLength) {
        int length = readVarInt();
        if (length < 0 || length > maxLength * 4 || length > readableBytes())
            throw new IllegalArgumentException("invalid string length");
        String value = new String(readRawBytes(length), StandardCharsets.UTF_8);
        if (value.length() > maxLength) throw new IllegalArgumentException("string too long");
        return value;
    }

    /** Alias used by the 1.21.4 packet codec API. */
    public String readUtf() { return readString(); }
    public String readUtf(int maxLength) { return readString(maxLength); }

    public int readVarInt() {
        int value = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            int part = readUnsignedByte();
            value |= (part & 0x7F) << shift;
            if ((part & 0x80) == 0) return value;
        }
        throw new IllegalArgumentException("varint too long");
    }

    public long readVarLong() {
        long value = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            int part = readUnsignedByte();
            value |= (long) (part & 0x7F) << shift;
            if ((part & 0x80) == 0) return value;
        }
        throw new IllegalArgumentException("varlong too long");
    }

    public UUID readUuid() { return new UUID(readLong(), readLong()); }

    public Identifier readIdentifier() {
        Identifier id = Identifier.tryParse(readString());
        if (id == null) throw new IllegalArgumentException("invalid identifier");
        return id;
    }

    public BlockPos readBlockPos() { return BlockPos.fromLong(readLong()); }

    public ItemStack readItemStack() {
        return readBoolean()
            ? new ItemStack(Item.fromRaw(0, readIdentifier().toString()), readUnsignedByte())
            : ItemStack.EMPTY;
    }

    public byte[] toByteArray() {
        byte[] value = new byte[writerIndex()];
        if (value.length > 0) getBytes(0, value);
        return value;
    }

    @Override public PacketByteBuf copy() {
        PacketByteBuf copy = new PacketByteBuf(toByteArray());
        copy.readerIndex(readerIndex());
        return copy;
    }

    private byte[] readRawBytes(int length) {
        byte[] value = new byte[length];
        readBytes(value);
        return value;
    }
}
