package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.packet.Packet;

/** Minimal 1.21.4 client-command packet ABI used by server mixins. */
public class ClientCommandC2SPacket implements Packet<Object> {
    @Override public void apply(Object listener) { }
}
