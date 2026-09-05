package net.fabricmc.fabric.api.networking.v1;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.network.packet.CustomPayload;

/** Codec registration table for the modern custom-payload API. */
public final class PayloadTypeRegistry<T extends CustomPayload> {
    private static final PayloadTypeRegistry<CustomPayload> SERVER_TO_CLIENT = new PayloadTypeRegistry<>();
    private static final PayloadTypeRegistry<CustomPayload> CLIENT_TO_SERVER = new PayloadTypeRegistry<>();
    private final Map<CustomPayload.Id<?>, Object> codecs = new ConcurrentHashMap<>();
    private PayloadTypeRegistry() {}
    @SuppressWarnings("unchecked") public static <T extends CustomPayload> PayloadTypeRegistry<T> playS2C() { return (PayloadTypeRegistry<T>) (PayloadTypeRegistry<?>) SERVER_TO_CLIENT; }
    @SuppressWarnings("unchecked") public static <T extends CustomPayload> PayloadTypeRegistry<T> playC2S() { return (PayloadTypeRegistry<T>) (PayloadTypeRegistry<?>) CLIENT_TO_SERVER; }
    public <P extends T> PayloadTypeRegistry<T> register(CustomPayload.Id<P> id, Object codec) { if (id == null || codec == null) throw new NullPointerException("id/codec"); codecs.put(id, codec); return this; }
    public boolean contains(CustomPayload.Id<?> id) { return codecs.containsKey(id); }
    public void clear() { codecs.clear(); }
}
