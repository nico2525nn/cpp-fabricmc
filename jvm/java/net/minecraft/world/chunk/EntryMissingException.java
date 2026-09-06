package net.minecraft.world.chunk;

/** Missing palette entry exception used by the vanilla palette ABI. */
public class EntryMissingException extends RuntimeException {
    public EntryMissingException() { super(); }
    public EntryMissingException(String message) { super(message); }
}
