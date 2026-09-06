package net.minecraft.util;

import net.minecraft.item.ItemStack;

/**
 * 1.21.4 interaction result hierarchy. The named result constants are
 * concrete nested values in this version, so compiled Fabric code can return
 * {@code ActionResult.Success} wherever the common base is expected.
 */
public class ActionResult {
    public static final PassToDefaultBlockAction PASS_TO_DEFAULT_BLOCK_ACTION = new PassToDefaultBlockAction();
    public static final Pass PASS = new Pass();
    public static final Success SUCCESS_SERVER = new Success(ItemContext.KEEP_HAND_STACK, SwingSource.SERVER);
    public static final Fail FAIL = new Fail();
    public static final Success CONSUME = new Success(ItemContext.KEEP_HAND_STACK, SwingSource.NONE);
    public static final Success SUCCESS = new Success(ItemContext.KEEP_HAND_STACK, SwingSource.SERVER);
    public static final Success SUCCESS_CLIENT = new Success(ItemContext.KEEP_HAND_STACK, SwingSource.CLIENT);

    private final String name;
    private final boolean accepted;
    protected ActionResult(String name, boolean accepted) {
        this.name = name;
        this.accepted = accepted;
    }

    public boolean isAccepted() { return accepted; }
    public boolean shouldSwingHand() { return accepted && this != CONSUME; }
    public boolean isAccepted(boolean client) { return accepted; }
    @Override public String toString() { return name; }

    public static class Success extends ActionResult {
        private final ItemContext itemContext;
        private final SwingSource swingSource;
        public Success() { this(ItemContext.KEEP_HAND_STACK, SwingSource.SERVER); }
        public Success(ItemContext itemContext, SwingSource swingSource) {
            super("success", true);
            this.itemContext = itemContext == null ? ItemContext.KEEP_HAND_STACK : itemContext;
            this.swingSource = swingSource == null ? SwingSource.NONE : swingSource;
        }
        public Success noIncrementStat() {
            return new Success(ItemContext.KEEP_HAND_STACK_NO_INCREMENT_STAT, swingSource);
        }
        public ItemStack getNewHandStack() { return itemContext.newHandStack(); }
        public boolean shouldIncrementStat() { return itemContext.incrementStat(); }
        public Success withNewHandStack(ItemStack stack) {
            return new Success(new ItemContext(stack, itemContext.incrementStat()), swingSource);
        }
        public SwingSource swingSource() { return swingSource; }
        public ItemContext itemContext() { return itemContext; }
    }

    public static final class Pass extends ActionResult {
        public Pass() { super("pass", false); }
    }

    public static final class Fail extends ActionResult {
        public Fail() { super("fail", false); }
    }

    public static final class PassToDefaultBlockAction extends ActionResult {
        public PassToDefaultBlockAction() { super("pass_to_default_block_action", false); }
    }

    public static class ItemContext {
        public static final ItemContext KEEP_HAND_STACK_NO_INCREMENT_STAT = new ItemContext(ItemStack.EMPTY, false);
        public static final ItemContext KEEP_HAND_STACK = new ItemContext(ItemStack.EMPTY, true);
        private final ItemStack newHandStack;
        private final boolean incrementStat;
        public ItemContext(ItemStack newHandStack, boolean incrementStat) {
            this.newHandStack = newHandStack == null ? ItemStack.EMPTY : newHandStack;
            this.incrementStat = incrementStat;
        }
        public ItemStack newHandStack() { return newHandStack; }
        public boolean incrementStat() { return incrementStat; }
    }

    public enum SwingSource { NONE, CLIENT, SERVER }
}
