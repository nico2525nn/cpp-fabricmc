package net.minecraft.util.crash;

/** Runtime exception surface used by vanilla and optimization mods. */
public class CrashException extends RuntimeException {
    private final CrashReport report;
    public CrashException(CrashReport report) {
        super(report == null ? null : report.asString());
        this.report = report;
    }
    public CrashException(String message, Throwable cause) { super(message, cause); report = new CrashReport(message); }
    public CrashReport getReport() { return report; }
}
