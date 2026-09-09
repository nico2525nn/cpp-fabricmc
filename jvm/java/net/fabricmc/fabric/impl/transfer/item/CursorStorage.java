package net.fabricmc.fabric.impl.transfer.item;

import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.item.base.SingleStackStorage;
import net.minecraft.item.ItemStack;
import net.minecraft.screen.ScreenHandler;

/** Transactional adapter for a screen handler's cursor stack. */
public final class CursorStorage extends SingleStackStorage {
    private final ScreenHandler handler;
    public CursorStorage(ScreenHandler handler) { this.handler = java.util.Objects.requireNonNull(handler, "handler"); }
    @Override protected ItemStack getStack() { return handler.getCursorStack(); }
    @Override protected void setStack(ItemStack stack) { handler.setCursorStack(stack); }
    @Override protected int getCapacity(ItemVariant variant) { return Math.min(64, variant.getItem().getMaxCount()); }
}
