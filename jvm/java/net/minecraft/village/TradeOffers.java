package net.minecraft.village;

import net.minecraft.entity.Entity;
import net.minecraft.util.math.random.Random;

/** Minimal factory namespace used by merchant recipe generation. */
public final class TradeOffers {
    private TradeOffers() { }

    @FunctionalInterface
    public interface Factory {
        TradeOffer create(Entity entity, Random random);
    }
}
