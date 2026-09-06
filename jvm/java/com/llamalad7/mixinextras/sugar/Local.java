package com.llamalad7.mixinextras.sugar;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/** Parameter selector used by MixinExtras local-capture handlers. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Local {
    int ordinal() default -1;
    int index() default -1;
    String name() default "";
    boolean argsOnly() default false;
    Class<?> type() default Object.class;
}
