package cppfm.accesswidener_fixture;

import cppfm.transform.AccessWidener;
import cppfm.transform.AccessWidenerTransformer;
import cppfm.transform.DescriptorResolver;
import cppfm.transform.ClassFileTransformer;
import cppfm.transform.MixinClassTransformer;
import cppfm.transform.TransformContext;
import cppfm.transform.TransformException;

import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import javax.tools.JavaCompiler;
import javax.tools.ToolProvider;

/** Focused byte-and-execution proof for Fabric Access Widener v2 semantics. */
public final class AccessWidenerContractTest {
    private static final String TARGET = "cppfm.corpus.fixture08.AccessTarget";
    private static final String TARGET_INTERNAL = "cppfm/corpus/fixture08/AccessTarget";

    public static void main(String[] args) throws Exception {
        byte[] original = resource("cppfm/corpus/fixture08/AccessTarget.class");
        byte[] widenerBytes = resource("fixture08.accesswidener");
        String metadata = new String(resource("fabric.mod.json"), StandardCharsets.UTF_8);
        assert metadata.contains("\"accessWidener\": \"fixture08.accesswidener\"");
        AccessWidener widener = AccessWidener.parse(widenerBytes);
        assert widener.getVersion() == 2;
        assert widener.getNamespace().equals("named");
        assert widener.getTargets().stream().anyMatch(AccessWidener.Target::transitive);

        namespaceBoundary();
        parserGuards();
        resolverCase(original);

        AccessWidenerTransformer transformer = new AccessWidenerTransformer("named", true);
        transformer.register(widener);
        TransformContext context = new TransformContext(TARGET, original, true);
        byte[] transformed = transformChain(TARGET, original, context,
            new MixinClassTransformer(true), transformer);
        assert !Arrays.equals(original, transformed);
        assert context.getChangedMethods().contains("secret(I)I");
        assert context.getChangedMethods().contains("overridable(I)I");

        Class<?> target = new BytesLoader().define(TARGET, transformed);
        assert Modifier.isPublic(target.getModifiers());
        assert !Modifier.isFinal(target.getModifiers());
        Constructor<?> constructor = target.getConstructor();
        Object instance = constructor.newInstance();

        Field mutable = target.getField("mutableValue");
        assert Modifier.isPublic(mutable.getModifiers());
        assert !Modifier.isFinal(mutable.getModifiers());
        mutable.setInt(instance, 23);
        assert ((Integer) target.getMethod("readMutable").invoke(instance)) == 23;

        Field accessibleFinal = target.getField("accessibleFinal");
        assert Modifier.isPublic(accessibleFinal.getModifiers());
        assert Modifier.isFinal(accessibleFinal.getModifiers());

        Method secret = target.getMethod("secret", int.class);
        assert Modifier.isPublic(secret.getModifiers());
        assert Modifier.isFinal(secret.getModifiers());
        assert ((Integer) secret.invoke(instance, 3)) == 7;

        Method overridable = target.getDeclaredMethod("overridable", int.class);
        assert Modifier.isProtected(overridable.getModifiers());
        assert !Modifier.isFinal(overridable.getModifiers());
        assert ((Integer) target.getMethod("callOverridable", int.class).invoke(instance, 3)) == 11;

        byte[] child = compileChild(transformed);
        BytesLoader loader = new BytesLoader();
        loader.define(TARGET, transformed);
        Class<?> childType = loader.define("cppfm.corpus.fixture08.AccessChild", child);
        Object childInstance = childType.getConstructor().newInstance();
        assert ((Integer) childType.getMethod("callOverridable", int.class)
            .invoke(childInstance, 3)) == 103;

        failClosed(original);
        System.out.println("ACCESS WIDENER CONTRACT PASS");
    }

    private static byte[] transformChain(String binaryName, byte[] original,
                                         TransformContext context,
                                         ClassFileTransformer... transformers) {
        byte[] current = original.clone();
        for (ClassFileTransformer transformer : transformers) {
            byte[] next = transformer.transform(binaryName, current.clone(), context);
            if (next == null) throw new AssertionError("transformer returned null");
            current = next.clone();
        }
        return current;
    }

    private static void namespaceBoundary() {
        AccessWidenerTransformer transformer = new AccessWidenerTransformer("named", true);
        boolean rejected = false;
        try {
            transformer.register("accessWidener v2 intermediary\n"
                + "accessible class " + TARGET_INTERNAL + "\n");
        } catch (TransformException expected) {
            rejected = true;
        }
        assert rejected : "intermediary input must not cross a named runtime boundary";
    }

