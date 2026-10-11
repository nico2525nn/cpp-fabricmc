package net.minecraft.util;

import cppfm.bridge.NativeBridge;

/**
 * Typed facade for the optional JNI boundary.
 *
 * <p>Javac-only API fixtures may execute without a registered native bridge,
 * so that one bootstrap state has neutral fallback values. Once the runtime
 * registers the bridge, linkage failures are defects and must remain visible.
 * Native runtime exceptions are never converted into ordinary game values.</p>
 */
public final class NativeAccess {
    private NativeAccess() { }

    @FunctionalInterface
    private interface NativeCall<T> { T invoke(); }

    @FunctionalInterface
    private interface NativeAction { void invoke(); }

    private static boolean usable(long handle) { return handle != 0L; }

    private static <T> T call(NativeCall<T> operation, T unavailableValue) {
        try {
            return operation.invoke();
        } catch (UnsatisfiedLinkError missingNativeBinding) {
            if (NativeBridge.isNativeBridgeInstalled())
                throw missingNativeBinding;
            return unavailableValue;
        }
    }

    private static void call(NativeAction operation) {
        try {
            operation.invoke();
        } catch (UnsatisfiedLinkError missingNativeBinding) {
            if (NativeBridge.isNativeBridgeInstalled())
                throw missingNativeBinding;
        }
    }

    public static long serverHandle() {
        return call(NativeBridge::nativeServerHandle, 0L);
    }

    public static long currentTick() {
        return call(NativeBridge::nativeCurrentTick, 0L);
    }

