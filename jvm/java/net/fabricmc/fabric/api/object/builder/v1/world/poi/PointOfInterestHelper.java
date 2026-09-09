package net.fabricmc.fabric.api.object.builder.v1.world.poi;

import java.util.LinkedHashSet;
import java.util.Set;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.registry.Registries;
import net.minecraft.util.Identifier;
import net.minecraft.world.poi.PointOfInterestType;

public final class PointOfInterestHelper {
    private PointOfInterestHelper() { }
    public static PointOfInterestType register(Identifier id, int ticketCount, int searchDistance, Block... blocks) {
        Set<BlockState> states = new LinkedHashSet<>();
        if (blocks != null) for (Block block : blocks) if (block != null) states.add(block.getDefaultState());
        return register(id, ticketCount, searchDistance, states);
    }
    public static PointOfInterestType register(Identifier id, int ticketCount, int searchDistance, Iterable<BlockState> states) {
        Set<BlockState> copy = new LinkedHashSet<>(); if (states != null) for (BlockState state : states) if (state != null) copy.add(state);
        PointOfInterestType type = new PointOfInterestType(copy, ticketCount, searchDistance);
        if (id != null) net.minecraft.registry.Registry.register((net.minecraft.registry.Registry) Registries.POINT_OF_INTEREST_TYPE, id, type);
        return type;
    }
}
