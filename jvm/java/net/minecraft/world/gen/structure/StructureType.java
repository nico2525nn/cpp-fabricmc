package net.minecraft.world.gen.structure;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/** Registry value for a server-side structure type in 1.21.4. */
public class StructureType<T extends Structure> {
    private static final Map<String, StructureType<?>> TYPES = new ConcurrentHashMap<>();
    private final String id;

    public StructureType() { this(""); }
    public StructureType(String id) { this.id = id == null ? "" : id; }

    public String id() { return id; }
    public String getId() { return id; }

    public static <T extends Structure> StructureType<T> register(String id, Object codec) {
        StructureType<T> type = new StructureType<>(id);
        if (id != null) TYPES.put(id, type);
        return type;
    }

    @SuppressWarnings("unchecked")
    public static <T extends Structure> StructureType<T> get(String id) {
        return (StructureType<T>) TYPES.get(id);
    }
}
