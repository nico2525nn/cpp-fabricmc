package cppfm.corpus.fixture17;

import cppfm.bridge.NativeBridge;
import net.minecraft.util.math.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;

/** The transformed call must preserve the x coordinate while observing it. */
@Mixin(targets = "net.minecraft.world.World")
public final class ModifyArgMixin {
    @ModifyArg(method = "getBlockState(Lnet/minecraft/util/math/BlockPos;)Lnet/minecraft/block/BlockState;",
               at = @At(value = "INVOKE", target = "Lnet/minecraft/util/NativeAccess;worldBlock(JIII)I"),
               index = 1)
    private static int modifyX(int x) {
        ModifyArgFixture.modified();
        NativeBridge.nativeLog("INFO", "CORPUS case=17 status=PASS phase=modify-arg-handler");
        return x;
    }
}
