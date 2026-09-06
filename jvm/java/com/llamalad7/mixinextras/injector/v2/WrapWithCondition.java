package com.llamalad7.mixinextras.injector.v2;

import org.spongepowered.asm.mixin.injection.At;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/** Minimal MixinExtras ABI declaration for conditionally executing a call. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface WrapWithCondition {
    String[] method() default {};
    At[] at();
    int require() default -1;
    int expect() default 1;
    int allow() default -1;
}
