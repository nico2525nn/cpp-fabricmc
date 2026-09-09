package net.minecraft.registry;

import com.mojang.serialization.Lifecycle;
import net.minecraft.util.Identifier;

/** Concrete defaulted registry constructor used by vanilla bootstrap code. */
public class SimpleDefaultedRegistry<T> extends SimpleRegistry<T> implements DefaultedRegistry<T> {
    private final Identifier defaultId;
    public SimpleDefaultedRegistry(String defaultId, RegistryKey<?> key,
                                   Lifecycle lifecycle, boolean intrusive) {
        super(key, lifecycle, intrusive);
        this.defaultId = Identifier.tryParse(defaultId);
    }
    public Identifier getDefaultId() { return defaultId; }
    @Override public T get(Identifier id) {
        T value = super.get(id);
        return value != null ? value : super.get(defaultId);
    }
}
