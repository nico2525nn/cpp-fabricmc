package net.fabricmc.fabric.api.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.registry.RegistryKey;
import net.minecraft.world.event.GameEvent;

/** Frequency overrides for vibration game events. */
public final class SculkSensorFrequencyRegistry {
    private static final Map<RegistryKey<GameEvent>, Integer> VALUES = new ConcurrentHashMap<>();
    private SculkSensorFrequencyRegistry() { }
    public static void register(RegistryKey<GameEvent> gameEvent, int frequency) {
        if (gameEvent == null) throw new NullPointerException("gameEvent");
        VALUES.put(gameEvent, Math.max(0, Math.min(15, frequency)));
    }
    public static int get(RegistryKey<GameEvent> gameEvent) { return VALUES.getOrDefault(gameEvent, 0); }
}
