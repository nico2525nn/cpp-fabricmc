package net.fabricmc.fabric.api.lookup.v1.custom;

import java.util.Iterator;
import java.util.concurrent.CopyOnWriteArrayList;
import net.minecraft.util.Identifier;

public interface ApiLookupMap<L> extends Iterable<L> {
    static <L> ApiLookupMap<L> create(LookupConstructor<L> constructor) {
        return new Impl<>(constructor == null ? null : constructor::get);
    }

    L getLookup(Identifier id, Class<?> apiClass, Class<?> contextClass);

    static <L> ApiLookupMap<L> create(LookupFactory<L> factory) {
        return new Impl<>((id, api, context) -> factory == null ? null : factory.get(api, context));
    }

    @FunctionalInterface
    interface LookupConstructor<L> {
        L get(Identifier id, Class<?> apiClass, Class<?> contextClass);
    }
    @FunctionalInterface
    interface LookupFactory<L> {
        L get(Class<?> apiClass, Class<?> contextClass);
    }

    final class Impl<L> implements ApiLookupMap<L> {
        private final BiFunction3<Identifier, Class<?>, Class<?>, L> constructor;
        private final CopyOnWriteArrayList<L> lookups = new CopyOnWriteArrayList<>();
        private Impl(BiFunction3<Identifier, Class<?>, Class<?>, L> constructor) {
            this.constructor = constructor;
        }
        @Override public L getLookup(Identifier id, Class<?> apiClass, Class<?> contextClass) {
            L lookup = constructor == null ? null : constructor.apply(id, apiClass, contextClass);
            if (lookup != null && !lookups.contains(lookup)) lookups.add(lookup);
            return lookup;
        }
        @Override public Iterator<L> iterator() { return lookups.iterator(); }
    }

    @FunctionalInterface
    interface BiFunction3<A, B, C, R> { R apply(A a, B b, C c); }
}
