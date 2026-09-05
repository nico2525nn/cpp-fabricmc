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

        URL[] empty = new URL[0];
        KnotClassLoader knot = new KnotClassLoader(empty, TransformerContractTest.class.getClassLoader());
        TransformResult result = knot.transformResult("cppfm.transformer_fixture.BranchTarget", target);
        assert result.getOriginalSha256().length() == 64;
        assert knot.getTransformedMethodHashes() != null;
        if (!knot.getTransformedMethodDescriptors().isEmpty()) throw new AssertionError("unexpected automatic mixin");

        System.out.println("TRANSFORMER CONTRACT PASS");
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
