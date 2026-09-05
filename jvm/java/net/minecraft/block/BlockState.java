package net.minecraft.block;

import net.minecraft.util.Identifier;

/** C++-backed raw block-state view. Property mutation is intentionally immutable. */
public class BlockState {
    private final int rawState;
    private final Block block;

    public BlockState(int rawState) { this(rawState, rawState == 0 ? Blocks.AIR : new Block(rawState)); }
    public BlockState(int rawState, Block block) {
        this.rawState = rawState;
        this.block = block == null ? Blocks.AIR : block;
    }
    public int getRawId() { return rawState; }
    public int getRawState() { return rawState; }
    public Block getBlock() { return block; }
    public boolean isAir() { return rawState == 0; }
    public boolean isOf(Block other) { return other != null && rawState == other.getRawState(); }
    public boolean isIn(Object tag) { return false; }
    public Identifier getRegistryId() { return block.getId(); }
    public <T extends Comparable<T>> T get(Object property) { return null; }
    public <T extends Comparable<T>> BlockState with(Object property, T value) { return this; }
    @Override public String toString() { return "BlockState{" + rawState + "}"; }
}