    private static void parserGuards() {
        AccessWidener v1 = AccessWidener.parse("accessWidener v1 named\n"
            + "accessible class " + TARGET_INTERNAL + "\n");
        assert v1.getVersion() == 1;

        boolean rejected = false;
        try {
            AccessWidener.parse("accessWidener v2 named\n accessible class " + TARGET_INTERNAL + "\n");
        } catch (TransformException expected) {
            rejected = true;
        }
        assert rejected : "v2 leading whitespace must be rejected";

        rejected = false;
        try {
            AccessWidener.parse("accessWidener v2 named\n"
                + "extendable field " + TARGET_INTERNAL + " mutableValue I\n");
        } catch (TransformException expected) {
            rejected = true;
        }
        assert rejected : "extendable fields must be rejected";
    }

    private static void resolverCase(byte[] original) {
        AccessWidenerTransformer transformer = new AccessWidenerTransformer(
            "named",
            new DescriptorResolver() {
                @Override
                public String resolveOwner(String owner) {
                    return owner.replace("fixture08/AccessTargetAlias", TARGET_INTERNAL);
                }

                @Override
                public String resolveDescriptor(String descriptor) {
                    return descriptor.replace("Lfixture08/AliasString;", "Ljava/lang/String;");
                }
            },
            true);
        transformer.register("accessWidener v2 named\n"
            + "accessible method fixture08/AccessTargetAlias <init> ()V\n"
            + "accessible method fixture08/AccessTargetAlias descriptorTarget "
            + "(Lfixture08/AliasString;)I\n");
        byte[] transformed = transformer.transform(TARGET, original,
            new TransformContext(TARGET, original, true));
        assert !Arrays.equals(original, transformed);
        try {
            Class<?> type = new BytesLoader().define(TARGET, transformed);
            Constructor<?> constructor = type.getConstructor();
            Object instance = constructor.newInstance();
            assert ((Integer) type.getMethod("descriptorTarget", String.class)
                .invoke(instance, "abc")) == 3;
        } catch (ReflectiveOperationException failure) {
            throw new AssertionError(failure);
        }
    }

    private static void failClosed(byte[] original) {
        AccessWidenerTransformer strict = new AccessWidenerTransformer("named", true);
        strict.register("accessWidener v2 named\n"
            + "accessible field " + TARGET_INTERNAL + " missing I\n");
        boolean rejected = false;
        try {
            strict.transform(TARGET, original, new TransformContext(TARGET, original, true));
        } catch (TransformException expected) {
            rejected = true;
        }
        assert rejected : "missing matching member must fail closed";

        AccessWidenerTransformer lenient = new AccessWidenerTransformer("named", false);
        lenient.register("accessWidener v2 named\n"
            + "accessible field " + TARGET_INTERNAL + " missing I\n");
        TransformContext context = new TransformContext(TARGET, original, false);
        assert Arrays.equals(original, lenient.transform(TARGET, original, context));
        assert !context.getDiagnostics().isEmpty();
    }

    private static byte[] compileChild(byte[] transformedTarget) throws IOException {
        JavaCompiler compiler = ToolProvider.getSystemJavaCompiler();
        if (compiler == null) throw new AssertionError("JDK compiler is unavailable");
        Path directory = Files.createTempDirectory("cppfm-aw-child-");
        try {
            Path target = directory.resolve("cppfm/corpus/fixture08/AccessTarget.class");
            Files.createDirectories(target.getParent());
            Files.write(target, transformedTarget);
            Path source = directory.resolve("AccessChild.java");
            Files.writeString(source, """
                package cppfm.corpus.fixture08;
                public final class AccessChild extends AccessTarget {
                    public AccessChild() { super(); }
                    @Override protected int overridable(int value) { return 100 + value; }
                }
                """, StandardCharsets.UTF_8);
            int exit = compiler.run(null, null, null, "--release", "17",
                "-classpath", directory.toString(), "-d", directory.toString(), source.toString());
            if (exit != 0) throw new AssertionError("javac child exit " + exit);
            return Files.readAllBytes(directory.resolve(
                "cppfm/corpus/fixture08/AccessChild.class"));
        } finally {
            deleteTree(directory);
        }
    }

    private static void deleteTree(Path root) throws IOException {
        try (var paths = Files.walk(root)) {
            paths.sorted(java.util.Comparator.reverseOrder()).forEach(path -> {
                try {
                    Files.deleteIfExists(path);
                } catch (IOException failure) {
                    throw new RuntimeException(failure);
                }
            });
        }
    }

    private static byte[] resource(String name) throws IOException {
        try (InputStream input = AccessWidenerContractTest.class.getClassLoader()
            .getResourceAsStream(name)) {
            if (input == null) throw new IOException("missing test resource: " + name);
            return input.readAllBytes();
        }
    }

    private static final class BytesLoader extends ClassLoader {
        Class<?> define(String name, byte[] bytes) {
            return defineClass(name, bytes, 0, bytes.length);
        }
    }
}
