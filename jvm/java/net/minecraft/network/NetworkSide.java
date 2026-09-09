package net.minecraft.network;

/**
 * Direction of a packet relative to the endpoint.  This is the vanilla
 * 1.21.4 enum used by Fabric's networking implementation when registering
 * server-bound and client-bound payloads.
 */
public enum NetworkSide {
    CLIENTBOUND("CLIENTBOUND"),
    SERVERBOUND("SERVERBOUND");

    private final String name;

    NetworkSide(String name) {
        this.name = name;
    }

    public String getName() {
        return name;
    }

    public NetworkSide getOpposite() {
        return this == CLIENTBOUND ? SERVERBOUND : CLIENTBOUND;
    }
}
