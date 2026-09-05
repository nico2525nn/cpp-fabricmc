package net.minecraft.server.network;

import cppfm.bridge.NativeBridge;
import cppfm.bridge.WrapperCache;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.text.Text;

public class ServerPlayerEntity extends PlayerEntity {
    protected ServerPlayerEntity(long nativeHandle) { super(nativeHandle); }
    public static ServerPlayerEntity of(long handle) {
        return WrapperCache.get(ServerPlayerEntity.class, handle, ServerPlayerEntity::new);
    }
    public ServerWorld getServerWorld() {
        return ServerWorld.of(NativeBridge.nativePlayerWorld(nativeHandle), getServer());
    }
    public MinecraftServer getServer() {
        return MinecraftServer.of(NativeBridge.nativeServerHandle());
    }
    public void sendMessage(Text message) { sendMessage(message, false); }
    public void sendMessage(Text message, boolean overlay) {
        if (message != null) NativeBridge.nativePlayerSendMessage(nativeHandle, message.getString(), overlay);
    }
    public void sendSystemMessage(Text message) { sendMessage(message, false); }
    public ServerPlayNetworkHandler networkHandler() { return new ServerPlayNetworkHandler(this); }
}
