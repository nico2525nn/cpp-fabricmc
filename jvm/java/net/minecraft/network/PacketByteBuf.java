package net.minecraft.network;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;

/** Small bounded payload buffer for server-side plugin-channel adapters. */
public class PacketByteBuf {
    private final ByteArrayOutputStream output = new ByteArrayOutputStream();
    public PacketByteBuf writeByte(int value) { output.write(value); return this; }
    public PacketByteBuf writeBoolean(boolean value) { return writeByte(value ? 1 : 0); }
    public PacketByteBuf writeInt(int value) {
        output.write((value >>> 24) & 0xFF); output.write((value >>> 16) & 0xFF);
        output.write((value >>> 8) & 0xFF); output.write(value & 0xFF); return this;
    }
    public PacketByteBuf writeVarInt(int value) {
        while ((value & ~0x7F) != 0) { writeByte((value & 0x7F) | 0x80); value >>>= 7; }
        return writeByte(value);
    }
    public PacketByteBuf writeString(String value) {
        byte[] bytes = (value == null ? "" : value).getBytes(StandardCharsets.UTF_8);
        writeVarInt(bytes.length); output.writeBytes(bytes); return this;
    }
    public byte[] toByteArray() { return output.toByteArray(); }
    public int readableBytes() { return output.size(); }
}
