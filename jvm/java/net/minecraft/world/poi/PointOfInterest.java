package net.minecraft.world.poi;

import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.math.BlockPos;

/** A single occupied point-of-interest location. */
public class PointOfInterest {
    private final BlockPos pos;
    private final RegistryEntry<PointOfInterestType> type;
    private int freeTickets;
    private final Runnable updateListener;

    public PointOfInterest(BlockPos pos, RegistryEntry<PointOfInterestType> type,
                           int freeTickets, Runnable updateListener) {
        this.pos = pos == null ? BlockPos.ORIGIN : pos;
        this.type = type;
        this.freeTickets = Math.max(0, freeTickets);
        this.updateListener = updateListener == null ? () -> { } : updateListener;
    }
    public PointOfInterest(BlockPos pos, RegistryEntry<PointOfInterestType> type, Runnable updateListener) {
        this(pos, type, type == null || type.value() == null ? 0 : type.value().ticketCount(), updateListener);
    }
    public BlockPos getPos() { return pos; }
    public RegistryEntry<PointOfInterestType> getType() { return type; }
    public int getFreeTickets() { return freeTickets; }
    public boolean isOccupied() { return freeTickets <= 0; }
    public boolean hasSpace() { return freeTickets > 0; }
    public boolean reserveTicket() {
        if (!hasSpace()) return false;
        freeTickets--;
        updateListener.run();
        return true;
    }
    public boolean acquireTicket() { return reserveTicket(); }
    /** Preserve the capitalized inferred Invoker spelling used by older mods. */
    public boolean AcquireTicket() { return acquireTicket(); }
    public boolean releaseTicket() {
        int max = type == null || type.value() == null ? Integer.MAX_VALUE : type.value().ticketCount();
        if (freeTickets >= max) return false;
        freeTickets++;
        updateListener.run();
        return true;
    }
}
