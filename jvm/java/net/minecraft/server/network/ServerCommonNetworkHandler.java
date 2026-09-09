package net.minecraft.server.network;

import net.minecraft.network.ClientConnection;
import net.minecraft.network.DisconnectionInfo;
import net.minecraft.network.PacketCallbacks;
import net.minecraft.network.packet.Packet;
import net.minecraft.network.packet.c2s.common.CommonPongC2SPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.text.Text;

/** Common server connection surface shared by configuration, login, and play. */
public abstract class ServerCommonNetworkHandler {
    public static final int KEEP_ALIVE_INTERVAL = 15;
    public static final int TRANSITION_TIMEOUT = 10;
    protected final MinecraftServer server;
    protected final ClientConnection connection;
    protected boolean transferred;
    protected boolean waitingForKeepAlive;
    protected long lastKeepAliveTime;
    protected long keepAliveId;
    protected boolean transitioning;
    protected boolean flushDisabled;
    protected int latency;

    protected ServerCommonNetworkHandler(MinecraftServer server, ClientConnection connection,
                                         ConnectedClientData clientData) {
        this.server = server;
        this.connection = connection == null ? new ClientConnection() : connection;
    }

    public boolean isHost() { return false; }
    public MinecraftServer getServer() { return server; }
    public boolean isConnectionOpen() { return connection != null && connection.isOpen(); }
    public void markTransitionTime() { transitioning = true; lastKeepAliveTime = System.currentTimeMillis(); }
    public boolean checkTransitionTimeout(long time) {
        return !transitioning || time - lastKeepAliveTime < TRANSITION_TIMEOUT * 1000L;
    }
    public int getLatency() { return latency; }
    public void disableFlush() { flushDisabled = true; }
    public void enableFlush() { flushDisabled = false; }
    public void sendPacket(Packet<?> packet) { if (connection != null) connection.send(packet); }
    public void send(Packet<?> packet, PacketCallbacks callbacks) { sendPacket(packet); }
    public void disconnect(Text reason) { if (connection != null) connection.disconnect(); }
    public void disconnect(DisconnectionInfo reason) { if (connection != null) connection.disconnect(); }
    public void baseTick() { }
    /** Common pong listener entrypoint used by Fabric's resource/network mixins. */
    public void onPong(CommonPongC2SPacket packet) { }
    /** Custom payload dispatch boundary shared by the server phases. */
    public void onCustomPayload(net.minecraft.network.packet.c2s.common.CustomPayloadC2SPacket packet) { }
}
