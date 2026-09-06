package net.minecraft.screen;

/** Screen-handler type descriptor. */
public class ScreenHandlerType<T extends ScreenHandler> {
    public interface Factory<T extends ScreenHandler> { T create(int syncId, net.minecraft.entity.player.PlayerInventory inventory); }
    private final Factory<T> factory;
    public ScreenHandlerType() { this(null); }
    public ScreenHandlerType(Factory<T> factory) { this.factory = factory; }
    public T create(int syncId, net.minecraft.entity.player.PlayerInventory inventory) {
        return factory == null ? null : factory.create(syncId, inventory);
    }
}
