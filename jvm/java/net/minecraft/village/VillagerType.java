package net.minecraft.village;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.util.Identifier;

/** Registry value for a villager appearance type. */
public final class VillagerType {
    private static final Map<Identifier, VillagerType> BY_ID = new ConcurrentHashMap<>();
    private final String id;
    public VillagerType(String id) { this.id = id == null ? "minecraft:plains" : id; }
    public String id() { return id; }
    public static VillagerType register(Identifier id, VillagerType type) { if (id != null && type != null) BY_ID.putIfAbsent(id, type); return type; }
    public static VillagerType of(Identifier id) { return BY_ID.get(id); }
}
