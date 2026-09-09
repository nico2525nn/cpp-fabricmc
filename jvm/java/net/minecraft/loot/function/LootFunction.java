package net.minecraft.loot.function;

import java.util.function.Consumer;
import net.minecraft.item.ItemStack;
import net.minecraft.loot.context.LootContext;

/** Server loot function contract. */
public interface LootFunction {
    default LootFunctionType getType() { return null; }
    default ItemStack apply(ItemStack stack, LootContext context) { return stack; }
    interface Builder { LootFunction build(); }
    default Consumer<ItemStack> apply(Consumer<ItemStack> consumer, LootContext context) {
        return stack -> consumer.accept(apply(stack, context));
    }
}
