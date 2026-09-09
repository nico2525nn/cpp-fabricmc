package net.minecraft.network.packet;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.util.Identifier;

public class CustomPayloadS2CPacket implements Packet<Object> {
    private static final int MAX_PAYLOAD_SIZE;
    private final CustomPayload.Id<? extends CustomPayload> id;
    private final PacketByteBuf data;
    private final CustomPayload payload;

    static {
        MAX_PAYLOAD_SIZE = 1048576;
    }
    public CustomPayloadS2CPacket(Identifier id, PacketByteBuf data) { this(new CustomPayload.Id<>(id), data, null); }
    public CustomPayloadS2CPacket(CustomPayload.Id<? extends CustomPayload> id, PacketByteBuf data) { this(id, data, null); }
    public CustomPayloadS2CPacket(CustomPayload payload) { this(payload, encode(payload)); }
    public CustomPayloadS2CPacket(CustomPayload payload, PacketByteBuf data) {
        this(payload == null ? null : payload.getId(), data, payload);
    }
    private CustomPayloadS2CPacket(CustomPayload.Id<? extends CustomPayload> id,
                                   PacketByteBuf data, CustomPayload payload) {
        if (id == null) throw new NullPointerException("payload id");
        this.id = id; this.data = data == null ? new PacketByteBuf() : data.copy(); this.payload = payload;
    }
    public CustomPayload.Id<? extends CustomPayload> payloadId() { return id; }
    public Identifier getChannel() { return id.id(); }
    public PacketByteBuf getData() { return data.copy(); }
    public CustomPayload payload() { return payload; }

    private static PacketByteBuf encode(CustomPayload payload) {
        PacketByteBuf data = new PacketByteBuf();
        if (payload == null) return data;
        try {
            java.lang.reflect.Method write = payload.getClass().getMethod("write", PacketByteBuf.class);
            write.invoke(payload, data);
        } catch (ReflectiveOperationException ignored) { }
        return data;
    }
}
