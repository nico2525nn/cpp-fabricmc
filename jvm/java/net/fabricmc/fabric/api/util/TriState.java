package net.fabricmc.fabric.api.util;

import java.util.Optional;
import java.util.function.BooleanSupplier;
import java.util.function.Supplier;

/** A three-valued boolean used by Fabric callbacks. */
public enum TriState {
    FALSE,
    DEFAULT,
    TRUE;

    public static TriState of(boolean value) { return value ? TRUE : FALSE; }
    public static TriState of(Boolean value) { return value == null ? DEFAULT : of(value.booleanValue()); }
    public boolean get() { return this == TRUE; }
    public Boolean getBoxed() { return this == DEFAULT ? null : get(); }
    public boolean orElse(boolean fallback) { return this == DEFAULT ? fallback : get(); }
    public boolean orElseGet(BooleanSupplier fallback) {
        return this == DEFAULT ? (fallback == null ? false : fallback.getAsBoolean()) : get();
    }
    public <T> Optional<T> map(BooleanFunction<? extends T> mapper) {
        if (this == DEFAULT || mapper == null) return Optional.empty();
        return Optional.ofNullable(mapper.apply(get()));
    }
    public <X extends Throwable> boolean orElseThrow(Supplier<X> exception) throws X {
        if (this == DEFAULT) throw exception.get();
        return get();
    }
}
