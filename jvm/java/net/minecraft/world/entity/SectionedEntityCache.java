package net.minecraft.world.entity;

/** Lightweight cache identity exposed to server entity-manager accessors. */
public final class SectionedEntityCache {
    public SectionedEntityCache() { }
    public EntityTrackingSection<net.minecraft.world.entity.EntityLike> addSection(long packedPosition) {
        return new EntityTrackingSection<>(packedPosition);
    }
    public void forEachInBox(net.minecraft.util.math.Box box,
                             net.minecraft.util.function.LazyIterationConsumer<?> consumer) { }
}
