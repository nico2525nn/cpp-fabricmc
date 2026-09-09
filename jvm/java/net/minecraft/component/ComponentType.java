package net.minecraft.component;

import com.mojang.serialization.Codec;
import java.util.Map;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;

/**
 * Typed key for a 1.21.4 data component.
 *
 * <p>Yarn exposes this as an interface.  The old shadow declared it as a
 * concrete identifier wrapper, which made real Fabric component code fail at
 * linkage time because the JVM class shape was different.  The small
 * {@link DataComponentType} implementation remains as a source-compatible
 * convenience for the existing native-facing code.</p>
 */
public interface ComponentType<T> {
    Codec<T> getCodec();

    PacketCodec<? super RegistryByteBuf, T> getPacketCodec();

    default Codec<T> getCodecOrThrow() {
        Codec<T> codec = getCodec();
        if (codec == null) throw new IllegalStateException("component type has no codec");
        return codec;
    }

    default boolean shouldSkipSerialization() { return getCodec() == null; }

    Codec<ComponentType<?>> CODEC = new Codec<>() { };
    PacketCodec<RegistryByteBuf, ComponentType<?>> PACKET_CODEC = PacketCodec.ofLegacy(
        (buffer, value) -> { }, buffer -> null);
    Codec<ComponentType<?>> PERSISTENT_CODEC = CODEC;
    Codec<Map<ComponentType<?>, Object>> TYPE_TO_VALUE_MAP_CODEC = new Codec<>() { };

    static <T> Builder<T> builder() { return new Builder<>(); }

    /** Builder used by vanilla and by Fabric-defined component types. */
    class Builder<T> {
        private Codec<T> codec;
        private PacketCodec<? super RegistryByteBuf, T> packetCodec;
        private boolean cache;

        public Builder<T> codec(Codec<T> value) {
            codec = value;
            return this;
        }

        public Builder<T> packetCodec(PacketCodec<? super RegistryByteBuf, T> value) {
            packetCodec = value;
            return this;
        }

        public Builder<T> cache() {
            cache = true;
            return this;
        }

        public ComponentType<T> build() {
            return new SimpleDataComponentType<>(codec, packetCodec, cache);
        }

        /** Vanilla's concrete implementation nested below the builder. */
        public static final class SimpleDataComponentType<T> implements ComponentType<T> {
            private final Codec<T> codec;
            private final PacketCodec<? super RegistryByteBuf, T> packetCodec;
            private final boolean cache;

            private SimpleDataComponentType(Codec<T> codec,
                                            PacketCodec<? super RegistryByteBuf, T> packetCodec,
                                            boolean cache) {
                this.codec = codec;
                this.packetCodec = packetCodec;
                this.cache = cache;
            }

            @Override public Codec<T> getCodec() { return codec; }
            @Override public PacketCodec<? super RegistryByteBuf, T> getPacketCodec() {
                return packetCodec;
            }
            public boolean caches() { return cache; }
        }
    }
}
