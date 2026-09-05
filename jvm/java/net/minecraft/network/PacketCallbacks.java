package net.minecraft.network;

/** Completion hook accepted by Fabric's PacketSender API. */
@FunctionalInterface
public interface PacketCallbacks {
    void onSuccess();

    default void onFailure(Throwable failure) { }
}
