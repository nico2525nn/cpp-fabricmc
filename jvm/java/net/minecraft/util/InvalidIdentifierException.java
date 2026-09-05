package net.minecraft.util;

/** Runtime error matching the failure category used by Identifier parsing. */
public class InvalidIdentifierException extends IllegalArgumentException {
    public InvalidIdentifierException(String message) { super(message); }
}
