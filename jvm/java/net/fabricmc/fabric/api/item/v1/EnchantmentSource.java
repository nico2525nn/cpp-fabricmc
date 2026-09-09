package net.fabricmc.fabric.api.item.v1;

public enum EnchantmentSource {
    VANILLA,
    MOD,
    DATA_PACK;

    public boolean isBuiltin() { return this == VANILLA; }
}
