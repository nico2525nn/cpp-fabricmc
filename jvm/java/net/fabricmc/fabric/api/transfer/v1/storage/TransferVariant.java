package net.fabricmc.fabric.api.transfer.v1.storage;

import java.util.Objects;
import net.minecraft.component.ComponentChanges;
import net.minecraft.component.ComponentMap;

/** Immutable resource plus component metadata used by transfer storages. */
public interface TransferVariant<O> {
    boolean isBlank();
    O getObject();
    ComponentChanges getComponents();
    ComponentMap getComponentMap();

    default boolean hasComponents() { return !getComponents().isEmpty(); }
    default boolean componentsMatch(ComponentChanges other) { return Objects.equals(getComponents(), other); }
    default boolean isOf(O object) { return getObject() == object; }
    default TransferVariant<O> withComponentChanges(ComponentChanges changes) {
        throw new UnsupportedOperationException("withComponentChanges is not supported");
    }
}
