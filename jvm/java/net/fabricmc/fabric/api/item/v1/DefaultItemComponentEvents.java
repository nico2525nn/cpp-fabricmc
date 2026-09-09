package net.fabricmc.fabric.api.item.v1;

import java.util.Collection;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Predicate;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.component.ComponentMap;
import net.minecraft.item.Item;

/** Events for modifying the default component map of registered items. */
public final class DefaultItemComponentEvents {
    public static final Event<ModifyCallback> MODIFY = EventFactory.createArrayBacked(
        ModifyCallback.class, callbacks -> context -> {
            for (ModifyCallback callback : callbacks) if (callback != null) callback.modify(context);
        });

    private DefaultItemComponentEvents() { }

    @FunctionalInterface
    public interface ModifyCallback {
        void modify(ModifyContext context);
    }

    public interface ModifyContext {
        void modify(Predicate<Item> predicate, BiConsumer<ComponentMap.Builder, Item> modifier);

        default void modify(Item item, Consumer<ComponentMap.Builder> modifier) {
            modify(candidate -> candidate == item,
                (builder, ignored) -> { if (modifier != null) modifier.accept(builder); });
        }

        default void modify(Collection<Item> items, BiConsumer<ComponentMap.Builder, Item> modifier) {
            modify(items == null ? candidate -> false : items::contains, modifier);
        }
    }
}
