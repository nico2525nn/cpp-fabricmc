package net.fabricmc.fabric.api.networking.v1;

import net.minecraft.network.packet.Packet;

public interface PacketSender {
    PacketSender NOOP = packet -> {};
    void sendPacket(Packet<?> packet);
    default void sendPacket(net.minecraft.util.Identifier channel, net.minecraft.network.PacketByteBuf payload) { }
    default void send(net.minecraft.network.packet.CustomPayload payload) { }
}
