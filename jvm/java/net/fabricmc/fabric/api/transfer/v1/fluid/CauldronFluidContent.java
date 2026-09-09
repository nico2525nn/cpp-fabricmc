package net.fabricmc.fabric.api.transfer.v1.fluid;

import java.util.Collection;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.fluid.Fluid;
import net.minecraft.fluid.Fluids;
import net.minecraft.state.property.IntProperty;

/**
 * Describes how a cauldron maps its level property to a fluid amount.
 * Registration is identity based, matching Fabric's copy-on-write lookup
 * contract while remaining useful in the dependency-free shadow runtime.
 */
public final class CauldronFluidContent {
    public final Block block;
    public final Fluid fluid;
    public final long amountPerLevel;
    public final int maxLevel;
    public final IntProperty levelProperty;

    private static final Map<Block, CauldronFluidContent> BLOCK_TO_CAULDRON = new ConcurrentHashMap<>();
    private static final Map<Fluid, CauldronFluidContent> FLUID_TO_CAULDRON = new ConcurrentHashMap<>();

    private CauldronFluidContent(Block block, Fluid fluid, long amountPerLevel,
                                 int maxLevel, IntProperty levelProperty) {
        this.block = block;
        this.fluid = fluid;
        this.amountPerLevel = amountPerLevel;
        this.maxLevel = maxLevel;
        this.levelProperty = levelProperty;
    }

    public static CauldronFluidContent getForBlock(Block block) {
        return BLOCK_TO_CAULDRON.get(block);
    }

    public static CauldronFluidContent getForFluid(Fluid fluid) {
        return FLUID_TO_CAULDRON.get(fluid);
    }

    public static synchronized CauldronFluidContent registerCauldron(
            Block block, Fluid fluid, long amountPerLevel, IntProperty levelProperty) {
        if (block == null || fluid == null) throw new NullPointerException("block/fluid");
        if (amountPerLevel <= 0) throw new IllegalArgumentException("amountPerLevel must be positive");
        CauldronFluidContent existing = BLOCK_TO_CAULDRON.get(block);
        if (existing != null) return existing;
        if (FLUID_TO_CAULDRON.containsKey(fluid)) {
            throw new IllegalArgumentException("Fluid already has a cauldron mapping");
        }
        int max = 1;
        if (levelProperty != null) {
            Collection<Integer> levels = levelProperty.getValues();
            if (levels.isEmpty()) throw new IllegalArgumentException("level property has no values");
            int min = Integer.MAX_VALUE;
            for (Integer level : levels) {
                if (level == null) continue;
                min = Math.min(min, level);
                max = Math.max(max, level);
            }
            if (min != 1 || max < 1) throw new IllegalArgumentException("cauldron levels must start at one");
        }
        CauldronFluidContent value = new CauldronFluidContent(block, fluid, amountPerLevel, max, levelProperty);
        BLOCK_TO_CAULDRON.put(block, value);
        FLUID_TO_CAULDRON.put(fluid, value);
        return value;
    }

    public int currentLevel(BlockState state) {
        if (fluid == Fluids.EMPTY) return 0;
        if (levelProperty == null || state == null) return 1;
        Integer value = state.get(levelProperty);
        return value == null ? 0 : value;
    }
}
