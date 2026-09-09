package net.minecraft.world.chunk;

import java.util.function.Predicate;
import net.minecraft.block.BlockState;
import net.minecraft.fluid.FluidState;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.registry.Registry;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.world.biome.source.BiomeSupplier;
import net.minecraft.world.biome.source.util.MultiNoiseUtil;

/** Compact 1.21.4 chunk-section ABI used by world and Lithium mixins. */
public class ChunkSection {
    private short nonEmptyBlockCount;
    private short nonEmptyFluidCount;
    private short randomTickableBlockCount;
    private PalettedContainer<Object> biomeContainer;
    private PalettedContainer<BlockState> blockStateContainer;

    public ChunkSection(Registry<?> biomeRegistry) {
        this(new PalettedContainer<>(), new PalettedContainer<>());
    }

    public ChunkSection(PalettedContainer<BlockState> blockStateContainer,
                        PalettedContainer<?> biomeContainer) {
        this.blockStateContainer = blockStateContainer == null ? new PalettedContainer<>() : blockStateContainer;
        @SuppressWarnings("unchecked") PalettedContainer<Object> typed =
            (PalettedContainer<Object>) (biomeContainer == null ? new PalettedContainer<>() : biomeContainer);
        this.biomeContainer = typed;
    }

    public ChunkSection(ChunkSection section) {
        this(section == null ? null : section.blockStateContainer,
             section == null ? null : section.biomeContainer);
        if (section != null) {
            nonEmptyBlockCount = section.nonEmptyBlockCount;
            nonEmptyFluidCount = section.nonEmptyFluidCount;
            randomTickableBlockCount = section.randomTickableBlockCount;
        }
    }

    public BlockState setBlockState(int x, int y, int z, BlockState state, boolean lock) {
        BlockState previous = getBlockState(x, y, z);
        blockStateContainer.set(index(x, y, z), state);
        return previous;
    }

    public BlockState setBlockState(int x, int y, int z, BlockState state) {
        return setBlockState(x, y, z, state, false);
    }

    public BlockState getBlockState(int x, int y, int z) {
        BlockState state = blockStateContainer.get(index(x, y, z));
        return state == null ? net.minecraft.block.Blocks.AIR.getDefaultState() : state;
    }

    public FluidState getFluidState(int x, int y, int z) { return getBlockState(x, y, z).getFluidState(); }
    public RegistryEntry<Object> getBiome(int x, int y, int z) { return null; }

    public PalettedContainer<BlockState> getBlockStateContainer() { return blockStateContainer; }
    public PalettedContainer<Object> getBiomeContainer() { return biomeContainer; }
    public boolean isEmpty() { return nonEmptyBlockCount == 0; }
    public boolean hasRandomBlockTicks() { return randomTickableBlockCount > 0; }
    public boolean hasRandomFluidTicks() { return nonEmptyFluidCount > 0; }
    public boolean hasAny(Predicate<BlockState> predicate) {
        if (predicate == null) return false;
        for (int i = 0; i < 4096; i++) if (predicate.test(blockStateContainer.get(i))) return true;
        return false;
    }
    public boolean hasRandomTicks() { return hasRandomBlockTicks() || hasRandomFluidTicks(); }
    public int getPacketSize() { return 0; }
    public void lock() { blockStateContainer.lock(); }
    public void unlock() { blockStateContainer.unlock(); }
    public ChunkSection copy() { return new ChunkSection(this); }
    public void calculateCounts() {}
    public void readDataPacket(PacketByteBuf buf) {}
    public void readBiomePacket(PacketByteBuf buf) {}
    public void toPacket(PacketByteBuf buf) {}
    public void populateBiomes(Object biomeSupplier, Object sampler, int x, int y, int z) {}
    public void populateBiomes(BiomeSupplier biomeSupplier,
                               MultiNoiseUtil.MultiNoiseSampler sampler,
                               int x, int y, int z) {}

    private static int index(int x, int y, int z) { return (y << 8) | (z << 4) | x; }

    @FunctionalInterface
    public interface BlockStateCounter {
        void accept(BlockState state, int count);
    }
}
