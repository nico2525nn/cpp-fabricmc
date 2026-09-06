package com.llamalad7.mixinextras.injector;

import org.spongepowered.asm.mixin.injection.At;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/** Minimal MixinExtras ABI declaration for a return-value modifier. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface ModifyReturnValue {
    String[] method() default {};
    At[] at();
    int require() default -1;
    int expect() default 1;
    int allow() default -1;
}
