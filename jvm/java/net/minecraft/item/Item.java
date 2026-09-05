package net.minecraft.item;

import net.minecraft.util.Identifier;

public class Item {
    private final Identifier id;
    private final int rawState;
    public static class Settings {
        public Settings maxCount(int count) { return this; }
        public Settings maxDamage(int damage) { return this; }
        public Settings fireproof() { return this; }
    }
    public Item() { this(Identifier.of("minecraft", "air"), 0); }
    public Item(Identifier id) { this(id, 0); }
    public Item(Identifier id, int rawState) { this.id = id; this.rawState = rawState; }
    public Item(Settings settings) { this(Identifier.of("cppfm", "custom_item"), 0); }
    public static Item fromRaw(int rawId, String name) {
        return new Item(Identifier.tryParse(name == null || name.isEmpty() ? "minecraft:air" : name), rawId);
    }
    public Identifier getId() { return id; }
    public int getRawState() { return rawState; }
    public int getRawId() { return rawState; }
}
