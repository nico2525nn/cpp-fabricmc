package net.minecraft.network.packet.c2s.common;

import net.minecraft.network.packet.CustomPayload;
import net.minecraft.network.packet.Packet;

/** Minimal 1.21.4 serverbound custom-payload packet ABI. */
public class CustomPayloadC2SPacket implements Packet<Object> {
    private final CustomPayload payload;
    public CustomPayloadC2SPacket(CustomPayload payload) { this.payload = payload; }
    public CustomPayload payload() { return payload; }
    public CustomPayload getPayload() { return payload; }
    @Override public void apply(Object listener) { }
}
