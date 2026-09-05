package cppfm.transform;

/**
 * Optional name/descriptor resolver used while matching structural and
 * Access Widener targets to runtime class bytes.
 *
 * <p>The default implementation is identity mapping.  A launcher that has
 * intermediary/official namespace metadata can provide a resolver without
 * coupling the class-file writer to a mapping format.</p>
 */
public interface DescriptorResolver {
    default String resolveOwner(String owner) {
        return owner;
    }

    /**
     * Resolve every object type embedded in a JVM descriptor.  Namespace
     * remappers must implement this together with the owner/member methods;
     * the identity default is suitable for already-remapped runtime bytes.
     */
    default String resolveDescriptor(String descriptor) {
        return descriptor;
    }

    default String resolveMethod(String owner, String name, String descriptor) {
        return name;
    }

    default String resolveField(String owner, String name, String descriptor) {
        return name;
    }

    /** Identity resolver suitable for already-remapped runtime classes. */
    DescriptorResolver IDENTITY = new DescriptorResolver() { };
}
