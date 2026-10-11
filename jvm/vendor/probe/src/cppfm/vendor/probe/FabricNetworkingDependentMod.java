package cppfm.vendor.probe;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.concurrent.atomic.AtomicInteger;

import net.fabricmc.api.ModInitializer;

/** Calls both real typed C2S APIs after Knot remaps their intermediary ABI. */
public final class FabricNetworkingDependentMod implements ModInitializer {
    public interface ProbePayloadView {
        int value();
    }

    @Override
    public void onInitialize() {
        try {
            runProbe();
        } catch (ReflectiveOperationException failure) {
            Throwable cause = failure.getCause() == null ? failure : failure.getCause();
            throw new IllegalStateException("official Fabric networking probe failed", cause);
        }
    }

    private static void runProbe() throws ReflectiveOperationException {
        ClassLoader loader = FabricNetworkingDependentMod.class.getClassLoader();
        Class<?> payloadType = Class.forName("net.minecraft.network.packet.CustomPayload",
            true, loader);
        Class<?> identifierType = Class.forName("net.minecraft.util.Identifier", true, loader);
        Class<?> payloadIdType = Class.forName("net.minecraft.network.packet.CustomPayload$Id",
            true, loader);
        Class<?> codecType = Class.forName(
            "net.minecraft.network.codec.PacketCodec", true, loader);
        Class<?> registryType = Class.forName(
            "net.fabricmc.fabric.api.networking.v1.PayloadTypeRegistry", true, loader);
        Class<?> bridgeType = Class.forName(
            "cppfm.bridge.FabricNetworkingDispatch", true, loader);
        System.out.println("CPPFM_NETWORK_PHASE_RESOURCE=" +
            loader.getResource("net/minecraft/network/NetworkPhase.class"));
        System.out.println("CPPFM_INVALID_IDENTIFIER_RESOURCE=" +
            loader.getResource("net/minecraft/util/InvalidIdentifierException.class"));
        verifyPhase(loader, payloadType, identifierType, payloadIdType, codecType,
            registryType, bridgeType, "playC2S", "ServerPlayNetworking",
            "dispatchPlay", "play", 42, 5);
        verifyPhase(loader, payloadType, identifierType, payloadIdType, codecType,
            registryType, bridgeType, "configurationC2S",
            "ServerConfigurationNetworking", "dispatchConfiguration",
            "configuration", 77, 4);
        System.out.println("CPPFM_FABRIC_API_PLAY_NETWORKING_FUNCTIONAL");
        System.out.println("CPPFM_FABRIC_API_CONFIGURATION_NETWORKING_FUNCTIONAL");
        System.out.println("CPPFM_FABRIC_API_NETWORKING_FUNCTIONAL");
    }

    private static void verifyPhase(ClassLoader loader, Class<?> payloadType,
                                    Class<?> identifierType, Class<?> payloadIdType,
                                    Class<?> codecType, Class<?> registryType,
                                    Class<?> bridgeType, String registryAccessor,
                                    String networkingClass, String dispatchName,
                                    String phase, int expectedValue,
                                    int dispatchArguments)
        throws ReflectiveOperationException {
        String path = "fabric_api_" + phase + "_probe";
        Object identifier = identifierType.getMethod("of", String.class, String.class)
            .invoke(null, "cppfm", path);
        Object payloadId = payloadIdType.getConstructor(identifierType)
            .newInstance(identifier);
        Object codec = Proxy.newProxyInstance(codecType.getClassLoader(),
            new Class<?>[] {codecType}, codecInvocation(payloadType, payloadId));
        Object registry = registryType.getMethod(registryAccessor).invoke(null);
        invokeTwoArgumentMethod(registry, "register", payloadId, codec);

        AtomicInteger calls = new AtomicInteger();
        Class<?> networkingType = Class.forName(
            "net.fabricmc.fabric.api.networking.v1." + networkingClass,
            true, loader);
        Method registerReceiver = findTwoArgumentMethod(
            networkingType, "registerGlobalReceiver", payloadId, null);
        Class<?> receiverType = registerReceiver.getParameterTypes()[1];
        if (!receiverType.isInterface())
            throw new IllegalStateException("Fabric receiver is not an interface");
        Object receiver = Proxy.newProxyInstance(receiverType.getClassLoader(),
            new Class<?>[] {receiverType}, receiverInvocation(identifier,
                expectedValue, phase, calls));
        registerReceiver.invoke(null, payloadId, receiver);

        Method dispatch = findDispatchMethod(bridgeType, dispatchName,
            dispatchArguments);
        Object[] arguments = dispatchArguments == 5
            ? new Object[] {"cppfm:" + path, new byte[] {(byte) expectedValue},
                null, null, null}
            : new Object[] {"cppfm:" + path, new byte[] {(byte) expectedValue},
                null, null};
        Object dispatched = dispatch.invoke(null, arguments);
        if (!Boolean.TRUE.equals(dispatched) || calls.get() != 1) {
            throw new IllegalStateException("Fabric " + phase +
                " receiver dispatch mismatch: accepted=" + dispatched +
                " calls=" + calls.get());
        }
    }

