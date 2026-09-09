package net.fabricmc.fabric.api.screenhandler.v1;

/** Access-widened screen lifecycle policy used by server screen mixins. */
public interface FabricScreenHandlerFactory {
    default boolean shouldCloseCurrentScreen() { return true; }
}
