package cppfm.transform;

/**
 * A dependency-free class-file transformer used by
 * {@link cppfm.loader.TransformingClassLoader}.
 *
 * <p>The returned array becomes the input of the next transformer.  Returning
 * the same array (or {@code null}) means that the transformer made no change.
 * A transformer must not mutate the supplied array.</p>
 */
@FunctionalInterface
public interface ClassFileTransformer {
    /** Transform one class before its definition is created. */
    byte[] transform(String binaryName, byte[] originalBytes, TransformContext context);
}
