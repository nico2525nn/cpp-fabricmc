package cppfm.bridge;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.Proxy;

import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.network.packet.Packet;
import net.minecraft.network.packet.s2c.common.CustomPayloadS2CPacket;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerConfigurationNetworkHandler;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;

/**
 * Adapts the native socket's raw custom-payload bytes to Fabric's typed
 * networking callbacks. Fabric 1.21.4 intentionally exposes registration and
 * packet creation APIs, but no public raw-payload receive method; the embedded
 * fallback API has such a method, while the official API routes through its
 * connection addon. Keep that ABI difference at this boundary.
 */
public final class FabricNetworkingDispatch {
    private static final String PLAY_API =
        "net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking";
    private static final String CONFIG_API =
        "net.fabricmc.fabric.api.networking.v1.ServerConfigurationNetworking";
    private static final String API_IMPL =
        "net.fabricmc.fabric.impl.networking.server.ServerNetworkingImpl";
    private static final String PAYLOAD_REGISTRY_IMPL =
        "net.fabricmc.fabric.impl.networking.PayloadTypeRegistryImpl";

    private FabricNetworkingDispatch() { }

    public static boolean dispatchPlay(String channel, byte[] bytes,
                                       MinecraftServer server,
                                       ServerPlayerEntity player,
                                       ServerPlayNetworkHandler handler) {
        Identifier id = Identifier.tryParse(channel);
        if (id == null) return false;
        ClassLoader loader = apiClassLoader(PLAY_API);
        Class<?> api = load(PLAY_API, loader);

        // Preserve the lightweight fallback implementation used by the
        // dependency-free runtime and its contract tests.
        Method fallback = findMethod(api, "receive", 5);
        if (fallback != null) {
            Object result = invoke(fallback, null, server, player, handler, id,
                new PacketByteBuf(bytes));
            return Boolean.TRUE.equals(result);
        }

        Object payload = decodePayload(loader, "PLAY_C2S", id,
            bytes, true);
        if (!(payload instanceof CustomPayload typedPayload)) return false;

        Object addon = handler == null ? null : findAddon(loader, handler);
        if (addon != null) {
            Method handle = findCompatibleMethod(addon.getClass(), "handle",
                typedPayload.getClass());
            if (handle != null && Boolean.TRUE.equals(invoke(handle, addon,
                    typedPayload)))
                return true;
        }

        Object receiver = findRegisteredHandler(loader, "PLAY", id);
        if (receiver == null) return false;
        dispatchRegistered(receiver, typedPayload, server, player, handler,
            null, loader, false);
        return true;
    }

    public static boolean dispatchConfiguration(
            String channel, byte[] bytes, MinecraftServer server,
            ServerConfigurationNetworkHandler handler) {
        Identifier id = Identifier.tryParse(channel);
        if (id == null) return false;
        ClassLoader loader = apiClassLoader(CONFIG_API);
        Class<?> api = load(CONFIG_API, loader);

        Object payload = decodePayload(loader, "CONFIGURATION_C2S", id,
            bytes, false);
        if (!(payload instanceof CustomPayload typedPayload)) return false;

        // The fallback's typed entry point already maintains per-handler and
        // global receiver maps. The official API has no equivalent public
        // dispatch entry point, so use its registered connection addon below.
        Method fallback = findMethod(api, "receive", 2);
        if (fallback != null && handler != null &&
            fallback.getParameterTypes()[0].isInstance(handler)) {
            Object result = invoke(fallback, null, handler, typedPayload);
            return Boolean.TRUE.equals(result);
        }

        Object addon = handler == null ? null : findAddon(loader, handler);
        if (addon != null) {
            Method handle = findCompatibleMethod(addon.getClass(), "handle",
                typedPayload.getClass());
            if (handle != null && Boolean.TRUE.equals(invoke(handle, addon,
                    typedPayload)))
                return true;
        }

        Object receiver = findRegisteredHandler(loader, "CONFIGURATION", id);
        if (receiver == null) return false;
        dispatchRegistered(receiver, typedPayload, server, null, null, handler,
            loader, true);
        return true;
    }

    /** Encodes an API-created S2C payload using the official phase registry. */
    public static byte[] encodeS2CPayload(CustomPayload payload, int phase) {
        if (payload == null || payload.getId() == null)
            throw new IllegalArgumentException("custom payload and id are required");
        String registryName = phase == 0 ? "CONFIGURATION_S2C" : "PLAY_S2C";
        ClassLoader loader = apiClassLoader(phase == 0 ? CONFIG_API : PLAY_API);
        Object codec = payloadCodec(loader, registryName, payload.getId().id());
        if (codec == null)
            throw new IllegalStateException("no S2C payload codec registered for " +
                payload.getId().id());
        PacketByteBuf buffer = phase == 0
            ? new PacketByteBuf() : new RegistryByteBuf();
        Method encode = findCompatibleMethod(codec.getClass(), "encode",
            buffer.getClass(), payload.getClass());
        if (encode == null)
            throw new IllegalStateException("registered S2C codec has no compatible encode method");
        invoke(encode, codec, buffer, payload);
        return buffer.toByteArray();
    }

