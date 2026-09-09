package net.minecraft.entity.ai.pathing;

/** Named path-node categories used by spawn restrictions and navigation hooks. */
public enum PathNodeType {
    BLOCKED,
    OPEN,
    WALKABLE,
    WALKABLE_DOOR,
    TRAPDOOR,
    FENCE,
    LAVA,
    WATER,
    WATER_BORDER,
    RAIL,
    UNPASSABLE_RAIL,
    DANGER_RAIL,
    DAMAGE_FLOOR,
    DAMAGE_CAUTIOUS,
    DANGER_FIRE,
    DANGER_OTHER,
    DOOR_OPEN,
    DOOR_WOOD_CLOSED,
    DOOR_IRON_CLOSED,
    BREACH,
    LEAVES,
    STICKY_HONEY,
    COCOA,
    POWDER_SNOW,
    DAMAGE_CACTUS,
    DAMAGE_OTHER,
    LONG_JUMP,
    COCOA_BEANS;
}
