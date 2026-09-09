package net.fabricmc.fabric.api.networking.v1;

import io.netty.buffer.ByteBuf;
import net.minecraft.network.PacketByteBuf;

/** Dependency-free equivalents of Fabric's packet-buffer factory helpers. */
public final class PacketByteBufs {
    private PacketByteBufs() { }

    public static PacketByteBuf empty() { return new PacketByteBuf(); }
    public static PacketByteBuf create() { return new PacketByteBuf(); }

    public static PacketByteBuf readBytes(ByteBuf source, int length) {
        if (source == null) throw new NullPointerException("source");
        if (length < 0 || length > source.readableBytes()) throw new IndexOutOfBoundsException("length");
        byte[] bytes = new byte[length]; source.readBytes(bytes); return new PacketByteBuf(bytes);
    }

    public static PacketByteBuf readSlice(ByteBuf source, int length) { return readBytes(source, length); }
    public static PacketByteBuf readRetainedSlice(ByteBuf source, int length) { return readBytes(source, length); }
    public static PacketByteBuf copy(ByteBuf source) {
        if (source == null) throw new NullPointerException("source");
        return copy(source, source.readerIndex(), source.readableBytes());
    }
    public static PacketByteBuf copy(ByteBuf source, int index, int length) {
        if (source == null) throw new NullPointerException("source");
        if (index < 0 || length < 0 || index > source.writerIndex() - length)
            throw new IndexOutOfBoundsException("range");
        byte[] bytes = new byte[length]; source.getBytes(index, bytes); return new PacketByteBuf(bytes);
    }
    public static PacketByteBuf slice(ByteBuf source) { return copy(source); }
    public static PacketByteBuf retainedSlice(ByteBuf source) { return copy(source); }
    public static PacketByteBuf slice(ByteBuf source, int index, int length) { return copy(source, index, length); }
    public static PacketByteBuf retainedSlice(ByteBuf source, int index, int length) { return copy(source, index, length); }
    public static PacketByteBuf duplicate(ByteBuf source) { return copy(source); }
    public static PacketByteBuf retainedDuplicate(ByteBuf source) { return copy(source); }
}
