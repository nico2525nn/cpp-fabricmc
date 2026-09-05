package cppfm.transformer_fixture;

import cppfm.loader.KnotClassLoader;
import cppfm.loader.TransformingClassLoader;
import cppfm.transform.MixinClassTransformer;
import cppfm.transform.MixinDispatch;
import cppfm.transform.TransformContext;
import cppfm.transform.TransformException;
import cppfm.transform.TransformResult;

import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.net.URL;
import java.util.Map;

/** Process-level contract test; run with assertions enabled. */
public final class TransformerContractTest {
    public static void main(String[] args) throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/BranchTarget.class");
        byte[] mixin = resource("cppfm/transformer_fixture/BranchMixin.class");
        MixinDispatch.clear();
        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin("cppfm.transformer_fixture.BranchMixin", mixin);
        TransformContext context = new TransformContext("cppfm.transformer_fixture.BranchTarget", target, true);
        byte[] transformed = transformer.transform("cppfm.transformer_fixture.BranchTarget", target, context);
        assert !java.util.Arrays.equals(target, transformed) : context.getDiagnostics();
        assert context.getChangedMethods().contains("compute(I)I");
        assert context.getChangedMethods().contains("call(I)I");
        assert context.getChangedMethods().contains("constant()I");
        assert MixinDispatch.isManualHookSuppressed("cppfm/transformer_fixture/BranchTarget", "compute", "(I)I");

        ByteArrayLoader loader = new ByteArrayLoader();
        Class<?> type = loader.define("cppfm.transformer_fixture.BranchTarget", transformed);
        Object instance = type.getConstructor().newInstance();
        PrintStream savedOut = System.out;
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        System.setOut(new PrintStream(captured));
        try {
            assert ((Integer) type.getMethod("compute", int.class).invoke(instance, -1)) == 7;
            assert ((Integer) type.getMethod("compute", int.class).invoke(instance, 2)) == 3;
        } finally {
            System.setOut(savedOut);
        }
        String log = captured.toString(java.nio.charset.StandardCharsets.UTF_8);
        assert occurrences(log, "HEAD_ONCE") == 2 : log;
        assert occurrences(log, "RETURN_ONCE") == 1 : log;
        assert ((Integer) type.getMethod("call", int.class).invoke(instance, 1)) == 13;
        assert ((Integer) type.getMethod("constant").invoke(instance)) == 7;

        phase7Cases();

        URL[] empty = new URL[0];
        KnotClassLoader knot = new KnotClassLoader(empty, TransformerContractTest.class.getClassLoader());
        TransformResult result = knot.transformResult("cppfm.transformer_fixture.BranchTarget", target);
        assert result.getOriginalSha256().length() == 64;
        assert knot.getTransformedMethodHashes() != null;
        if (!knot.getTransformedMethodDescriptors().isEmpty()) throw new AssertionError("unexpected automatic mixin");

