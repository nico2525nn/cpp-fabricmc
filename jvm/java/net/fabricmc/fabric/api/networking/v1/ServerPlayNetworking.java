package net.fabricmc.fabric.api.networking.v1;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.Identifier;

/** Server plugin-channel registration surface with an explicit native transport boundary. */
public final class ServerPlayNetworking {
    private ServerPlayNetworking() {}
    @FunctionalInterface public interface PlayChannelHandler {
        void receive(MinecraftServer server, ServerPlayerEntity player,
                     ServerPlayNetworkHandler handler, PacketByteBuf buf,
                     PacketSender responseSender);
    }
    private static final Map<Identifier, PlayChannelHandler> RECEIVERS = new ConcurrentHashMap<>();
    public static void registerGlobalReceiver(Identifier channel, PlayChannelHandler handler) {
        if (channel == null || handler == null) throw new NullPointerException("channel/handler");
        if (RECEIVERS.putIfAbsent(channel, handler) != null)
            throw new IllegalArgumentException("receiver already registered: " + channel);
    }
    public static boolean canSend(ServerPlayerEntity player, Identifier channel) {
        return player != null && channel != null;
    }
    public static void send(ServerPlayerEntity player, Identifier channel, PacketByteBuf payload) {
        // No protocol ID is invented here; a native custom-payload route must
        // be added before this method can claim client transport support.
    }
    public static void clear() { RECEIVERS.clear(); }
}
