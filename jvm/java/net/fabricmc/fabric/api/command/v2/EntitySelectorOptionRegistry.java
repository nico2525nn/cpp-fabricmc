package net.fabricmc.fabric.api.command.v2;

import java.util.function.Predicate;
import net.minecraft.command.EntitySelectorOptions;
import net.minecraft.command.EntitySelectorReader;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;

/** Registration bridge for custom {@code @e[option=...]} selector options. */
public final class EntitySelectorOptionRegistry {
    private EntitySelectorOptionRegistry() { }

    public static void register(Identifier id, Text description,
                                EntitySelectorOptions.SelectorHandler handler,
                                Predicate<EntitySelectorReader> condition) {
        if (id == null || handler == null || condition == null)
            throw new NullPointerException("id/handler/condition");
        EntitySelectorOptions.putOption(id.getPath(), handler, condition, description);
    }

    public static void registerNonRepeatable(Identifier id, Text description,
                                             EntitySelectorOptions.SelectorHandler handler) {
        if (id == null || handler == null) throw new NullPointerException("id/handler");
        register(id, description, reader -> {
                handler.handle(reader);
                reader.setCustomFlag(id, true);
            }, reader -> !reader.getCustomFlag(id));
    }
}
