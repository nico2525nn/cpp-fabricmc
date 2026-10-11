package cppfm.api_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;

@Mixin(targets = "net.minecraft.class_100", remap = false)
public abstract class MixinRemapControlFixture {
    @Inject(method = "method_2()V", remap = true, at = @At(
        value = "INVOKE",
        target = "Lnet/minecraft/class_100;method_2()V",
        remap = true))
    private void explicitlyRemapped() { }

    @Inject(method = "method_2()V", remap = false, at = @At(
        value = "INVOKE",
        target = "Lnet/minecraft/class_100;method_2()V",
        remap = false))
    private void deliberatelyUnmapped() { }
}
