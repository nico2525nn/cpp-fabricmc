package net.minecraft.loot.context;

import net.minecraft.util.math.random.Random;

/** Minimal context passed to server loot predicates and number providers. */
public class LootContext {
    private final Random random;
    public LootContext() { this(new Random(0L)); }
    public LootContext(Random random) { this.random = random == null ? new Random(0L) : random; }
    public Random getRandom() { return random; }
}
