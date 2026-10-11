package cppfm.corpus.fixture18;

import cppfm.bridge.NativeBridge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.ModifyConstant;

/** The transformed method must pass through the 1.21.4 chunk-height literal. */
@Mixin(targets = "net.minecraft.world.chunk.Chunk")
public final class ModifyConstantMixin {
    @ModifyConstant(method = "getHeight()I", constant = @Constant(intValue = 384))
    private static int modifyHeight(int value) {
        ModifyConstantFixture.modified();
        NativeBridge.nativeLog("INFO", "CORPUS case=18 status=PASS phase=modify-constant-handler");
        return value;
    }
}
