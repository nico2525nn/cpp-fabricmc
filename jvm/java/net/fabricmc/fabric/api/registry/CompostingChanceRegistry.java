package net.fabricmc.fabric.api.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.item.Item;

public final class CompostingChanceRegistry {
    public static final CompostingChanceRegistry INSTANCE = new CompostingChanceRegistry();
    private final Map<Item, Float> values = new ConcurrentHashMap<>();
    private CompostingChanceRegistry() {}
    public void add(Item item, float chance) { if (item != null) values.put(item, Math.max(0.0f, Math.min(1.0f, chance))); }
    public Float get(Item item) { return values.get(item); }
    public boolean contains(Item item) { return values.containsKey(item); }
    public void clear() { values.clear(); }
}
