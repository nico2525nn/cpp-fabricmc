package net.minecraft.world.entity;

/** Minimal section identity returned by SectionedEntityCache.addSection. */
public class EntityTrackingSection<T> {
    private final long packedPosition;
    public EntityTrackingSection() { this(0L); }
    public EntityTrackingSection(long packedPosition) { this.packedPosition = packedPosition; }
    public long packedPosition() { return packedPosition; }
}
