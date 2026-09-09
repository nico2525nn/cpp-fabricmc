package net.fabricmc.fabric.api.screenhandler.v1;

import net.minecraft.screen.NamedScreenHandlerFactory;
import net.minecraft.server.network.ServerPlayerEntity;

/** Screen factory that supplies the extra payload sent when opening a screen. */
public interface ExtendedScreenHandlerFactory<D> extends NamedScreenHandlerFactory {
    D getScreenOpeningData(ServerPlayerEntity player);
}
