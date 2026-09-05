package net.minecraft.world;

import cppfm.bridge.NativeBridge;
import cppfm.bridge.MixinHooks;
import cppfm.bridge.WrapperCache;
import net.minecraft.block.BlockState;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

public class World {
    protected final long nativeHandle;
    protected final boolean client;

    protected World(long nativeHandle, boolean client) {
        this.nativeHandle = nativeHandle;
        this.client = client;
    }
    public static World of(long handle) {
        return WrapperCache.get(World.class, handle, h -> new World(h, false));
    }
    public long nativeHandle() { return nativeHandle; }
    public boolean isClient() { return client; }
    public BlockState getBlockState(BlockPos pos) {
        if (pos == null) return new BlockState(0);
        BlockState overwritten = MixinHooks.invokeOverwrite(this, "getBlockState", pos);
        if (overwritten != null) return overwritten;
        CallbackInfoReturnable<BlockState> head = MixinHooks.invokeHeadReturn(
            this, "getBlockState", null, pos);
        if (head.isCancelled()) return head.getReturnValue();
        BlockState nativeState = new BlockState(NativeBridge.nativeWorldBlock(
            nativeHandle, pos.getX(), pos.getY(), pos.getZ()));
        CallbackInfoReturnable<BlockState> tail = MixinHooks.invokeTailReturn(
            this, "getBlockState", nativeState, pos);
        if (tail.isCancelled()) return tail.getReturnValue();
        CallbackInfoReturnable<BlockState> returned = MixinHooks.invokeReturn(
            this, "getBlockState", tail.getReturnValue(), pos);
        return returned.getReturnValue();
    }
    public boolean setBlockState(BlockPos pos, BlockState state) { return setBlockState(pos, state, 3); }
    public boolean setBlockState(BlockPos pos, BlockState state, int flags) {
        if (pos == null || state == null) return false;
        Boolean overwritten = MixinHooks.invokeOverwrite(this, "setBlockState", pos, state, flags);
        if (overwritten != null) return overwritten;
        CallbackInfoReturnable<Boolean> head = MixinHooks.invokeHeadReturn(
            this, "setBlockState", false, pos, state, flags);
        if (head.isCancelled()) return Boolean.TRUE.equals(head.getReturnValue());
        boolean changed = NativeBridge.nativeWorldSetBlock(
            nativeHandle, pos.getX(), pos.getY(), pos.getZ(), state.getRawState());
        CallbackInfoReturnable<Boolean> tail = MixinHooks.invokeTailReturn(
            this, "setBlockState", changed, pos, state, flags);
        if (tail.isCancelled()) return Boolean.TRUE.equals(tail.getReturnValue());
        CallbackInfoReturnable<Boolean> returned = MixinHooks.invokeReturn(
            this, "setBlockState", tail.getReturnValue(), pos, state, flags);
        return Boolean.TRUE.equals(returned.getReturnValue());
    }
    @SuppressWarnings("unchecked")
    public RegistryKey<World> getRegistryKey() {
        String name = NativeBridge.nativeWorldName(nativeHandle);
        return new RegistryKey<>(Identifier.tryParse(name == null || name.isEmpty() ? "minecraft:overworld" : name));
    }
    public long getTime() { return NativeBridge.currentTick(); }
    public long getTimeOfDay() { return getTime(); }
    public int getBottomY() { return -64; }
    public int getTopY() { return 320; }
    public boolean isChunkLoaded(int chunkX, int chunkZ) { return true; }
    public net.minecraft.server.MinecraftServer getServer() { return null; }
}
