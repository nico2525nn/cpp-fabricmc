package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.packet.Packet;

/** Minimal 1.21.4 screen-button packet ABI used by server mixins. */
public class ButtonClickC2SPacket implements Packet<Object> {
    @Override public void apply(Object listener) { }
}