    private static void dispatchRegistered(Object receiver,
                                           CustomPayload payload,
                                           MinecraftServer server,
                                           ServerPlayerEntity player,
                                           ServerPlayNetworkHandler playHandler,
                                           ServerConfigurationNetworkHandler configHandler,
                                           ClassLoader loader,
                                           boolean configuration) {
        String contextName = configuration
            ? CONFIG_API + "$Context" : PLAY_API + "$Context";
        Class<?> contextType = load(contextName, loader);
        Class<?> packetSenderType = load(
            "net.fabricmc.fabric.api.networking.v1.PacketSender", loader);
        Object sender = Proxy.newProxyInstance(loader,
            new Class<?>[] {packetSenderType},
            senderInvocation(playHandler, configHandler, configuration));
        Object context = Proxy.newProxyInstance(loader,
            new Class<?>[] {contextType},
            contextInvocation(server, player, playHandler, configHandler,
                sender, configuration));

        Method receive = findReceiverMethod(receiver.getClass(), contextType,
            payload.getClass());
        if (receive == null)
            throw new IllegalStateException("registered Fabric receiver has no receive(payload, context) method: " +
                receiver.getClass().getName());
        Runnable callback = () -> invoke(receive, receiver, payload, context);
        if (server == null) callback.run();
        else server.execute(callback);
    }

    private static InvocationHandler contextInvocation(
            MinecraftServer server, ServerPlayerEntity player,
            ServerPlayNetworkHandler playHandler,
            ServerConfigurationNetworkHandler configHandler,
            Object sender, boolean configuration) {
        return (proxy, method, arguments) -> switch (method.getName()) {
            case "server" -> server;
            case "player" -> player;
            case "networkHandler" -> configuration ? configHandler : playHandler;
            case "responseSender" -> sender;
            case "toString" -> "CppFabricMC networking context";
            case "hashCode" -> System.identityHashCode(proxy);
            case "equals" -> proxy == (arguments == null ? null : arguments[0]);
            default -> throw new UnsupportedOperationException(
                "unsupported Fabric networking context method: " + method);
        };
    }

    private static InvocationHandler senderInvocation(
            ServerPlayNetworkHandler playHandler,
            ServerConfigurationNetworkHandler configHandler,
            boolean configuration) {
        return (proxy, method, arguments) -> {
            Object first = arguments == null || arguments.length == 0
                ? null : arguments[0];
            switch (method.getName()) {
                case "createPacket":
                    if (!(first instanceof CustomPayload payload)) return null;
                    return new CustomPayloadS2CPacket(payload);
                case "sendPacket":
                    if (first instanceof CustomPayload payload) {
                        sendPacket(new CustomPayloadS2CPacket(payload),
                            playHandler, configHandler);
                    } else if (first instanceof Packet<?> packet) {
                        sendPacket(packet, playHandler, configHandler);
                    }
                    signalSuccess(arguments);
                    return null;
                case "disconnect":
                    if (first instanceof Text reason) {
                        if (configuration && configHandler != null)
                            configHandler.disconnect(reason);
                        else if (playHandler != null)
                            playHandler.disconnect(reason);
                    }
                    return null;
                case "toString": return "CppFabricMC native PacketSender";
                case "hashCode": return System.identityHashCode(proxy);
                case "equals": return proxy == (arguments == null ? null : arguments[0]);
                default:
                    throw new UnsupportedOperationException(
                        "unsupported Fabric PacketSender method: " + method);
            }
        };
    }

    private static void sendPacket(Packet<?> packet,
                                   ServerPlayNetworkHandler playHandler,
                                   ServerConfigurationNetworkHandler configHandler) {
        if (playHandler != null) playHandler.sendPacket(packet);
        else if (configHandler != null) configHandler.sendPacket(packet);
    }

    private static void signalSuccess(Object[] arguments) {
        if (arguments == null || arguments.length < 2 || arguments[1] == null) return;
        Object callbacks = arguments[1];
        Method success = findMethod(callbacks.getClass(), "onSuccess", 0);
        if (success != null) invoke(success, callbacks);
    }

    private static Object decodePayload(ClassLoader loader, String registryName,
                                        Identifier id, byte[] bytes,
                                        boolean registryAware) {
        Object codec = payloadCodec(loader, registryName, id);
        if (codec == null) return null;
        PacketByteBuf buffer = registryAware
            ? new RegistryByteBuf(bytes) : new PacketByteBuf(bytes);
        Method decode = findCompatibleMethod(codec.getClass(), "decode",
            buffer.getClass());
        if (decode == null)
            throw new IllegalStateException("registered payload codec has no compatible decode method: " +
                codec.getClass().getName());
        return invoke(decode, codec, buffer);
    }

