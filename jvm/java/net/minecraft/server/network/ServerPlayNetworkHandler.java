package net.minecraft.server.network;

import net.minecraft.network.packet.Packet;
import net.minecraft.text.Text;
import net.fabricmc.fabric.api.networking.v1.PacketSender;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;

public class ServerPlayNetworkHandler {
    private final ServerPlayerEntity player;
    private volatile boolean disconnected;
    public ServerPlayNetworkHandler(ServerPlayerEntity player) {
        this.player = player;
        ServerPlayConnectionEvents.INIT.invoker().onPlayInit(this, player == null ? null : player.getServer());
    }
    public ServerPlayerEntity getPlayer() { return player; }
    public void sendPacket(Packet<?> packet) {
        if (packet != null && player != null) ServerPlayNetworking.send(player, packet);
    }
    public void disconnect(Text reason) {
        if (disconnected) return;
        disconnected = true;
        if (player != null) player.sendMessage(reason, false);
    }
    public boolean isConnectionOpen() { return player != null && !disconnected && !player.isRemoved(); }
    public boolean isDisconnected() { return disconnected; }
    public PacketSender getPacketSender() { return player == null ? PacketSender.NOOP : ServerPlayNetworking.getSender(player); }
}
