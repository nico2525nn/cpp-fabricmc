package net.minecraft.network.packet;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.util.Identifier;

public class CustomPayloadS2CPacket implements Packet<Object> {
    private final CustomPayload.Id<? extends CustomPayload> id;
    private final PacketByteBuf data;
    public CustomPayloadS2CPacket(Identifier id, PacketByteBuf data) { this(new CustomPayload.Id<>(id), data); }
    public CustomPayloadS2CPacket(CustomPayload.Id<? extends CustomPayload> id, PacketByteBuf data) { this.id = id; this.data = data == null ? new PacketByteBuf() : data.copy(); }
    public CustomPayload.Id<? extends CustomPayload> payloadId() { return id; }
    public Identifier getChannel() { return id.id(); }
    public PacketByteBuf getData() { return data.copy(); }
}
