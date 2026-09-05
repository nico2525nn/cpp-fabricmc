package cppfm.corpus.fixture09;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** The transformer must add this accessor interface to the target class. */
@Mixin(targets = "net.minecraft.server.MinecraftServer")
public interface TickAccessor {
    @Accessor("tick")
    long cppfm$getTick();
}
