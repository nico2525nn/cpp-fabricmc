package net.minecraft.loot.provider.number;

import net.minecraft.loot.context.LootContext;

/** Number source used by loot pool roll and bonus-roll builders. */
public interface LootNumberProvider {
    default float nextFloat(LootContext context) { return nextInt(context); }
    default int nextInt(LootContext context) { return 0; }
    default LootNumberProviderType getType() { return null; }
}
