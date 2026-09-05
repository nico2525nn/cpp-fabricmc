package cppfm.bridge;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * A deliberately bounded execution-shell dispatcher.  It supports the
 * topology that cppfm exposes explicitly (HEAD/TAIL/RETURN and simple
 * overwrite); it is not a bytecode transformer and reports unsupported
 * injection points instead of pretending to apply them.
 */
public final class MixinHooks {
    private static final Map<String, List<Handler>> HANDLERS = new ConcurrentHashMap<>();
    private static final Map<String, List<Handler>> OVERWRITES = new ConcurrentHashMap<>();
    private static final List<String> WARNINGS = new ArrayList<>();

    private MixinHooks() {}

    public static synchronized void clear() {
        HANDLERS.clear();
        OVERWRITES.clear();
        WARNINGS.clear();
    }

    public static synchronized List<String> warnings() { return List.copyOf(WARNINGS); }

    public static void registerMixinClass(Class<?> mixinClass) {
        Mixin mixin = mixinClass.getAnnotation(Mixin.class);
        if (mixin == null) return;
        List<String> targets = new ArrayList<>();
        for (Class<?> target : mixin.value()) targets.add(target.getName());
        for (String target : mixin.targets()) if (!target.isEmpty()) targets.add(target.replace('/', '.'));
        if (targets.isEmpty()) {
            warn("mixin has no target: " + mixinClass.getName());
            return;
        }
        Object instance = null;
        for (Method method : mixinClass.getDeclaredMethods()) {
            Inject inject = method.getAnnotation(Inject.class);
            if (inject != null) {
                if (inject.at().value().equals("HEAD") || inject.at().value().equals("TAIL") ||
                    inject.at().value().equals("RETURN")) {
                    for (String methodName : inject.method()) {
                        for (String target : targets) {
                            add(HANDLERS, key(target, baseMethod(methodName)),
                                new Handler(method, instance, inject.at().value(), inject.cancellable(),
                                            descriptor(methodName)));
                            NativeBridge.nativeRegisterTransformedMethod(
                                target.replace('.', '/'), baseMethod(methodName), descriptor(methodName));
                        }
                    }
                } else {
                    warn("unsupported @Inject point " + inject.at().value() + " in " + method);
                }
            }
            Overwrite overwrite = method.getAnnotation(Overwrite.class);
            if (overwrite != null) {
                for (String target : targets)
                    add(OVERWRITES, key(target, method.getName()),
                        new Handler(method, instance, "OVERWRITE", true, "*"));
                for (String target : targets)
                    NativeBridge.nativeRegisterTransformedMethod(target.replace('.', '/'), method.getName(), "*");
            }
        }
        // Static callbacks need no instance.  For instance callbacks retry
        // with a no-arg constructor; ordinary mixins often use static helpers.
        if (needsInstance(mixinClass)) {
            try { instance = mixinClass.getDeclaredConstructor().newInstance(); }
            catch (Throwable failure) { warn("cannot instantiate mixin " + mixinClass.getName() + ": " + failure); }
            replaceNullInstances(mixinClass, instance);
        }
    }

    private static boolean needsInstance(Class<?> type) {
        for (Method method : type.getDeclaredMethods())
            if (!Modifier.isStatic(method.getModifiers()) &&
                (method.isAnnotationPresent(Inject.class) || method.isAnnotationPresent(Overwrite.class))) return true;
        return false;
    }

    private static void replaceNullInstances(Class<?> type, Object instance) {
        for (List<Handler> handlers : HANDLERS.values())
            for (Handler handler : handlers) if (handler.method.getDeclaringClass() == type) handler.instance = instance;
        for (List<Handler> handlers : OVERWRITES.values())
            for (Handler handler : handlers) if (handler.method.getDeclaringClass() == type) handler.instance = instance;
    }

    private static void add(Map<String, List<Handler>> map, String key, Handler handler) {
        handler.method.setAccessible(true);
        map.computeIfAbsent(key, ignored -> new ArrayList<>()).add(handler);
        map.get(key).sort(Comparator.comparingInt(h -> h.method.getDeclaringClass().getAnnotation(Mixin.class).priority()));
    }

    public static CallbackInfo invokeHead(Object target, String method, Object... args) {
        CallbackInfo info = new CallbackInfo(method + ":HEAD", true);
        invoke(target, method, null, "HEAD", args, info);
        return info;
    }

    public static <T> CallbackInfoReturnable<T> invokeHeadReturn(Object target, String method,
                                                                  T value, Object... args) {
        CallbackInfoReturnable<T> info = new CallbackInfoReturnable<>(method + ":HEAD", true, value);
        invoke(target, method, null, "HEAD", args, info);
        return info;
    }

    public static <T> CallbackInfoReturnable<T> invokeReturn(Object target, String method,
                                                               T value, Object... args) {
        CallbackInfoReturnable<T> info = new CallbackInfoReturnable<>(method + ":RETURN", true, value);
        invoke(target, method, null, "RETURN", args, info);
        return info;
    }

    public static CallbackInfo invokeTail(Object target, String method, Object... args) {
        CallbackInfo info = new CallbackInfo(method + ":TAIL", true);
        invoke(target, method, null, "TAIL", args, info);
        return info;
    }

