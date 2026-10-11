package net.minecraft.network;

import cppfm.bridge.FabricNetworkingDispatch;
import net.minecraft.network.packet.Packet;
import net.minecraft.util.NativeAccess;

/** Transport placeholder for the server-side network handler ABI. */
public class ClientConnection {
    private boolean open = true;
    private final long nativePlayerHandle;
    private final int customPayloadPhase;

    public ClientConnection() { this(0L, 1); }
    public ClientConnection(long nativePlayerHandle, int customPayloadPhase) {
        this.nativePlayerHandle = nativePlayerHandle;
        this.customPayloadPhase = customPayloadPhase;
    }

    public void send(Packet<?> packet) {
        sendIfSupported(packet);
    }

    private boolean sendIfSupported(Packet<?> packet) {
        if (!open || nativePlayerHandle == 0L || packet == null) return false;
        net.minecraft.network.packet.CustomPayloadS2CPacket custom =
            packet instanceof net.minecraft.network.packet.CustomPayloadS2CPacket value
                ? value : null;
        if (custom == null) return false;
        String channel;
        byte[] payload;
        if (custom.payload() != null) {
            channel = custom.payload().getId().id().toString();
            payload = FabricNetworkingDispatch.encodeS2CPayload(
                custom.payload(), customPayloadPhase);
        } else {
            channel = custom.getChannel().toString();
            payload = custom.getData().toByteArray();
        }
        return NativeAccess.sendPluginMessage(nativePlayerHandle, channel, payload,
            customPayloadPhase);
    }
    public void send(Packet<?> packet, PacketCallbacks callbacks) {
        boolean sent;
        try {
            sent = sendIfSupported(packet);
        } catch (RuntimeException failure) {
            if (callbacks == null) throw failure;
            callbacks.onFailure(failure);
            return;
        }
        if (callbacks == null) return;
        if (sent) callbacks.onSuccess();
        else callbacks.onFailure(new IllegalStateException(
            "packet was not sent by the native connection"));
    }
    public void disconnect() { open = false; }
    public boolean isOpen() { return open; }
}
