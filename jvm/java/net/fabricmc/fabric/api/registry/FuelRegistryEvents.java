package net.fabricmc.fabric.api.registry;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.item.FuelRegistry;
import net.minecraft.registry.RegistryWrapper;
import net.minecraft.resource.featuretoggle.FeatureSet;

/** Fuel registry build and exclusion hooks. */
public interface FuelRegistryEvents {
    Event<BuildCallback> BUILD = EventFactory.createArrayBacked(BuildCallback.class,
        callbacks -> (builder, context) -> {
            for (BuildCallback callback : callbacks) callback.build(builder, context);
        });
    Event<ExclusionsCallback> EXCLUSIONS = EventFactory.createArrayBacked(ExclusionsCallback.class,
        callbacks -> (builder, context) -> {
            for (ExclusionsCallback callback : callbacks) callback.buildExclusions(builder, context);
        });

    @FunctionalInterface interface BuildCallback {
        void build(FuelRegistry.Builder builder, Context context);
    }
    interface Context {
        int baseSmeltTime();
        RegistryWrapper.WrapperLookup registries();
        FeatureSet enabledFeatures();
    }

    @FunctionalInterface interface ExclusionsCallback {
        void buildExclusions(FuelRegistry.Builder builder, Context context);
    }
}
