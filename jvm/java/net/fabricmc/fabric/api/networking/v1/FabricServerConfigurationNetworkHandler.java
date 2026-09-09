package net.fabricmc.fabric.api.networking.v1;

import net.minecraft.server.network.ServerPlayerConfigurationTask;

/** Access to Fabric configuration tasks injected into the vanilla handler. */
public interface FabricServerConfigurationNetworkHandler {
    default void addTask(ServerPlayerConfigurationTask task) {
        if (this instanceof net.minecraft.server.network.ServerConfigurationNetworkHandler handler)
            handler.addTask(task);
    }

    default void completeTask(ServerPlayerConfigurationTask.Key key) {
        if (this instanceof net.minecraft.server.network.ServerConfigurationNetworkHandler handler)
            handler.onTaskFinished(key);
    }
}