    private static InvocationHandler codecInvocation(Class<?> payloadType,
                                                       Object payloadId) {
        return (proxy, method, arguments) -> {
            switch (method.getName()) {
                case "cast": return proxy;
                case "decode": {
                    Object buffer = arguments[0];
                    int value = ((Number) buffer.getClass()
                        .getMethod("readVarInt").invoke(buffer)).intValue();
                    return Proxy.newProxyInstance(payloadType.getClassLoader(),
                        new Class<?>[] {payloadType, ProbePayloadView.class},
                        payloadInvocation(payloadId, value));
                }
                case "toString": return "CppFabricMC Fabric API probe codec";
                case "hashCode": return System.identityHashCode(proxy);
                case "equals": return proxy == arguments[0];
                default:
                    throw new UnsupportedOperationException(
                        "unexpected Fabric probe codec method: " + method);
            }
        };
    }

    private static InvocationHandler payloadInvocation(Object payloadId, int value) {
        return (proxy, method, arguments) -> switch (method.getName()) {
            case "getId" -> payloadId;
            case "value" -> value;
            case "toString" -> "ProbePayload[value=" + value + "]";
            case "hashCode" -> System.identityHashCode(proxy);
            case "equals" -> proxy == (arguments == null ? null : arguments[0]);
            default -> throw new UnsupportedOperationException(
                "unexpected Fabric probe payload method: " + method);
        };
    }

    private static InvocationHandler receiverInvocation(Object expectedId,
                                                         int expectedValue,
                                                         String phase,
                                                         AtomicInteger calls) {
        return (proxy, method, arguments) -> {
            if (method.getName().equals("receive")) {
                Object payload = arguments[0];
                Object context = arguments[1];
                int value = ((Number) payload.getClass().getMethod("value")
                    .invoke(payload)).intValue();
                Object actualId = payload.getClass().getMethod("getId")
                    .invoke(payload);
                Object actualIdentifier = actualId.getClass().getMethod("id")
                    .invoke(actualId);
                Object server = context.getClass().getMethod("server").invoke(context);
                boolean contextMatches = server == null;
                if ("play".equals(phase)) {
                    contextMatches &= context.getClass().getMethod("player")
                        .invoke(context) == null;
                } else {
                    contextMatches &= context.getClass().getMethod("networkHandler")
                        .invoke(context) == null;
                }
                if (value != expectedValue || !expectedId.equals(actualIdentifier) ||
                    !contextMatches) {
                    throw new IllegalStateException("Fabric payload/context mismatch");
                }
                calls.incrementAndGet();
                return null;
            }
            if (method.getName().equals("toString")) return "CppFabricMC probe receiver";
            if (method.getName().equals("hashCode")) return System.identityHashCode(proxy);
            if (method.getName().equals("equals")) return proxy == arguments[0];
            throw new UnsupportedOperationException(
                "unexpected Fabric probe receiver method: " + method);
        };
    }

    private static void invokeTwoArgumentMethod(Object target, String name,
                                                Object first, Object second)
        throws ReflectiveOperationException {
        findTwoArgumentMethod(target.getClass(), name, first, second)
            .invoke(target, first, second);
    }

    private static Method findTwoArgumentMethod(Class<?> owner, String name,
                                                Object first, Object second) {
        for (Method method : owner.getMethods()) {
            if (!method.getName().equals(name) || method.getParameterCount() != 2)
                continue;
            Class<?>[] parameters = method.getParameterTypes();
            if (first != null && !parameters[0].isInstance(first)) continue;
            if (second != null && !parameters[1].isInstance(second)) continue;
            return method;
        }
        throw new IllegalStateException("missing compatible " + name + " method on " +
            owner.getName());
    }

    private static Method findDispatchMethod(Class<?> owner, String name,
                                            int parameterCount) {
        for (Method method : owner.getMethods()) {
            if (method.getName().equals(name) &&
                method.getParameterCount() == parameterCount &&
                method.getParameterTypes()[0] == String.class &&
                method.getParameterTypes()[1] == byte[].class) return method;
        }
        throw new IllegalStateException("FabricNetworkingDispatch." + name +
            " is missing");
    }
}
