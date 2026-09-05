package net.fabricmc.fabric.api.networking.v1;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.PacketCallbacks;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.network.packet.Packet;
import net.minecraft.network.packet.s2c.common.CustomPayloadS2CPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.Identifier;
import net.minecraft.util.NativeAccess;
import net.minecraft.text.Text;

/**
 * Server play networking adapter with global/per-connection receivers and a
 * deterministic outbound queue for the native transport.
 */
public final class ServerPlayNetworking {
    private ServerPlayNetworking() { }

    /** Legacy raw-payload callback retained for the embedded API. */
    @FunctionalInterface
    public interface PlayChannelHandler {
        void receive(MinecraftServer server, ServerPlayerEntity player,
                     ServerPlayNetworkHandler handler, PacketByteBuf buf,
                     PacketSender responseSender);
    }

    /** Modern Fabric payload callback. All callbacks run on the server thread. */
    @FunctionalInterface
    public interface PlayPayloadHandler<T extends CustomPayload> {
        void receive(T payload, Context context);
    }

    /** Explicit adapter for mods that still use the old three-argument shape. */
    @FunctionalInterface
    public interface LegacyPlayPayloadHandler<T extends CustomPayload> {
        void receive(T payload, ServerPlayNetworkHandler handler, PacketSender responseSender);
    }

    public static final class Context {
        private final MinecraftServer server;
        private final ServerPlayerEntity player;
        private final ServerPlayNetworkHandler handler;
        private final PacketSender responseSender;

        private Context(MinecraftServer server, ServerPlayerEntity player,
                        ServerPlayNetworkHandler handler, PacketSender responseSender) {
            this.server = server; this.player = player; this.handler = handler;
            this.responseSender = responseSender;
        }
        public MinecraftServer server() { return server; }
        public ServerPlayerEntity player() { return player; }
        public ServerPlayNetworkHandler handler() { return handler; }
        public ServerPlayNetworkHandler networkHandler() { return handler; }
        public PacketSender responseSender() { return responseSender; }
        public void runOnServer(Runnable task) { if (server != null) server.execute(task); }
        public void disconnect(net.minecraft.text.Text reason) { if (handler != null) handler.disconnect(reason); }
    }

    private static final Map<Identifier, PlayChannelHandler> GLOBAL_RECEIVERS = new ConcurrentHashMap<>();
    private static final Map<Identifier, PlayPayloadHandler<?>> GLOBAL_PAYLOAD_RECEIVERS = new ConcurrentHashMap<>();
    private static final Map<ServerPlayNetworkHandler, Map<Identifier, PlayPayloadHandler<?>>> RECEIVERS = new ConcurrentHashMap<>();
    private static final Map<Long, List<Packet<?>>> OUTBOUND = new ConcurrentHashMap<>();

    public static boolean registerGlobalReceiver(Identifier channel, PlayChannelHandler handler) {
        if (channel == null || handler == null) throw new NullPointerException("channel/handler");
        if (GLOBAL_RECEIVERS.putIfAbsent(channel, handler) != null) return false;
        return true;
    }

    public static <T extends CustomPayload> boolean registerGlobalReceiver(CustomPayload.Id<T> id,
                                                                            PlayPayloadHandler<T> handler) {
        if (id == null || handler == null) throw new NullPointerException("id/handler");
        if (GLOBAL_PAYLOAD_RECEIVERS.putIfAbsent(id.id(), handler) != null) return false;
        return true;
    }

    public static <T extends CustomPayload> boolean registerGlobalReceiver(CustomPayload.Id<T> id,
                                                                            LegacyPlayPayloadHandler<T> handler) {
        if (id == null || handler == null) throw new NullPointerException("id/handler");
        return registerGlobalReceiver(id, (payload, context) ->
            handler.receive(payload, context.handler(), context.responseSender()));
    }

