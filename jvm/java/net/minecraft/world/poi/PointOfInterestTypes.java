package net.minecraft.world.poi;

import java.util.Optional;
import java.util.Set;
import java.util.stream.Stream;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;

/** Static POI registration helpers. */
public final class PointOfInterestTypes {
    private PointOfInterestTypes() { }
    public static final Set<BlockState> BED_HEADS = Set.of();
    public static final Set<BlockState> CAULDRONS = Set.of();
    public static boolean isPointOfInterest(BlockState state) { return false; }
    public static Optional<PointOfInterestType> getTypeForState(BlockState state) { return Optional.empty(); }
    public static Set<BlockState> getStatesOfBlock(Block block) { return Set.of(); }
    public static Stream<BlockState> method_43997(Block block) { return getStatesOfBlock(block).stream(); }
    public static PointOfInterestType register(Registry<PointOfInterestType> registry,
                                                RegistryKey<PointOfInterestType> key,
                                                Set<BlockState> states, int ticketCount, int searchDistance) {
        PointOfInterestType type = new PointOfInterestType(states, ticketCount, searchDistance);
        if (registry != null && key != null) Registry.register(registry, key.getValue(), type);
        return type;
    }
}
