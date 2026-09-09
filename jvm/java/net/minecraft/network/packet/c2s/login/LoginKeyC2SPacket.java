package net.minecraft.network.packet.c2s.login;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.Packet;

/**
 * Serverbound login encryption-key packet ABI for Minecraft 1.21.4.
 *
 * <p>The native transport performs the actual login handshake.  The object is
 * nevertheless part of the named listener descriptor used by Fabric/Krypton,
 * so the JVM compatibility surface must expose both packet construction
 * forms used by vanilla's codec path.</p>
 */
public final class LoginKeyC2SPacket implements Packet<Object> {
    public LoginKeyC2SPacket() { }
    public LoginKeyC2SPacket(PacketByteBuf buf) { }

    @Override
    public void apply(Object listener) { }
}
