package net.minecraft.network.packet;

import net.minecraft.network.codec.PacketDecoder;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.codec.ValueFirstEncoder;
import net.minecraft.util.Identifier;

public interface CustomPayload {
    Id<? extends CustomPayload> getId();

    /** Official 1.21.4 helper used by Fabric payload declarations. */
    static <T extends CustomPayload> Id<T> id(String path) {
        return new Id<>(Identifier.ofVanilla(path));
    }

    /** Compose the value-first encoder and decoder used by vanilla payloads. */
    static <B, V extends CustomPayload> PacketCodec<B, V> codecOf(
            ValueFirstEncoder<? super V, ? super B> encoder,
            PacketDecoder<? super B, ? extends V> decoder) {
        return PacketCodec.of(encoder, decoder);
    }

    /** The payload type descriptor returned by PayloadTypeRegistry.register. */
    final class Type<T extends CustomPayload> {
        private final Id<T> id;
        private final PacketCodec<?, T> codec;
        public Type(Id<T> id, PacketCodec<?, T> codec) {
            this.id = id; this.codec = codec;
        }
        public Id<T> id() { return id; }
        public PacketCodec<?, T> codec() { return codec; }
    }
    record Id<T extends CustomPayload>(Identifier id) { public Id { if (id == null) throw new NullPointerException("id"); } }
}
