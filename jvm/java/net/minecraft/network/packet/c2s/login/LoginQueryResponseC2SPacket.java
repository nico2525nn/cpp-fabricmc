package net.minecraft.network.packet.c2s.login;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.Packet;

/** Minimal named ABI for Fabric's serverbound login query response. */
public final class LoginQueryResponseC2SPacket implements Packet<Object> {
    private final int queryId;
    private final LoginQueryResponsePayload response;

    public LoginQueryResponseC2SPacket() {
        this(0, null);
    }

    public LoginQueryResponseC2SPacket(int queryId, LoginQueryResponsePayload response) {
        this.queryId = queryId;
        this.response = response;
    }

    public LoginQueryResponseC2SPacket(PacketByteBuf buf) {
        this();
    }

    public int queryId() { return queryId; }
    public LoginQueryResponsePayload response() { return response; }
    public LoginQueryResponsePayload getResponse() { return response; }

    @Override
    public void apply(Object listener) { }
}
