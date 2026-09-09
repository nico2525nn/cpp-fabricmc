package net.fabricmc.fabric.api.networking.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerLoginNetworkHandler;

public final class ServerLoginConnectionEvents {
    private ServerLoginConnectionEvents() { }
    @FunctionalInterface public interface Init {
        void onLoginInit(ServerLoginNetworkHandler handler, MinecraftServer server);
    }
    @FunctionalInterface public interface QueryStart {
        void onLoginStart(ServerLoginNetworkHandler handler, MinecraftServer server,
                          LoginPacketSender sender, ServerLoginNetworking.LoginSynchronizer synchronizer);
    }
    @FunctionalInterface public interface Disconnect {
        void onLoginDisconnect(ServerLoginNetworkHandler handler, MinecraftServer server);
    }
    public static final Event<Init> INIT = EventFactory.createArrayBacked(
        Init.class, callbacks -> (handler, server) -> {
            for (Init callback : callbacks) callback.onLoginInit(handler, server);
        });
    public static final Event<QueryStart> QUERY_START = EventFactory.createArrayBacked(
        QueryStart.class, callbacks -> (handler, server, sender, synchronizer) -> {
            for (QueryStart callback : callbacks) callback.onLoginStart(handler, server, sender, synchronizer);
        });
    public static final Event<Disconnect> DISCONNECT = EventFactory.createArrayBacked(
        Disconnect.class, callbacks -> (handler, server) -> {
            for (Disconnect callback : callbacks) callback.onLoginDisconnect(handler, server);
        });
    public static void clear() { INIT.clear(); QUERY_START.clear(); DISCONNECT.clear(); }
}
