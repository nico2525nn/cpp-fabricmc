package cppfm.corpus.fixture19;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

/** The transformed setter must invoke the variable handler at its argument boundary. */
@Mixin(targets = "net.minecraft.server.MinecraftServer")
public final class ModifyVariableMixin {
    private static final AtomicBoolean SEEN = new AtomicBoolean();

    @ModifyVariable(method = "setTick(J)V", at = @At("HEAD"), argsOnly = true)
    private static long modifyTick(long tick) {
        if (tick == 1 && SEEN.compareAndSet(false, true)) {
            ModifyVariableFixture.modified();
            NativeBridge.nativeLog("INFO", "CORPUS case=19 status=PASS phase=modify-variable-handler");
        }
        return tick;
    }
}
