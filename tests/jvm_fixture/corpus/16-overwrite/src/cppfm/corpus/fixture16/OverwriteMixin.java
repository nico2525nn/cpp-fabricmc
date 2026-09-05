package cppfm.corpus.fixture16;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;

/** This method is called through the explicitly wired shadow hook. */
@Mixin(targets = "net.minecraft.server.MinecraftServer")
public final class OverwriteMixin {
    @Overwrite
    public long getTickTime() {
        return 42L;
    }
}
