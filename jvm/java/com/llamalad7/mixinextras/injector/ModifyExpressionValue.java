package com.llamalad7.mixinextras.injector;

import org.spongepowered.asm.mixin.injection.At;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * Minimal MixinExtras ABI declaration for an expression-value modifier.
 *
 * <p>The transformer consumes this annotation from class-file metadata.  It
 * is nevertheless exposed in the shadow class tree so fixture and ordinary
 * Fabric code can be compiled against the same API surface.</p>
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface ModifyExpressionValue {
    String[] method() default {};
    At[] at();
    int require() default -1;
    int expect() default 1;
    int allow() default -1;
}
