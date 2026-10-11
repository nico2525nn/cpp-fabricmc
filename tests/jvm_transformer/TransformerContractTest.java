package cppfm.transformer_fixture;

import cppfm.loader.KnotClassLoader;
import cppfm.transform.MixinClassTransformer;
import cppfm.transform.MixinConfiguration;
import cppfm.transform.MixinDispatch;
import cppfm.transform.TransformContext;
import cppfm.transform.TransformException;
import cppfm.transform.TransformResult;

import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.net.URL;
import java.util.List;

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
        selectorSyntaxCase();
        selectorParserEdgeCases();
        accessorInflectionCase();

        URL[] empty = new URL[0];
        KnotClassLoader knot = new KnotClassLoader(empty, TransformerContractTest.class.getClassLoader());
        loaderAccessWidenerCase(knot);
        TransformResult result = knot.transformResult("cppfm.transformer_fixture.BranchTarget", target);
        assert result.getOriginalSha256().length() == 64;
        assert knot.getTransformedMethodHashes() != null;
        if (!result.getModifiedMethodDescriptors().isEmpty())
            throw new AssertionError("unexpected automatic mixin: " + result.getModifiedMethodDescriptors());

        System.out.println("TRANSFORMER CONTRACT PASS");
    }

    private static void accessorInflectionCase() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/AcronymAccessorTarget.class");
        byte[] mixin = resource("cppfm/transformer_fixture/AcronymAccessorMixin.class");
        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin("cppfm.transformer_fixture.AcronymAccessorMixin", mixin);
        TransformContext context = new TransformContext(
            "cppfm.transformer_fixture.AcronymAccessorTarget", target, true);
        byte[] transformed = transformer.transform(
            "cppfm.transformer_fixture.AcronymAccessorTarget", target, context);
        assert !java.util.Arrays.equals(target, transformed) : context.getDiagnostics();
        Class<?> type = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.AcronymAccessorTarget", transformed);
        Object instance = type.getConstructor().newInstance();
        assert ((Integer) type.getMethod("getROOT").invoke(instance)) == 17;
        assert "https://example.invalid".equals(type.getMethod("getURL").invoke(instance));
        assert ((Integer) type.getMethod("getXValue").invoke(instance)) == 23;
    }

    private static void selectorSyntaxCase() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/SelectorTarget.class");
        byte[] mixin = resource("cppfm/transformer_fixture/SelectorMixin.class");
        MixinDispatch.clear();
        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin("cppfm.transformer_fixture.SelectorMixin", mixin);
        TransformContext context = new TransformContext(
            "cppfm.transformer_fixture.SelectorTarget", target, true);
        byte[] transformed = transformer.transform(
            "cppfm.transformer_fixture.SelectorTarget", target, context);
        assert !java.util.Arrays.equals(target, transformed) : context.getDiagnostics();
        assert context.getChangedMethods().contains("run()I");
        assert context.getChangedMethods().contains("run(I)I");

        Class<?> type = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.SelectorTarget", transformed);
        Object instance = type.getConstructor().newInstance();
        PrintStream savedOut = System.out;
        ByteArrayOutputStream noArgumentCapture = new ByteArrayOutputStream();
        System.setOut(new PrintStream(noArgumentCapture));
        try {
            assert ((Integer) type.getMethod("run").invoke(instance)) == 4;
        } finally {
            System.setOut(savedOut);
        }
        ByteArrayOutputStream integerArgumentCapture = new ByteArrayOutputStream();
        System.setOut(new PrintStream(integerArgumentCapture));
        try {
            assert ((Integer) type.getMethod("run", int.class).invoke(instance, 4)) == 5;
        } finally {
            System.setOut(savedOut);
        }
        String noArgumentLog = noArgumentCapture.toString(
            java.nio.charset.StandardCharsets.UTF_8);
        String integerArgumentLog = integerArgumentCapture.toString(
            java.nio.charset.StandardCharsets.UTF_8);
        assert occurrences(noArgumentLog, "QUANTIFIER_HOOK") == 1 : noArgumentLog;
        assert occurrences(integerArgumentLog, "QUANTIFIER_HOOK") == 1 : integerArgumentLog;
        assert occurrences(integerArgumentLog, "DOTTED_OWNER_HOOK") == 1 : integerArgumentLog;
        assert occurrences(noArgumentLog, "FIRST_MATCH_HOOK") == 1 : noArgumentLog;
        assert occurrences(integerArgumentLog, "FIRST_MATCH_HOOK") == 0 : integerArgumentLog;
        assert occurrences(noArgumentLog, "DEFAULT_MATCH_HOOK") == 1 : noArgumentLog;
        assert occurrences(integerArgumentLog, "DEFAULT_MATCH_HOOK") == 0 : integerArgumentLog;
        assert occurrences(noArgumentLog, "DESCRIPTOR_ONLY_HOOK") == 0 : noArgumentLog;
        assert occurrences(integerArgumentLog, "DESCRIPTOR_ONLY_HOOK") == 1 : integerArgumentLog;
        assert occurrences(noArgumentLog, "REDIRECT_HOOK") == 1 : noArgumentLog;
        assert occurrences(integerArgumentLog, "REDIRECT_HOOK") == 0 : integerArgumentLog;
        assert occurrences(noArgumentLog, "UNBOUNDED_RANGE_HOOK") == 1 : noArgumentLog;
        assert occurrences(integerArgumentLog, "UNBOUNDED_RANGE_HOOK") == 1 : integerArgumentLog;

        byte[] incompatibleMixin = resource(
            "cppfm/transformer_fixture/SelectorArgumentMixin.class");
        MixinClassTransformer incompatible = new MixinClassTransformer(true);
        incompatible.registerMixin(
            "cppfm.transformer_fixture.SelectorArgumentMixin", incompatibleMixin);
        TransformContext incompatibleContext = new TransformContext(
            "cppfm.transformer_fixture.SelectorTarget", target, true);
        boolean rejectedLaterOverloadFallback = false;
        try {
            incompatible.transform("cppfm.transformer_fixture.SelectorTarget",
                target, incompatibleContext);
        } catch (TransformException expected) {
            assert expected.getMessage().contains(
                    "handler requests local capture without locals") &&
                expected.getMessage().contains(
                    "mustNotSelectLaterCompatibleOverload")
                : expected.getMessage();
            rejectedLaterOverloadFallback = true;
        }
        assert rejectedLaterOverloadFallback
            : "default selector must not skip the first overload to fit handler arguments";
        assert incompatibleContext.getChangedMethods().isEmpty();

        selectorStaticEligibilityCase();

        byte[] invalidMixin = resource(
            "cppfm/transformer_fixture/SelectorLimitMixin.class");
        MixinClassTransformer limited = new MixinClassTransformer(true);
        limited.registerMixin("cppfm.transformer_fixture.SelectorLimitMixin",
            invalidMixin);
        TransformContext limitedContext = new TransformContext(
            "cppfm.transformer_fixture.SelectorTarget", target, true);
        boolean rejectedInvalidMatchCount = false;
        try {
            limited.transform("cppfm.transformer_fixture.SelectorTarget",
                target, limitedContext);
        } catch (TransformException expected) {
            rejectedInvalidMatchCount = true;
        }
        assert rejectedInvalidMatchCount;
        assert limitedContext.getChangedMethods().isEmpty();
    }

    private static void selectorParserEdgeCases() {
        var unnamed = cppfm.transform.MixinMemberSelector.parse("*(I)I");
        assert unnamed.member().isEmpty();
        assert "(I)I".equals(unnamed.descriptor());

        var defaultPackageOwner = cppfm.transform.MixinMemberSelector.parse(
            "SelectorTarget.run()I");
        assert defaultPackageOwner.hasOwner();
        assert "SelectorTarget".equals(defaultPackageOwner.owner());
        assert "run".equals(defaultPackageOwner.member());

        var internalOwner = cppfm.transform.MixinMemberSelector.parse(
            "Lexample/Target;run()I");
        assert "example/Target".equals(internalOwner.owner());
        assert "run".equals(internalOwner.member());

        var spaced = cppfm.transform.MixinMemberSelector.parse(
            "  run { 1 } ( I ) I  ");
        assert "run".equals(spaced.member());
        assert spaced.hasQuantifier() && spaced.maximumMatches() == 1;
        assert "(I)I".equals(spaced.descriptor());

        var unbounded = cppfm.transform.MixinMemberSelector.parse("run{,}");
        assert unbounded.hasQuantifier();
        assert unbounded.minimumMatches() == 0;
        assert unbounded.maximumMatches() == Integer.MAX_VALUE;

        boolean rejectedMalformedLeadingL = false;
        try {
            cppfm.transform.MixinMemberSelector.parse(
                "Lexample.Target.run()I");
        } catch (TransformException expected) {
            rejectedMalformedLeadingL = true;
        }
        assert rejectedMalformedLeadingL
            : "a dotted owner beginning with L must use the formal Lowner; form";

        for (String zeroMaximum : List.of("run{0}", "run{,0}", "run{0,0}")) {
            boolean rejected = false;
            try {
                cppfm.transform.MixinMemberSelector.parse(zeroMaximum);
            } catch (TransformException expected) {
                rejected = expected.getMessage().contains("must allow at least one match");
            }
            assert rejected : "zero maximum must be rejected: " + zeroMaximum;
        }

        for (String malformed : List.of("run{}", "run{bad}", "run{1")) {
            boolean rejected = false;
            try {
                cppfm.transform.MixinMemberSelector.parse(malformed);
            } catch (TransformException expected) {
                rejected = expected.getMessage().contains("quantifier");
            }
            assert rejected : "malformed selector quantifier must be rejected: " + malformed;
        }
    }

    private static void selectorStaticEligibilityCase() throws Exception {
        byte[] target = resource(
            "cppfm/transformer_fixture/SelectorStaticTarget.class");
        byte[] mixin = resource(
            "cppfm/transformer_fixture/SelectorStaticMixin.class");
        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin(
            "cppfm.transformer_fixture.SelectorStaticMixin", mixin);
        TransformContext context = new TransformContext(
            "cppfm.transformer_fixture.SelectorStaticTarget", target, true);
        byte[] transformed = transformer.transform(
            "cppfm.transformer_fixture.SelectorStaticTarget", target, context);
        assert context.getChangedMethods().contains("run(I)V")
            : context.getChangedMethods();
        assert !context.getChangedMethods().contains("run()V")
            : context.getChangedMethods();

        Class<?> type = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.SelectorStaticTarget", transformed);
        Object instance = type.getConstructor().newInstance();
        PrintStream savedOut = System.out;
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        System.setOut(new PrintStream(captured));
        try {
            type.getMethod("run").invoke(null);
            type.getMethod("run", int.class).invoke(instance, 7);
        } finally {
            System.setOut(savedOut);
        }
        String log = captured.toString(java.nio.charset.StandardCharsets.UTF_8);
        assert occurrences(log, "INSTANCE_SELECTOR_HOOK") == 1 : log;

        byte[] staticOnlyTarget = resource(
            "cppfm/transformer_fixture/SelectorStaticOnlyTarget.class");
        byte[] staticOnlyMixin = resource(
            "cppfm/transformer_fixture/SelectorStaticOnlyMixin.class");
        MixinClassTransformer optional = new MixinClassTransformer(true);
        optional.registerMixin(
            "cppfm.transformer_fixture.SelectorStaticOnlyMixin", staticOnlyMixin);
        TransformContext optionalContext = new TransformContext(
            "cppfm.transformer_fixture.SelectorStaticOnlyTarget",
            staticOnlyTarget, true);
        byte[] optionalResult = optional.transform(
            "cppfm.transformer_fixture.SelectorStaticOnlyTarget",
            staticOnlyTarget, optionalContext);
        assert java.util.Arrays.equals(staticOnlyTarget, optionalResult)
            : optionalContext.getDiagnostics();
        assert optionalContext.getChangedMethods().isEmpty()
            : optionalContext.getChangedMethods();

        MixinConfiguration requiredByDefault = MixinConfiguration.parse(""
            + "{\"package\":\"cppfm.transformer_fixture\","
            + "\"server\":[\"SelectorStaticOnlyMixin\"],"
            + "\"injectors\":{\"defaultRequire\":1}}");
        assert requiredByDefault.defaultRequire() == 1;
        MixinClassTransformer configured = new MixinClassTransformer(true);
        configured.registerConfiguration(requiredByDefault,
            TransformerContractTest.class.getClassLoader());
        TransformContext configuredContext = new TransformContext(
            "cppfm.transformer_fixture.SelectorStaticOnlyTarget",
            staticOnlyTarget, true);
        boolean rejectedByConfigRequirement = false;
        try {
            configured.transform(
                "cppfm.transformer_fixture.SelectorStaticOnlyTarget",
                staticOnlyTarget, configuredContext);
        } catch (TransformException expected) {
            rejectedByConfigRequirement = expected.getMessage().contains("require=1");
        }
        assert rejectedByConfigRequirement
            : "injectors.defaultRequire applies when an injector omits require";
    }

    private static void loaderAccessWidenerCase(KnotClassLoader knot) throws Exception {
        String owner = "cppfm/transformer_fixture/LoaderAccessTarget";
        String widener = "accessWidener v2 named\n"
            + "accessible class " + owner + "\n"
            + "extendable class " + owner + "\n"
            + "accessible field " + owner + " hidden I\n"
            + "mutable field " + owner + " hidden I\n"
            + "accessible method " + owner + " <init> ()V\n"
            + "accessible method " + owner + " secret (I)I\n";
        knot.registerAccessWidener(widener.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        assert knot.getAccessWidenerTargetCount() == 4;
        byte[] original = resource("cppfm/transformer_fixture/LoaderAccessTarget.class");
        TransformResult result = knot.transformResult(
            "cppfm.transformer_fixture.LoaderAccessTarget", original);
        assert result.getModifiedMethodDescriptors().contains("<init>()V");
        assert result.getModifiedMethodDescriptors().contains("secret(I)I");
        Class<?> type = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.LoaderAccessTarget", result.getTransformedBytes());
        assert java.lang.reflect.Modifier.isPublic(type.getModifiers());
        assert !java.lang.reflect.Modifier.isFinal(type.getModifiers());
        java.lang.reflect.Constructor<?> constructor = type.getConstructor();
        Object instance = constructor.newInstance();
        java.lang.reflect.Field field = type.getField("hidden");
        assert !java.lang.reflect.Modifier.isFinal(field.getModifiers());
        field.setInt(instance, 9);
        int observed = (Integer) type.getMethod("secret", int.class).invoke(instance, 1);
        assert observed == 10 : observed;
    }

    private static void phase7Cases() throws Exception {
        configurationNameCases();
        advancedInjectionCases();
        sharedReferenceCase();
        mixinExtrasOperationCase();
        crossMixinOrderingCase();
        constructorCases();
        matchLimitRollbackCase();
    }

    private static void configurationNameCases() {
        MixinConfiguration lithium = MixinConfiguration.parse(""
            + "{\"package\":\"net.caffeinemc.mods.lithium.mixin\","
            + "\"server\":[\"ai.pathing.BlockStateBaseMixin\","
            + "\"net.caffeinemc.mods.lithium.mixin.AlreadyQualifiedMixin\","
            + "\"SimpleMixin\"]}");
        assert lithium.serverMixins().equals(List.of(
            "net.caffeinemc.mods.lithium.mixin.ai.pathing.BlockStateBaseMixin",
            "net.caffeinemc.mods.lithium.mixin.AlreadyQualifiedMixin",
            "net.caffeinemc.mods.lithium.mixin.SimpleMixin")) : lithium.serverMixins();
        assert lithium.defaultRequire() == 0;
        assert MixinConfiguration.parse("{\"injectors\":{\"defaultRequire\":0}}")
            .defaultRequire() == 0;

        boolean rejectedNonIntegerDefaultRequire = false;
        try {
            MixinConfiguration.parse("{\"injectors\":{\"defaultRequire\":1.5}}")
                .defaultRequire();
        } catch (TransformException expected) {
            rejectedNonIntegerDefaultRequire = true;
        }
        assert rejectedNonIntegerDefaultRequire;

        MixinClassTransformer strict = new MixinClassTransformer(true);
        strict.registerConfiguration(lithium, new ClassLoader(null) { });
        assert strict.getDiagnostics().size() == 3 : strict.getDiagnostics();

        MixinConfiguration required = MixinConfiguration.parse(""
            + "{\"package\":\"example.mixin\",\"required\":true,"
            + "\"server\":[\"missing.NeverPresentMixin\"]}");
        boolean rejected = false;
        try {
            new MixinClassTransformer(true).registerConfiguration(required, new ClassLoader(null) { });
        } catch (TransformException expected) {
            rejected = true;
        }
        assert rejected : "required missing mixin must remain fatal";
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
        mixinType.getField("namedSliceHits").setInt(null, 0);
        mixinType.getField("allInvokeHits").setInt(null, 0);
        mixinType.getField("assignObservedCalls").setInt(null, 0);
        mixinType.getField("headCancellationHits").setInt(null, 0);
        mixinType.getField("returnCancellationHits").setInt(null, 0);
        mixinType.getField("voidCancellationHits").setInt(null, 0);
        mixinType.getField("capturedWide").setLong(null, 0L);
        mixinType.getField("capturedReference").set(null, null);

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
        assert ((Integer) type.getMethod("assign", int.class).invoke(instance, 3)) == 8;
        assert ((Integer) type.getMethod("cancelAtHead", int.class).invoke(instance, 5)) == 99;
        assert ((Integer) type.getMethod("cancelAtHead", int.class).invoke(instance, 2)) == 12;
        assert ((Integer) type.getMethod("cancelAtReturn", int.class).invoke(instance, 1)) == 103;
        type.getMethod("cancelVoid", int.class).invoke(instance, 7);
        type.getMethod("cancelVoid", int.class).invoke(instance, -1);
        assert type.getField("voidState").getInt(instance) == 7;
        assert ((Integer) type.getMethod("mixedCapture", int.class).invoke(instance, 4)) == 14;

        assert mixinType.getField("arrayAtHits").getInt(null) == 2;
        assert mixinType.getField("sliceOrdinalHits").getInt(null) == 1;
        assert mixinType.getField("namedSliceHits").getInt(null) == 1;
        assert mixinType.getField("allInvokeHits").getInt(null) == 3;
        assert mixinType.getField("assignObservedCalls").getInt(null) == 4
            : "assign callback observed " + mixinType.getField("assignObservedCalls").getInt(null);
        assert mixinType.getField("headCancellationHits").getInt(null) == 2;
        assert mixinType.getField("returnCancellationHits").getInt(null) == 1;
        assert mixinType.getField("voidCancellationHits").getInt(null) == 2;
        assert mixinType.getField("constantHits").getInt(null) == 1;
        assert mixinType.getField("variableHits").getInt(null) == 1;
        assert mixinType.getField("capturedLocal").getInt(null) == 6;
        assert mixinType.getField("capturedWide").getLong(null) == 12L;
        assert "xy".equals(mixinType.getField("capturedReference").get(null));
        assert mixinType.getField("jumpHits").getInt(null) > 0;

        MixinClassTransformer explicitAllowSentinel =
            new MixinClassTransformer(true);
        explicitAllowSentinel.registerMixin(
            "cppfm.transformer_fixture.AllowSentinelMixin",
            resource("cppfm/transformer_fixture/AllowSentinelMixin.class"));
        boolean explicitNegativeAllowClamped = false;
        try {
            explicitAllowSentinel.transform(
                "cppfm.transformer_fixture.AdvancedTarget", target,
                new TransformContext(
                    "cppfm.transformer_fixture.AdvancedTarget", target, true));
        } catch (TransformException expected) {
            explicitNegativeAllowClamped = expected.getMessage().contains("allow=1");
        }
        assert explicitNegativeAllowClamped
            : "an explicitly stored allow=-1 follows Mixin 0.8.7's non-null clamp behavior";
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

    private static void sharedReferenceCase() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/ShareTarget.class");
        byte[] mixin = resource("cppfm/transformer_fixture/ShareMixin.class");
        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin("cppfm.transformer_fixture.ShareMixin", mixin);
        TransformContext context = new TransformContext(
            "cppfm.transformer_fixture.ShareTarget", target, true);
        byte[] transformed = transformer.transform(
            "cppfm.transformer_fixture.ShareTarget", target, context);
        assert !java.util.Arrays.equals(target, transformed) : context.getDiagnostics();
        Class<?> type = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.ShareTarget", transformed);
        Object instance = type.getConstructor().newInstance();
        // input 2 -> local 3; the first hook stores 13 in the shared ref and
        // the later redirect observes that same object: 3 + 13 = 16.
        assert ((Integer) type.getMethod("compute", int.class).invoke(instance, 2)) == 16;
    }

    private static void mixinExtrasOperationCase() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/ExtrasTarget.class");
        byte[] mixin = resource("cppfm/transformer_fixture/ExtrasMixin.class");
        MixinClassTransformer transformer = new MixinClassTransformer(true);
        transformer.registerMixin("cppfm.transformer_fixture.ExtrasMixin", mixin);
        TransformContext context = new TransformContext(
            "cppfm.transformer_fixture.ExtrasTarget", target, true);
        byte[] transformed = transformer.transform(
            "cppfm.transformer_fixture.ExtrasTarget", target, context);
        assert !java.util.Arrays.equals(target, transformed) : context.getDiagnostics();
        Class<?> type = new ByteArrayLoader().define(
            "cppfm.transformer_fixture.ExtrasTarget", transformed);
        Object instance = type.getConstructor().newInstance();
        assert ((Integer) type.getMethod("returnValue", int.class).invoke(instance, 2)) == 13;
        assert ((Integer) type.getMethod("expression", int.class).invoke(instance, 2)) == 7;
        int conditionalTrue = (Integer) type.getMethod("conditional", int.class).invoke(instance, 3);
        assert conditionalTrue == 5 : "conditional true=" + conditionalTrue;
        int conditionalFalse = (Integer) type.getMethod("conditional", int.class).invoke(instance, -1);
        assert conditionalFalse == 5 : "conditional false=" + conditionalFalse;
        assert ((Integer) type.getMethod("wrapped", int.class).invoke(instance, 2)) == 9;
        assert ((Integer) type.getMethod("wrappedMethod", int.class).invoke(instance, 2)) == 26;
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

    private static void matchLimitRollbackCase() throws Exception {
        byte[] target = resource("cppfm/transformer_fixture/AdvancedTarget.class");
        byte[] mixin = resource("cppfm/transformer_fixture/MatchLimitMixin.class");

        MixinDispatch.clear();
        MixinClassTransformer strict = new MixinClassTransformer(true);
        strict.registerMixin("cppfm.transformer_fixture.MatchLimitMixin", mixin);
        TransformContext strictContext = new TransformContext(
            "cppfm.transformer_fixture.AdvancedTarget", target, true);
        boolean rejected = false;
        try {
            strict.transform("cppfm.transformer_fixture.AdvancedTarget", target, strictContext);
        } catch (TransformException expected) {
            rejected = true;
        }
        assert rejected;
        assert strictContext.getChangedMethods().isEmpty();
        assert MixinDispatch.transformedMethods().isEmpty();

        MixinDispatch.clear();
        MixinClassTransformer lenient = new MixinClassTransformer(false);
        lenient.registerMixin("cppfm.transformer_fixture.MatchLimitMixin", mixin);
        TransformContext lenientContext = new TransformContext(
            "cppfm.transformer_fixture.AdvancedTarget", target, false);
        byte[] unchanged = lenient.transform(
            "cppfm.transformer_fixture.AdvancedTarget", target, lenientContext);
        assert java.util.Arrays.equals(target, unchanged);
        assert lenientContext.getChangedMethods().isEmpty();
        assert MixinDispatch.transformedMethods().isEmpty();
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
