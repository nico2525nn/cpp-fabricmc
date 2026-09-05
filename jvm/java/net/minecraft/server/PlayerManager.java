package net.minecraft.server;

import cppfm.bridge.NativeBridge;
import net.minecraft.util.NativeAccess;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.server.network.ServerPlayerEntity;

/** Native-backed online-player query surface. */
public final class PlayerManager {
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
    public void broadcast(net.minecraft.text.Text message) { broadcast(message, false); }
}
