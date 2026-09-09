package net.minecraft.potion;

/** Minimal named potion value for the 1.21.4 brewing API. */
public class Potion {
    private final String id;
    public Potion() { this("minecraft:empty"); }
    public Potion(String id) { this.id = id == null ? "minecraft:empty" : id; }
    public String id() { return id; }
    @Override public String toString() { return id; }
}
