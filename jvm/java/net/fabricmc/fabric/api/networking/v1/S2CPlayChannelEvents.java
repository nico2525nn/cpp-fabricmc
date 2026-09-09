package net.fabricmc.fabric.api.networking.v1;

import java.util.List;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.util.Identifier;

public final class S2CPlayChannelEvents {
    private S2CPlayChannelEvents() { }
    @FunctionalInterface public interface Register {
        void onChannelRegister(ServerPlayNetworkHandler handler, PacketSender sender,
                               MinecraftServer server, List<Identifier> channels);
    }
    @FunctionalInterface public interface Unregister {
        void onChannelUnregister(ServerPlayNetworkHandler handler, PacketSender sender,
                                 MinecraftServer server, List<Identifier> channels);
    }
    public static final Event<Register> REGISTER = EventFactory.createArrayBacked(
        Register.class, callbacks -> (handler, sender, server, channels) -> {
            for (Register callback : callbacks) callback.onChannelRegister(handler, sender, server, channels);
        });
    public static final Event<Unregister> UNREGISTER = EventFactory.createArrayBacked(
        Unregister.class, callbacks -> (handler, sender, server, channels) -> {
            for (Unregister callback : callbacks) callback.onChannelUnregister(handler, sender, server, channels);
        });
    public static void clear() { REGISTER.clear(); UNREGISTER.clear(); }
}
