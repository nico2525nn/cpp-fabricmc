package net.minecraft.screen;

import net.minecraft.entity.player.PlayerInventory;

/** Server-side ABI base for anvil/smithing/other forging handlers. */
public abstract class ForgingScreenHandler extends ScreenHandler {
    protected ForgingScreenHandler(ScreenHandlerType<?> type, int syncId, PlayerInventory inventory) {
        super(type, syncId);
    }
}
