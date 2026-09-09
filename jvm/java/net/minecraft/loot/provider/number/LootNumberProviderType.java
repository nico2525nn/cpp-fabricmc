package net.minecraft.loot.provider.number;

public final class LootNumberProviderType {
    private final LootNumberProvider provider;
    public LootNumberProviderType() { this(null); }
    public LootNumberProviderType(LootNumberProvider provider) { this.provider = provider; }
    public LootNumberProvider provider() { return provider; }
}
