package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.packet.Packet;

/** Minimal 1.21.4 player-action packet ABI used by server mixins. */
public class PlayerActionC2SPacket implements Packet<Object> {
    @Override public void apply(Object listener) { }
}
