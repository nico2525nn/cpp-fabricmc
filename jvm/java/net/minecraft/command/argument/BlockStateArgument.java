package net.minecraft.command.argument;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.function.Predicate;
import net.minecraft.block.BlockState;
import net.minecraft.block.pattern.CachedBlockPosition;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

/**
 * Parsed block-state argument.  The native world owns the block-state id;
 * this value keeps the Java-side state and the optional property predicate
 * available to command implementations and mod bytecode.
 */
public final class BlockStateArgument implements Predicate<CachedBlockPosition> {
    private final BlockState state;
    private final Set<String> properties;
    private final NbtCompound data;

    public BlockStateArgument(BlockState state, Set<String> properties, NbtCompound data) {
        this.state = state == null ? new BlockState(0) : state;
        this.properties = properties == null
            ? Set.of()
            : Collections.unmodifiableSet(new LinkedHashSet<>(properties));
        this.data = data == null ? new NbtCompound() : data;
    }

    public BlockState getBlockState() { return state; }
    public Set<String> getProperties() { return properties; }

    public boolean setBlockState(ServerWorld world, BlockPos pos, int flags) {
        return world != null && pos != null && world.setBlockState(pos, state, flags);
    }

    public BlockState copyPropertiesTo(BlockState target) {
        if (target == null) return state;
        BlockState result = target;
        for (var property : state.getProperties()) {
            @SuppressWarnings({"rawtypes", "unchecked"})
            net.minecraft.state.property.Property raw = property;
            Comparable value = (Comparable) state.get(raw);
            if (value != null && target.contains(property)) {
                try {
                    result = result.with(raw, value);
                } catch (RuntimeException ignored) {
                    // A property from a different block may not be compatible.
                }
            }
        }
        return result;
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    public BlockState copyProperty(BlockState from, BlockState to,
                                   net.minecraft.state.property.Property property) {
        if (from == null || to == null || property == null) return to;
        Comparable value = from.get(property);
        return value == null ? to : to.with(property, value);
    }

    @Override public boolean test(CachedBlockPosition position) {
        return position != null && state.equals(position.getBlockState());
    }

    public boolean test(World world, BlockPos pos) {
        return world != null && pos != null && state.equals(world.getBlockState(pos));
    }

    public NbtCompound getData() { return data.copy(); }
}
