package net.minecraft.server;

import cppfm.bridge.NativeBridge;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.server.network.ServerPlayerEntity;

/** Native-backed online-player query surface. */
public final class PlayerManager {
    public List<ServerPlayerEntity> getPlayerList() {
        int count = NativeBridge.nativeOnlinePlayerCount();
        List<ServerPlayerEntity> result = new ArrayList<>(Math.max(0, count));
        for (int i = 0; i < count; ++i) {
            ServerPlayerEntity player = ServerPlayerEntity.of(NativeBridge.nativeOnlinePlayerHandle(i));
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
}