    public static <T> CallbackInfoReturnable<T> invokeTailReturn(Object target, String method,
                                                                  T value, Object... args) {
        CallbackInfoReturnable<T> info = new CallbackInfoReturnable<>(method + ":TAIL", true, value);
        invoke(target, method, null, "TAIL", args, info);
        return info;
    }

    public static <T> T invokeOverwrite(Object target, String method, Object... args) {
        List<Handler> handlers = findHandlers(OVERWRITES, target, method, null);
        if (handlers == null || handlers.isEmpty()) return null;
        Object[] callArgs = args;
        for (Handler handler : handlers) {
            try {
                Object result = handler.method.invoke(handler.instance, callArgs);
                @SuppressWarnings("unchecked") T cast = (T) result;
                return cast;
            } catch (InvocationTargetException failure) {
                throw sneaky(failure.getCause());
            } catch (ReflectiveOperationException failure) {
                throw new IllegalStateException("mixin overwrite failed: " + handler.method, failure);
            }
        }
        return null;
    }

    private static void invoke(Object target, String method, String descriptor, String point,
                               Object[] args, CallbackInfo info) {
        List<Handler> handlers = findHandlers(HANDLERS, target, method, descriptor);
        if (handlers == null) return;
        for (Handler handler : handlers) {
            if (!handler.point.equals(point)) continue;
            try {
                CallbackInfo callbackInfo = copyInfo(info, handler.cancellable);
                Object[] callArgs = callbackArguments(handler.method, args, callbackInfo);
                handler.method.invoke(handler.instance, callArgs);
                if (callbackInfo.isCancelled()) {
                    if (callbackInfo instanceof CallbackInfoReturnable<?> local &&
                        info instanceof CallbackInfoReturnable<?> outer) {
                        @SuppressWarnings("unchecked")
                        CallbackInfoReturnable<Object> mutable =
                            (CallbackInfoReturnable<Object>) outer;
                        mutable.setReturnValue(local.getReturnValue());
                    } else if (!info.isCancelled()) {
                        info.cancel();
                    }
                }
            } catch (InvocationTargetException failure) {
                throw sneaky(failure.getCause());
            } catch (ReflectiveOperationException failure) {
                throw new IllegalStateException("mixin callback failed: " + handler.method, failure);
            }
        }
    }

    private static CallbackInfo copyInfo(CallbackInfo source, boolean cancellable) {
        if (source instanceof CallbackInfoReturnable<?> returnable)
            return new CallbackInfoReturnable<>(source.getId(), cancellable,
                                                returnable.getReturnValue());
        return new CallbackInfo(source.getId(), cancellable);
    }

    private static Object[] callbackArguments(Method method, Object[] args, CallbackInfo info) {
        Class<?>[] types = method.getParameterTypes();
        if (types.length == 0) return new Object[0];
        Object[] output = new Object[types.length];
        int ordinary = types[types.length - 1].isAssignableFrom(info.getClass()) ? types.length - 1 : types.length;
        for (int i = 0; i < ordinary && i < args.length; ++i) output[i] = args[i];
        if (ordinary < types.length) output[types.length - 1] = info;
        return output;
    }

    private static String key(Object target, String method) {
        return key(target.getClass().getName(), method);
    }
    private static String key(String target, String method) { return target + "#" + method; }
    private static List<Handler> findHandlers(Map<String, List<Handler>> map,
                                              Object target, String method,
                                              String descriptor) {
        if (target == null) return List.of();
        for (Class<?> type = target.getClass(); type != null; type = type.getSuperclass()) {
            List<Handler> handlers = map.get(key(type.getName(), method));
            if (handlers == null || handlers.isEmpty()) continue;
            if (descriptor == null) return handlers;
            List<Handler> matching = new ArrayList<>();
            for (Handler handler : handlers)
                if (handler.descriptor.equals("*") || handler.descriptor.equals(descriptor))
                    matching.add(handler);
            if (!matching.isEmpty()) return matching;
        }
        return List.of();
    }
    private static String baseMethod(String method) {
        int descriptor = method.indexOf('(');
        return descriptor < 0 ? method : method.substring(0, descriptor);
    }
    private static String descriptor(String method) {
        int descriptor = method.indexOf('(');
        return descriptor < 0 ? "*" : method.substring(descriptor);
    }
    private static synchronized void warn(String warning) {
        WARNINGS.add(warning);
        try { NativeBridge.nativeLog("WARN", warning); } catch (Throwable ignored) {}
    }
    private static RuntimeException sneaky(Throwable failure) {
        if (failure instanceof RuntimeException runtime) return runtime;
        if (failure instanceof Error error) throw error;
        return new RuntimeException(failure);
    }

    private static final class Handler {
        private final Method method;
        private Object instance;
        private final String point;
        @SuppressWarnings("unused") private final boolean cancellable;
        private final String descriptor;
        private Handler(Method method, Object instance, String point, boolean cancellable,
                        String descriptor) {
            this.method = method; this.instance = instance; this.point = point;
            this.cancellable = cancellable; this.descriptor = descriptor;
        }
    }
}
