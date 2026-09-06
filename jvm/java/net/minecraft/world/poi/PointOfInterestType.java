package net.minecraft.world.poi;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;
import net.minecraft.block.BlockState;

/** A block-state category recognized by villager AI. */
public class PointOfInterestType {
    private final Set<BlockState> blockStates;
    private final int ticketCount;
    private final int searchDistance;

    public PointOfInterestType(Set<BlockState> blockStates, int ticketCount, int searchDistance) {
        this.blockStates = Collections.unmodifiableSet(new LinkedHashSet<>(
                blockStates == null ? Set.of() : blockStates));
        this.ticketCount = ticketCount;
        this.searchDistance = searchDistance;
    }
    public Set<BlockState> blockStates() { return blockStates; }
    public int ticketCount() { return ticketCount; }
    public int searchDistance() { return searchDistance; }
    public boolean contains(BlockState state) { return blockStates.contains(state); }
}
