package net.fabricmc.fabric.api.transfer.v1.fluid;

import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.Hand;

/** Player-facing fluid interaction helper. */
public final class FluidStorageUtil {
    private FluidStorageUtil() { }
    public static boolean interactWithFluidStorage(Storage<FluidVariant> storage, PlayerEntity player, Hand hand) {
        if (storage == null || player == null || hand == null) return false;
        return false;
    }
}
