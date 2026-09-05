package org.spongepowered.asm.mixin.injection;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.ANNOTATION_TYPE)
public @interface Constant {
    int intValue() default 0;
    long longValue() default 0L;
    float floatValue() default 0.0f;
    double doubleValue() default 0.0d;
    String stringValue() default "";
    Class<?> classValue() default void.class;
    boolean nullValue() default false;
    int ordinal() default -1;
}
