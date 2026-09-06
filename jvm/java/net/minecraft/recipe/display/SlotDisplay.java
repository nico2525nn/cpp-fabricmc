package net.minecraft.recipe.display;

import java.util.List;
import java.util.stream.Stream;
import net.minecraft.item.ItemStack;
import net.minecraft.resource.featuretoggle.FeatureSet;
import net.minecraft.util.context.ContextParameterMap;

/** A client-visible recipe-book slot description. */
public interface SlotDisplay {
    default ItemStack getFirst(ContextParameterMap context) {
        List<ItemStack> stacks = getStacks(context);
        return stacks.isEmpty() ? null : stacks.get(0);
    }

    default Stream<ItemStack> appendStacks(ContextParameterMap parameters,
                                           DisplayedItemFactory factory) {
        return getStacks(parameters).stream();
    }

    default boolean isEnabled(FeatureSet features) { return true; }
    default Serializer serializer() { return null; }
    default List<ItemStack> getStacks(ContextParameterMap parameters) { return List.of(); }

    interface Serializer {
        default Object codec() { return null; }
        default Object streamCodec() { return null; }
    }
}
