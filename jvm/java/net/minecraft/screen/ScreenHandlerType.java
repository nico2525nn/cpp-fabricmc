package net.minecraft.screen;

import net.minecraft.resource.featuretoggle.FeatureSet;

/** Screen-handler type descriptor. */
public class ScreenHandlerType<T extends ScreenHandler> {
    public interface Factory<T extends ScreenHandler> { T create(int syncId, net.minecraft.entity.player.PlayerInventory inventory); }
    private final Factory<T> factory;
    private final FeatureSet requiredFeatures;
    public ScreenHandlerType() { this(null, FeatureSet.EMPTY); }
    public ScreenHandlerType(Factory<T> factory) { this(factory, FeatureSet.EMPTY); }
    public ScreenHandlerType(Factory<T> factory, FeatureSet requiredFeatures) {
        this.factory = factory;
        this.requiredFeatures = requiredFeatures == null ? FeatureSet.EMPTY : requiredFeatures;
    }
    public FeatureSet getRequiredFeatures() { return requiredFeatures; }
    public T create(int syncId, net.minecraft.entity.player.PlayerInventory inventory) {
        return factory == null ? null : factory.create(syncId, inventory);
    }
}
