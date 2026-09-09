package net.fabricmc.fabric.api.screenhandler.v1;

import net.minecraft.entity.player.PlayerInventory;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.screen.ScreenHandler;
import net.minecraft.screen.ScreenHandlerType;

/** Screen-handler type carrying a codec-defined opening payload. */
public class ExtendedScreenHandlerType<T extends ScreenHandler, D> extends ScreenHandlerType<T> {
    private final ExtendedFactory<T, D> factory;
    private final PacketCodec<? super RegistryByteBuf, D> packetCodec;
    public ExtendedScreenHandlerType(ExtendedFactory<T, D> factory,
            PacketCodec<? super RegistryByteBuf, D> packetCodec) {
        super((syncId, inventory) -> factory == null ? null : factory.create(syncId, inventory, null));
        this.factory = factory;
        this.packetCodec = packetCodec;
    }
    /** Compatibility name used by the vanilla screen registry. */
    public final T method_17434(int syncId, PlayerInventory inventory) { return create(syncId, inventory, null); }
    public T create(int syncId, PlayerInventory inventory, D data) { return factory == null ? null : factory.create(syncId, inventory, data); }
    public PacketCodec<? super RegistryByteBuf, D> getPacketCodec() { return packetCodec; }
    @FunctionalInterface public interface ExtendedFactory<T extends ScreenHandler, D> { T create(int syncId, PlayerInventory inventory, D data); }
}
