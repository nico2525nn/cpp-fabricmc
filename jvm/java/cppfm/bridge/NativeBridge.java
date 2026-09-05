package cppfm.bridge;

/**
 * Narrow JNI surface for the embedded runtime.  Handles are opaque
 * generation-checked values; no native address is ever exposed to Java.
 */
public final class NativeBridge {
    private NativeBridge() {}

    public static native void nativeLog(String level, String message);
    public static native long nativeServerHandle();
    public static native long nativeCurrentTick();
    public static native String nativePlayerName(long handle);
    public static native String nativePlayerUuid(long handle);
    public static native int nativePlayerEntityId(long handle);
    public static native int nativePlayerGameMode(long handle);
    public static native boolean nativePlayerSneaking(long handle);
    public static native int nativeOnlinePlayerCount();
    public static native long nativeOnlinePlayerHandle(int index);
    public static native int nativePlayerHeldSlot(long handle);
    /** PlayerInventory logical slots: main 0..35, armor 36..39, offhand 40. */
    public static native int nativePlayerInventoryItemId(long handle, int slot);
    public static native int nativePlayerInventoryItemCount(long handle, int slot);
    public static native String nativePlayerInventoryItemName(long handle, int slot);
    public static native boolean nativePlayerSetInventoryItemCount(long handle, int slot, int count);
    public static native boolean nativePlayerSetInventoryItem(long handle, int slot, int itemId, int count);
    /** axis 0=x, 1=y, 2=z. */
    public static native double nativePlayerCoordinate(long handle, int axis);
    public static native boolean nativePlayerSetPosition(long handle, double x, double y, double z);
    public static native boolean nativePlayerSendMessage(long handle, String message, boolean overlay);
    public static native long nativePlayerWorld(long handle);
    public static native long nativeServerWorld(int dimension);
    public static native String nativeWorldName(long handle);
    public static native int nativeWorldBlock(long handle, int x, int y, int z);
    public static native boolean nativeWorldSetBlock(long handle, int x, int y, int z, int state);
    public static native boolean nativeExecuteCommand(String command);
    public static native void nativeSetModStats(int discovered, int initialized);
    public static native void nativeRegisterTransformedMethod(String owner, String name, String descriptor);

    public static long serverHandle() { return nativeServerHandle(); }
    public static long currentTick() { return nativeCurrentTick(); }
}
