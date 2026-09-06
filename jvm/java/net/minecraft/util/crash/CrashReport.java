package net.minecraft.util.crash;

/** Small crash-report value used by the shadow exception ABI. */
public class CrashReport {
    private final String message;
    public CrashReport() { this(""); }
    public CrashReport(String message) { this.message = message == null ? "" : message; }
    public String asString() { return message; }
    @Override public String toString() { return message; }
}
