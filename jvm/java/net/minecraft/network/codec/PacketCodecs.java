package net.minecraft.network.codec;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import io.netty.buffer.ByteBuf;
import java.util.Optional;
import java.util.function.IntFunction;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.nbt.NbtElement;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;

/** Common codecs used by server-side Fabric payload declarations. */
public interface PacketCodecs {

    public static final PacketCodec<PacketByteBuf, Byte> BYTE = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeByte(value), PacketByteBuf::readByte);
    public static final PacketCodec<PacketByteBuf, Boolean> BOOLEAN = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeBoolean(value), PacketByteBuf::readBoolean);
    public static final PacketCodec<PacketByteBuf, Short> SHORT = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeShort(value), PacketByteBuf::readShort);
    public static final PacketCodec<PacketByteBuf, Integer> INTEGER = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeInt(value), PacketByteBuf::readInt);
    public static final PacketCodec<PacketByteBuf, Integer> INT = INTEGER;
    public static final PacketCodec<PacketByteBuf, Long> LONG = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeLong(value), PacketByteBuf::readLong);
    public static final PacketCodec<PacketByteBuf, Float> FLOAT = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeFloat(value), PacketByteBuf::readFloat);
    public static final PacketCodec<PacketByteBuf, Double> DOUBLE = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeDouble(value), PacketByteBuf::readDouble);
    public static final PacketCodec<PacketByteBuf, Integer> VAR_INT = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeVarInt(value), PacketByteBuf::readVarInt);
    public static final PacketCodec<PacketByteBuf, Long> VAR_LONG = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeVarLong(value), PacketByteBuf::readVarLong);
    public static final PacketCodec<PacketByteBuf, String> STRING = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeString(value), PacketByteBuf::readString);
    public static final PacketCodec<PacketByteBuf, Identifier> IDENTIFIER = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeIdentifier(value), PacketByteBuf::readIdentifier);
    public static final PacketCodec<PacketByteBuf, BlockPos> BLOCK_POS = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeBlockPos(value), PacketByteBuf::readBlockPos);
    public static final PacketCodec<PacketByteBuf, byte[]> BYTE_ARRAY = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeByteArray(value), PacketByteBuf::readByteArray);

    /** NBT codecs exposed by the 1.21.4 packet API. */
    public static final PacketCodec<PacketByteBuf, NbtCompound> NBT_COMPOUND = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeNbt(value), PacketByteBuf::readNbt);
    public static final PacketCodec<PacketByteBuf, NbtElement> NBT_ELEMENT = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeNbt(value), PacketByteBuf::readNbtElement);
    public static final PacketCodec<PacketByteBuf, Optional<NbtCompound>> OPTIONAL_NBT = PacketCodec.ofLegacy(
        (buffer, value) -> buffer.writeNbt(value == null || value.isEmpty() ? null : value.get()),
        buffer -> Optional.ofNullable(buffer.readNbt()));
    public static final PacketCodec<PacketByteBuf, NbtCompound> UNLIMITED_NBT_COMPOUND = NBT_COMPOUND;
    public static final PacketCodec<PacketByteBuf, NbtElement> UNLIMITED_NBT_ELEMENT = NBT_ELEMENT;

    public static <B extends ByteBuf, T> PacketCodec<B, Optional<T>> optional(PacketCodec<? super B, T> codec) {
        return PacketCodec.ofLegacy((buffer, value) -> {
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
        return PacketCodec.ofLegacy((buffer, value) -> {
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

    /** Codec for a bounded variable-length collection, matching Yarn 1.21.4. */
    public static <B extends ByteBuf, T, C extends Collection<T>> PacketCodec<B, C> collection(
            IntFunction<C> factory, PacketCodec<? super B, T> elementCodec) {
        return collection(factory, elementCodec, 1024);
    }

    /** Codec for a collection with an explicit maximum element count. */
    public static <B extends ByteBuf, T, C extends Collection<T>> PacketCodec<B, C> collection(
            IntFunction<C> factory, PacketCodec<? super B, T> elementCodec, int maxSize) {
        if (factory == null || elementCodec == null) throw new NullPointerException();
        if (maxSize < 0) throw new IllegalArgumentException("maxSize");
        return PacketCodec.ofLegacy((buffer, value) -> {
            if (value == null || value.size() > maxSize) throw new IllegalArgumentException("collection too large");
            writeCollectionSize(buffer, value.size(), maxSize);
            for (T item : value) elementCodec.encode(buffer, item);
        }, buffer -> {
            int size = readCollectionSize(buffer, maxSize);
            C value = factory.apply(size);
            if (value == null) throw new IllegalStateException("collection factory returned null");
            for (int index = 0; index < size; index++) value.add(elementCodec.decode(buffer));
            return value;
        });
    }

    public static <B extends ByteBuf, T, C extends Collection<T>> PacketCodec.ResultFunction<B, T, C>
            toCollection(IntFunction<C> factory) {
        if (factory == null) throw new NullPointerException("factory");
        return codec -> collection(factory, codec);
    }

    public static <B extends ByteBuf, T> PacketCodec.ResultFunction<B, T, List<T>> toList() {
        return toList(1024);
    }

    public static <B extends ByteBuf, T> PacketCodec.ResultFunction<B, T, List<T>> toList(int maxSize) {
        if (maxSize < 0) throw new IllegalArgumentException("maxSize");
        return codec -> collection(ArrayList::new, codec, maxSize);
    }

    public static int readCollectionSize(ByteBuf buffer, int maxSize) {
        if (maxSize < 0) throw new IllegalArgumentException("maxSize");
        int size = readVarInt(buffer);
        if (size < 0 || size > maxSize) throw new IllegalArgumentException("collection too large");
        return size;
    }

    public static void writeCollectionSize(ByteBuf buffer, int size, int maxSize) {
        if (maxSize < 0 || size < 0 || size > maxSize)
            throw new IllegalArgumentException("collection too large");
        writeVarInt(buffer, size);
    }

    private static void writeVarInt(ByteBuf buffer, int value) {
        while ((value & ~0x7F) != 0) {
            buffer.writeByte((value & 0x7F) | 0x80);
            value >>>= 7;
        }
        buffer.writeByte(value);
    }

    private static int readVarInt(ByteBuf buffer) {
        int value = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            int part = buffer.readUnsignedByte();
            value |= (part & 0x7F) << shift;
            if ((part & 0x80) == 0) return value;
        }
        throw new IllegalArgumentException("varint too long");
    }

    public static <E extends Enum<E>> PacketCodec<PacketByteBuf, E> enumConstant(Class<E> type) {
        if (type == null || type.getEnumConstants().length == 0) throw new IllegalArgumentException("enum type");
        return PacketCodec.ofLegacy((buffer, value) -> buffer.writeEnumConstant(value), buffer -> {
            int ordinal = buffer.readVarInt();
            E[] values = type.getEnumConstants();
            if (ordinal < 0 || ordinal >= values.length) throw new IllegalArgumentException("enum ordinal");
            return values[ordinal];
        });
    }
}
