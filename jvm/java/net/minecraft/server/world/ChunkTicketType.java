package net.minecraft.server.world;

import java.util.Comparator;

/**
 * 1.21.4 chunk-ticket type ABI.  Ticket scheduling remains native; this value
 * carries the name, ordering policy, and optional expiry used by mods.
 */
public class ChunkTicketType<T> {
    public static final ChunkTicketType<Object> START = create("start", null);
    public static final ChunkTicketType<Object> DRAGON = create("dragon", null);
    public static final ChunkTicketType<Object> PLAYER = create("player", null);
    public static final ChunkTicketType<Object> FORCED = create("forced", null);
    public static final ChunkTicketType<Object> UNKNOWN = create("unknown", null);
    public static final ChunkTicketType<Object> PORTAL = create("portal", null);
    public static final ChunkTicketType<Object> ENDER_PEARL = create("ender_pearl", null);

    private final String name;
    private final Comparator<T> argumentComparator;
    private final long expiryTicks;

    public ChunkTicketType(String name, Comparator<T> argumentComparator, long expiryTicks) {
        this.name = name == null ? "" : name;
        this.argumentComparator = argumentComparator;
        this.expiryTicks = expiryTicks;
    }

    public static <T> ChunkTicketType<T> create(String name, Comparator<T> argumentComparator) {
        return new ChunkTicketType<>(name, argumentComparator, 0L);
    }

    public static <T> ChunkTicketType<T> create(
        String name, Comparator<T> argumentComparator, int expiryTicks) {
        return new ChunkTicketType<>(name, argumentComparator, expiryTicks);
    }

    public String getName() { return name; }
    public Comparator<T> getArgumentComparator() { return argumentComparator; }
    public long getExpiryTicks() { return expiryTicks; }
}
