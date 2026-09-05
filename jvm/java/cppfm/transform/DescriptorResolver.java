package cppfm.transform;

/**
 * Optional name/descriptor resolver used at Mixin target matching time.
 *
 * <p>The default implementation is identity mapping.  A launcher that has
 * intermediary/official namespace metadata can provide a resolver without
 * coupling the class-file writer to a mapping format.</p>
 */
public interface DescriptorResolver {
    default String resolveOwner(String owner) {
        return owner;
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
