package net.minecraft.server.command;

import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;

public class ServerCommandSource {
    private final ServerPlayerEntity player;
    private final MinecraftServer server;
    public ServerCommandSource(ServerPlayerEntity player, MinecraftServer server) {
        this.player = player; this.server = server;
    }
    public ServerPlayerEntity getPlayer() { return player; }
    public MinecraftServer getServer() { return server; }
    public boolean isExecutedByPlayer() { return player != null; }
    public ServerCommandSource withLevel(int level) { return this; }
}
