package net.fabricmc.fabric.api.gamerule.v1;

import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.Function;
import net.fabricmc.fabric.api.gamerule.v1.rule.DoubleRule;
import net.fabricmc.fabric.api.gamerule.v1.rule.EnumRule;
import net.minecraft.resource.featuretoggle.FeatureSet;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.GameRules;

/** Factory methods matching Fabric Game Rule API v1 for Minecraft 1.21.4. */
public final class GameRuleFactory {
    private GameRuleFactory() { }

    public static GameRules.Type<GameRules.BooleanRule> createBooleanRule(boolean initialValue) {
        return createBooleanRule(initialValue, (server, rule) -> { });
    }

    public static GameRules.Type<GameRules.BooleanRule> createBooleanRule(
            boolean initialValue,
            BiConsumer<MinecraftServer, GameRules.BooleanRule> callback) {
        return GameRules.BooleanRule.create(initialValue, callback);
    }

    public static GameRules.Type<GameRules.IntRule> createIntRule(int initialValue) {
        return createIntRule(initialValue, (server, rule) -> { });
    }

    public static GameRules.Type<GameRules.IntRule> createIntRule(int initialValue, int min) {
        return createIntRule(initialValue, min, Integer.MAX_VALUE);
    }

    public static GameRules.Type<GameRules.IntRule> createIntRule(
            int initialValue, int min,
            BiConsumer<MinecraftServer, GameRules.IntRule> callback) {
        return createIntRule(initialValue, min, Integer.MAX_VALUE, callback);
    }

    public static GameRules.Type<GameRules.IntRule> createIntRule(
            int initialValue, int min, int max) {
        return createIntRule(initialValue, min, max, (server, rule) -> { });
    }

    public static GameRules.Type<GameRules.IntRule> createIntRule(
            int initialValue,
            BiConsumer<MinecraftServer, GameRules.IntRule> callback) {
        return createIntRule(initialValue, Integer.MIN_VALUE, Integer.MAX_VALUE, callback);
    }

    public static GameRules.Type<GameRules.IntRule> createIntRule(
            int initialValue, int min, int max,
            BiConsumer<MinecraftServer, GameRules.IntRule> callback) {
        return GameRules.IntRule.create(initialValue, min, max, FeatureSet.EMPTY,
            callback == null ? (server, rule) -> { } : callback);
    }

    public static GameRules.Type<DoubleRule> createDoubleRule(double initialValue) {
        return createDoubleRule(initialValue, (server, rule) -> { });
    }

    public static GameRules.Type<DoubleRule> createDoubleRule(double initialValue, double min) {
        return createDoubleRule(initialValue, min, Double.MAX_VALUE);
    }

    public static GameRules.Type<DoubleRule> createDoubleRule(
            double initialValue, double min,
            BiConsumer<MinecraftServer, DoubleRule> callback) {
        return createDoubleRule(initialValue, min, Double.MAX_VALUE, callback);
    }

    public static GameRules.Type<DoubleRule> createDoubleRule(
            double initialValue, double min, double max) {
        return createDoubleRule(initialValue, min, max, (server, rule) -> { });
    }

    public static GameRules.Type<DoubleRule> createDoubleRule(
            double initialValue,
            BiConsumer<MinecraftServer, DoubleRule> callback) {
        return createDoubleRule(initialValue, Double.MIN_VALUE, Double.MAX_VALUE, callback);
    }

    public static GameRules.Type<DoubleRule> createDoubleRule(
            double initialValue, double min, double max,
            BiConsumer<MinecraftServer, DoubleRule> callback) {
        if (!Double.isFinite(initialValue) || !Double.isFinite(min) || !Double.isFinite(max)
                || min > max || initialValue < min || initialValue > max)
            throw new IllegalArgumentException("invalid double gamerule range");
        return new GameRules.Type<>(
            () -> com.mojang.brigadier.arguments.DoubleArgumentType.doubleArg(min, max),
            (Function<GameRules.Type<DoubleRule>, DoubleRule>) type ->
                new DoubleRule(type, initialValue, min, max),
            callback == null ? (server, rule) -> { } : callback,
            (visitor, key, type) -> {
                if (visitor instanceof FabricGameRuleVisitor fabric)
                    fabric.visitDouble((GameRules.Key<DoubleRule>) key,
                        (GameRules.Type<DoubleRule>) type);
            }, FeatureSet.EMPTY);
    }

    public static <E extends Enum<E>> GameRules.Type<EnumRule<E>> createEnumRule(E initialValue) {
        Objects.requireNonNull(initialValue, "Default rule value cannot be null");
        return createEnumRule(initialValue, initialValue.getDeclaringClass().getEnumConstants());
    }

    public static <E extends Enum<E>> GameRules.Type<EnumRule<E>> createEnumRule(
            E initialValue,
            BiConsumer<MinecraftServer, EnumRule<E>> callback) {
        Objects.requireNonNull(initialValue, "Default rule value cannot be null");
        return createEnumRule(initialValue, initialValue.getDeclaringClass().getEnumConstants(), callback);
    }

    public static <E extends Enum<E>> GameRules.Type<EnumRule<E>> createEnumRule(
            E initialValue, E[] supportedValues) {
        return createEnumRule(initialValue, supportedValues, (server, rule) -> { });
    }

    public static <E extends Enum<E>> GameRules.Type<EnumRule<E>> createEnumRule(
            E initialValue, E[] supportedValues,
            BiConsumer<MinecraftServer, EnumRule<E>> callback) {
        Objects.requireNonNull(initialValue, "Default rule value cannot be null");
        Objects.requireNonNull(supportedValues, "Supported Values cannot be null");
        if (supportedValues.length == 0)
            throw new IllegalArgumentException("Cannot register an enum rule where no values are supported");
        E[] values = supportedValues.clone();
        return new GameRules.Type<>(
            () -> com.mojang.brigadier.arguments.StringArgumentType.word(),
            (Function<GameRules.Type<EnumRule<E>>, EnumRule<E>>) type ->
                new EnumRule<>(type, initialValue, values),
            callback == null ? (server, rule) -> { } : callback,
            (visitor, key, type) -> {
                if (visitor instanceof FabricGameRuleVisitor fabric)
                    fabric.visitEnum((GameRules.Key<EnumRule<E>>) key,
                        (GameRules.Type<EnumRule<E>>) type);
            }, FeatureSet.EMPTY);
    }
}
