package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.Packet;

/** Serverbound acknowledgement for the configuration-to-play transition. */
public final class AcknowledgeReconfigurationC2SPacket implements Packet<Object> {
    public static final AcknowledgeReconfigurationC2SPacket INSTANCE =
        new AcknowledgeReconfigurationC2SPacket();
    public static final PacketCodec<PacketByteBuf, AcknowledgeReconfigurationC2SPacket> CODEC =
        PacketCodec.unit(INSTANCE);

    public AcknowledgeReconfigurationC2SPacket() { }

    @Override
    public void apply(Object listener) { }
}