    public static <T extends CustomPayload> boolean registerReceiver(ServerPlayNetworkHandler networkHandler,
                                                                      CustomPayload.Id<T> id,
                                                                      PlayPayloadHandler<T> handler) {
        if (networkHandler == null || id == null || handler == null)
            throw new NullPointerException("networkHandler/id/handler");
        Map<Identifier, PlayPayloadHandler<?>> receivers = RECEIVERS.computeIfAbsent(networkHandler,
            ignored -> new ConcurrentHashMap<>());
        return receivers.putIfAbsent(id.id(), handler) == null;
    }

    public static <T extends CustomPayload> boolean registerReceiver(ServerPlayNetworkHandler networkHandler,
                                                                      CustomPayload.Id<T> id,
                                                                      LegacyPlayPayloadHandler<T> handler) {
        if (handler == null) throw new NullPointerException("handler");
        return registerReceiver(networkHandler, id, (payload, context) ->
            handler.receive(payload, context.handler(), context.responseSender()));
    }

    public static PlayPayloadHandler<?> unregisterGlobalReceiver(Identifier channel) {
        if (channel == null) return null;
        PlayPayloadHandler<?> removed = GLOBAL_PAYLOAD_RECEIVERS.remove(channel);
        PlayChannelHandler legacy = GLOBAL_RECEIVERS.remove(channel);
        if (removed != null) return removed;
        if (legacy == null) return null;
        return new PlayPayloadHandler<CustomPayload>() {
            @Override public void receive(CustomPayload payload, Context context) {
                legacy.receive(context.server(), context.player(), context.handler(),
                    new PacketByteBuf(), context.responseSender());
            }
        };
    }

    /** Removes only a legacy raw-channel receiver. */
    public static PlayChannelHandler unregisterLegacyGlobalReceiver(Identifier channel) {
        return channel == null ? null : GLOBAL_RECEIVERS.remove(channel);
    }

    public static <T extends CustomPayload> PlayPayloadHandler<T> unregisterGlobalReceiver(CustomPayload.Id<T> id) {
        if (id == null) return null;
        @SuppressWarnings("unchecked") PlayPayloadHandler<T> removed =
            (PlayPayloadHandler<T>) GLOBAL_PAYLOAD_RECEIVERS.remove(id.id());
        return removed;
    }

    public static <T extends CustomPayload> boolean unregisterReceiver(ServerPlayNetworkHandler networkHandler,
                                                                         CustomPayload.Id<T> id) {
        if (networkHandler == null || id == null) return false;
        Map<Identifier, PlayPayloadHandler<?>> receivers = RECEIVERS.get(networkHandler);
        if (receivers == null) return false;
        boolean removed = receivers.remove(id.id()) != null;
        if (receivers.isEmpty()) RECEIVERS.remove(networkHandler, receivers);
        return removed;
    }

    public static PlayPayloadHandler<?> unregisterReceiver(ServerPlayNetworkHandler networkHandler,
                                                           Identifier channel) {
        if (networkHandler == null || channel == null) return null;
        Map<Identifier, PlayPayloadHandler<?>> receivers = RECEIVERS.get(networkHandler);
        if (receivers == null) return null;
        PlayPayloadHandler<?> removed = receivers.remove(channel);
        if (receivers.isEmpty()) RECEIVERS.remove(networkHandler, receivers);
        return removed;
    }

    public static boolean canSend(ServerPlayerEntity player, Identifier channel) {
        return player != null && channel != null && !player.isRemoved();
    }

    /** Object overload keeps null calls source-compatible while accepting a network handler. */
    public static boolean canSend(Object connection, Identifier channel) {
        if (connection instanceof ServerPlayNetworkHandler handler)
            return handler.isConnectionOpen() && channel != null;
        return connection instanceof ServerPlayerEntity player && canSend(player, channel);
    }

    public static boolean canSend(ServerPlayerEntity player, CustomPayload.Id<?> id) {
        return player != null && id != null && canSend(player, id.id());
    }

    /** Handler form routed through Object to keep {@code canSend(null, id)} unambiguous. */
    public static boolean canSend(Object connection, CustomPayload.Id<?> id) {
        return connection instanceof ServerPlayNetworkHandler handler
            ? id != null && canSend(handler, id.id())
            : connection instanceof ServerPlayerEntity player && canSend(player, id);
    }

