package net.minecraft.server.network;

import java.util.ArrayDeque;
import java.util.Queue;
import net.minecraft.network.ClientConnection;
import net.minecraft.network.packet.Packet;
import net.minecraft.server.MinecraftServer;

/**
 * Configuration-phase connection surface exposed by the 1.21.4 server ABI.
 *
 * <p>The native connection state machine remains authoritative.  The JVM
 * side keeps the task queue and lifecycle methods intentionally small so
 * Fabric networking modules can link and register their callbacks without
 * manufacturing a second network connection.</p>
 */
public class ServerConfigurationNetworkHandler extends ServerCommonNetworkHandler
        implements net.fabricmc.fabric.api.networking.v1.FabricServerConfigurationNetworkHandler {
    private final Queue<ServerPlayerConfigurationTask> tasks = new ArrayDeque<>();

    public ServerConfigurationNetworkHandler(MinecraftServer server,
                                             ClientConnection connection,
                                             ConnectedClientData clientData) {
        super(server, connection, clientData);
    }

    public void sendPacket(Packet<?> packet) {
        super.sendPacket(packet);
    }

    public void sendConfigurations() { pollTask(); }

    public void onTaskFinished(ServerPlayerConfigurationTask.Key key) { pollTask(); }
    @Override public void completeTask(ServerPlayerConfigurationTask.Key key) { onTaskFinished(key); }

    public void endConfiguration() { tasks.clear(); }

    public void queueSendResourcePackTask() { }

    public void pollTask() {
        ServerPlayerConfigurationTask task = tasks.poll();
        if (task != null) task.sendPacket(this::sendPacket);
    }

    public void addTask(ServerPlayerConfigurationTask task) {
        if (task != null) tasks.add(task);
    }

    public boolean isReconfiguring() { return transitioning; }
}
