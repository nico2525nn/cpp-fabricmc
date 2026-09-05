package org.spongepowered.asm.mixin.injection;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface ModifyConstant {
    String[] method() default {};
    Constant[] constant() default {};
    Slice[] slice() default {};
    int require() default -1;
    int expect() default 1;
    int allow() default -1;
    boolean remap() default true;
    String constraints() default "";
}
