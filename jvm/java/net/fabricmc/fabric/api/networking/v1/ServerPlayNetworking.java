package net.fabricmc.fabric.api.networking.v1;

import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.Collections;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.network.packet.CustomPayloadS2CPacket;
import net.minecraft.network.packet.Packet;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.Identifier;

/** Server custom-payload API with a bounded Java transport registry. */
public final class ServerPlayNetworking {
    private ServerPlayNetworking() {}
    @FunctionalInterface public interface PlayChannelHandler {
        void receive(MinecraftServer server, ServerPlayerEntity player,
                     ServerPlayNetworkHandler handler, PacketByteBuf buf,
                     PacketSender responseSender);
    }
    @FunctionalInterface public interface PlayPayloadHandler<T extends CustomPayload> {
        void receive(T payload, ServerPlayNetworkHandler handler, PacketSender responseSender);
    }
    public static final class Context {
        private final MinecraftServer server;
        private final ServerPlayerEntity player;
        private final ServerPlayNetworkHandler handler;
        private final PacketSender responseSender;
        private Context(MinecraftServer server, ServerPlayerEntity player, ServerPlayNetworkHandler handler, PacketSender responseSender) { this.server = server; this.player = player; this.handler = handler; this.responseSender = responseSender; }
        public MinecraftServer server() { return server; }
        public ServerPlayerEntity player() { return player; }
        public ServerPlayNetworkHandler handler() { return handler; }
        public PacketSender responseSender() { return responseSender; }
    }
    private static final Map<Identifier, PlayChannelHandler> RECEIVERS = new ConcurrentHashMap<>();
    private static final Map<Identifier, PlayPayloadHandler<?>> PAYLOAD_RECEIVERS = new ConcurrentHashMap<>();
    private static final Map<Long, List<Packet<?>>> OUTBOUND = new ConcurrentHashMap<>();
    public static void registerGlobalReceiver(Identifier channel, PlayChannelHandler handler) {
        if (channel == null || handler == null) throw new NullPointerException("channel/handler");
        if (RECEIVERS.putIfAbsent(channel, handler) != null) throw new IllegalArgumentException("receiver already registered: " + channel);
    }
    public static <T extends CustomPayload> void registerGlobalReceiver(CustomPayload.Id<T> id, PlayPayloadHandler<T> handler) {
        if (id == null || handler == null) throw new NullPointerException("id/handler");
        if (PAYLOAD_RECEIVERS.putIfAbsent(id.id(), handler) != null) throw new IllegalArgumentException("receiver already registered: " + id.id());
    }
    public static void unregisterGlobalReceiver(Identifier channel) { if (channel != null) { RECEIVERS.remove(channel); PAYLOAD_RECEIVERS.remove(channel); } }
    public static boolean canSend(ServerPlayerEntity player, Identifier channel) { return player != null && channel != null && !player.isRemoved(); }
    public static void send(ServerPlayerEntity player, Identifier channel, PacketByteBuf payload) {
        if (!canSend(player, channel)) return;
        OUTBOUND.computeIfAbsent(player.nativeHandle(), ignored -> Collections.synchronizedList(new ArrayList<>())).add(new CustomPayloadS2CPacket(channel, payload));
    }
    /** Sends an already-built play packet through the same safe outbound queue. */
    public static void send(ServerPlayerEntity player, Packet<?> packet) {
        if (player == null || packet == null || player.isRemoved()) return;
        OUTBOUND.computeIfAbsent(player.nativeHandle(), ignored -> Collections.synchronizedList(new ArrayList<>())).add(packet);
    }
    public static <T extends CustomPayload> void send(ServerPlayerEntity player, T payload) {
        if (payload != null && payload.getId() != null) send(player, payload.getId().id(), new PacketByteBuf());
    }
    public static Packet<?> createS2CPacket(Identifier channel, PacketByteBuf payload) { return new CustomPayloadS2CPacket(channel, payload); }
    public static PacketSender getSender(ServerPlayerEntity player) {
        return packet -> { if (player != null && packet != null) OUTBOUND.computeIfAbsent(player.nativeHandle(), ignored -> Collections.synchronizedList(new ArrayList<>())).add(packet); };
    }
    public static void receive(MinecraftServer server, ServerPlayerEntity player, ServerPlayNetworkHandler handler, Identifier channel, PacketByteBuf payload) {
        if (channel == null) return;
        PacketSender sender = getSender(player);
        PlayChannelHandler handlerCallback = RECEIVERS.get(channel);
        if (handlerCallback != null) handlerCallback.receive(server, player, handler, payload == null ? new PacketByteBuf() : payload, sender);
    }
    public static List<Packet<?>> getOutbound(ServerPlayerEntity player) { return player == null ? List.of() : List.copyOf(OUTBOUND.getOrDefault(player.nativeHandle(), List.of())); }
    public static Map<Identifier, PlayChannelHandler> getGlobalReceivers() { return Map.copyOf(RECEIVERS); }
    public static void clear() { RECEIVERS.clear(); PAYLOAD_RECEIVERS.clear(); OUTBOUND.clear(); }
}
