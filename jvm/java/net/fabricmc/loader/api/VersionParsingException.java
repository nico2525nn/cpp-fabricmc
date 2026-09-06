package net.fabricmc.loader.api;

/** Public facade for Fabric Loader version parsing failures. */
public class VersionParsingException extends net.fabricmc.loader.util.version.VersionParsingException {
    public VersionParsingException() { super(); }
    public VersionParsingException(Throwable cause) { super(cause); }
    public VersionParsingException(String message) { super(message); }
    public VersionParsingException(String message, Throwable cause) { super(message, cause); }
}
