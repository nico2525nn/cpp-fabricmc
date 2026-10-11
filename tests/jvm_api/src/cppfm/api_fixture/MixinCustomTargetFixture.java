package cppfm.api_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(targets = "net.minecraft.example.method_2")
public abstract class MixinCustomTargetFixture {
    @Accessor("field_1")
    public abstract int getField();

    @Invoker("method_2")
    public abstract void invokeTarget();
}
