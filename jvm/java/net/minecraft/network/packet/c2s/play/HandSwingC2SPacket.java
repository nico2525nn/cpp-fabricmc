package net.minecraft.network.packet.c2s.play;

import net.minecraft.network.packet.Packet;
import net.minecraft.util.Hand;

/** Minimal 1.21.4 hand-swing packet ABI used by server mixins. */
public class HandSwingC2SPacket implements Packet<Object> {
    private final Hand hand;
    public HandSwingC2SPacket() { this(Hand.MAIN_HAND); }
    public HandSwingC2SPacket(Hand hand) { this.hand = hand == null ? Hand.MAIN_HAND : hand; }
    public Hand getHand() { return hand; }
    @Override public void apply(Object listener) { }
}
