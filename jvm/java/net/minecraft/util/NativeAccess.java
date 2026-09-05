package net.minecraft.util;

import cppfm.bridge.NativeBridge;

/**
 * Exception-safe facade for the optional JNI boundary.
 *
 * The Java ABI is also used by javac-only mod fixtures and by early JVM
 * bootstrap code, where the native library may not have been loaded yet.
 * All methods therefore treat a missing library, an invalid handle, or a
 * native-side runtime failure as the documented neutral value.
 */
public final class NativeAccess {
    private NativeAccess() {}

    private static boolean usable(long handle) { return handle != 0L; }

    public static long serverHandle() {
        try { return NativeBridge.serverHandle(); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static long currentTick() {
        try { return NativeBridge.currentTick(); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static String playerName(long handle) {
        if (!usable(handle)) return "";
        try { return valueOrEmpty(NativeBridge.nativePlayerName(handle)); }
        catch (LinkageError | RuntimeException ignored) { return ""; }
    }

    public static String playerUuid(long handle) {
        if (!usable(handle)) return "";
        try { return valueOrEmpty(NativeBridge.nativePlayerUuid(handle)); }
        catch (LinkageError | RuntimeException ignored) { return ""; }
    }

    public static int playerEntityId(long handle) {
        if (!usable(handle)) return 0;
        try { return NativeBridge.nativePlayerEntityId(handle); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static int playerGameMode(long handle) {
        if (!usable(handle)) return 0;
        try { return NativeBridge.nativePlayerGameMode(handle); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static boolean playerSneaking(long handle) {
        if (!usable(handle)) return false;
        try { return NativeBridge.nativePlayerSneaking(handle); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static int onlinePlayerCount() {
        try { return Math.max(0, NativeBridge.nativeOnlinePlayerCount()); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static long onlinePlayerHandle(int index) {
        if (index < 0) return 0L;
        try { return NativeBridge.nativeOnlinePlayerHandle(index); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static int heldSlot(long handle) {
        if (!usable(handle)) return 0;
        try { return clamp(NativeBridge.nativePlayerHeldSlot(handle), 0, 8); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static int inventoryItemId(long handle, int slot) {
        if (!usable(handle) || slot < 0 || slot >= 41) return 0;
        try { return Math.max(0, NativeBridge.nativePlayerInventoryItemId(handle, slot)); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static int inventoryItemCount(long handle, int slot) {
        if (!usable(handle) || slot < 0 || slot >= 41) return 0;
        try { return Math.max(0, NativeBridge.nativePlayerInventoryItemCount(handle, slot)); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static String inventoryItemName(long handle, int slot) {
        if (!usable(handle) || slot < 0 || slot >= 41) return "minecraft:air";
        try {
            String value = NativeBridge.nativePlayerInventoryItemName(handle, slot);
            return value == null || value.isEmpty() ? "minecraft:air" : value;
        } catch (LinkageError | RuntimeException ignored) { return "minecraft:air"; }
    }

    public static boolean setInventoryItemCount(long handle, int slot, int count) {
        if (!usable(handle) || slot < 0 || slot >= 41) return false;
        try { return NativeBridge.nativePlayerSetInventoryItemCount(handle, slot, Math.max(0, count)); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static boolean setInventoryItem(long handle, int slot, int itemId, int count) {
        if (!usable(handle) || slot < 0 || slot >= 41) return false;
        try {
            return NativeBridge.nativePlayerSetInventoryItem(handle, slot, Math.max(0, itemId), Math.max(0, count));
        } catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static double coordinate(long handle, int axis) {
        if (!usable(handle) || axis < 0 || axis > 2) return 0.0;
        try { return NativeBridge.nativePlayerCoordinate(handle, axis); }
        catch (LinkageError | RuntimeException ignored) { return 0.0; }
    }

    public static boolean setPosition(long handle, double x, double y, double z) {
        if (!usable(handle)) return false;
        try { return NativeBridge.nativePlayerSetPosition(handle, x, y, z); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static boolean sendMessage(long handle, String message, boolean overlay) {
        if (!usable(handle) || message == null) return false;
        try { return NativeBridge.nativePlayerSendMessage(handle, message, overlay); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static long playerWorld(long handle) {
        if (!usable(handle)) return 0L;
        try { return NativeBridge.nativePlayerWorld(handle); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static long serverWorld(int dimension) {
        try { return NativeBridge.nativeServerWorld(dimension); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static String worldName(long handle) {
        if (!usable(handle)) return "minecraft:overworld";
        try {
            String value = NativeBridge.nativeWorldName(handle);
            return value == null || value.isEmpty() ? "minecraft:overworld" : value;
        } catch (LinkageError | RuntimeException ignored) { return "minecraft:overworld"; }
    }

    public static int worldBlock(long handle, int x, int y, int z) {
        if (!usable(handle)) return 0;
        try { return Math.max(0, NativeBridge.nativeWorldBlock(handle, x, y, z)); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static boolean setWorldBlock(long handle, int x, int y, int z, int state) {
        if (!usable(handle)) return false;
        try { return NativeBridge.nativeWorldSetBlock(handle, x, y, z, Math.max(0, state)); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static String entityType(long handle) {
        if (!usable(handle)) return "minecraft:unknown";
        try {
            String value = NativeBridge.nativeEntityType(handle);
            return value == null || value.isEmpty() ? "minecraft:unknown" : value;
        } catch (LinkageError | RuntimeException ignored) { return "minecraft:unknown"; }
    }

    public static int entityTypeId(long handle) {
        if (!usable(handle)) return -1;
        try { return NativeBridge.nativeEntityTypeId(handle); }
        catch (LinkageError | RuntimeException ignored) { return -1; }
    }

    public static float entityHealth(long handle) {
        if (!usable(handle)) return 0.0f;
        try { return Math.max(0.0f, NativeBridge.nativeEntityHealth(handle)); }
        catch (LinkageError | RuntimeException ignored) { return 0.0f; }
    }

    public static boolean setEntityHealth(long handle, float health) {
        if (!usable(handle) || !Float.isFinite(health) || health < 0.0f) return false;
        try { return NativeBridge.nativeEntitySetHealth(handle, health); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static boolean entityDead(long handle) {
        if (!usable(handle)) return false;
        try { return NativeBridge.nativeEntityDead(handle); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static long entityWorld(long handle) {
        if (!usable(handle)) return 0L;
        try { return NativeBridge.nativeEntityWorld(handle); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static long entityCount() {
        try { return Math.max(0, NativeBridge.nativeEntityCount()); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static long entityHandle(int index) {
        if (index < 0) return 0L;
        try { return NativeBridge.nativeEntityHandle(index); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static long worldTime(long handle) {
        if (!usable(handle)) return 0L;
        try { return NativeBridge.nativeWorldTime(handle); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static int registryItemId(String name) {
        if (name == null || name.isEmpty()) return -1;
        try { return NativeBridge.nativeRegistryItemId(name); }
        catch (LinkageError | RuntimeException ignored) { return -1; }
    }

    public static String registryItemName(int id) {
        if (id < 0) return "";
        try { return valueOrEmpty(NativeBridge.nativeRegistryItemName(id)); }
        catch (LinkageError | RuntimeException ignored) { return ""; }
    }

    public static int registryBlockState(String name) {
        if (name == null || name.isEmpty()) return -1;
        try { return NativeBridge.nativeRegistryBlockState(name); }
        catch (LinkageError | RuntimeException ignored) { return -1; }
    }

    public static String registryBlockName(int state) {
        if (state < 0) return "";
        try { return valueOrEmpty(NativeBridge.nativeRegistryBlockName(state)); }
        catch (LinkageError | RuntimeException ignored) { return ""; }
    }

    public static int registryEntryCount(String registry) {
        if (registry == null || registry.isEmpty()) return 0;
        try { return Math.max(0, NativeBridge.nativeRegistryEntryCount(registry)); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static String registryEntryName(String registry, int id) {
        if (registry == null || registry.isEmpty() || id < 0) return "";
        try { return valueOrEmpty(NativeBridge.nativeRegistryEntryName(registry, id)); }
        catch (LinkageError | RuntimeException ignored) { return ""; }
    }

    public static boolean sendPluginMessage(long handle, String channel, byte[] payload, int phase) {
        if (!usable(handle) || channel == null || channel.isEmpty()) return false;
        try { return NativeBridge.nativePlayerSendPluginMessage(handle, channel,
            payload == null ? new byte[0] : payload.clone(), phase); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static String serverSetting(String key) {
        if (key == null || key.isEmpty()) return "";
        try { return valueOrEmpty(NativeBridge.nativeServerSetting(key)); }
        catch (LinkageError | RuntimeException ignored) { return ""; }
    }

    public static int routePath(String owner, String name, String descriptor) {
        try { return NativeBridge.nativeRoutePath(owner, name, descriptor); }
        catch (LinkageError | RuntimeException ignored) { return 0; }
    }

    public static long routeHash(String owner, String name, String descriptor) {
        try { return NativeBridge.nativeRouteHash(owner, name, descriptor); }
        catch (LinkageError | RuntimeException ignored) { return 0L; }
    }

    public static boolean executeCommand(String command) {
        if (command == null || command.isBlank()) return false;
        try { return NativeBridge.nativeExecuteCommand(command); }
        catch (LinkageError | RuntimeException ignored) { return false; }
    }

    public static void log(String level, String message) {
        try { NativeBridge.nativeLog(level == null ? "INFO" : level, message == null ? "" : message); }
        catch (LinkageError | RuntimeException ignored) { }
    }

    private static String valueOrEmpty(String value) { return value == null ? "" : value; }
    private static int clamp(int value, int min, int max) { return Math.max(min, Math.min(max, value)); }
}
