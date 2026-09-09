package net.minecraft.screen;

import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.entity.player.PlayerInventory;

/** Factory contract shared by server-side screen handlers. */
public interface ScreenHandlerFactory {
    ScreenHandler createMenu(int syncId, PlayerInventory inventory, PlayerEntity player);
}
