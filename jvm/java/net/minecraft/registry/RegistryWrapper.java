package net.minecraft.registry;

import java.util.Objects;
import java.util.function.Predicate;
import java.util.stream.Stream;

/** Read-only registry wrapper ABI used by Minecraft data and command APIs. */
public interface RegistryWrapper<T> {
    default Stream<?> streamEntries() { return Stream.empty(); }
    default Stream<?> streamKeys() { return Stream.empty(); }
    default Stream<?> streamTagKeys() { return Stream.empty(); }
    default Stream<?> getTags() { return Stream.empty(); }

    /** A lookup containing the set of registries visible to a data operation. */
    interface WrapperLookup extends RegistryWrapper<Object> {
        default Object getOps(Object delegate) { return delegate; }
        default Object getLifecycle() { return null; }

        static WrapperLookup of(Stream<?> wrappers) {
            Objects.requireNonNull(wrappers, "wrappers");
            return new WrapperLookup() {
                private final java.util.List<?> values = wrappers.toList();
                @Override public Stream<?> stream() { return values.stream(); }
            };
        }

        default Impl<?> getOrThrow(RegistryKey<?> registryRef) {
            if (registryRef == null) throw new IllegalArgumentException("registry key is null");
            return new Impl<>(registryRef);
        }

        default Stream<?> streamAllRegistryKeys() { return Stream.empty(); }
        default Stream<?> stream() { return Stream.empty(); }
        default Impl<?> method_46760(Impl<?> wrapper) { return wrapper; }
    }

    /** Registry lookup implementation; kept concrete so mod casts remain valid. */
    class Impl<T> implements RegistryWrapper<T> {
        private final RegistryKey<T> key;
        public Impl() { this(null); }
        public Impl(RegistryKey<T> key) { this.key = key; }
        public Object getLifecycle() { return null; }
        public RegistryKey<T> getKey() { return key; }
        public Impl<T> withFeatureFilter(Object enabledFeatures) { return this; }
        public Impl<T> withPredicateFilter(Predicate<T> predicate) { return this; }
        public boolean method_45920(Object feature, Object value) {
            return true;
        }
        public Impl<T> getBase() { return this; }
    }
}
