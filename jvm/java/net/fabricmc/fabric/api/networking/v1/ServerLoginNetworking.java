package net.fabricmc.fabric.api.networking.v1;

import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Future;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.Packet;
import net.minecraft.network.packet.s2c.common.CustomPayloadS2CPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerLoginNetworkHandler;
import net.minecraft.util.Identifier;

/** Login query receiver registry used by server-side Fabric mods. */
public final class ServerLoginNetworking {
    private ServerLoginNetworking() { }
    @FunctionalInterface public interface LoginQueryResponseHandler {
        void receive(MinecraftServer server, ServerLoginNetworkHandler handler, boolean understood,
                     PacketByteBuf buf, LoginSynchronizer synchronizer, PacketSender responseSender);
    }
    @FunctionalInterface public interface LoginSynchronizer { void waitFor(Future<?> future); }
    private static final Map<Identifier, LoginQueryResponseHandler> GLOBAL = new ConcurrentHashMap<>();
    private static final Map<ServerLoginNetworkHandler, Map<Identifier, LoginQueryResponseHandler>> LOCAL =
        new ConcurrentHashMap<>();

    public static boolean registerGlobalReceiver(Identifier id, LoginQueryResponseHandler handler) {
        if (id == null || handler == null) throw new NullPointerException("id/handler");
        return GLOBAL.putIfAbsent(id, handler) == null;
    }
    public static LoginQueryResponseHandler unregisterGlobalReceiver(Identifier id) {
        return id == null ? null : GLOBAL.remove(id);
    }
    public static Set<Identifier> getGlobalReceivers() { return Set.copyOf(GLOBAL.keySet()); }
    public static boolean registerReceiver(ServerLoginNetworkHandler loginHandler, Identifier id,
                                            LoginQueryResponseHandler handler) {
        if (loginHandler == null || id == null || handler == null) throw new NullPointerException("handler/id");
        return LOCAL.computeIfAbsent(loginHandler, ignored -> new ConcurrentHashMap<>()).putIfAbsent(id, handler) == null;
    }
    public static LoginQueryResponseHandler unregisterReceiver(ServerLoginNetworkHandler handler, Identifier id) {
        if (handler == null || id == null) return null;
        Map<Identifier, LoginQueryResponseHandler> receivers = LOCAL.get(handler);
        if (receivers == null) return null;
        LoginQueryResponseHandler result = receivers.remove(id);
        if (receivers.isEmpty()) LOCAL.remove(handler, receivers);
        return result;
    }
    public static MinecraftServer getServer(ServerLoginNetworkHandler handler) {
        return handler == null ? null : handler.getServer();
    }
    public static LoginQueryResponseHandler find(ServerLoginNetworkHandler handler, Identifier id) {
        Map<Identifier, LoginQueryResponseHandler> local = LOCAL.get(handler);
        LoginQueryResponseHandler result = local == null ? null : local.get(id);
        return result == null ? GLOBAL.get(id) : result;
    }
    public static LoginPacketSender getSender(ServerLoginNetworkHandler handler) {
        if (handler == null) return null;
        return new LoginPacketSender() {
            @Override public Packet<?> createPacket(Identifier id, PacketByteBuf payload) {
                return new CustomPayloadS2CPacket(id, payload);
            }
            @Override public Packet<?> createPacket(net.minecraft.network.packet.CustomPayload payload) {
                return new CustomPayloadS2CPacket(payload);
            }
            @Override public void sendPacket(Packet<?> packet, net.minecraft.network.PacketCallbacks callbacks) {
                handler.sendPacket(packet); if (callbacks != null) callbacks.onSuccess();
            }
            @Override public void disconnect(net.minecraft.text.Text reason) { handler.disconnect(reason); }
        };
    }
    public static void clear() { GLOBAL.clear(); LOCAL.clear(); }
}
