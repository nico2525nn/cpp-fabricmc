package net.fabricmc.fabric.api.event;

import java.util.function.Function;

public final class EventFactory {
    private EventFactory() {}
    public static <T> Event<T> createArrayBacked(Class<T> type, Function<T[], T> invokerFactory) {
        return new Event<>(type, invokerFactory);
    }
    public static <T> Event<T> createArrayBacked(Class<T> type, T emptyInvoker,
                                                  Function<T[], T> invokerFactory) {
        return new Event<>(null, type, emptyInvoker, invokerFactory);
    }
    public static <T> Event<T> createWithPhases(Class<T> type, Function<T[], T> invokerFactory, net.minecraft.util.Identifier... phases) {
        return new Event<>(type, invokerFactory);
    }
}
