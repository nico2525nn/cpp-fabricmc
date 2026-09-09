package net.fabricmc.fabric.api.event.lifecycle.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.resource.LifecycledResourceManager;

public final class ServerLifecycleEvents {
    private ServerLifecycleEvents() {}
    @FunctionalInterface public interface ServerStarting { void onServerStarting(MinecraftServer server); }
    @FunctionalInterface public interface ServerStarted { void onServerStarted(MinecraftServer server); }
    @FunctionalInterface public interface ServerStopping { void onServerStopping(MinecraftServer server); }
    @FunctionalInterface public interface ServerStopped { void onServerStopped(MinecraftServer server); }
    @FunctionalInterface public interface SyncDataPackContents {
        void onSyncDataPackContents(ServerPlayerEntity player, boolean joined);
    }
    @FunctionalInterface public interface StartDataPackReload {
        void startDataPackReload(MinecraftServer server, LifecycledResourceManager resourceManager);
    }
    @FunctionalInterface public interface EndDataPackReload {
        void endDataPackReload(MinecraftServer server, LifecycledResourceManager resourceManager,
                               boolean success);
    }
    @FunctionalInterface public interface BeforeSave {
        void onBeforeSave(MinecraftServer server, boolean flush, boolean skipErrors);
    }
    @FunctionalInterface public interface AfterSave {
        void onAfterSave(MinecraftServer server, boolean flush, boolean skipErrors);
    }

    public static final Event<ServerStarting> SERVER_STARTING = new Event<>(CppModRuntime::registerServerStarting, ServerStarting.class, callbacks -> server -> { for (ServerStarting callback : callbacks) callback.onServerStarting(server); });
    public static final Event<ServerStarted> SERVER_STARTED = new Event<>(CppModRuntime::registerServerStarted, ServerStarted.class, callbacks -> server -> { for (ServerStarted callback : callbacks) callback.onServerStarted(server); });
    public static final Event<ServerStopping> SERVER_STOPPING = new Event<>(CppModRuntime::registerServerStopping, ServerStopping.class, callbacks -> server -> { for (ServerStopping callback : callbacks) callback.onServerStopping(server); });
    public static final Event<ServerStopped> SERVER_STOPPED = new Event<>(CppModRuntime::registerServerStopped, ServerStopped.class, callbacks -> server -> { for (ServerStopped callback : callbacks) callback.onServerStopped(server); });
    public static final Event<SyncDataPackContents> SYNC_DATA_PACK_CONTENTS = new Event<>(CppModRuntime::registerSyncDataPackContents, SyncDataPackContents.class, callbacks -> (player, joined) -> { for (SyncDataPackContents callback : callbacks) callback.onSyncDataPackContents(player, joined); });
    public static final Event<StartDataPackReload> START_DATA_PACK_RELOAD = new Event<>(CppModRuntime::registerStartDataPackReload, StartDataPackReload.class, callbacks -> (server, manager) -> { for (StartDataPackReload callback : callbacks) callback.startDataPackReload(server, manager); });
    public static final Event<EndDataPackReload> END_DATA_PACK_RELOAD = new Event<>(CppModRuntime::registerEndDataPackReload, EndDataPackReload.class, callbacks -> (server, manager, success) -> { for (EndDataPackReload callback : callbacks) callback.endDataPackReload(server, manager, success); });
    public static final Event<BeforeSave> BEFORE_SAVE = new Event<>(CppModRuntime::registerBeforeSave, BeforeSave.class, callbacks -> (server, flush, skipErrors) -> { for (BeforeSave callback : callbacks) callback.onBeforeSave(server, flush, skipErrors); });
    public static final Event<AfterSave> AFTER_SAVE = new Event<>(CppModRuntime::registerAfterSave, AfterSave.class, callbacks -> (server, flush, skipErrors) -> { for (AfterSave callback : callbacks) callback.onAfterSave(server, flush, skipErrors); });
    public static void clear() {
        SERVER_STARTING.clear(); SERVER_STARTED.clear();
        SERVER_STOPPING.clear(); SERVER_STOPPED.clear();
        SYNC_DATA_PACK_CONTENTS.clear(); START_DATA_PACK_RELOAD.clear();
        END_DATA_PACK_RELOAD.clear(); BEFORE_SAVE.clear(); AFTER_SAVE.clear();
    }
}
