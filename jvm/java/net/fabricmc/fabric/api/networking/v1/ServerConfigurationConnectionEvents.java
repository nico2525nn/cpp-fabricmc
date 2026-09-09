package net.fabricmc.fabric.api.networking.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerConfigurationNetworkHandler;

public final class ServerConfigurationConnectionEvents {
    private ServerConfigurationConnectionEvents() { }
    @FunctionalInterface public interface Configure {
        void onSendConfiguration(ServerConfigurationNetworkHandler handler, MinecraftServer server);
    }
    @FunctionalInterface public interface Disconnect {
        void onConfigureDisconnect(ServerConfigurationNetworkHandler handler, MinecraftServer server);
    }
    public static final Event<Configure> BEFORE_CONFIGURE = EventFactory.createArrayBacked(
        Configure.class, callbacks -> (handler, server) -> {
            for (Configure callback : callbacks) callback.onSendConfiguration(handler, server);
        });
    public static final Event<Configure> CONFIGURE = EventFactory.createArrayBacked(
        Configure.class, callbacks -> (handler, server) -> {
            for (Configure callback : callbacks) callback.onSendConfiguration(handler, server);
        });
    public static final Event<Disconnect> DISCONNECT = EventFactory.createArrayBacked(
        Disconnect.class, callbacks -> (handler, server) -> {
            for (Disconnect callback : callbacks) callback.onConfigureDisconnect(handler, server);
        });
    public static void clear() { BEFORE_CONFIGURE.clear(); CONFIGURE.clear(); DISCONNECT.clear(); }
}
