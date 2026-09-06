package cppfm.transform;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.HashMap;
import java.util.Map;

/**
 * Reflection-backed original operation supplied to a generated
 * {@code @WrapOperation} call site.
 *
 * <p>MixinExtras deliberately exposes the original operation as an erased
 * varargs interface.  The structural transformer records the original member
 * reference in this object and lets the JVM's normal shadow class loader
 * resolve the declaring type at invocation time.  This keeps the bytecode
 * rewrite independent of a compile-time Minecraft class hierarchy.</p>
 */
public final class OriginalOperation implements Operation<Object> {
    private static final Map<Character, Class<?>> PRIMITIVES = new HashMap<>();
    static {
        PRIMITIVES.put('Z', boolean.class);
        PRIMITIVES.put('B', byte.class);
        PRIMITIVES.put('C', char.class);
        PRIMITIVES.put('S', short.class);
        PRIMITIVES.put('I', int.class);
        PRIMITIVES.put('J', long.class);
        PRIMITIVES.put('F', float.class);
        PRIMITIVES.put('D', double.class);
    }

    private final String owner;
    private final String member;
    private final String descriptor;
    private final int opcode;
    private final Object boundReceiver;

    public OriginalOperation(String owner, String member, String descriptor, int opcode) {
        this(null, owner, member, descriptor, opcode);
    }

    /** Construct an operation whose receiver is already bound by @WrapMethod. */
    public OriginalOperation(Object boundReceiver, String owner, String member,
                             String descriptor, int opcode) {
        this.boundReceiver = boundReceiver;
        this.owner = owner;
        this.member = member;
        this.descriptor = descriptor;
        this.opcode = opcode;
    }

    @Override
    public Object call(Object... arguments) {
        try {
            Descriptor.MethodDesc method = descriptor.startsWith("(") ? Descriptor.method(descriptor) : null;
            boolean field = method == null;
            boolean receiverArgument = opcode != 178 && opcode != 179 && opcode != 184;
            Object receiver = boundReceiver != null ? boundReceiver
                : receiverArgument && arguments.length > 0 ? arguments[0] : null;
            Object[] values = (boundReceiver != null || !receiverArgument)
                ? arguments : java.util.Arrays.copyOfRange(arguments, 1, arguments.length);
            Class<?> ownerClass = resolveOwner(receiver);
            if (field) return invokeField(ownerClass, receiver, values);
            if (member.equals("<init>")) return invokeConstructor(ownerClass, method, values);
            Method target = findMethod(ownerClass, member, method.arguments, receiver != null);
            if (target == null) throw new NoSuchMethodException(owner + "." + member + descriptor);
            target.setAccessible(true);
            return target.invoke(Modifier.isStatic(target.getModifiers()) ? null : receiver, values);
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("cannot invoke wrapped operation "
                + owner + "." + member + descriptor, failure);
        }
    }

    private Object invokeConstructor(Class<?> ownerClass, Descriptor.MethodDesc method, Object[] values)
            throws ReflectiveOperationException {
        Constructor<?> constructor = ownerClass.getDeclaredConstructor(parameterTypes(ownerClass.getClassLoader(), method.arguments));
        constructor.setAccessible(true);
        return constructor.newInstance(values);
    }

    private Object invokeField(Class<?> ownerClass, Object receiver, Object[] values)
            throws ReflectiveOperationException {
        Field target = findField(ownerClass, member);
        if (target == null) throw new NoSuchFieldException(owner + "." + member);
        target.setAccessible(true);
        boolean staticField = Modifier.isStatic(target.getModifiers());
        if (opcode == 178 || opcode == 180) return target.get(staticField ? null : receiver);
        if (values.length != 1) throw new IllegalArgumentException("field store expects one value");
        target.set(staticField ? null : receiver, values[0]);
        return null;
    }

    private Class<?> resolveOwner(Object receiver) throws ClassNotFoundException {
        if (receiver != null) {
            Class<?> receiverClass = receiver.getClass();
            Class<?> declared = findClassInHierarchy(receiverClass, owner.replace('/', '.'));
            if (declared != null) return declared;
        }
        ClassLoader loader = Thread.currentThread().getContextClassLoader();
        if (loader == null) loader = OriginalOperation.class.getClassLoader();
        return Class.forName(owner.replace('/', '.'), true, loader);
    }

    private static Class<?> findClassInHierarchy(Class<?> type, String binaryName) {
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            if (current.getName().equals(binaryName)) return current;
            for (Class<?> iface : current.getInterfaces()) {
                Class<?> found = findClassInHierarchy(iface, binaryName);
                if (found != null) return found;
            }
        }
        return null;
    }

    private static Method findMethod(Class<?> type, String name, java.util.List<Descriptor.Type> descriptorTypes,
                                     boolean instance) {
        Class<?>[] expected = parameterTypes(type.getClassLoader(), descriptorTypes);
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            for (Method candidate : current.getDeclaredMethods()) {
                if (!candidate.getName().equals(name) || candidate.getParameterCount() != expected.length) continue;
                if (sameTypes(candidate.getParameterTypes(), expected)) return candidate;
            }
        }
        for (Class<?> iface : type.getInterfaces()) {
            Method found = findMethod(iface, name, descriptorTypes, instance);
            if (found != null) return found;
        }
        return null;
    }

    private static Field findField(Class<?> type, String name) {
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            try { return current.getDeclaredField(name); }
            catch (NoSuchFieldException ignored) { }
        }
        for (Class<?> iface : type.getInterfaces()) {
            Field found = findField(iface, name);
            if (found != null) return found;
        }
        return null;
    }

    private static Class<?>[] parameterTypes(ClassLoader loader, java.util.List<Descriptor.Type> types) {
        Class<?>[] output = new Class<?>[types.size()];
        for (int i = 0; i < types.size(); ++i) output[i] = typeClass(loader, types.get(i).descriptor);
        return output;
    }

    private static Class<?> typeClass(ClassLoader loader, String descriptor) {
        if (descriptor.length() == 1) {
            Class<?> primitive = PRIMITIVES.get(descriptor.charAt(0));
            if (primitive != null) return primitive;
        }
        try {
            String binary = descriptor.startsWith("[")
                ? descriptor.replace('/', '.')
                : descriptor.substring(1, descriptor.length() - 1).replace('/', '.');
            return Class.forName(binary, false, loader == null ? OriginalOperation.class.getClassLoader() : loader);
        } catch (ClassNotFoundException failure) {
            throw new IllegalStateException("cannot resolve operation descriptor " + descriptor, failure);
        }
    }

    private static boolean sameTypes(Class<?>[] actual, Class<?>[] expected) {
        if (actual.length != expected.length) return false;
        for (int i = 0; i < actual.length; ++i) if (!actual[i].equals(expected[i])) return false;
        return true;
    }
}
