package net.minecraft.network;

/** Vanilla protocol phases exposed to Fabric's networking implementation. */
public enum NetworkPhase {
    HANDSHAKING("handshake"),
    STATUS("status"),
    LOGIN("login"),
    CONFIGURATION("configuration"),
    PLAY("play");

    private final String id;

    NetworkPhase(String id) {
        this.id = id;
    }

    public String getId() {
        return id;
    }
}
