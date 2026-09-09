package net.fabricmc.fabric.api.networking.v1;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.Packet;
import net.minecraft.util.Identifier;

/** Packet sender used while the connection is still in the login phase. */
public interface LoginPacketSender extends PacketSender {
    Packet<?> createPacket(Identifier channel, PacketByteBuf payload);

    default void sendPacket(Identifier channel, PacketByteBuf payload) {
        sendPacket(createPacket(channel, payload));
    }

    default void sendPacket(Identifier channel, PacketByteBuf payload,
                            net.minecraft.network.PacketCallbacks callbacks) {
        sendPacket(createPacket(channel, payload), callbacks);
    }
}
