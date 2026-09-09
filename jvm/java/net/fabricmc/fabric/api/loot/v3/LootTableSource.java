package net.fabricmc.fabric.api.loot.v3;

/** Origin of a loot table during reload. */
public enum LootTableSource {
    VANILLA(true), MOD(true), DATA_PACK(false), REPLACED(false);
    private final boolean builtin;
    LootTableSource(boolean builtin) { this.builtin = builtin; }
    public boolean isBuiltin() { return builtin; }
}
