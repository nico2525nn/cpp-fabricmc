package net.fabricmc.fabric.api.gamerule.v1;

import net.minecraft.world.GameRules;

/** Fabric registration facade for vanilla and custom gamerule categories. */
public final class GameRuleRegistry {
    private GameRuleRegistry() { }

    public static <T extends GameRules.Rule<T>> GameRules.Key<T> register(
            String name, GameRules.Category category, GameRules.Type<T> type) {
        return GameRules.register(name, category, type);
    }

    public static <T extends GameRules.Rule<T>> GameRules.Key<T> register(
            String name, CustomGameRuleCategory category, GameRules.Type<T> type) {
        GameRules.Key<T> key = GameRules.register(name, GameRules.Category.MISC, type);
        CustomGameRuleCategory.attach(key, category);
        return key;
    }

    public static boolean hasRegistration(String name) {
        if (name == null) return false;
        return GameRules.getRuleTypes().keySet().stream()
            .anyMatch(key -> name.equals(key.getName()));
    }
}