    public static void send(ServerPlayerEntity player, Identifier channel, PacketByteBuf payload) {
        if (!canSend(player, channel)) return;
        PacketByteBuf safePayload = payload == null ? new PacketByteBuf() : payload.copy();
        queue(player, new CustomPayloadS2CPacket(channel, safePayload));
        NativeAccess.sendPluginMessage(player.nativeHandle(), channel.toString(), safePayload.toByteArray(), 1);
    }

    public static void send(ServerPlayerEntity player, Packet<?> packet) {
        if (player == null || packet == null || player.isRemoved()) return;
        queue(player, packet);
    }

    public static <T extends CustomPayload> void send(ServerPlayerEntity player, T payload) {
        if (payload == null || payload.getId() == null || !canSend(player, payload.getId().id())) return;
        Packet<?> packet = createS2CPacket(payload);
        if (!(packet instanceof CustomPayloadS2CPacket customPacket)) return;
        queue(player, packet);
        NativeAccess.sendPluginMessage(player.nativeHandle(), payload.getId().id().toString(),
            customPacket.getData().toByteArray(), 1);
    }

    public static Packet<?> createS2CPacket(Identifier channel, PacketByteBuf payload) {
        return new CustomPayloadS2CPacket(channel, payload);
    }

    public static <T extends CustomPayload> Packet<?> createS2CPacket(T payload) {
        if (payload == null || payload.getId() == null) return null;
        return new CustomPayloadS2CPacket(payload, encode(payload));
    }

    public static PacketSender getSender(ServerPlayerEntity player) {
        if (player == null) return PacketSender.NOOP;
        return new PacketSender() {
            @Override public Packet<?> createPacket(CustomPayload payload) { return createS2CPacket(payload); }
            @Override public void sendPacket(Packet<?> packet, PacketCallbacks callback) {
                ServerPlayNetworking.send(player, packet);
                if (callback != null) callback.onSuccess();
            }
            @Override public void sendPacket(Identifier channel, PacketByteBuf payload) { ServerPlayNetworking.send(player, channel, payload); }
            @Override public void send(CustomPayload payload) { ServerPlayNetworking.send(player, payload); }
            @Override public void disconnect(Text reason) { if (player.getNetworkHandler() != null) player.getNetworkHandler().disconnect(reason); }
        };
    }

    public static PacketSender getSender(ServerPlayNetworkHandler handler) {
        return handler == null ? PacketSender.NOOP : getSender(handler.getPlayer());
    }

    /** Dispatch a raw channel; true means a receiver accepted/handled it. */
    public static boolean receive(MinecraftServer server, ServerPlayerEntity player,
                                  ServerPlayNetworkHandler handler, Identifier channel,
                                  PacketByteBuf payload) {
        if (channel == null) return false;
        PacketSender sender = getSender(player);
        PlayChannelHandler raw = GLOBAL_RECEIVERS.get(channel);
        if (raw != null) {
            raw.receive(server, player, handler, payload == null ? new PacketByteBuf() : payload.copy(), sender);
            return true;
        }
        Object codecValue = PayloadTypeRegistry.playC2S().getCodec(new CustomPayload.Id<>(channel));
        if (codecValue instanceof PacketCodec<?, ?> codec) {
            try {
                RegistryByteBuf data = new RegistryByteBuf(payload == null ? null : payload.toByteArray());
                Object decoded = decode(codec, data);
                if (decoded instanceof CustomPayload decodedPayload)
                    return receive(server, player, handler, decodedPayload);
            } catch (RuntimeException ignored) { return false; }
        }
        return false;
    }

