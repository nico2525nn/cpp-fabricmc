package net.minecraft.network.codec;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;

/** Common codecs used by server-side Fabric payload declarations. */
public final class PacketCodecs {
    private PacketCodecs() { }

    public static final PacketCodec<PacketByteBuf, Byte> BYTE = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeByte(value), PacketByteBuf::readByte);
    public static final PacketCodec<PacketByteBuf, Boolean> BOOLEAN = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeBoolean(value), PacketByteBuf::readBoolean);
    public static final PacketCodec<PacketByteBuf, Short> SHORT = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeShort(value), PacketByteBuf::readShort);
    public static final PacketCodec<PacketByteBuf, Integer> INTEGER = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeInt(value), PacketByteBuf::readInt);
    public static final PacketCodec<PacketByteBuf, Integer> INT = INTEGER;
    public static final PacketCodec<PacketByteBuf, Long> LONG = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeLong(value), PacketByteBuf::readLong);
    public static final PacketCodec<PacketByteBuf, Float> FLOAT = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeFloat(value), PacketByteBuf::readFloat);
    public static final PacketCodec<PacketByteBuf, Double> DOUBLE = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeDouble(value), PacketByteBuf::readDouble);
    public static final PacketCodec<PacketByteBuf, Integer> VAR_INT = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeVarInt(value), PacketByteBuf::readVarInt);
    public static final PacketCodec<PacketByteBuf, Long> VAR_LONG = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeVarLong(value), PacketByteBuf::readVarLong);
    public static final PacketCodec<PacketByteBuf, String> STRING = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeString(value), PacketByteBuf::readString);
    public static final PacketCodec<PacketByteBuf, Identifier> IDENTIFIER = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeIdentifier(value), PacketByteBuf::readIdentifier);
    public static final PacketCodec<PacketByteBuf, BlockPos> BLOCK_POS = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeBlockPos(value), PacketByteBuf::readBlockPos);
    public static final PacketCodec<PacketByteBuf, byte[]> BYTE_ARRAY = PacketCodec.ofStatic(
        (buffer, value) -> buffer.writeByteArray(value), PacketByteBuf::readByteArray);

    public static <T> PacketCodec<PacketByteBuf, Optional<T>> optional(PacketCodec<? super PacketByteBuf, T> codec) {
        return PacketCodec.ofStatic((buffer, value) -> {
            buffer.writeBoolean(value.isPresent());
            value.ifPresent(item -> codec.encode(buffer, item));
        }, buffer -> buffer.readBoolean() ? Optional.of(codec.decode(buffer)) : Optional.empty());
    }

    public static <T> PacketCodec<PacketByteBuf, List<T>> list(PacketCodec<? super PacketByteBuf, T> codec) {
        return list(codec, 1024);
    }

    public static <T> PacketCodec<PacketByteBuf, List<T>> list(PacketCodec<? super PacketByteBuf, T> codec,
                                                                int maxSize) {
        if (maxSize < 0) throw new IllegalArgumentException("maxSize");
        return PacketCodec.ofStatic((buffer, value) -> {
            if (value == null || value.size() > maxSize) throw new IllegalArgumentException("list too large");
            buffer.writeVarInt(value.size());
            for (T item : value) codec.encode(buffer, item);
        }, buffer -> {
            int size = buffer.readVarInt();
            if (size < 0 || size > maxSize) throw new IllegalArgumentException("list too large");
            List<T> value = new ArrayList<>(size);
            for (int index = 0; index < size; index++) value.add(codec.decode(buffer));
            return List.copyOf(value);
        });
    }

    public static <E extends Enum<E>> PacketCodec<PacketByteBuf, E> enumConstant(Class<E> type) {
        if (type == null || type.getEnumConstants().length == 0) throw new IllegalArgumentException("enum type");
        return PacketCodec.ofStatic((buffer, value) -> buffer.writeEnumConstant(value), buffer -> {
            int ordinal = buffer.readVarInt();
            E[] values = type.getEnumConstants();
            if (ordinal < 0 || ordinal >= values.length) throw new IllegalArgumentException("enum ordinal");
            return values[ordinal];
        });
    }
}
