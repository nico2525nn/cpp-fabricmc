package net.minecraft.server;

import net.minecraft.util.NativeAccess;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.network.ClientConnection;
import net.minecraft.server.network.ConnectedClientData;
import java.util.Optional;
import java.util.function.Function;

/** Native-backed online-player query surface. */
public final class PlayerManager {
    private int viewDistance = 10;
    private int simulationDistance = 10;
    public List<ServerPlayerEntity> getPlayerList() {
        int count = NativeAccess.onlinePlayerCount();
        List<ServerPlayerEntity> result = new ArrayList<>(Math.max(0, count));
        for (int i = 0; i < count; ++i) {
            ServerPlayerEntity player = ServerPlayerEntity.of(NativeAccess.onlinePlayerHandle(i));
            if (player != null) result.add(player);
        }
        return List.copyOf(result);
    }
    public ServerPlayerEntity getPlayer(String name) {
        if (name == null) return null;
        for (ServerPlayerEntity player : getPlayerList())
            if (name.equals(player.getName().getString())) return player;
        return null;
    }
    public ServerPlayerEntity getPlayer(java.util.UUID uuid) {
        if (uuid == null) return null;
        for (ServerPlayerEntity player : getPlayerList()) if (uuid.equals(player.getUuid())) return player;
        return null;
    }
    public int getCurrentPlayerCount() { return getPlayerList().size(); }
    public void broadcast(net.minecraft.text.Text message, boolean overlay) { for (ServerPlayerEntity player : getPlayerList()) player.sendMessage(message, overlay); }
    public void broadcast(net.minecraft.text.Text message,
                          Function<ServerPlayerEntity, ? extends net.minecraft.text.Text> perPlayer,
                          boolean overlay) {
        for (ServerPlayerEntity player : getPlayerList()) {
            net.minecraft.text.Text target = perPlayer == null ? message : perPlayer.apply(player);
            if (target != null) player.sendMessage(target, overlay);
        }
    }
    public void broadcast(net.minecraft.text.Text message) { broadcast(message, false); }
    public void broadcast(net.minecraft.network.message.SignedMessage message,
                          ServerPlayerEntity sender,
                          net.minecraft.network.message.MessageType.Parameters parameters) { }
    public void broadcast(net.minecraft.network.message.SignedMessage message,
                          ServerCommandSource source,
                          net.minecraft.network.message.MessageType.Parameters parameters) { }
    /** Reload callback exposed by the 1.21.4 server player manager. */
    public void onDataPacksReloaded() { }
    public void placeNewPlayer(ClientConnection connection, ServerPlayerEntity player,
                               ConnectedClientData clientData) {
        if (player != null && !getPlayerList().contains(player))
            NativeAccess.log("DEBUG", "placeNewPlayer " + player.getName().getString());
    }
    /** Yarn name used by the 1.21.4 mapped server mixins. */
    public void onPlayerConnect(ClientConnection connection, ServerPlayerEntity player,
                                ConnectedClientData clientData) {
        placeNewPlayer(connection, player, clientData);
    }
    /** World-info synchronization hook retained for server-player mixins. */
    public void sendWorldInfo(ServerPlayerEntity player, net.minecraft.server.world.ServerWorld world) { }
    public int getViewDistance() { return viewDistance; }
    public void setViewDistance(int viewDistance) { this.viewDistance = Math.max(2, viewDistance); }
    public int getSimulationDistance() { return simulationDistance; }
    public void setSimulationDistance(int simulationDistance) {
        this.simulationDistance = Math.max(2, simulationDistance);
    }
    public Optional<net.minecraft.nbt.NbtCompound> loadPlayerData(ServerPlayerEntity player) {
        return Optional.empty();
    }
    public ServerPlayerEntity respawnPlayer(ServerPlayerEntity player, boolean alive,
                                            net.minecraft.entity.Entity.RemovalReason reason) {
        if (player != null && reason != null) player.remove(reason);
        return player;
    }
}
