package net.minecraft.component;

import com.mojang.serialization.Codec;
import java.util.Objects;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;

/**
 * Compatibility implementation of the official {@link ComponentType}
 * interface used by the native shadow's pre-existing item settings API.
 */
public final class DataComponentType<T> implements ComponentType<T> {
    private final String id;
    private final Codec<T> codec;
    private final PacketCodec<? super RegistryByteBuf, T> packetCodec;

    public DataComponentType(String id) { this(id, null, null); }

    public DataComponentType(String id, Codec<T> codec,
                             PacketCodec<? super RegistryByteBuf, T> packetCodec) {
        this.id = id == null ? "" : id;
        this.codec = codec;
        this.packetCodec = packetCodec;
    }

    public String id() { return id; }
    @Override public Codec<T> getCodec() { return codec; }
    @Override public PacketCodec<? super RegistryByteBuf, T> getPacketCodec() {
        return packetCodec;
    }

    @Override public boolean equals(Object other) {
        return other instanceof DataComponentType<?> type && id.equals(type.id);
    }

    @Override public int hashCode() { return Objects.hash(id); }
    @Override public String toString() { return id; }
}
