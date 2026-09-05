package net.minecraft.network.packet.s2c.common;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.util.Identifier;

/** Canonical 1.21.4 package adapter for the common custom-payload packet. */
public class CustomPayloadS2CPacket extends net.minecraft.network.packet.CustomPayloadS2CPacket {
    public CustomPayloadS2CPacket(Identifier id, PacketByteBuf data) { super(id, data); }
    public CustomPayloadS2CPacket(CustomPayload.Id<? extends CustomPayload> id, PacketByteBuf data) {
        super(id, data);
    }
    public CustomPayload payload() { return null; }
}
