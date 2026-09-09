package net.minecraft.network.packet.c2s.login;

import net.minecraft.network.PacketByteBuf;

/** Payload marker used by the login query-response packet codec. */
public interface LoginQueryResponsePayload {
    default void write(PacketByteBuf buf) { }
}
