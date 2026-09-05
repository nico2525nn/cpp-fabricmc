package cppfm.corpus.fixture20;

import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.LocalCapture;

/** TAIL must expose the nativeState local, not only the callback object. */
@Mixin(targets = "net.minecraft.world.World")
public final class LocalCaptureMixin {
    @Inject(method = "getBlockState(Lnet/minecraft/util/math/BlockPos;)Lnet/minecraft/block/BlockState;",
            at = @At("TAIL"), locals = LocalCapture.PRINT)
    private static void capture(BlockPos pos, BlockState nativeState,
                                 CallbackInfoReturnable<BlockState> info) {
        boolean ok = pos != null && nativeState != null && info.getReturnValue() != null
            && nativeState.getRawId() == info.getReturnValue().getRawId();
        LocalCaptureFixture.captured(ok);
    }
}
