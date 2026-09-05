package cppfm.corpus.fixture18;

import cppfm.bridge.NativeBridge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.ModifyConstant;

/** The transformed method must pass through the -64 literal. */
@Mixin(targets = "net.minecraft.world.World")
public final class ModifyConstantMixin {
    @ModifyConstant(method = "getBottomY()I", constant = @Constant(intValue = -64))
    private static int modifyBottom(int value) {
        ModifyConstantFixture.modified();
        NativeBridge.nativeLog("INFO", "CORPUS case=18 status=PASS phase=modify-constant-handler");
        return value;
    }
}
