package net.minecraft.block.enums;

import net.minecraft.util.StringIdentifiable;

/** 1.21.4 rail-shape enum used by the minecart controller ABI. */
public enum RailShape implements StringIdentifiable {
    NORTH_SOUTH("north_south"),
    EAST_WEST("east_west"),
    ASCENDING_EAST("ascending_east"),
    ASCENDING_WEST("ascending_west"),
    ASCENDING_NORTH("ascending_north"),
    ASCENDING_SOUTH("ascending_south"),
    SOUTH_EAST("south_east"),
    SOUTH_WEST("south_west"),
    NORTH_WEST("north_west"),
    NORTH_EAST("north_east");

    private final String name;
    RailShape(String name) { this.name = name; }
    public boolean isAscending() { return name.startsWith("ascending_"); }
    public String getName() { return name; }
    @Override public String asString() { return name; }
}
