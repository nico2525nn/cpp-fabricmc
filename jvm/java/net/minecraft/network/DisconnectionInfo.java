package net.minecraft.network;

/** Structured disconnect reason placeholder. */
public class DisconnectionInfo {
    private final String reason;
    public DisconnectionInfo() { this(""); }
    public DisconnectionInfo(String reason) { this.reason = reason == null ? "" : reason; }
    public String reason() { return reason; }
}
