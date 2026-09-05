package net.minecraft.server.network;

import net.minecraft.network.packet.Packet;
import net.minecraft.text.Text;
import net.fabricmc.fabric.api.networking.v1.PacketSender;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;

public class ServerPlayNetworkHandler {
    private final ServerPlayerEntity player;
    public ServerPlayNetworkHandler(ServerPlayerEntity player) { this.player = player; }
    public ServerPlayerEntity getPlayer() { return player; }
    public void sendPacket(Packet<?> packet) {
        if (packet != null && player != null) ServerPlayNetworking.send(player, packet);
    }
    public void disconnect(Text reason) {
        if (player != null) player.sendMessage(reason, false);
    }
    public boolean isConnectionOpen() { return player != null && !player.isDisconnected(); }
    public PacketSender getPacketSender() { return player == null ? PacketSender.NOOP : ServerPlayNetworking.getSender(player); }
}
