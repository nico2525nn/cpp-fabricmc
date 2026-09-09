package net.minecraft.network.packet.c2s.login;

import net.minecraft.network.packet.Packet;

/** Serverbound acknowledgement for entering the configuration phase. */
public final class EnterConfigurationC2SPacket implements Packet<Object> {
    public static final EnterConfigurationC2SPacket INSTANCE =
        new EnterConfigurationC2SPacket();

    public EnterConfigurationC2SPacket() { }

    @Override
    public void apply(Object listener) { }
}
