package net.minecraft.recipe.display;

import net.minecraft.resource.featuretoggle.FeatureSet;

/** A recipe display sent to the client recipe book. */
public interface RecipeDisplay {
    Serializer serializer();
    boolean isEnabled(FeatureSet features);
    SlotDisplay craftingStation();
    SlotDisplay result();

    interface Serializer {
        default Object codec() { return null; }
        default Object streamCodec() { return null; }
    }
}
