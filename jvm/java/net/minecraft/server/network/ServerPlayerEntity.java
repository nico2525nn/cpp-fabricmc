package net.minecraft.server.network;

import cppfm.bridge.WrapperCache;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.text.Text;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.Identifier;
import net.minecraft.world.World;

public class ServerPlayerEntity extends PlayerEntity {
    protected ServerPlayerEntity(long nativeHandle) { super(nativeHandle, null, EntityType.PLAYER); }
    protected ServerPlayerEntity(long nativeHandle, World world) { super(nativeHandle, world, EntityType.PLAYER); }
    public ServerPlayerEntity(MinecraftServer server, ServerWorld world,
                              com.mojang.authlib.GameProfile profile,
                              net.minecraft.network.packet.c2s.common.SyncedClientOptions clientOptions) {
        super(0L, world, EntityType.PLAYER);
    }
    public static ServerPlayerEntity of(long handle) {
        return WrapperCache.get(ServerPlayerEntity.class, handle, ServerPlayerEntity::new);
    }
    public ServerWorld getServerWorld() {
        return ServerWorld.of(NativeAccess.playerWorld(nativeHandle), getServer());
    }
    public MinecraftServer getServer() {
        return MinecraftServer.of(NativeAccess.serverHandle());
    }
    public void sendMessage(Text message) { sendMessage(message, false); }
    public void sendMessage(Text message, boolean overlay) {
        if (message != null) NativeAccess.sendMessage(nativeHandle, message.getString(), overlay);
    }
    public void sendSystemMessage(Text message) { sendMessage(message, false); }
    public ServerPlayNetworkHandler networkHandler() { return networkHandler; }
    public final ServerPlayNetworkHandler networkHandler = new ServerPlayNetworkHandler(this);
    public ServerPlayNetworkHandler getNetworkHandler() { return networkHandler; }
    public void teleport(double x, double y, double z) { super.teleport(x, y, z); }
    /** Teleport-target overload used by server lifecycle mixins. */
    public ServerPlayerEntity teleportTo(net.minecraft.world.TeleportTarget target) { return this; }
    public void sendChatMessage(Text message) { sendMessage(message, false); }
    public boolean sendPluginMessage(Identifier channel, byte[] payload) {
        return NativeAccess.sendPluginMessage(nativeHandle, channel == null ? "" : channel.toString(), payload, 1);
    }
    public boolean isDisconnected() { return networkHandler == null || networkHandler.isDisconnected(); }
    public void increaseStat(net.minecraft.stat.Stat<?> stat, int amount) { }
    public void resetLastActionTime() { }
    @Override public void onDeath(net.minecraft.entity.damage.DamageSource source) {
        super.onDeath(source);
    }
    /** Item-consumption lifecycle hook used by server-player mixins. */
    public void consumeItem() { }
    @Override public void tick() { super.tick(); }
}
