package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.Packet;

/** Serverbound pick-entity packet from the 1.21.4 play protocol. */
public final class PickItemFromEntityC2SPacket implements Packet<Object> {
    public static final PacketCodec<PacketByteBuf, PickItemFromEntityC2SPacket> CODEC =
        PacketCodec.ofLegacy((buffer, packet) -> { }, buffer ->
            new PickItemFromEntityC2SPacket(0, false));

    private final int id;
    private final boolean includeData;

    public PickItemFromEntityC2SPacket(int id, boolean includeData) {
        this.id = id;
        this.includeData = includeData;
    }

    public int id() { return id; }
    public boolean includeData() { return includeData; }

    @Override
    public void apply(Object listener) { }
}
