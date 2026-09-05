package net.fabricmc.fabric.api.networking.v1;

import net.minecraft.network.packet.Packet;

public interface PacketSender {
    PacketSender NOOP = packet -> {};
    void sendPacket(Packet<?> packet);
}
