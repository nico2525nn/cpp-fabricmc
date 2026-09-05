package org.spongepowered.asm.mixin.injection;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.ANNOTATION_TYPE, ElementType.METHOD})
public @interface At {
    String value();
    String target() default "";
    int ordinal() default -1;
    int opcode() default -1;
    Shift shift() default Shift.NONE;
    String[] args() default {};
    String id() default "";
    boolean remap() default true;

    enum Shift { NONE, BEFORE, AFTER, BY }
}
