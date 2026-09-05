package org.spongepowered.asm.mixin.injection;

import org.spongepowered.asm.mixin.injection.callback.LocalCapture;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface Inject {
    String[] method() default {};
    At[] at();
    boolean cancellable() default false;
    Slice[] slice() default {};
    LocalCapture locals() default LocalCapture.NO_CAPTURE;
    int require() default -1;
    int expect() default 1;
    int order() default 1000;
    boolean remap() default true;
    String constraints() default "";
}