    public static boolean playerHandleLive(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativePlayerHandleLive(handle), false);
    }

    public static String playerName(long handle) {
        return !usable(handle) ? "" : valueOrEmpty(call(
            () -> NativeBridge.nativePlayerName(handle), ""));
    }

    public static String playerUuid(long handle) {
        return !usable(handle) ? "" : valueOrEmpty(call(
            () -> NativeBridge.nativePlayerUuid(handle), ""));
    }

    public static int playerEntityId(long handle) {
        return usable(handle) ? call(
            () -> NativeBridge.nativePlayerEntityId(handle), 0) : 0;
    }

    public static int playerGameMode(long handle) {
        return usable(handle) ? call(
            () -> NativeBridge.nativePlayerGameMode(handle), 0) : 0;
    }

    public static boolean playerSneaking(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativePlayerSneaking(handle), false);
    }

    public static int onlinePlayerCount() {
        return Math.max(0, call(NativeBridge::nativeOnlinePlayerCount, 0));
    }

    public static long onlinePlayerHandle(int index) {
        return index < 0 ? 0L : call(
            () -> NativeBridge.nativeOnlinePlayerHandle(index), 0L);
    }

    public static int heldSlot(long handle) {
        return usable(handle) ? clamp(call(
            () -> NativeBridge.nativePlayerHeldSlot(handle), 0), 0, 8) : 0;
    }

    public static int inventoryItemId(long handle, int slot) {
        return !usable(handle) || slot < 0 || slot >= 41 ? 0
            : Math.max(0, call(
                () -> NativeBridge.nativePlayerInventoryItemId(handle, slot), 0));
    }

    public static int inventoryItemCount(long handle, int slot) {
        return !usable(handle) || slot < 0 || slot >= 41 ? 0
            : Math.max(0, call(
                () -> NativeBridge.nativePlayerInventoryItemCount(handle, slot), 0));
    }

    public static String inventoryItemName(long handle, int slot) {
        if (!usable(handle) || slot < 0 || slot >= 41) return "minecraft:air";
        String value = call(
            () -> NativeBridge.nativePlayerInventoryItemName(handle, slot),
            "minecraft:air");
        return value == null || value.isEmpty() ? "minecraft:air" : value;
    }

    public static boolean setInventoryItemCount(long handle, int slot,
                                                int count) {
        return usable(handle) && slot >= 0 && slot < 41 && call(
            () -> NativeBridge.nativePlayerSetInventoryItemCount(
                handle, slot, Math.max(0, count)), false);
    }

    public static boolean setInventoryItem(long handle, int slot, int itemId,
                                           int count) {
        return usable(handle) && slot >= 0 && slot < 41 && call(
            () -> NativeBridge.nativePlayerSetInventoryItem(
                handle, slot, Math.max(0, itemId), Math.max(0, count)), false);
    }

    public static double coordinate(long handle, int axis) {
        return usable(handle) && axis >= 0 && axis <= 2 ? call(
            () -> NativeBridge.nativePlayerCoordinate(handle, axis), 0.0) : 0.0;
    }

    public static boolean setPosition(long handle, double x, double y,
                                      double z) {
        return usable(handle) && call(
            () -> NativeBridge.nativePlayerSetPosition(handle, x, y, z), false);
    }

    /** Commits a server-player world/position change through its owning session. */
    public static boolean teleportPlayerToWorld(long playerHandle,
                                                long worldHandle,
                                                double x, double y, double z,
                                                float yaw, float pitch) {
        if (!usable(playerHandle) || !usable(worldHandle) ||
            !Double.isFinite(x) || !Double.isFinite(y) ||
            !Double.isFinite(z) || !Float.isFinite(yaw) ||
            !Float.isFinite(pitch)) return false;
        return call(() -> NativeBridge.nativePlayerTeleportToWorld(
            playerHandle, worldHandle, x, y, z, yaw, pitch), false);
    }

    /** Reads authoritative entity rotation in degrees (axis 0=yaw, 1=pitch). */
    public static float entityRotation(long handle, int axis) {
        return usable(handle) && axis >= 0 && axis <= 1 ? call(
            () -> NativeBridge.nativeEntityRotation(handle, axis), 0.0f) : 0.0f;
    }

    /** Updates authoritative entity rotation in degrees. */
    public static boolean setEntityRotation(long handle, int axis,
                                            float degrees) {
        return usable(handle) && axis >= 0 && axis <= 1 &&
            Float.isFinite(degrees) && call(
                () -> NativeBridge.nativeEntitySetRotation(
                    handle, axis, degrees), false);
    }

    public static boolean sendMessage(long handle, String message,
                                      boolean overlay) {
        return usable(handle) && message != null && call(
            () -> NativeBridge.nativePlayerSendMessage(
                handle, message, overlay), false);
    }

    public static long playerWorld(long handle) {
        return usable(handle) ? call(
            () -> NativeBridge.nativePlayerWorld(handle), 0L) : 0L;
    }

    public static long serverWorld(int dimension) {
        return call(() -> NativeBridge.nativeServerWorld(dimension), 0L);
    }

    public static String worldName(long handle) {
        if (!usable(handle)) return "minecraft:overworld";
        String value = call(
            () -> NativeBridge.nativeWorldName(handle), "minecraft:overworld");
        return value == null || value.isEmpty()
            ? "minecraft:overworld" : value;
    }

    public static int worldBlock(long handle, int x, int y, int z) {
        return usable(handle) ? Math.max(0, call(
            () -> NativeBridge.nativeWorldBlock(handle, x, y, z), 0)) : 0;
    }

    public static boolean worldHasChunk(long handle, int chunkX, int chunkZ) {
        return usable(handle) && call(
            () -> NativeBridge.nativeWorldHasChunk(handle, chunkX, chunkZ), false);
    }

    public static boolean setWorldBlock(long handle, int x, int y, int z,
                                        int state) {
        return setWorldBlock(handle, x, y, z, state, 3, 512);
    }

    public static boolean setWorldBlock(long handle, int x, int y, int z,
                                        int state, int flags,
                                        int maxUpdateDepth) {
        return usable(handle) && state >= 0 && call(
            () -> NativeBridge.nativeWorldSetBlock(
                handle, x, y, z, state, flags, maxUpdateDepth), false);
    }

    public static String entityType(long handle) {
        if (!usable(handle)) return "minecraft:unknown";
        String value = call(
            () -> NativeBridge.nativeEntityType(handle), "minecraft:unknown");
        return value == null || value.isEmpty() ? "minecraft:unknown" : value;
    }

    public static boolean entityHandleCreatable(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativeEntityHandleCreatable(handle), false);
    }

    public static boolean beginEntityRetirement(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativeEntityBeginRetirement(handle), false);
    }

    public static int entityTypeId(long handle) {
        return usable(handle) ? call(
            () -> NativeBridge.nativeEntityTypeId(handle), -1) : -1;
    }

    public static boolean entityIsLiving(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativeEntityIsLiving(handle), false);
    }

    public static boolean entityIsHostile(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativeEntityIsHostile(handle), false);
    }

    public static boolean entityIsAnimal(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativeEntityIsAnimal(handle), false);
    }

    public static boolean entityIsFish(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativeEntityIsFish(handle), false);
    }

    public static float entityHealth(long handle) {
        return usable(handle) ? Math.max(0.0f, call(
            () -> NativeBridge.nativeEntityHealth(handle), 0.0f)) : 0.0f;
    }

    public static float entityMaxHealth(long handle) {
        return usable(handle) ? Math.max(0.0f, call(
            () -> NativeBridge.nativeEntityMaxHealth(handle), 0.0f)) : 0.0f;
    }

    public static boolean setEntityHealth(long handle, float health) {
        return usable(handle) && Float.isFinite(health) && health >= 0.0f && call(
            () -> NativeBridge.nativeEntitySetHealth(handle, health), false);
    }

    public static boolean damageEntity(long handle, float amount, String cause) {
        if (!usable(handle) || !Float.isFinite(amount) || amount <= 0.0f)
            return false;
        String safeCause = cause == null || cause.isBlank() ? "generic" : cause;
        return call(() -> NativeBridge.nativeEntityDamage(
            handle, amount, safeCause), false);
    }

    /**
     * Requests native removal for KILLED and DISCARDED. Chunk persistence and
     * cross-dimension transfer removal reasons are rejected until native
     * entity storage can preserve or transfer their state.
     */
    public static boolean removeEntity(long handle, int reason) {
        return usable(handle) && reason >= 0 && reason <= 1 && call(
            () -> NativeBridge.nativeEntityRemove(handle, reason), false);
    }

    public static boolean entityDead(long handle) {
        return usable(handle) && call(
            () -> NativeBridge.nativeEntityDead(handle), false);
    }

    public static long entityWorld(long handle) {
        return usable(handle) ? call(
            () -> NativeBridge.nativeEntityWorld(handle), 0L) : 0L;
    }

    public static long entityCount() {
        return Math.max(0L, call(
            () -> (long) NativeBridge.nativeEntityCount(), 0L));
    }

    public static long entityHandle(int index) {
        return index >= 0 ? call(
            () -> NativeBridge.nativeEntityHandle(index), 0L) : 0L;
    }

    public static long worldTime(long handle) {
        return usable(handle) ? call(
            () -> NativeBridge.nativeWorldTime(handle), 0L) : 0L;
    }

    public static int registryItemId(String name) {
        return name == null || name.isEmpty() ? -1 : call(
            () -> NativeBridge.nativeRegistryItemId(name), -1);
    }

    public static String registryItemName(int id) {
        return id < 0 ? "" : valueOrEmpty(call(
            () -> NativeBridge.nativeRegistryItemName(id), ""));
    }

    public static int registryBlockState(String name) {
        return name == null || name.isEmpty() ? -1 : call(
            () -> NativeBridge.nativeRegistryBlockState(name), -1);
    }

    public static String registryBlockName(int state) {
        return state < 0 ? "" : valueOrEmpty(call(
            () -> NativeBridge.nativeRegistryBlockName(state), ""));
    }

    public static int registryEntryCount(String registry) {
        return registry == null || registry.isEmpty() ? 0 : Math.max(0, call(
            () -> NativeBridge.nativeRegistryEntryCount(registry), 0));
    }

    public static String registryEntryName(String registry, int id) {
        return registry == null || registry.isEmpty() || id < 0 ? ""
            : valueOrEmpty(call(
                () -> NativeBridge.nativeRegistryEntryName(registry, id), ""));
    }

    public static boolean sendPluginMessage(long handle, String channel,
                                            byte[] payload, int phase) {
        if (!usable(handle) || channel == null || channel.isEmpty()) return false;
        byte[] safePayload = payload == null ? new byte[0] : payload.clone();
        return call(() -> NativeBridge.nativePlayerSendPluginMessage(
            handle, channel, safePayload, phase), false);
    }

    public static String serverSetting(String key) {
        return key == null || key.isEmpty() ? "" : valueOrEmpty(call(
            () -> NativeBridge.nativeServerSetting(key), ""));
    }

    public static int routePath(String owner, String name, String descriptor) {
        return call(() -> NativeBridge.nativeRoutePath(owner, name, descriptor), 0);
    }

    public static long routeHash(String owner, String name, String descriptor) {
        return call(() -> NativeBridge.nativeRouteHash(owner, name, descriptor), 0L);
    }

    public static boolean executeCommand(String command) {
        return command != null && !command.isBlank() && call(
            () -> NativeBridge.nativeExecuteCommand(command), false);
    }

    public static void log(String level, String message) {
        String safeLevel = level == null ? "INFO" : level;
        String safeMessage = message == null ? "" : message;
        call(() -> NativeBridge.nativeLog(safeLevel, safeMessage));
    }

    private static String valueOrEmpty(String value) {
        return value == null ? "" : value;
    }

    private static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }
}