    /** Dispatch an already-decoded payload to a per-handler receiver first. */
    public static boolean receive(MinecraftServer server, ServerPlayerEntity player,
                                  ServerPlayNetworkHandler handler, CustomPayload payload) {
        if (payload == null || payload.getId() == null) return false;
        Identifier channel = payload.getId().id();
        PacketSender sender = getSender(player);
        Context context = new Context(server, player, handler, sender);
        PlayPayloadHandler<?> callback = null;
        Map<Identifier, PlayPayloadHandler<?>> local = RECEIVERS.get(handler);
        if (local != null) callback = local.get(channel);
        if (callback == null) callback = GLOBAL_PAYLOAD_RECEIVERS.get(channel);
        if (callback == null) return false;
        invokePayload(callback, payload, context);
        return true;
    }

    public static Set<Identifier> getReceived(ServerPlayNetworkHandler handler) {
        Set<Identifier> result = new LinkedHashSet<>(GLOBAL_PAYLOAD_RECEIVERS.keySet());
        Map<Identifier, PlayPayloadHandler<?>> local = RECEIVERS.get(handler);
        if (local != null) result.addAll(local.keySet());
        result.addAll(GLOBAL_RECEIVERS.keySet());
        return Set.copyOf(result);
    }

    public static Set<Identifier> getReceived(ServerPlayerEntity player) {
        return player == null ? Set.of() : getReceived(player.getNetworkHandler());
    }

    public static Set<Identifier> getSendable(ServerPlayNetworkHandler handler) {
        Set<Identifier> result = new LinkedHashSet<>(PayloadTypeRegistry.playS2C().getIds());
        result.addAll(GLOBAL_RECEIVERS.keySet());
        return Set.copyOf(result);
    }

    public static Set<Identifier> getSendable(ServerPlayerEntity player) {
        return player == null ? Set.of() : getSendable(player.getNetworkHandler());
    }

    public static List<Packet<?>> getOutbound(ServerPlayerEntity player) {
        if (player == null) return List.of();
        List<Packet<?>> packets = OUTBOUND.get(player.nativeHandle());
        return packets == null ? List.of() : List.copyOf(packets);
    }

    public static Set<Identifier> getGlobalReceivers() {
        Set<Identifier> result = new LinkedHashSet<>(GLOBAL_RECEIVERS.keySet());
        result.addAll(GLOBAL_PAYLOAD_RECEIVERS.keySet());
        return Set.copyOf(result);
    }

    public static void clear() {
        GLOBAL_RECEIVERS.clear(); GLOBAL_PAYLOAD_RECEIVERS.clear(); RECEIVERS.clear(); OUTBOUND.clear();
        PayloadTypeRegistry.playS2C().clear(); PayloadTypeRegistry.playC2S().clear();
        PayloadTypeRegistry.configurationS2C().clear(); PayloadTypeRegistry.configurationC2S().clear();
    }

    private static void queue(ServerPlayerEntity player, Packet<?> packet) {
        OUTBOUND.computeIfAbsent(player.nativeHandle(), ignored ->
            Collections.synchronizedList(new ArrayList<>())).add(packet);
    }

    private static PacketByteBuf encode(CustomPayload payload) {
        PacketByteBuf data = new PacketByteBuf();
        Object registered = PayloadTypeRegistry.playS2C().getCodec(payload.getId());
        if (registered instanceof PacketCodec<?, ?> codec) {
            encode(codec, new RegistryByteBuf(), payload, data);
            return data;
        }
        // Fabric's codec is normally installed in PayloadTypeRegistry. The
        // embedded ABI also accepts the common write(PacketByteBuf) shape.
        try {
            Method write = payload.getClass().getMethod("write", PacketByteBuf.class);
            write.invoke(payload, data);
        } catch (ReflectiveOperationException ignored) { }
        return data;
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static void encode(PacketCodec codec, RegistryByteBuf registryBuffer,
                               CustomPayload payload, PacketByteBuf output) {
        codec.encode(registryBuffer, payload);
        output.writeBytes(registryBuffer);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static Object decode(PacketCodec codec, RegistryByteBuf buffer) {
        return codec.decode(buffer);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static void invokePayload(PlayPayloadHandler callback, CustomPayload payload, Context context) {
        callback.receive(payload, context);
    }
}
