package net.minecraft.server.network;

public class ServerPlayNetworkHandler {
    private final ServerPlayerEntity player;
    public ServerPlayNetworkHandler(ServerPlayerEntity player) { this.player = player; }
    public ServerPlayerEntity getPlayer() { return player; }
}
