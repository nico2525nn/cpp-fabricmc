package net.minecraft.loot.function;

/** Registry descriptor for a loot function implementation. */
public final class LootFunctionType {
    private final LootFunction function;
    public LootFunctionType() { this(null); }
    public LootFunctionType(LootFunction function) { this.function = function; }
    public LootFunction function() { return function; }
}
