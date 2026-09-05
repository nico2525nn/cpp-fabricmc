package net.fabricmc.fabric.api.networking.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;

public final class ServerPlayConnectionEvents {
    private ServerPlayConnectionEvents() {}
    @FunctionalInterface public interface Init {
        void onPlayInit(ServerPlayNetworkHandler handler, MinecraftServer server);
    }
    @FunctionalInterface public interface Join {
        void onPlayReady(ServerPlayNetworkHandler handler, PacketSender sender, MinecraftServer server);
    }
    @FunctionalInterface public interface Disconnect {
        void onPlayDisconnect(ServerPlayNetworkHandler handler, MinecraftServer server);
    }
    public static final Event<Init> INIT = EventFactory.createArrayBacked(
        Init.class, callbacks -> (handler, server) -> {
            for (Init callback : callbacks) callback.onPlayInit(handler, server);
        });
    public static final Event<Join> JOIN = new Event<>(CppModRuntime::registerPlayerJoin, Join.class, callbacks -> (handler, sender, server) -> { for (Join callback : callbacks) callback.onPlayReady(handler, sender, server); });
    public static final Event<Disconnect> DISCONNECT = new Event<>(CppModRuntime::registerPlayerQuit, Disconnect.class, callbacks -> (handler, server) -> { for (Disconnect callback : callbacks) callback.onPlayDisconnect(handler, server); });
    public static void clear() { INIT.clear(); JOIN.clear(); DISCONNECT.clear(); }
}
