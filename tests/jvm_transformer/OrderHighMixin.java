package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = OrderTarget.class, priority = 2000)
public final class OrderHighMixin {
    @Inject(method = "run()V", at = @At("HEAD"))
    private static void high(CallbackInfo ignored) { OrderRecorder.LOG.append('H'); }
}
