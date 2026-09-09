package net.fabricmc.fabric.api.networking.v1;

import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.packet.CustomPayload;

/**
 * Codec registration contract used by Fabric API 0.119.x for Minecraft
 * 1.21.4. It is an interface in the official API; keeping that distinction
 * matters because API modules invoke the four accessors with
 * {@code invokeinterface}, not {@code invokevirtual}.
 */
public interface PayloadTypeRegistry<B extends PacketByteBuf> {
    <P extends CustomPayload> CustomPayload.Type<P> register(
        CustomPayload.Id<P> id, PacketCodec<? super B, P> codec);

    /** Legacy object-typed bridge retained for the local shadow consumers. */
    default <P extends CustomPayload> PayloadTypeRegistry<B> register(
            CustomPayload.Id<P> id, Object codec) {
        if (codec instanceof PacketCodec<?, ?> packetCodec) {
            @SuppressWarnings("unchecked")
            PacketCodec<? super B, P> typed = (PacketCodec<? super B, P>) packetCodec;
            register(id, typed);
        } else {
            register(id, PacketCodec.unit(null));
        }
        return this;
    }

    default boolean contains(CustomPayload.Id<?> id) {
        return ids().contains(id == null ? null : id.id());
    }

    /** Local inspection helpers; they are not required by the official ABI. */
    default Set<net.minecraft.util.Identifier> getIds() { return ids(); }
    default Set<net.minecraft.util.Identifier> ids() { return Set.of(); }
    default Object getCodec(CustomPayload.Id<?> id) { return null; }
    default CustomPayload.Type<?> getType(CustomPayload.Id<?> id) { return null; }
    default void clear() { }

    static PayloadTypeRegistry<PacketByteBuf> configurationC2S() {
        return net.fabricmc.fabric.impl.networking.PayloadTypeRegistryImpl.CONFIGURATION_C2S;
    }

    static PayloadTypeRegistry<PacketByteBuf> configurationS2C() {
        return net.fabricmc.fabric.impl.networking.PayloadTypeRegistryImpl.CONFIGURATION_S2C;
    }

    static PayloadTypeRegistry<RegistryByteBuf> playC2S() {
        return net.fabricmc.fabric.impl.networking.PayloadTypeRegistryImpl.PLAY_C2S;
    }

    static PayloadTypeRegistry<RegistryByteBuf> playS2C() {
        return net.fabricmc.fabric.impl.networking.PayloadTypeRegistryImpl.PLAY_S2C;
    }

    /** Lazy holder avoids constructing all registries when networking is unused. */
    final class Holder {
        private Holder() { }
        private static final PayloadTypeRegistry<PacketByteBuf> CONFIGURATION_C2S = new Impl<>();
        private static final PayloadTypeRegistry<PacketByteBuf> CONFIGURATION_S2C = new Impl<>();
        private static final PayloadTypeRegistry<RegistryByteBuf> CLIENT_TO_SERVER = new Impl<>();
        private static final PayloadTypeRegistry<RegistryByteBuf> SERVER_TO_CLIENT = new Impl<>();
    }

    /** Thread-safe implementation behind the four official singleton accessors. */
    final class Impl<B extends PacketByteBuf> implements PayloadTypeRegistry<B> {
        private final Map<net.minecraft.util.Identifier, Registration<?>> codecs =
            new ConcurrentHashMap<>();

        @Override
        public <P extends CustomPayload> CustomPayload.Type<P> register(
                CustomPayload.Id<P> id, PacketCodec<? super B, P> codec) {
            if (id == null || codec == null) throw new NullPointerException("id/codec");
            Registration<?> previous = codecs.putIfAbsent(id.id(), new Registration<>(id, codec));
            if (previous != null) throw new IllegalArgumentException("duplicate payload id: " + id.id());
            return new CustomPayload.Type<>(id, codec);
        }

        @Override
        public Set<net.minecraft.util.Identifier> ids() {
            return Set.copyOf(codecs.keySet());
        }

        @Override
        public Object getCodec(CustomPayload.Id<?> id) {
            Registration<?> registration = id == null ? null : codecs.get(id.id());
            return registration == null ? null : registration.codec();
        }

        @Override
        public CustomPayload.Type<?> getType(CustomPayload.Id<?> id) {
            Registration<?> registration = id == null ? null : codecs.get(id.id());
            return registration == null ? null : registration.type();
        }

        @Override
        public void clear() { codecs.clear(); }

        private record Registration<P extends CustomPayload>(
                CustomPayload.Id<P> id, PacketCodec<?, P> codec) {
            private CustomPayload.Type<P> type() { return new CustomPayload.Type<>(id, codec); }
        }
    }
}
