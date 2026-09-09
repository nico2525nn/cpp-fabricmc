package net.minecraft.block.entity;

import net.minecraft.block.BlockState;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.inventory.Inventory;
import net.minecraft.item.ItemStack;
import net.minecraft.screen.ScreenHandler;
import net.minecraft.text.Text;
import net.minecraft.util.collection.DefaultedList;
import net.minecraft.util.math.BlockPos;

/**
 * 1.21.4 container base.  Inventory contents are deliberately delegated to
 * the existing native-safe list; subclasses can still expose the vanilla
 * method and field descriptors used by Fabric block-entity mixins.
 */
public abstract class LockableContainerBlockEntity extends LootableContainerBlockEntity
        implements Inventory {
    protected Text customName;
    protected Object lock;
    protected DefaultedList<ItemStack> heldStacks = DefaultedList.ofSize(27, ItemStack.EMPTY);

    protected LockableContainerBlockEntity(BlockEntityType<?> type, BlockPos pos, BlockState state) {
        super(type, pos, state);
    }
    protected LockableContainerBlockEntity() { super(); }

    @Override public int size() { return heldStacks.size(); }
    @Override public ItemStack getStack(int slot) { return heldStacks.get(slot); }
    @Override public void setStack(int slot, ItemStack stack) {
        heldStacks.set(slot, stack == null ? ItemStack.EMPTY : stack);
        markDirty();
    }
    public DefaultedList<ItemStack> getHeldStacks() { return heldStacks; }
    public void setHeldStacks(DefaultedList<ItemStack> inventory) {
        heldStacks = inventory == null ? DefaultedList.ofSize(27, ItemStack.EMPTY) : inventory;
    }
    public Text getContainerName() { return customName == null ? Text.empty() : customName; }
    public Text getDisplayName() { return getContainerName(); }
    public void setCustomName(Text name) { customName = name; }
    public boolean checkUnlocked(PlayerEntity player) { return true; }
    public boolean checkUnlocked(PlayerEntity player, Object lock, Text containerName) { return true; }
    public abstract ScreenHandler createScreenHandler(int syncId, net.minecraft.entity.player.PlayerInventory inventory);
}
