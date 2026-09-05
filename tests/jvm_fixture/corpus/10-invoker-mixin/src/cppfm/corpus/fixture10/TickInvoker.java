package cppfm.corpus.fixture10;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/** The transformer must add this invoker interface to the target class. */
@Mixin(targets = "net.minecraft.server.MinecraftServer")
public interface TickInvoker {
    @Invoker("getTicks")
    int cppfm$getTicks();
}
