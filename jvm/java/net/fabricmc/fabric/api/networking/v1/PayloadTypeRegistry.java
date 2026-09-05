package net.fabricmc.fabric.api.networking.v1;

import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;

/** Codec registration table for the modern custom-payload API. */
public final class PayloadTypeRegistry<B extends PacketByteBuf> {
    private static final PayloadTypeRegistry<PacketByteBuf> CONFIGURATION_S2C = new PayloadTypeRegistry<>();
    private static final PayloadTypeRegistry<PacketByteBuf> CONFIGURATION_C2S = new PayloadTypeRegistry<>();
    private static final PayloadTypeRegistry<RegistryByteBuf> SERVER_TO_CLIENT = new PayloadTypeRegistry<>();
    private static final PayloadTypeRegistry<RegistryByteBuf> CLIENT_TO_SERVER = new PayloadTypeRegistry<>();
    private final Map<net.minecraft.util.Identifier, Registration<?>> codecs = new ConcurrentHashMap<>();
    private PayloadTypeRegistry() {}

    public static PayloadTypeRegistry<PacketByteBuf> configurationS2C() { return CONFIGURATION_S2C; }
    public static PayloadTypeRegistry<PacketByteBuf> configurationC2S() { return CONFIGURATION_C2S; }
    public static PayloadTypeRegistry<RegistryByteBuf> playS2C() { return SERVER_TO_CLIENT; }
    public static PayloadTypeRegistry<RegistryByteBuf> playC2S() { return CLIENT_TO_SERVER; }

    public <P extends CustomPayload> CustomPayload.Type<P> register(CustomPayload.Id<P> id,
                                                                      PacketCodec<? super B, P> codec) {
        if (id == null || codec == null) throw new NullPointerException("id/codec");
        Registration<?> previous = codecs.putIfAbsent(id.id(), new Registration<>(id, codec));
        if (previous != null) throw new IllegalArgumentException("duplicate payload id: " + id.id());
        return new CustomPayload.Type<>(id, codec);
    }

    /** Legacy escape hatch for the pre-codec shadow API. */
    public <P extends CustomPayload> PayloadTypeRegistry<B> register(CustomPayload.Id<P> id, Object codec) {
        if (id == null || codec == null) throw new NullPointerException("id/codec");
        if (codec instanceof PacketCodec<?, ?> packetCodec) {
            @SuppressWarnings("unchecked") PacketCodec<? super B, P> typed =
                (PacketCodec<? super B, P>) packetCodec;
            register(id, typed);
        } else {
            Registration<?> previous = codecs.putIfAbsent(id.id(), new Registration<>(id, null));
            if (previous != null) throw new IllegalArgumentException("duplicate payload id: " + id.id());
        }
        return this;
    }

    public boolean contains(CustomPayload.Id<?> id) { return id != null && codecs.containsKey(id.id()); }
    public Set<net.minecraft.util.Identifier> getIds() {
        java.util.Set<net.minecraft.util.Identifier> result = new java.util.LinkedHashSet<>();
        result.addAll(codecs.keySet());
        return Set.copyOf(result);
    }
    public Object getCodec(CustomPayload.Id<?> id) {
        Registration<?> registration = id == null ? null : codecs.get(id.id());
        return registration == null ? null : registration.codec;
    }
    public CustomPayload.Type<?> getType(CustomPayload.Id<?> id) {
        Registration<?> registration = id == null ? null : codecs.get(id.id());
        return registration == null ? null : registration.type();
    }
    public void clear() { codecs.clear(); }

    private record Registration<P extends CustomPayload>(CustomPayload.Id<P> id,
                                                          PacketCodec<?, P> codec) {
        private CustomPayload.Type<P> type() { return new CustomPayload.Type<>(id, codec); }
    }
}
