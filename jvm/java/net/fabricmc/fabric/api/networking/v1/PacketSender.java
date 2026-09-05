package net.fabricmc.fabric.api.networking.v1;

import net.minecraft.network.packet.Packet;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.text.Text;

public interface PacketSender {
    PacketSender NOOP = new PacketSender() {
        @Override public Packet<?> createPacket(CustomPayload payload) { return null; }
        @Override public void sendPacket(Packet<?> packet, net.minecraft.network.PacketCallbacks callback) { }
        @Override public void disconnect(Text reason) { }
    };
    Packet<?> createPacket(CustomPayload payload);
    default void sendPacket(Packet<?> packet) { sendPacket(packet, null); }
    default void sendPacket(CustomPayload payload) { sendPacket(createPacket(payload)); }
    void sendPacket(Packet<?> packet, net.minecraft.network.PacketCallbacks callback);
    default void sendPacket(CustomPayload payload, net.minecraft.network.PacketCallbacks callback) {
        sendPacket(createPacket(payload), callback);
    }
    void disconnect(Text reason);
    default void sendPacket(net.minecraft.util.Identifier channel, net.minecraft.network.PacketByteBuf payload) { }
    default void send(CustomPayload payload) { sendPacket(payload); }
}
