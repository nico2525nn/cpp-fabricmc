package net.minecraft.registry;

import io.netty.buffer.ByteBuf;
import java.nio.charset.StandardCharsets;
import net.minecraft.network.codec.PacketCodec;

/** Pack identifier carrying namespace, id and version metadata. */
public record VersionedIdentifier(String namespace, String id, String version) {
    public static final String DEFAULT_NAMESPACE = "minecraft";
    public static final PacketCodec<ByteBuf, VersionedIdentifier> PACKET_CODEC = PacketCodec.ofLegacy(
            (buffer, value) -> {
                writeString(buffer, value == null ? "" : value.namespace());
                writeString(buffer, value == null ? "" : value.id());
                writeString(buffer, value == null ? "" : value.version());
            }, buffer -> new VersionedIdentifier(readString(buffer), readString(buffer), readString(buffer)));

    public static VersionedIdentifier createVanilla(String path) {
        return new VersionedIdentifier(DEFAULT_NAMESPACE, path == null ? "" : path, "");
    }

    public boolean isVanilla() { return DEFAULT_NAMESPACE.equals(namespace); }

    private static void writeString(ByteBuf buffer, String value) {
        byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
        buffer.writeInt(bytes.length).writeBytes(bytes);
    }

    private static String readString(ByteBuf buffer) {
        int length = buffer.readInt();
        if (length < 0 || length > 1 << 20 || length > buffer.readableBytes())
            throw new IllegalArgumentException("invalid versioned identifier string length");
        byte[] bytes = new byte[length];
        buffer.readBytes(bytes);
        return new String(bytes, StandardCharsets.UTF_8);
    }
}
