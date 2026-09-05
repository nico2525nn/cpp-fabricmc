package net.minecraft.server;

import cppfm.bridge.NativeBridge;
import cppfm.bridge.WrapperCache;
import cppfm.bridge.MixinHooks;
import net.minecraft.registry.RegistryKey;
import net.minecraft.text.Text;
import net.minecraft.world.World;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.server.command.CommandManager;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

public class MinecraftServer {
    private final long nativeHandle;
    private volatile long tick;
    private final CommandManager commandManager;
    private final PlayerManager playerManager;

    protected MinecraftServer(long nativeHandle) {
        this.nativeHandle = nativeHandle;
        this.commandManager = new CommandManager();
        this.playerManager = new PlayerManager();
    }
    public static MinecraftServer of(long handle) {
        return WrapperCache.get(MinecraftServer.class, handle, MinecraftServer::new);
    }
    public long nativeHandle() { return nativeHandle; }
    public int getTicks() {
        Integer overwritten = MixinHooks.invokeOverwrite(this, "getTicks");
        if (overwritten != null) return overwritten;
        CallbackInfoReturnable<Integer> tail = MixinHooks.invokeTailReturn(
            this, "getTicks", (int) tick);
        if (tail.isCancelled()) return tail.getReturnValue();
        CallbackInfoReturnable<Integer> returned = MixinHooks.invokeReturn(
            this, "getTicks", tail.getReturnValue());
        return returned.getReturnValue();
    }
    public long getTickTime() {
        Long overwritten = MixinHooks.invokeOverwrite(this, "getTickTime");
        return overwritten != null ? overwritten : tick;
    }
    public void setTick(long tick) {
        // Void overwrites cannot be distinguished from a handler that returns
        // null through this bounded API, so the explicit HEAD hook remains
        // the safe path for setter methods.
        CallbackInfo callback = MixinHooks.invokeHead(this, "setTick", tick);
        if (callback.isCancelled()) return;
        this.tick = tick;
        MixinHooks.invokeTail(this, "setTick", tick);
    }
    public ServerWorld getOverworld() {
        return ServerWorld.of(NativeBridge.nativeServerWorld(0), this);
    }
    public ServerWorld getWorld(RegistryKey<World> key) {
        String value = key == null ? "minecraft:overworld" : key.getValue().toString();
        int dimension = value.endsWith("the_nether") ? -1 : value.endsWith("the_end") ? 1 : 0;
        return ServerWorld.of(NativeBridge.nativeServerWorld(dimension), this);
    }
    public void sendSystemMessage(Text text) {
        if (text != null) NativeBridge.nativeLog("INFO", text.getString());
    }
    public void execute(Runnable task) { if (task != null) task.run(); }
    public boolean isDedicated() { return true; }
    public CommandManager getCommandManager() { return commandManager; }
    public PlayerManager getPlayerManager() { return playerManager; }
}
