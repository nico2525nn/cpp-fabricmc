package net.fabricmc.fabric.api.networking.v1;

import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.network.packet.Packet;
import net.minecraft.network.packet.s2c.common.CustomPayloadS2CPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerConfigurationNetworkHandler;
import net.minecraft.util.Identifier;

/** Configuration-phase payload registration and dispatch. */
public final class ServerConfigurationNetworking {
    private ServerConfigurationNetworking() { }
    @FunctionalInterface public interface ConfigurationPacketHandler<T extends CustomPayload> {
        void receive(T payload, Context context);
    }
    public interface Context {
        MinecraftServer server();
        ServerConfigurationNetworkHandler networkHandler();
        PacketSender responseSender();
    }

    private static final Map<Identifier, ConfigurationPacketHandler<?>> GLOBAL = new ConcurrentHashMap<>();
    private static final Map<ServerConfigurationNetworkHandler, Map<Identifier, ConfigurationPacketHandler<?>>> LOCAL =
        new ConcurrentHashMap<>();

    public static <T extends CustomPayload> boolean registerGlobalReceiver(CustomPayload.Id<T> id,
                                                                            ConfigurationPacketHandler<T> handler) {
        if (id == null || handler == null) throw new NullPointerException("id/handler");
        return GLOBAL.putIfAbsent(id.id(), handler) == null;
    }
    public static ConfigurationPacketHandler<?> unregisterGlobalReceiver(Identifier id) {
        return id == null ? null : GLOBAL.remove(id);
    }
    public static Set<Identifier> getGlobalReceivers() { return Set.copyOf(GLOBAL.keySet()); }
    public static <T extends CustomPayload> boolean registerReceiver(ServerConfigurationNetworkHandler networkHandler,
                                                                       CustomPayload.Id<T> id,
                                                                       ConfigurationPacketHandler<T> handler) {
        if (networkHandler == null || id == null || handler == null) throw new NullPointerException("handler/id");
        return LOCAL.computeIfAbsent(networkHandler, ignored -> new ConcurrentHashMap<>())
            .putIfAbsent(id.id(), handler) == null;
    }
    public static ConfigurationPacketHandler<?> unregisterReceiver(ServerConfigurationNetworkHandler handler,
                                                                    Identifier id) {
        if (handler == null || id == null) return null;
        Map<Identifier, ConfigurationPacketHandler<?>> receivers = LOCAL.get(handler);
        if (receivers == null) return null;
        ConfigurationPacketHandler<?> result = receivers.remove(id);
        if (receivers.isEmpty()) LOCAL.remove(handler, receivers);
        return result;
    }
    public static Set<Identifier> getReceived(ServerConfigurationNetworkHandler handler) {
        Set<Identifier> result = ConcurrentHashMap.newKeySet(); result.addAll(GLOBAL.keySet());
        Map<Identifier, ConfigurationPacketHandler<?>> local = LOCAL.get(handler);
        if (local != null) result.addAll(local.keySet()); return Set.copyOf(result);
    }
    public static Set<Identifier> getSendable(ServerConfigurationNetworkHandler handler) {
        return PayloadTypeRegistry.configurationS2C().getIds();
    }
    public static boolean canSend(ServerConfigurationNetworkHandler handler, Identifier id) {
        return handler != null && handler.isConnectionOpen() && id != null;
    }
    public static boolean canSend(ServerConfigurationNetworkHandler handler, CustomPayload.Id<?> id) {
        return id != null && canSend(handler, id.id());
    }
    @SuppressWarnings("unchecked")
    public static Packet<net.minecraft.network.listener.ClientCommonPacketListener> createS2CPacket(CustomPayload payload) {
        return (Packet<net.minecraft.network.listener.ClientCommonPacketListener>) (Packet<?>) new CustomPayloadS2CPacket(payload);
    }
    public static PacketSender getSender(ServerConfigurationNetworkHandler handler) {
        if (handler == null) return PacketSender.NOOP;
        return new PacketSender() {
            @Override public Packet<?> createPacket(CustomPayload payload) { return createS2CPacket(payload); }
            @Override public void sendPacket(Packet<?> packet, net.minecraft.network.PacketCallbacks callbacks) {
                handler.sendPacket(packet); if (callbacks != null) callbacks.onSuccess();
            }
            @Override public void sendPacket(Identifier id, net.minecraft.network.PacketByteBuf data) {
                if (id != null) handler.sendPacket(new CustomPayloadS2CPacket(id, data));
            }
            @Override public void disconnect(net.minecraft.text.Text reason) { handler.disconnect(reason); }
        };
    }
    public static void send(ServerConfigurationNetworkHandler handler, CustomPayload payload) {
        if (canSend(handler, payload == null ? null : payload.getId())) handler.sendPacket(createS2CPacket(payload));
    }
    public static MinecraftServer getServer(ServerConfigurationNetworkHandler handler) {
        return handler == null ? null : handler.getServer();
    }
    public static boolean isReconfiguring(ServerConfigurationNetworkHandler handler) {
        return handler != null && handler.isReconfiguring();
    }
    public static <T extends CustomPayload> boolean receive(ServerConfigurationNetworkHandler handler, T payload) {
        if (handler == null || payload == null || payload.getId() == null) return false;
        Map<Identifier, ConfigurationPacketHandler<?>> local = LOCAL.get(handler);
        ConfigurationPacketHandler<?> raw = local == null ? null : local.get(payload.getId().id());
        if (raw == null) raw = GLOBAL.get(payload.getId().id());
        if (raw == null) return false;
        invoke(raw, payload, new ContextImpl(handler)); return true;
    }
    @SuppressWarnings({"rawtypes", "unchecked"})
    private static void invoke(ConfigurationPacketHandler handler, CustomPayload payload, Context context) {
        handler.receive(payload, context);
    }
    private record ContextImpl(ServerConfigurationNetworkHandler networkHandler) implements Context {
        @Override public MinecraftServer server() { return networkHandler.getServer(); }
        @Override public PacketSender responseSender() { return getSender(networkHandler); }
    }
    public static void clear() { GLOBAL.clear(); LOCAL.clear(); }
}
