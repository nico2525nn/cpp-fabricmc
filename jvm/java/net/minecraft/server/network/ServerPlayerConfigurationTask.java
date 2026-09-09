package net.minecraft.server.network;

import java.util.function.Consumer;
import net.minecraft.network.packet.Packet;

/**
 * A configuration-phase task submitted while a player is joining a server.
 * Fabric API links this interface even when the native server does not use a
 * particular task implementation, so retain the exact 1.21.4 method shape.
 */
public interface ServerPlayerConfigurationTask {
    void sendPacket(Consumer<Packet<?>> sender);

    Key getKey();

    record Key(String id) { }
}
