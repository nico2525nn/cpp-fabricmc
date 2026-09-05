package net.minecraft.block;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import net.minecraft.fluid.FluidState;
import net.minecraft.fluid.Fluids;
import net.minecraft.registry.TagKey;
import net.minecraft.state.property.Property;
import net.minecraft.util.Identifier;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.util.shape.VoxelShapes;
import net.minecraft.world.World;
import net.minecraft.util.math.BlockPos;

/** C++-backed raw block-state view with immutable Java property overlays. */
public class BlockState {
    private final int rawState;
    private final Block block;
    private final Map<Property<?>, Comparable<?>> properties;

    public BlockState(int rawState) { this(rawState, rawState == 0 ? Blocks.AIR : new Block(rawState)); }
    public BlockState(int rawState, Block block) {
        this(rawState, block, defaultProperties(block));
    }
    private BlockState(int rawState, Block block, Map<Property<?>, Comparable<?>> properties) {
        this.rawState = Math.max(0, rawState);
        this.block = block == null ? Blocks.AIR : block;
        this.properties = Collections.unmodifiableMap(new LinkedHashMap<>(properties));
    }
    private static Map<Property<?>, Comparable<?>> defaultProperties(Block block) {
        Map<Property<?>, Comparable<?>> values = new LinkedHashMap<>();
        if (block != null && block.getStateManager() != null)
            for (Property<?> property : block.getStateManager().getProperties()) {
                java.util.Collection<?> options = property.getValues();
                if (!options.isEmpty()) values.put(property, (Comparable<?>) options.iterator().next());
            }
        return values;
    }
    public int getRawId() { return rawState; }
    public int getRawState() { return rawState; }
    public Block getBlock() { return block; }
    public boolean isAir() { return rawState == 0 || block == Blocks.AIR; }
    public boolean isOf(Block other) { return other != null && (block == other || rawState == other.getRawState()); }
    public boolean isIn(TagKey<Block> tag) { return block.getRegistryEntry() != null && block.getRegistryEntry().isIn(tag); }
    public boolean isIn(net.minecraft.registry.tag.TagKey<Block> tag) {
        return block.getRegistryEntry() != null && block.getRegistryEntry().isIn(tag);
    }
    public boolean isIn(Set<Block> blocks) { return blocks != null && blocks.contains(block); }
    public Identifier getRegistryId() { return block.getId(); }
    public <T extends Comparable<T>> T get(Property<T> property) {
        @SuppressWarnings("unchecked") T value = (T) properties.get(property); return value;
    }
    @SuppressWarnings("unchecked") public <T extends Comparable<T>> T get(Object property) {
        return property instanceof Property<?> p ? (T) properties.get(p) : null;
    }
    public <T extends Comparable<T>> BlockState with(Property<T> property, T value) {
        if (property == null || value == null || !property.getValues().contains(value)) throw new IllegalArgumentException("invalid block property value");
        Map<Property<?>, Comparable<?>> copy = new LinkedHashMap<>(properties); copy.put(property, value); return new BlockState(rawState, block, copy);
    }
    public <T extends Comparable<T>> BlockState with(Object property, T value) {
        return property instanceof Property<?> p ? withUnchecked(p, value) : this;
    }
    @SuppressWarnings({"rawtypes", "unchecked"}) private static BlockState withUnchecked(BlockState state, Property property, Comparable value) { return state.with(property, value); }
    private <T extends Comparable<T>> BlockState withUnchecked(Property<?> property, T value) {
        return with((Property<T>) property, value);
    }
    public <T extends Comparable<T>> BlockState cycle(Property<T> property) {
        if (property == null) return this;
        java.util.List<T> values = new java.util.ArrayList<>(property.getValues());
        int index = values.indexOf(get(property)); return with(property, values.get((index + 1 + values.size()) % values.size()));
    }
    public boolean contains(Property<?> property) { return properties.containsKey(property); }
    public Set<Property<?>> getProperties() { return properties.keySet(); }
    public Map<Property<?>, Comparable<?>> getEntries() { return properties; }
    public int getLuminance() { return block.getLuminance(); }
    public float getHardness(World world, BlockPos pos) { return block.getHardness(); }
    public boolean isOpaque() { return block.isOpaque(); }
    public boolean isSolidBlock(World world, BlockPos pos) { return !isAir(); }
    public boolean isTransparent() { return isAir(); }
    public BlockRenderType getRenderType() { return isAir() ? BlockRenderType.INVISIBLE : BlockRenderType.MODEL; }
    public VoxelShape getCollisionShape(World world, BlockPos pos) { return isAir() ? VoxelShapes.EMPTY : VoxelShapes.FULL_CUBE; }
    public VoxelShape getOutlineShape(World world, BlockPos pos) { return getCollisionShape(world, pos); }
    public FluidState getFluidState() {
        for (Map.Entry<Property<?>, Comparable<?>> entry : properties.entrySet())
            if ("waterlogged".equals(entry.getKey().getName()) && Boolean.TRUE.equals(entry.getValue())) return Fluids.WATER.getDefaultState();
        return Fluids.EMPTY.getDefaultState();
    }
    public BlockState toImmutable() { return this; }
    @Override public boolean equals(Object other) { return other instanceof BlockState state && rawState == state.rawState && block.equals(state.block) && properties.equals(state.properties); }
    @Override public int hashCode() { return java.util.Objects.hash(rawState, block, properties); }
    @Override public String toString() { return "BlockState{" + block.getId() + properties + "}"; }
}
