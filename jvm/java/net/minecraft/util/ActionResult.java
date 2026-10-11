package net.minecraft.util;

import net.minecraft.item.ItemStack;

/**
 * The 1.21.4 result of a hand interaction. Nested result values intentionally
 * mirror the record/interface ABI used by Fabric-compiled mods.
 */
public interface ActionResult {
    PassToDefaultBlockAction PASS_TO_DEFAULT_BLOCK_ACTION =
        new PassToDefaultBlockAction();
    Pass PASS = new Pass();
    Success SUCCESS_SERVER =
        new Success(SwingSource.SERVER, ItemContext.KEEP_HAND_STACK);
    Fail FAIL = new Fail();
    Success CONSUME =
        new Success(SwingSource.NONE, ItemContext.KEEP_HAND_STACK);
    Success SUCCESS =
        new Success(SwingSource.CLIENT, ItemContext.KEEP_HAND_STACK);

    default boolean isAccepted() {
        return this instanceof Success;
    }

    record Success(SwingSource swingSource, ItemContext itemContext)
        implements ActionResult {
        public Success noIncrementStat() {
            return new Success(
                swingSource, ItemContext.KEEP_HAND_STACK_NO_INCREMENT_STAT);
        }

        public ItemStack getNewHandStack() {
            return itemContext.newHandStack();
        }

        public boolean shouldIncrementStat() {
            return itemContext.incrementStat();
        }

        public Success withNewHandStack(ItemStack stack) {
            return new Success(
                swingSource,
                new ItemContext(itemContext.incrementStat(), stack));
        }
    }

    record Pass() implements ActionResult {}

    record Fail() implements ActionResult {}

    record PassToDefaultBlockAction() implements ActionResult {}

    record ItemContext(boolean incrementStat, ItemStack newHandStack) {
        public static final ItemContext KEEP_HAND_STACK =
            new ItemContext(true, null);
        public static final ItemContext KEEP_HAND_STACK_NO_INCREMENT_STAT =
            new ItemContext(false, null);
    }

    enum SwingSource {
        NONE,
        CLIENT,
        SERVER
    }
}
