package net.minecraft.world.explosion;

import java.util.Optional;
import net.minecraft.block.BlockState;
import net.minecraft.entity.Entity;
import net.minecraft.fluid.FluidState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.BlockView;

/** Vanilla-compatible extension point for explosion resistance and damage. */
public class ExplosionBehavior {
    public float getKnockbackModifier(Entity entity) { return 1.0f; }
    public boolean shouldDamage(Explosion explosion, Entity entity) { return true; }
    public float calculateDamage(Explosion explosion, Entity entity, float amount) { return amount; }
    public boolean canDestroyBlock(Explosion explosion, BlockView world, BlockPos pos, BlockState state, float power) {
        return true;
    }
    public Optional<Float> getBlastResistance(Explosion explosion, BlockView world, BlockPos pos,
                                                BlockState state, FluidState fluidState) {
        return Optional.empty();
    }
}
