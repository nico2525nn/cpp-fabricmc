package net.minecraft.block.entity;

import net.minecraft.util.math.BlockPos;
import net.minecraft.block.BlockState;
import net.minecraft.world.World;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.registry.RegistryWrapper;

public class BlockEntity implements net.fabricmc.fabric.api.attachment.v1.AttachmentTarget,
        net.fabricmc.fabric.api.blockview.v2.RenderDataBlockEntity {
    private final BlockPos pos;
    private World world;
    private BlockState cachedState;
    private boolean removed;
    public BlockEntity() { this(new BlockPos(0, 0, 0)); }
    public BlockEntity(BlockPos pos) { this(pos, new BlockState(0)); }
    public BlockEntity(BlockPos pos, BlockState state) {
        this.pos = pos == null ? new BlockPos(0, 0, 0) : pos;
        this.cachedState = state == null ? new BlockState(0) : state;
    }
    public BlockEntity(BlockEntityType<?> type, BlockPos pos, BlockState state) { this(pos, state); }
    public BlockPos getPos() { return pos; }
    public World getWorld() { return world; }
    public void setWorld(World world) { this.world = world; }
    public BlockState getCachedState() { return cachedState; }
    public void setCachedState(BlockState state) {
        cachedState = state == null ? new BlockState(0) : state;
    }
    /** Vanilla lifecycle hook used when a block entity leaves its chunk. */
    public void markRemoved() { removed = true; }
    /** Whether this block entity has been detached from its owning chunk. */
    public boolean isRemoved() { return removed; }
    public void markDirty() { }
    /** 1.21.4 data-component-aware load hook. */
    public void read(NbtCompound nbt, RegistryWrapper.WrapperLookup registries) { }
    /** 1.21.4 data-component-aware save hook. */
    public NbtCompound writeNbt(NbtCompound nbt, RegistryWrapper.WrapperLookup registries) {
        return nbt == null ? new NbtCompound() : nbt;
    }
    /**
     * Vanilla's public snapshot helper.  Fabric transfer mixins call this
     * overload while copying a block entity between inventories; keeping the
     * lookup argument in the signature is important because it is part of the
     * 1.21.4 ABI even when this native-backed shadow has no components to write.
     */
    public NbtCompound createNbt(RegistryWrapper.WrapperLookup registries) {
        return writeNbt(new NbtCompound(), registries);
    }
}
