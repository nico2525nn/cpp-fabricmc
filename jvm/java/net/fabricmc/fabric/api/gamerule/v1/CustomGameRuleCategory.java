package net.fabricmc.fabric.api.gamerule.v1;

import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;
import net.minecraft.world.GameRules;

/** A namespaced category for a Fabric-registered game rule. */
public final class CustomGameRuleCategory {
    private static final Map<GameRules.Key<?>, CustomGameRuleCategory> CATEGORIES =
        new ConcurrentHashMap<>();
    private final Identifier id;
    private final Text name;

    public CustomGameRuleCategory(Identifier id, Text name) {
        this.id = Objects.requireNonNull(id, "id");
        this.name = Objects.requireNonNull(name, "name");
    }

    public Identifier getId() { return id; }
    public Text getName() { return name; }

    static void attach(GameRules.Key<?> key, CustomGameRuleCategory category) {
        if (key != null && category != null) CATEGORIES.put(key, category);
    }

    public static <T extends GameRules.Rule<T>> Optional<CustomGameRuleCategory>
    getCategory(GameRules.Key<T> key) {
        return Optional.ofNullable(CATEGORIES.get(key));
    }

    static void clear() { CATEGORIES.clear(); }

    @Override public boolean equals(Object other) {
        return other instanceof CustomGameRuleCategory category && id.equals(category.id);
    }
    @Override public int hashCode() { return id.hashCode(); }
}
