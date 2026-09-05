package cppfm.corpus.fixture08;

import cppfm.bridge.NativeBridge;
import java.io.InputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import net.fabricmc.api.DedicatedServerModInitializer;

/** Corpus 08: resource parsing plus access-flag transformation and execution. */
public final class AccessWidener implements DedicatedServerModInitializer {
    private static final String TARGET = "cppfm.corpus.fixture08.AccessTarget";

    @Override
    public void onInitializeServer() {
        boolean ok = false;
        try (InputStream input = AccessWidener.class.getResourceAsStream(
                "/fixture08.accesswidener");
             InputStream targetInput = AccessWidener.class.getClassLoader().getResourceAsStream(
                 "cppfm/corpus/fixture08/AccessTarget.class")) {
            if (input == null || targetInput == null) throw new IllegalStateException("fixture resource missing");
            cppfm.transform.AccessWidener widener =
                cppfm.transform.AccessWidener.parse(input.readAllBytes());
            cppfm.transform.AccessWidenerTransformer transformer =
                new cppfm.transform.AccessWidenerTransformer("named", true);
            transformer.register(widener);
            byte[] original = targetInput.readAllBytes();
            byte[] transformed = transformer.transform(
                TARGET, original, new cppfm.transform.TransformContext(TARGET, original, true));
            cppfm.transform.ClassFileIntrospection.validate(transformed);
            Class<?> type = new BytesLoader().define(TARGET, transformed);
            Constructor<?> constructor = type.getConstructor();
            Object instance = constructor.newInstance();
            Field mutable = type.getField("mutableValue");
            mutable.setInt(instance, 23);
            Method readMutable = type.getMethod("readMutable");
            Method secret = type.getMethod("secret", int.class);
            ok = ((Integer) readMutable.invoke(instance)) == 23
                && ((Integer) secret.invoke(instance, 3)) == 7;
        } catch (Exception ignored) {
            ok = false;
        }
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=08 status=" + (ok ? "PASS" : "FAIL")
            + " phase=access-widener-runtime");
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=08 status=" + (ok ? "PASS" : "FAIL")
            + " phase=metadata-resource");
    }

    private static final class BytesLoader extends ClassLoader {
        Class<?> define(String name, byte[] bytes) {
            return defineClass(name, bytes, 0, bytes.length);
        }
    }
}
