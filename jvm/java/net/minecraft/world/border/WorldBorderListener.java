package net.minecraft.world.border;

/**
 * Listener callbacks exposed by the 1.21.4 world-border API.
 *
 * <p>The production server owns the wire broadcast.  The shadow keeps the
 * vanilla callback contract so Fabric mixins can observe the same state
 * changes without depending on that native transport implementation.</p>
 */
public interface WorldBorderListener {
    default void onSizeChange(WorldBorder border, double size) {}
    default void onInterpolateSize(WorldBorder border, double fromSize,
                                   double toSize, long time) {}
    default void onCenterChanged(WorldBorder border, double x, double z) {}
    default void onWarningTimeChanged(WorldBorder border, int warningTime) {}
    default void onWarningBlocksChanged(WorldBorder border, int warningBlocks) {}
    default void onDamagePerBlockChanged(WorldBorder border, double damagePerBlock) {}
    default void onSafeZoneChanged(WorldBorder border, double safeZone) {}

    /** Minimal API-compatible holder used by vanilla-style network bridges. */
    class WorldBorderSyncer implements WorldBorderListener {
        protected final WorldBorder border;

        public WorldBorderSyncer(WorldBorder border) {
            this.border = border;
        }

        public WorldBorder getBorder() { return border; }
    }
}
