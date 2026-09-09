package net.minecraft.entity;

import net.minecraft.util.StringIdentifiable;

/**
 * 1.21.4 natural-spawn categories.  Carpet reads this enum while building
 * its spawn command, so both the declaration order and serialized names must
 * match vanilla.
 */
public enum SpawnGroup implements StringIdentifiable {
    MONSTER("monster", 70, false, false, 128),
    CREATURE("creature", 10, true, true, 128),
    AMBIENT("ambient", 15, true, false, 128),
    AXOLOTLS("axolotls", 5, true, false, 128),
    UNDERGROUND_WATER_CREATURE("underground_water_creature", 5, true, false, 128),
    WATER_CREATURE("water_creature", 5, true, false, 128),
    WATER_AMBIENT("water_ambient", 20, true, false, 64),
    MISC("misc", -1, true, true, 128);

    private final String name;
    private final int capacity;
    private final boolean peaceful;
    private final boolean rare;
    private final int despawnStartRange;
    private final int immediateDespawnRange;

    SpawnGroup(String name, int capacity, boolean peaceful, boolean rare,
               int immediateDespawnRange) {
        this.name = name;
        this.capacity = capacity;
        this.peaceful = peaceful;
        this.rare = rare;
        // Vanilla keeps the start of the probabilistic despawn band at 32
        // for every group; only the immediate range is constructor data in
        // the 1.21.4 ABI.
        this.despawnStartRange = 32;
        this.immediateDespawnRange = immediateDespawnRange;
    }

    public int getCapacity() { return capacity; }
    public int getMaxInstancesPerChunk() { return capacity; }
    public int getDespawnStartRange() { return despawnStartRange; }
    public int getImmediateDespawnRange() { return immediateDespawnRange; }
    public String getName() { return name; }
    public boolean isRare() { return rare; }
    public boolean isPeaceful() { return peaceful; }
    @Override public String asString() { return name; }
    @Override public String toString() { return name; }

    public static SpawnGroup byName(String name) {
        if (name != null) {
            for (SpawnGroup group : values()) {
                if (group.name.equals(name)) return group;
            }
        }
        return null;
    }
}
