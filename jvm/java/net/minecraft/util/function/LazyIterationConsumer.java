package net.minecraft.util.function;

import java.util.Objects;
import java.util.function.Consumer;

/** Consumer whose caller may stop an iteration after each element. */
@FunctionalInterface
public interface LazyIterationConsumer<T> {
    NextIteration accept(T value);

    static <T> LazyIterationConsumer<T> forConsumer(Consumer<T> consumer) {
        Objects.requireNonNull(consumer, "consumer");
        return value -> {
            consumer.accept(value);
            return NextIteration.CONTINUE;
        };
    }

    static <T> NextIteration method_47542(Consumer<T> consumer, T value) {
        return forConsumer(consumer).accept(value);
    }

    enum NextIteration {
        CONTINUE,
        ABORT;

        public boolean shouldAbort() { return this == ABORT; }
    }
}
