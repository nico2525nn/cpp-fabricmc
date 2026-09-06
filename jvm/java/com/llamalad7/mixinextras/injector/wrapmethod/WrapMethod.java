package com.llamalad7.mixinextras.injector.wrapmethod;

import org.spongepowered.asm.mixin.injection.At;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/** ABI declaration for the method-level MixinExtras wrapper. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface WrapMethod {
    String[] method() default {};
    At[] at() default {};
    int require() default -1;
    int expect() default 1;
    int allow() default -1;
}
