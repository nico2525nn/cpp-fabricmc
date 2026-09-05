package cppfm.transform;

/**
 * Thrown when a class-file transformation cannot be represented safely.
 *
 * <p>The default transformer policy records recoverable incompatibilities as
 * diagnostics.  Strict callers can ask the transformer to promote those
 * diagnostics to this exception.</p>
 */
public final class TransformException extends RuntimeException {
    public TransformException(String message) {
        super(message);
    }

    public TransformException(String message, Throwable cause) {
        super(message, cause);
    }
}