    private static Object payloadCodec(ClassLoader loader, String registryName,
                                       Identifier id) {
        try {
            Class<?> registryOwner = load(
                "net.fabricmc.fabric.api.networking.v1.PayloadTypeRegistry", loader);
            String accessor = switch (registryName) {
                case "CONFIGURATION_C2S" -> "configurationC2S";
                case "CONFIGURATION_S2C" -> "configurationS2C";
                case "PLAY_C2S" -> "playC2S";
                case "PLAY_S2C" -> "playS2C";
                default -> throw new IllegalArgumentException("unknown payload registry " + registryName);
            };
            Object registry = registryOwner.getMethod(accessor).invoke(null);
            CustomPayload.Id<?> payloadId = new CustomPayload.Id<>(id);
            Method fallbackCodec = findMethod(registry.getClass(), "getCodec", 1);
            if (fallbackCodec != null)
                return invoke(fallbackCodec, registry, payloadId);

            Class<?> registryImpl = load(PAYLOAD_REGISTRY_IMPL, loader);
            Field field = registryImpl.getField(registryName);
            Object implementation = field.get(null);
            Method get = findCompatibleMethod(registryImpl, "get", id.getClass());
            if (get == null)
                throw new IllegalStateException("Fabric payload registry has no get(Identifier)");
            Object type = invoke(get, implementation, id);
            if (type == null) return null;
            Method codecMethod = findMethod(type.getClass(), "codec", 0);
            return codecMethod == null ? null : invoke(codecMethod, type);
        } catch (ReflectiveOperationException failure) {
            throw reflectionFailure("resolve Fabric payload codec", failure);
        }
    }

    private static Object findRegisteredHandler(ClassLoader loader,
                                                String registryName,
                                                Identifier id) {
        try {
            Class<?> implementation = load(API_IMPL, loader);
            Object registry = implementation.getField(registryName).get(null);
            Method getHandler = findCompatibleMethod(registry.getClass(),
                "getHandler", id.getClass());
            return getHandler == null ? null : invoke(getHandler, registry, id);
        } catch (ReflectiveOperationException missingOfficialImplementation) {
            return null;
        }
    }

    private static Object findAddon(ClassLoader loader, Object networkHandler) {
        try {
            Class<?> implementation = load(API_IMPL, loader);
            for (Method method : implementation.getMethods()) {
                if (!Modifier.isStatic(method.getModifiers()) ||
                    !method.getName().equals("getAddon") ||
                    method.getParameterCount() != 1 ||
                    !method.getParameterTypes()[0].isInstance(networkHandler)) continue;
                return invoke(method, null, networkHandler);
            }
        } catch (RuntimeException unavailableAddon) {
            // Direct global dispatch below remains usable when the host's
            // network-handler mixin has not supplied Fabric's addon yet.
        }
        return null;
    }

    private static Method findReceiverMethod(Class<?> receiverType,
                                             Class<?> contextType,
                                             Class<?> payloadType) {
        for (Method method : receiverType.getMethods()) {
            if (!method.getName().equals("receive") || method.getParameterCount() != 2)
                continue;
            if (method.getParameterTypes()[0].isAssignableFrom(payloadType) &&
                method.getParameterTypes()[1].isAssignableFrom(contextType))
                return method;
        }
        return null;
    }

    private static ClassLoader apiClassLoader(String className) {
        try {
            Class<?> type = Class.forName(className, false,
                FabricNetworkingDispatch.class.getClassLoader());
            return type.getClassLoader();
        } catch (ClassNotFoundException failure) {
            throw new IllegalStateException("Fabric networking API is not available: " +
                className, failure);
        }
    }

    private static Class<?> load(String name, ClassLoader loader) {
        try {
            return Class.forName(name, true, loader);
        } catch (ClassNotFoundException failure) {
            throw new IllegalStateException("required Fabric networking class is missing: " +
                name, failure);
        }
    }

    private static Method findMethod(Class<?> owner, String name, int parameterCount) {
        for (Method method : owner.getMethods()) {
            if (method.getName().equals(name) &&
                method.getParameterCount() == parameterCount) return method;
        }
        return null;
    }

    private static Method findCompatibleMethod(Class<?> owner, String name,
                                               Class<?>... argumentTypes) {
        for (Method method : owner.getMethods()) {
            if (!method.getName().equals(name) ||
                method.getParameterCount() != argumentTypes.length) continue;
            Class<?>[] parameters = method.getParameterTypes();
            boolean compatible = true;
            for (int index = 0; index < parameters.length; ++index) {
                if (!parameters[index].isAssignableFrom(argumentTypes[index])) {
                    compatible = false;
                    break;
                }
            }
            if (compatible) return method;
        }
        return null;
    }

    private static Object invoke(Method method, Object receiver, Object... arguments) {
        try {
            if (!method.canAccess(receiver) && !method.trySetAccessible())
                throw new IllegalStateException("Fabric networking method is inaccessible: " + method);
            return method.invoke(receiver, arguments);
        } catch (IllegalAccessException failure) {
            throw reflectionFailure("invoke Fabric networking method " + method, failure);
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();
            if (cause instanceof RuntimeException runtime) throw runtime;
            if (cause instanceof Error error) throw error;
            throw new IllegalStateException("Fabric networking callback failed", cause);
        }
    }

    private static IllegalStateException reflectionFailure(String action,
                                                           ReflectiveOperationException failure) {
        return new IllegalStateException("cannot " + action, failure);
    }
}
