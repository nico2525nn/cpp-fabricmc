package net.minecraft.network.packet.c2s.common;

import net.minecraft.network.packet.Packet;

/**
 * Serverbound common pong packet used by the 1.21.4 connection listener.
 *
 * <p>The native server consumes the actual protocol packet before the JVM
 * compatibility layer sees it.  This class therefore provides the stable
 * object/descriptor surface that Fabric mixins and callbacks link against.</p>
 */
public final class CommonPongC2SPacket implements Packet<Object> {
    private final int challenge;

    public CommonPongC2SPacket(int challenge) {
        this.challenge = challenge;
    }

    public int getChallenge() {
        return challenge;
    }

    public int challenge() {
        return challenge;
    }

    @Override
    public void apply(Object listener) { }
}
