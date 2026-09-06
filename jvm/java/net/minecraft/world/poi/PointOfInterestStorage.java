package net.minecraft.world.poi;

import java.util.Optional;
import java.util.function.Predicate;
import java.util.stream.Stream;
import net.minecraft.util.math.BlockPos;

/** Lightweight point-of-interest storage surface. */
public class PointOfInterestStorage {
    public enum OccupationStatus { ANY, HAS_SPACE, IS_OCCUPIED }
    public Optional<PointOfInterestType> getType(BlockPos pos) { return Optional.empty(); }
    public Optional<BlockPos> getNearestPosition(Predicate<PointOfInterestType> typePredicate,
                                                  BlockPos pos, int radius,
                                                  OccupationStatus occupationStatus) {
        return Optional.empty();
    }
    public Stream<PointOfInterest> getInCircle(Predicate<PointOfInterestType> typePredicate,
                                                BlockPos pos, int radius,
                                                OccupationStatus occupationStatus) {
        return Stream.empty();
    }
    public boolean releaseTicket(BlockPos pos) { return false; }
}