        System.out.println("TRANSFORMER CONTRACT PASS");
    }

    private static void phase7Cases() throws Exception {
        advancedInjectionCases();
        crossMixinOrderingCase();
        constructorCases();
    }

    private static void advancedInjectionCases() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/AdvancedTarget.class");
        byte[] mixin = resource("cppfm/transformer_fixture/AdvancedMixin.class");
        MixinDispatch.clear();
        Class<?> mixinType = Class.forName("cppfm.transformer_fixture.AdvancedMixin");
        mixinType.getField("arrayAtHits").setInt(null, 0);
        mixinType.getField("sliceOrdinalHits").setInt(null, 0);
        mixinType.getField("constantHits").setInt(null, 0);
        mixinType.getField("variableHits").setInt(null, 0);
        mixinType.getField("capturedLocal").setInt(null, 0);
        mixinType.getField("jumpHits").setInt(null, 0);

        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin("cppfm.transformer_fixture.AdvancedMixin", mixin);
        TransformContext context = new TransformContext(
            "cppfm.transformer_fixture.AdvancedTarget", target, true);
        byte[] transformed = transformer.transform(
            "cppfm.transformer_fixture.AdvancedTarget", target, context);
        assert !java.util.Arrays.equals(target, transformed) : context.getDiagnostics();

        ByteArrayLoader loader = new ByteArrayLoader();
        Class<?> type = loader.define("cppfm.transformer_fixture.AdvancedTarget", transformed);
        Object instance = type.getConstructor().newInstance();
        assert ((Integer) type.getMethod("sliced", int.class).invoke(instance, 3)) == 15;
        assert ((Integer) type.getMethod("constants").invoke(instance)) == 106;
        assert ((Integer) type.getMethod("modifyVariable", int.class).invoke(instance, 1)) == 12;
        assert ((Integer) type.getMethod("capture", int.class).invoke(instance, 3)) == 7;
        assert ((Integer) type.getMethod("control", int.class).invoke(instance, 4)) == 6;

        assert mixinType.getField("arrayAtHits").getInt(null) == 2;
        assert mixinType.getField("sliceOrdinalHits").getInt(null) == 1;
        assert mixinType.getField("constantHits").getInt(null) == 1;
        assert mixinType.getField("variableHits").getInt(null) == 1;
        assert mixinType.getField("capturedLocal").getInt(null) == 6;
        assert mixinType.getField("jumpHits").getInt(null) > 0;
    }

    private static void crossMixinOrderingCase() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/OrderTarget.class");
        MixinDispatch.clear();
        Class.forName("cppfm.transformer_fixture.OrderRecorder").getMethod("reset").invoke(null);
        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin("cppfm.transformer_fixture.OrderLowMixin",
            resource("cppfm/transformer_fixture/OrderLowMixin.class"));
        transformer.registerMixin("cppfm.transformer_fixture.OrderHighMixin",
            resource("cppfm/transformer_fixture/OrderHighMixin.class"));
        TransformContext context = new TransformContext(
            "cppfm.transformer_fixture.OrderTarget", target, true);
        byte[] transformed = transformer.transform(
            "cppfm.transformer_fixture.OrderTarget", target, context);
        Class<?> type = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.OrderTarget", transformed);
        type.getMethod("run").invoke(type.getConstructor().newInstance());
        String log = (String) Class.forName("cppfm.transformer_fixture.OrderRecorder")
            .getField("LOG").get(null).toString();
        // MixinInfo.compareTo applies lower priorities first; callbacks at the
        // same point therefore retain that application order.
        assert log.equals("LH") : log;
    }

    private static void constructorCases() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/ConstructorTarget.class");
        byte[] safeMixin = resource("cppfm/transformer_fixture/ConstructorReturnMixin.class");
        Class<?> safeType = Class.forName("cppfm.transformer_fixture.ConstructorReturnMixin");
        safeType.getField("hits").setInt(null, 0);
        MixinDispatch.clear();
        MixinClassTransformer safeTransformer = new MixinClassTransformer(true);
        safeTransformer.registerMixin("cppfm.transformer_fixture.ConstructorReturnMixin", safeMixin);
        TransformContext safeContext = new TransformContext(
            "cppfm.transformer_fixture.ConstructorTarget", target, true);
        byte[] transformed = safeTransformer.transform(
            "cppfm.transformer_fixture.ConstructorTarget", target, safeContext);
        Class<?> safeTarget = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.ConstructorTarget", transformed);
        Object instance = safeTarget.getConstructor().newInstance();
        assert ((Integer) safeTarget.getMethod("value").invoke(instance)) == 41;
        assert safeType.getField("hits").getInt(null) == 1;

        byte[] unsafeMixin = resource("cppfm/transformer_fixture/UnsafeConstructorMixin.class");
        MixinClassTransformer strictTransformer = new MixinClassTransformer(true);
        strictTransformer.registerMixin("cppfm.transformer_fixture.UnsafeConstructorMixin", unsafeMixin);
        boolean rejected = false;
        try {
            strictTransformer.transform("cppfm.transformer_fixture.ConstructorTarget", target,
                new TransformContext("cppfm.transformer_fixture.ConstructorTarget", target, true));
        } catch (TransformException expected) {
            rejected = true;
        }
        assert rejected;

        MixinClassTransformer lenientTransformer = new MixinClassTransformer(false);
        lenientTransformer.registerMixin("cppfm.transformer_fixture.UnsafeConstructorMixin", unsafeMixin);
        TransformContext lenientContext = new TransformContext(
            "cppfm.transformer_fixture.ConstructorTarget", target, false);
        byte[] unchanged = lenientTransformer.transform(
            "cppfm.transformer_fixture.ConstructorTarget", target, lenientContext);
        assert java.util.Arrays.equals(target, unchanged);
        assert !lenientContext.getDiagnostics().isEmpty();
    }

    private static int occurrences(String value, String needle) {
        int count = 0;
        for (int index = 0; (index = value.indexOf(needle, index)) >= 0; index += needle.length()) count++;
        return count;
    }

    private static byte[] resource(String name) throws Exception {
        try (InputStream input = TransformerContractTest.class.getClassLoader().getResourceAsStream(name)) {
            if (input == null) throw new IllegalStateException("missing test resource " + name);
            return input.readAllBytes();
        }
    }

    private static final class ByteArrayLoader extends ClassLoader {
        Class<?> define(String name, byte[] bytes) {
            return defineClass(name, bytes, 0, bytes.length);
        }
    }
}
