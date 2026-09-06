package net.minecraft.enchantment;

import net.minecraft.entity.LivingEntity;
import net.minecraft.server.world.ServerWorld;

/** Server-side enchantment tick boundary retained for 1.21.4 mixin targets. */
public final class EnchantmentHelper {
    private EnchantmentHelper() { }

    public static void onTick(ServerWorld world, LivingEntity user) { }
}
