package net.fabricmc.loader.util.version;

/** Checked failure used by Fabric's public version parser contract. */
public class VersionParsingException extends Exception {
    public VersionParsingException() { super(); }
    public VersionParsingException(Throwable cause) { super(cause); }
    public VersionParsingException(String message) { super(message); }
    public VersionParsingException(String message, Throwable cause) { super(message, cause); }
}
