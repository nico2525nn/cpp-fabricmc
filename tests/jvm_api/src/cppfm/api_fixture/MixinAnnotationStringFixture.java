package cppfm.api_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyConstant;

@Mixin(targets = "net.minecraft.class_100")
public abstract class MixinAnnotationStringFixture {
    @Shadow(aliases = "field_1")
    private int shadow$value;

    @Shadow(aliases = "method_2")
    private void shadow$targetMethod() {
    }

    @Inject(method = {
        "method_2",
        "method_2*()V",
        "net.minecraft.class_100.method_2{1,3}()V"
    }, at = @At(
        value = "INVOKE",
        target = "Lnet/minecraft/class_100;method_2",
        args = {"Lnet/minecraft/class_100;", "method_2", "BEFORE"},
        shift = At.Shift.BEFORE,
        id = "method_2"))
    @org.spongepowered.asm.mixin.injection.custom.Inject(method = "method_2()V")
    private void onTarget() {
    }

    @ModifyConstant(method = "method_2()V", constant = @Constant(
        stringValue = "method_2"))
    private String modifyConstant(String original) {
        return original;
    }
}
