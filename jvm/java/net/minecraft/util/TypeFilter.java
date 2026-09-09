package net.minecraft.util;

/**
 * A typed predicate/downcast pair used by the entity collection APIs.
 *
 * <p>This is the small, allocation-free surface exposed by Minecraft 1.21.4
 * to callers.  Keeping the factory methods on the interface is important:
 * compiled mods call the named static methods directly after intermediary
 * remapping.</p>
 */
public interface TypeFilter<B, T extends B> {
    Class<? extends B> getBaseClass();

    @SuppressWarnings("unchecked")
    default T downcast(B object) {
        Class<? extends B> base = getBaseClass();
        return base.isInstance(object) ? (T) object : null;
    }

    static <B, T extends B> TypeFilter<B, T> instanceOf(Class<T> cls) {
        if (cls == null) throw new NullPointerException("cls");
        return new TypeFilter<>() {
            @Override public Class<T> getBaseClass() { return cls; }
        };
    }

    static <B, T extends B> TypeFilter<B, T> equals(Class<T> cls) {
        if (cls == null) throw new NullPointerException("cls");
        return new TypeFilter<>() {
            @Override public Class<T> getBaseClass() { return cls; }
            @Override @SuppressWarnings("unchecked")
            public T downcast(B object) {
                return object != null && object.getClass() == cls ? (T) object : null;
            }
        };
    }
}
