package net.minecraft.network.packet;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.util.Identifier;

public interface CustomPayload {
    Id<? extends CustomPayload> getId();
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
