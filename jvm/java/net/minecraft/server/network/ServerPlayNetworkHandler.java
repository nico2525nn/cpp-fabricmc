package net.minecraft.server.network;

import net.minecraft.network.packet.Packet;
import net.minecraft.text.Text;
import net.fabricmc.fabric.api.networking.v1.PacketSender;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import net.minecraft.network.packet.c2s.play.PlayerInteractBlockC2SPacket;
import net.minecraft.network.packet.c2s.play.PlayerInteractItemC2SPacket;
import net.minecraft.network.packet.c2s.play.PlayerActionC2SPacket;
import net.minecraft.network.packet.c2s.play.PlayerInputC2SPacket;
import net.minecraft.network.packet.c2s.play.PlayerMoveC2SPacket;
import net.minecraft.network.packet.c2s.play.ClientCommandC2SPacket;
import net.minecraft.network.packet.c2s.play.ButtonClickC2SPacket;
import net.minecraft.network.packet.c2s.play.CraftRequestC2SPacket;
import net.minecraft.network.packet.c2s.play.UpdateSelectedSlotC2SPacket;
import net.minecraft.network.packet.c2s.play.HandSwingC2SPacket;
import net.minecraft.network.packet.c2s.play.CommandExecutionC2SPacket;
import net.minecraft.network.packet.c2s.play.AcknowledgeReconfigurationC2SPacket;
import net.minecraft.network.packet.c2s.play.PickItemFromBlockC2SPacket;
import net.minecraft.network.packet.c2s.play.PickItemFromEntityC2SPacket;
import net.minecraft.network.packet.c2s.common.CustomPayloadC2SPacket;
import net.minecraft.network.packet.c2s.play.VehicleMoveC2SPacket;
import net.minecraft.network.packet.c2s.play.PlayerInteractEntityC2SPacket;
import net.minecraft.network.DisconnectionInfo;
import net.minecraft.network.ClientConnection;
import net.minecraft.server.MinecraftServer;

public class ServerPlayNetworkHandler extends ServerCommonNetworkHandler {
    private final ServerPlayerEntity player;
    private volatile boolean disconnected;
    /** Movement anti-floating counters retained for server packet mixins. */
    private int floatingTicks;
    private int vehicleFloatingTicks;
    private boolean vehicleFloating;
    public ServerPlayNetworkHandler(ServerPlayerEntity player) {
        this(player == null ? null : player.getServer(), new ClientConnection(), player,
            new ConnectedClientData());
    }
    public ServerPlayNetworkHandler(MinecraftServer server, ClientConnection connection,
                                    ServerPlayerEntity player, ConnectedClientData clientData) {
        super(server, connection, clientData);
        this.player = player;
        ServerPlayConnectionEvents.INIT.invoker().onPlayInit(this, player == null ? null : player.getServer());
    }
    public ServerPlayerEntity getPlayer() { return player; }
    public void sendPacket(Packet<?> packet) {
        if (packet != null && player != null) ServerPlayNetworking.send(player, packet);
    }
    public void disconnect(Text reason) {
        if (disconnected) return;
        disconnected = true;
        if (player != null) player.sendMessage(reason, false);
    }
    public boolean isConnectionOpen() { return player != null && !disconnected && !player.isRemoved(); }
    public boolean isDisconnected() { return disconnected; }
    @Override public boolean isHost() { return false; }
    /** Per-connection tick hook used by Carpet's anti-cheat mixin. */
    public void tick() {
        if (vehicleFloating) vehicleFloatingTicks++;
        else vehicleFloatingTicks = 0;
    }
    public PacketSender getPacketSender() { return player == null ? PacketSender.NOOP : ServerPlayNetworking.getSender(player); }
    /** Interaction packet entrypoint retained for server-side mixin targets. */
    public void onPlayerInteractBlock(PlayerInteractBlockC2SPacket packet) { }
    /** Item interaction packet entrypoint retained for server-side mixin targets. */
    public void onPlayerInteractItem(PlayerInteractItemC2SPacket packet) { }
    /** Entity-interaction packet entrypoint retained for Fabric networking mixins. */
    public void onPlayerInteractEntity(PlayerInteractEntityC2SPacket packet) { }
    /** Block-action packet entrypoint retained for server-side mixin targets. */
    public void onPlayerAction(PlayerActionC2SPacket packet) { }
    /** Player-input packet entrypoint retained for server-side mixin targets. */
    public void onPlayerInput(PlayerInputC2SPacket packet) { }
    /** Player-movement packet entrypoint retained for server-side mixin targets. */
    public void onPlayerMove(PlayerMoveC2SPacket packet) { }
    /** Client-command packet entrypoint retained for Carpet and server mixins. */
    public void onClientCommand(ClientCommandC2SPacket packet) { }
    /** Screen-button packet entrypoint retained for Carpet and server mixins. */
    public void onButtonClick(ButtonClickC2SPacket packet) {
        if (player != null) player.resetLastActionTime();
    }
    /** Recipe-request packet entrypoint retained for Carpet and server mixins. */
    public void onCraftRequest(CraftRequestC2SPacket packet) {
        if (player != null) player.resetLastActionTime();
    }
    /** Selected-hotbar-slot packet entrypoint retained for server mixins. */
    public void onUpdateSelectedSlot(UpdateSelectedSlotC2SPacket packet) { }
    /** Hand-swing packet entrypoint retained for Carpet and server mixins. */
    public void onHandSwing(HandSwingC2SPacket packet) {
        if (player != null) player.resetLastActionTime();
    }
    /** Command-execution packet entrypoint retained for Carpet and server mixins. */
    public void onCommandExecution(CommandExecutionC2SPacket packet) { }
    /** Configuration-to-play acknowledgement entrypoint used by Fabric. */
    public void onAcknowledgeReconfiguration(AcknowledgeReconfigurationC2SPacket packet) { }
    /** Pick-block packet entrypoint retained for Fabric and server mixins. */
    public void onPickItemFromBlock(PickItemFromBlockC2SPacket packet) { }
    /** Pick-entity packet entrypoint retained for Fabric and server mixins. */
    public void onPickItemFromEntity(PickItemFromEntityC2SPacket packet) { }
    /** Custom-payload packet entrypoint retained for Fabric/Carpet mixins. */
    public void onCustomPayload(CustomPayloadC2SPacket packet) { }
    /** Vehicle-movement packet entrypoint retained for server mixins. */
    public void onVehicleMove(VehicleMoveC2SPacket packet) {
        if (isHost()) return;
    }
    /** Packet-listener lifecycle callback exposed by the 1.21.4 ABI. */
    public void onDisconnected(DisconnectionInfo info) {
        if (!disconnected) disconnected = true;
    }
}
