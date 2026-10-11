package cppfm.api_fixture;

import cppfm.loader.AccessWidenerNamespaceRemapper;
import cppfm.loader.ClassFileNamespaceRemapper;
import cppfm.loader.IntermediaryNamedMappings;
import cppfm.loader.ModJarStager;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.io.InputStream;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/** Contract tests for namespace conversion of top-level and nested Fabric mods. */
public final class ModJarStagerContractTest {
    private static final String TINY = "tiny\t2\t0\tintermediary\tnamed\n"
        + "c\tnet/minecraft/class_100\tnet/minecraft/example/Target\n"
        + "\tf\tI\tfield_1\tvalue\n"
        + "\tm\t()Lnet/minecraft/class_101;\tmethod_2\tresolve\n"
        + "\tm\t()V\tmethod_2\tresolve\n"
        + "\tm\t()V\tmethod_4\tinheritedReload\n"
        + "c\tnet/minecraft/class_101\tnet/minecraft/example/Other\n"
        + "\tm\t()V\tmethod_2\totherResolve\n"
        + "c\tnet/minecraft/class_102\tnet/minecraft/example/Ambiguous\n"
        + "\tm\t()V\tmethod_3\tfirstName\n"
        + "\tm\t(I)V\tmethod_3\tsecondName\n"
        + "c\torg/spongepowered/asm/mixin/injection/At$Shift"
        + "\torg/spongepowered/asm/mixin/injection/At$Shift\n"
        + "\tf\tLorg/spongepowered/asm/mixin/injection/At$Shift;"
        + "\tBEFORE\tAFTER\n"
        + "c\tcppfm/api_fixture/ModJarStagerContractTest$IntermediaryType"
        + "\tcppfm/api_fixture/ModJarStagerContractTest$NamedType\n";

    private static final String AW_INTERMEDIARY = "accessWidener v2 intermediary\n"
        + "# preserve supported comments\n"
        + "accessible class net/minecraft/class_100\n"
        + "mutable field net/minecraft/class_100 field_1 I  # preserve trailing comment\n"
        + "transitive-accessible method net/minecraft/class_100 method_2 "
        + "()Lnet/minecraft/class_101;\n";

    private static final String AW_NAMED = "accessWidener\tv2\tnamed\n"
        + "# preserve supported comments\n"
        + "accessible\tclass\tnet/minecraft/example/Target\n"
        + "mutable\tfield\tnet/minecraft/example/Target\tvalue\tI  # preserve trailing comment\n"
        + "transitive-accessible\tmethod\tnet/minecraft/example/Target\tresolve\t"
        + "()Lnet/minecraft/example/Other;\n";

    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("cppfm-mod-stager-contract-");
        try {
            IntermediaryNamedMappings mappings = mapping(root);
            remapDirectContent(mappings);
            remapIntermediaryOverrideDeclarations(root, mappings);
            remapOverrideInheritedThroughFabricInterface(root, mappings);
            honorMixinRemapControls(mappings);
            preserveMixinStringConstants(mappings);
            preserveSharedDescriptorStringConstant(mappings);
            remapOnlyMixinAnnotationSelectors(mappings);
            preserveCustomMixinTarget(mappings);
            preserveOrdinaryClassWithMixinDescriptor(root, mappings);
            remapTopLevelAndNestedArchives(root, mappings);
            enforceExpandedSizeBudgetsAndCleanup(root, mappings);
            countNestedJarContentsOnce(root, mappings);
            rejectDestinationSymlinkIntoSource(root, mappings);
            rejectUnsupportedInput(mappings);
            System.out.println("MOD JAR STAGER CONTRACT PASS");
        } finally {
            deleteTree(root);
        }
    }

    private static void remapIntermediaryOverrideDeclarations(
        Path root, IntermediaryNamedMappings mappings) throws Exception {
        byte[] intermediaryInterface;
        byte[] implementation;
        try (InputStream input = ModJarStagerContractTest.class
                .getResourceAsStream("/net/minecraft/class_100.class")) {
            if (input == null) throw new AssertionError("intermediary interface fixture missing");
            intermediaryInterface = input.readAllBytes();
        }
        try (InputStream input = ModJarStagerContractTest.class
                .getResourceAsStream("IntermediaryOverrideFixture.class")) {
            if (input == null) throw new AssertionError("intermediary override fixture missing");
            implementation = input.readAllBytes();
        }

        Path source = Files.createDirectory(root.resolve("override-mods"));
        Path destination = root.resolve("override-staged");
        Files.write(source.resolve("override.jar"), zip(Map.of(
            "fabric.mod.json",
            "{\"schemaVersion\":1,\"id\":\"override_probe\",\"version\":\"1\"}"
                .getBytes(StandardCharsets.UTF_8),
            "cppfm/api_fixture/IntermediaryOverrideFixture.class", implementation)));
        ModJarStager.stage(source, destination, mappings);

        ClassFileNamespaceRemapper remapper =
            new ClassFileNamespaceRemapper(mappings);
        byte[] namedInterface = remapper.remap(
            "net.minecraft.class_100", intermediaryInterface);
        byte[] namedImplementation = readZipEntry(destination.resolve("override.jar"),
            "cppfm/api_fixture/IntermediaryOverrideFixture.class");
        class FixtureLoader extends ClassLoader {
            FixtureLoader() { super(null); }
            Class<?> defineTarget() {
                return defineClass("net.minecraft.example.Target", namedInterface,
                    0, namedInterface.length);
            }
            Class<?> defineImplementation() {
                return defineClass("cppfm.api_fixture.IntermediaryOverrideFixture",
                    namedImplementation, 0, namedImplementation.length);
            }
        }
        FixtureLoader loader = new FixtureLoader();
        Class<?> target = loader.defineTarget();
        Class<?> fixture = loader.defineImplementation();
        var mappedMethod = fixture.getDeclaredMethod("resolve");
        equal(target, mappedMethod.getDeclaringClass().getInterfaces()[0],
            "fixture implements the named intermediary interface");
        equal("resolve", mappedMethod.getName(),
            "ordinary mod overrides are renamed with their intermediary interface method");
        Object instance = fixture.getConstructor().newInstance();
        mappedMethod.invoke(instance);
        fixture.getDeclaredMethod("invokeSelf").invoke(instance);
    }

    private static void remapOverrideInheritedThroughFabricInterface(
        Path root, IntermediaryNamedMappings mappings) throws Exception {
        byte[] intermediaryTarget = resource("/net/minecraft/class_100.class");
        byte[] fabricInterface = resource(
            "/net/fabricmc/fabric/api/resource/TransitiveReloadListener.class");
        byte[] modInterface = resource("ModOwnedReloadLayer.class");
        byte[] implementation = resource(
            "TransitiveFabricOverrideFixture.class");
        Path source = Files.createDirectory(root.resolve("transitive-override-mods"));
        Path destination = root.resolve("transitive-override-staged");
        Files.write(source.resolve("override.jar"), zip(Map.of(
            "fabric.mod.json",
            "{\"schemaVersion\":1,\"id\":\"transitive_override_probe\",\"version\":\"1\"}"
                .getBytes(StandardCharsets.UTF_8),
            "net/fabricmc/fabric/api/resource/TransitiveReloadListener.class",
            fabricInterface,
            "cppfm/api_fixture/ModOwnedReloadLayer.class", modInterface,
            "cppfm/api_fixture/TransitiveFabricOverrideFixture.class",
            implementation)));
        ModJarStager.stage(source, destination, mappings);
        byte[] namedTarget = new ClassFileNamespaceRemapper(mappings).remap(
            "net.minecraft.class_100", intermediaryTarget);
        byte[] stagedFabricInterface = readZipEntry(destination.resolve("override.jar"),
            "net/fabricmc/fabric/api/resource/TransitiveReloadListener.class");
        byte[] stagedModInterface = readZipEntry(destination.resolve("override.jar"),
            "cppfm/api_fixture/ModOwnedReloadLayer.class");
        byte[] stagedImplementation = readZipEntry(destination.resolve("override.jar"),
            "cppfm/api_fixture/TransitiveFabricOverrideFixture.class");
        class FixtureLoader extends ClassLoader {
            FixtureLoader() { super(null); }
            Class<?> defineTarget() {
                return defineClass("net.minecraft.example.Target", namedTarget,
                    0, namedTarget.length);
            }
            Class<?> defineFabricInterface() {
                return defineClass(
                    "net.fabricmc.fabric.api.resource.TransitiveReloadListener",
                    stagedFabricInterface, 0, stagedFabricInterface.length);
            }
            Class<?> defineModInterface() {
                return defineClass("cppfm.api_fixture.ModOwnedReloadLayer",
                    stagedModInterface, 0, stagedModInterface.length);
            }
            Class<?> defineImplementation() {
                return defineClass("cppfm.api_fixture.TransitiveFabricOverrideFixture",
                    stagedImplementation, 0, stagedImplementation.length);
            }
        }
        FixtureLoader loader = new FixtureLoader();
        Class<?> target = loader.defineTarget();
        Class<?> fabric = loader.defineFabricInterface();
        Class<?> modLayer = loader.defineModInterface();
        Class<?> fixture = loader.defineImplementation();
        equal(target, fabric.getInterfaces()[0],
            "unmapped Fabric interface retains its mapped Minecraft ancestor");
        equal(fabric, modLayer.getInterfaces()[0],
            "mod-owned intermediate interface retains the Fabric contract");
        equal(modLayer, fixture.getInterfaces()[0],
            "implementation keeps its mod-owned intermediate interface");
        equal("inheritedReload", fixture.getDeclaredMethod("inheritedReload").getName(),
            "intermediary override maps through a mod-owned Fabric contract layer");
        fixture.getDeclaredMethod("invokeInheritedReload")
            .invoke(fixture.getConstructor().newInstance());
    }

    private static byte[] resource(String name) throws IOException {
        try (InputStream input = ModJarStagerContractTest.class.getResourceAsStream(name)) {
            if (input == null) throw new IOException("fixture class missing: " + name);
            return input.readAllBytes();
        }
    }

    private static void preserveOrdinaryClassWithMixinDescriptor(
        Path root, IntermediaryNamedMappings mappings) throws Exception {
        byte[] original;
        try (InputStream input = ModJarStagerContractTest.class
                .getResourceAsStream("FalseMixinDescriptorFixture.class")) {
            if (input == null) throw new AssertionError("false-mixin fixture missing");
            original = input.readAllBytes();
        }
        Path source = Files.createDirectory(root.resolve("false-mixin-mods"));
        Path destination = root.resolve("false-mixin-staged");
        Files.write(source.resolve("fixture.jar"), zip(Map.of(
            "fabric.mod.json",
            "{\"schemaVersion\":1,\"id\":\"false_mixin_probe\",\"version\":\"1\"}"
                .getBytes(StandardCharsets.UTF_8),
            "cppfm/api_fixture/FalseMixinDescriptorFixture.class", original)));
        ModJarStager.stage(source, destination, mappings);
        byte[] staged = readZipEntry(destination.resolve("fixture.jar"),
            "cppfm/api_fixture/FalseMixinDescriptorFixture.class");
        class FixtureLoader extends ClassLoader {
            FixtureLoader() { super(null); }
            Class<?> defineFixture() {
                return defineClass("cppfm.api_fixture.FalseMixinDescriptorFixture",
                    staged, 0, staged.length);
            }
        }
        var field = new FixtureLoader().defineFixture().getDeclaredField("field_1");
        equal("field_1", field.getName(),
            "a descriptor string does not trigger Mixin declaration remapping");
    }

    private static void preserveMixinStringConstants(
        IntermediaryNamedMappings mappings) throws Exception {
        byte[] original;
        try (InputStream input = ModJarStagerContractTest.class
                .getResourceAsStream("MixinStringConstantFixture.class")) {
            if (input == null) throw new AssertionError("mixin string fixture class missing");
            original = input.readAllBytes();
        }
        byte[] remapped = new ClassFileNamespaceRemapper(mappings).remapMixin(
            "cppfm.api_fixture.MixinStringConstantFixture", original);
        class FixtureLoader extends ClassLoader {
            FixtureLoader() { super(null); }
            Class<?> defineFixture() {
                return defineClass("cppfm.api_fixture.MixinStringConstantFixture",
                    remapped, 0, remapped.length);
            }
        }
        var field = new FixtureLoader().defineFixture().getDeclaredField("TEXT");
        field.setAccessible(true);
        equal("method_2", field.get(null),
            "mixin namespace remapping preserves ordinary string constants");
    }

    private static void honorMixinRemapControls(
        IntermediaryNamedMappings mappings) throws Exception {
        byte[] original = resource("MixinRemapControlFixture.class");
        byte[] remapped = new ClassFileNamespaceRemapper(mappings).remapMixin(
            "cppfm.api_fixture.MixinRemapControlFixture", original);
        class FixtureLoader extends ClassLoader {
            FixtureLoader() { super(ModJarStagerContractTest.class.getClassLoader()); }
            Class<?> defineFixture() {
                return defineClass("cppfm.api_fixture.MixinRemapControlFixture",
                    remapped, 0, remapped.length);
            }
        }
        Class<?> fixture = new FixtureLoader().defineFixture();
        equal("net.minecraft.example.Target",
            fixture.getAnnotation(org.spongepowered.asm.mixin.Mixin.class).targets()[0],
            "mixin target class is namespace-remapped independently of member remap policy");
        var explicit = fixture.getDeclaredMethod("explicitlyRemapped")
            .getAnnotation(org.spongepowered.asm.mixin.injection.Inject.class);
        equal("resolve()V", explicit.method()[0],
            "member remap=true overrides the class-level Mixin remap=false");
        equal("Lnet/minecraft/example/Target;resolve()V", explicit.at()[0].target(),
            "At remap=true overrides the class-level Mixin remap=false");
        var disabled = fixture.getDeclaredMethod("deliberatelyUnmapped")
            .getAnnotation(org.spongepowered.asm.mixin.injection.Inject.class);
        equal("method_2()V", disabled.method()[0],
            "Inject remap=false preserves its intermediary selector");
        equal("Lnet/minecraft/class_100;method_2()V", disabled.at()[0].target(),
            "At remap=false preserves its intermediary target");

        byte[] memberFlags = resource("MixinMemberRemapFalseFixture.class");
        byte[] memberFlagsRemapped = new ClassFileNamespaceRemapper(mappings)
            .remapMixin("cppfm.api_fixture.MixinMemberRemapFalseFixture", memberFlags);
        class MemberFlagsLoader extends ClassLoader {
            MemberFlagsLoader() {
                super(ModJarStagerContractTest.class.getClassLoader());
            }
            Class<?> defineFixture() {
                return defineClass("cppfm.api_fixture.MixinMemberRemapFalseFixture",
                    memberFlagsRemapped, 0, memberFlagsRemapped.length);
            }
        }
        Class<?> memberFlagsFixture = new MemberFlagsLoader().defineFixture();
        var injectionDisabled = memberFlagsFixture.getDeclaredMethod("allRemappingDisabled")
            .getAnnotation(org.spongepowered.asm.mixin.injection.Inject.class);
        equal("method_2()V", injectionDisabled.method()[0],
            "Inject remap=false overrides the default Mixin remap policy");
        equal("Lnet/minecraft/class_100;method_2()V", injectionDisabled.at()[0].target(),
            "nested At remap=false is honored independently");
        var atOnlyDisabled = memberFlagsFixture
            .getDeclaredMethod("onlyAtRemappingDisabled")
            .getAnnotation(org.spongepowered.asm.mixin.injection.Inject.class);
        equal("resolve()V", atOnlyDisabled.method()[0],
            "default Inject remapping still applies when nested At opts out");
        equal("Lnet/minecraft/class_100;method_2()V", atOnlyDisabled.at()[0].target(),
            "At remap=false does not inherit remapping from its Inject parent");
    }

    private static void preserveSharedDescriptorStringConstant(
        IntermediaryNamedMappings mappings) throws Exception {
        byte[] original;
        try (InputStream input = ModJarStagerContractTest.class
                .getResourceAsStream("SharedDescriptorStringFixture.class")) {
            if (input == null) throw new AssertionError("shared descriptor fixture class missing");
            original = input.readAllBytes();
        }
        assertSharedStringAndFieldDescriptor(original);
        byte[] remapped = new ClassFileNamespaceRemapper(mappings).remap(
            "cppfm.api_fixture.SharedDescriptorStringFixture", original);
        class FixtureLoader extends ClassLoader {
            FixtureLoader() {
                super(ModJarStagerContractTest.class.getClassLoader());
            }
            Class<?> defineFixture() {
                return defineClass("cppfm.api_fixture.SharedDescriptorStringFixture",
                    remapped, 0, remapped.length);
            }
        }
        Class<?> fixture = new FixtureLoader().defineFixture();
        var text = fixture.getDeclaredField("TEXT");
        text.setAccessible(true);
        equal("Lcppfm/api_fixture/ModJarStagerContractTest$IntermediaryType;",
            text.get(null), "shared descriptor remapping preserves its string literal");
        equal(NamedType.class, fixture.getDeclaredField("value").getType(),
            "shared UTF-8 descriptor is still remapped for the field declaration");
    }

    private static void remapOnlyMixinAnnotationSelectors(
        IntermediaryNamedMappings mappings) throws Exception {
        byte[] original;
        try (InputStream input = ModJarStagerContractTest.class
                .getResourceAsStream("MixinAnnotationStringFixture.class")) {
            if (input == null) throw new AssertionError("mixin annotation fixture class missing");
            original = input.readAllBytes();
        }
        assertSharedEnumAndAnnotationString(original);
        byte[] remapped = new ClassFileNamespaceRemapper(mappings).remapMixin(
            "cppfm.api_fixture.MixinAnnotationStringFixture", original);
        class FixtureLoader extends ClassLoader {
            FixtureLoader() { super(ModJarStagerContractTest.class.getClassLoader()); }
            Class<?> defineFixture() {
                return defineClass("cppfm.api_fixture.MixinAnnotationStringFixture",
                    remapped, 0, remapped.length);
            }
        }

        Class<?> fixture = new FixtureLoader().defineFixture();
        var mixin = fixture.getAnnotation(org.spongepowered.asm.mixin.Mixin.class);
        equal("net.minecraft.example.Target", mixin.targets()[0],
            "mixin target selector is remapped");

        var inject = fixture.getDeclaredMethod("onTarget")
            .getAnnotation(org.spongepowered.asm.mixin.injection.Inject.class);
        equal("resolve", inject.method()[0],
            "descriptorless inject method selector is remapped with its declared owner");
        equal("resolve*()V", inject.method()[1],
            "quantified inject method selector is remapped without losing its quantifier");
        equal("net.minecraft.example.Target.resolve{1,3}()V", inject.method()[2],
            "dotted-owner inject selector maps owner and member while preserving quantifier");
        var at = inject.at()[0];
        equal("Lnet/minecraft/example/Target;resolve", at.target(),
            "descriptorless At target selector is remapped with its encoded owner");
        equal("Lnet/minecraft/class_100;", at.args()[0],
            "At args descriptor-looking string remains literal");
        equal("method_2", at.args()[1], "At args symbol-looking string remains literal");
        equal("BEFORE", at.args()[2], "enum-like At args value remains literal");
        equal("method_2", at.id(), "At id symbol-looking string remains literal");
        equal(org.spongepowered.asm.mixin.injection.At.Shift.AFTER, at.shift(),
            "enum annotation constant maps with its owner and descriptor");
        var lookalike = fixture.getDeclaredMethod("onTarget")
            .getAnnotation(org.spongepowered.asm.mixin.injection.custom.Inject.class);
        equal("method_2()V", lookalike.method()[0],
            "lookalike annotation in a nested package remains untouched");

        var shadow = fixture.getDeclaredField("shadow$value")
            .getAnnotation(org.spongepowered.asm.mixin.Shadow.class);
        equal("value", shadow.aliases()[0], "shadow field alias is remapped");
        var shadowMethod = fixture.getDeclaredMethod("shadow$targetMethod")
            .getAnnotation(org.spongepowered.asm.mixin.Shadow.class);
        equal("resolve", shadowMethod.aliases()[0],
            "shadow method alias uses its declaration descriptor");

        var modifyConstant = fixture.getDeclaredMethod("modifyConstant", String.class)
            .getAnnotation(org.spongepowered.asm.mixin.injection.ModifyConstant.class);
        equal("resolve()V", modifyConstant.method()[0],
            "modify-constant method selector is remapped");
        equal("method_2", modifyConstant.constant()[0].stringValue(),
            "Constant.stringValue symbol-looking string remains literal");
    }

    private static void preserveCustomMixinTarget(
        IntermediaryNamedMappings mappings) throws Exception {
        byte[] original;
        try (InputStream input = ModJarStagerContractTest.class
                .getResourceAsStream("MixinCustomTargetFixture.class")) {
            if (input == null) throw new AssertionError("custom-target fixture class missing");
            original = input.readAllBytes();
        }
        byte[] remapped = new ClassFileNamespaceRemapper(mappings).remapMixin(
            "cppfm.api_fixture.MixinCustomTargetFixture", original);
        class FixtureLoader extends ClassLoader {
            FixtureLoader() { super(ModJarStagerContractTest.class.getClassLoader()); }
            Class<?> defineFixture() {
                return defineClass("cppfm.api_fixture.MixinCustomTargetFixture",
                    remapped, 0, remapped.length);
            }
        }
        Class<?> fixture = new FixtureLoader().defineFixture();
        var mixin = fixture.getAnnotation(org.spongepowered.asm.mixin.Mixin.class);
        equal("net.minecraft.example.method_2", mixin.targets()[0],
            "custom Mixin target containing a member-like token remains unchanged");
        var accessor = fixture.getDeclaredMethod("getField")
            .getAnnotation(org.spongepowered.asm.mixin.gen.Accessor.class);
        equal("field_1", accessor.value(),
            "unmapped Minecraft-named target does not inherit a global field mapping");
        var invoker = fixture.getDeclaredMethod("invokeTarget")
            .getAnnotation(org.spongepowered.asm.mixin.gen.Invoker.class);
        equal("method_2", invoker.value(),
            "unmapped Minecraft-named target does not inherit a global method mapping");
    }

    private static void assertSharedStringAndFieldDescriptor(byte[] bytes)
        throws IOException {
        try (DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(bytes))) {
            if (input.readInt() != 0xCAFEBABE)
                throw new AssertionError("shared-descriptor fixture has invalid class magic");
            input.readUnsignedShort();
            input.readUnsignedShort();
            int constantPoolCount = input.readUnsignedShort();
            String[] utf8 = new String[constantPoolCount];
            int[] stringUtf8Indices = new int[constantPoolCount];
            for (int index = 1; index < constantPoolCount; ++index) {
                switch (input.readUnsignedByte()) {
                    case 1 -> utf8[index] = input.readUTF();
                    case 3, 4 -> input.readInt();
                    case 5, 6 -> {
                        input.readLong();
                        ++index;
                    }
                    case 7, 16, 19, 20 -> input.readUnsignedShort();
                    case 8 -> stringUtf8Indices[index] = input.readUnsignedShort();
                    case 9, 10, 11, 12, 17, 18 -> {
                        input.readUnsignedShort();
                        input.readUnsignedShort();
                    }
                    case 15 -> {
                        input.readUnsignedByte();
                        input.readUnsignedShort();
                    }
                    default -> throw new AssertionError("unsupported class-file constant tag");
                }
            }
            input.readUnsignedShort();
            input.readUnsignedShort();
            input.readUnsignedShort();
            int interfaceCount = input.readUnsignedShort();
            input.skipNBytes(2L * interfaceCount);
            int fieldCount = input.readUnsignedShort();
            int valueDescriptorIndex = 0;
            for (int field = 0; field < fieldCount; ++field) {
                input.readUnsignedShort();
                int nameIndex = input.readUnsignedShort();
                int descriptorIndex = input.readUnsignedShort();
                if ("value".equals(utf8[nameIndex]))
                    valueDescriptorIndex = descriptorIndex;
                int attributeCount = input.readUnsignedShort();
                for (int attribute = 0; attribute < attributeCount; ++attribute) {
                    input.readUnsignedShort();
                    int length = input.readInt();
                    if (length < 0) throw new AssertionError("negative class attribute length");
                    input.skipNBytes(length);
                }
            }
            if (valueDescriptorIndex == 0 ||
                !"Lcppfm/api_fixture/ModJarStagerContractTest$IntermediaryType;"
                    .equals(utf8[valueDescriptorIndex]))
                throw new AssertionError("fixture field does not have its intermediary descriptor");
            for (int index = 1; index < constantPoolCount; ++index) {
                if (stringUtf8Indices[index] == valueDescriptorIndex) return;
            }
            throw new AssertionError(
                "javac fixture does not share the CONSTANT_String and field descriptor UTF-8 entry");
        }
    }

    private static void assertSharedEnumAndAnnotationString(byte[] bytes)
        throws IOException {
        try (DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(bytes))) {
            if (input.readInt() != 0xCAFEBABE)
                throw new AssertionError("annotation fixture has invalid class magic");
            input.readUnsignedShort();
            input.readUnsignedShort();
            int constantPoolCount = input.readUnsignedShort();
            String[] utf8 = new String[constantPoolCount];
            for (int index = 1; index < constantPoolCount; ++index) {
                switch (input.readUnsignedByte()) {
                    case 1 -> utf8[index] = input.readUTF();
                    case 3, 4 -> input.readInt();
                    case 5, 6 -> {
                        input.readLong();
                        ++index;
                    }
                    case 7, 8, 16, 19, 20 -> input.readUnsignedShort();
                    case 9, 10, 11, 12, 17, 18 -> {
                        input.readUnsignedShort();
                        input.readUnsignedShort();
                    }
                    case 15 -> {
                        input.readUnsignedByte();
                        input.readUnsignedShort();
                    }
                    default -> throw new AssertionError(
                        "unsupported annotation-fixture constant tag");
                }
            }
            input.readUnsignedShort();
            input.readUnsignedShort();
            input.readUnsignedShort();
            int interfaceCount = input.readUnsignedShort();
            input.skipNBytes(2L * interfaceCount);

            Set<Integer> strings = new HashSet<>();
            Set<Integer> enumNames = new HashSet<>();
            readAnnotatedMembers(input, utf8, strings, enumNames);
            readAnnotatedMembers(input, utf8, strings, enumNames);
            readAnnotationAttributes(input, utf8, strings, enumNames);
            for (int index : enumNames) {
                if (strings.contains(index) && "BEFORE".equals(utf8[index])) return;
            }
            throw new AssertionError(
                "javac fixture must share enum-name and annotation-string UTF-8 indexes");
        }
    }

    private static void readAnnotatedMembers(DataInputStream input, String[] utf8,
                                             Set<Integer> strings,
                                             Set<Integer> enumNames)
        throws IOException {
        int count = input.readUnsignedShort();
        for (int member = 0; member < count; ++member) {
            input.readUnsignedShort();
            input.readUnsignedShort();
            input.readUnsignedShort();
            readAnnotationAttributes(input, utf8, strings, enumNames);
        }
    }

    private static void readAnnotationAttributes(DataInputStream input,
                                                 String[] utf8,
                                                 Set<Integer> strings,
                                                 Set<Integer> enumNames)
        throws IOException {
        int count = input.readUnsignedShort();
        for (int attribute = 0; attribute < count; ++attribute) {
            int nameIndex = input.readUnsignedShort();
            int length = input.readInt();
            if (length < 0) throw new AssertionError("negative annotation attribute length");
            byte[] info = input.readNBytes(length);
            if (info.length != length)
                throw new AssertionError("truncated annotation fixture attribute");
            String name = utf8[nameIndex];
            if (!"RuntimeVisibleAnnotations".equals(name) &&
                !"RuntimeInvisibleAnnotations".equals(name)) continue;
            try (DataInputStream annotations = new DataInputStream(
                    new ByteArrayInputStream(info))) {
                for (int i = 0, annotationsCount = annotations.readUnsignedShort();
                     i < annotationsCount; ++i)
                    readAnnotation(annotations, strings, enumNames);
                if (annotations.available() != 0)
                    throw new AssertionError("trailing annotation fixture bytes");
            }
        }
    }

    private static void readAnnotation(DataInputStream input, Set<Integer> strings,
                                       Set<Integer> enumNames) throws IOException {
        input.readUnsignedShort();
        for (int i = 0, values = input.readUnsignedShort(); i < values; ++i) {
            input.readUnsignedShort();
            readAnnotationValue(input, strings, enumNames);
        }
    }

    private static void readAnnotationValue(DataInputStream input,
                                            Set<Integer> strings,
                                            Set<Integer> enumNames)
        throws IOException {
        switch (input.readUnsignedByte()) {
            case 'B', 'C', 'D', 'F', 'I', 'J', 'S', 'Z' -> input.readUnsignedShort();
            case 's' -> strings.add(input.readUnsignedShort());
            case 'e' -> {
                input.readUnsignedShort();
                enumNames.add(input.readUnsignedShort());
            }
            case 'c' -> input.readUnsignedShort();
            case '@' -> readAnnotation(input, strings, enumNames);
            case '[' -> {
                for (int i = 0, count = input.readUnsignedShort(); i < count; ++i)
                    readAnnotationValue(input, strings, enumNames);
            }
            default -> throw new AssertionError("unknown annotation fixture element tag");
        }
    }

    private static void remapDirectContent(IntermediaryNamedMappings mappings)
        throws IOException {
        equal("resolve", mappings.mapMethodName(
            "net/minecraft/class_100", "method_2", null),
            "descriptorless method resolves in its declared target owner");
        equal("otherResolve", mappings.mapMethodName(
            "net/minecraft/class_101", "method_2", null),
            "same descriptorless symbol resolves independently for another owner");
        equal("method_3", mappings.mapMethodName(
            "net/minecraft/class_102", "method_3", null),
            "descriptorless overloaded symbol stays unchanged when owner-local mapping is ambiguous");
        equal("firstName", mappings.mapMethodName(
            "net/minecraft/class_102", "method_3", "()V"),
            "first overloaded mapping is present and descriptor-selectable");
        equal("secondName", mappings.mapMethodName(
            "net/minecraft/class_102", "method_3", "(I)V"),
            "second overloaded mapping is present and descriptor-selectable");
        equal("field_1", mappings.mapFieldName(
            "net/minecraft/example/method_2", "field_1", null),
            "unknown target under net/minecraft cannot use global field fallback");
        equal("method_2", mappings.mapMethodName(
            "net/minecraft/example/method_2", "method_2", null),
            "unknown target under net/minecraft cannot use global method fallback");
        equal("resolve", mappings.mapMixinSelector("method_2",
            Set.of("net/minecraft/class_100"),
            IntermediaryNamedMappings.MixinSelectorKind.METHOD),
            "mixin selector uses its declared owner when descriptor is absent");
        equal("resolve*()V", mappings.mapMixinSelector("method_2*()V",
            Set.of("net/minecraft/class_100"),
            IntermediaryNamedMappings.MixinSelectorKind.METHOD),
            "Mixin selector quantifier is preserved while the member maps");
        equal("net.minecraft.example.Target.resolve{1,3}()V",
            mappings.mapMixinSelector(
                "net.minecraft.class_100.method_2{1,3}()V", Set.of(),
                IntermediaryNamedMappings.MixinSelectorKind.METHOD),
            "dotted-owner selector maps both its owner and quantified member");
        equal("net.minecraft.example.Target.value*:I",
            mappings.mapMixinSelector("net.minecraft.class_100.field_1*:I",
                Set.of(), IntermediaryNamedMappings.MixinSelectorKind.FIELD),
            "dotted-owner field selector preserves quantifier and descriptor");
        equal("resolve", mappings.resolveMethod(
            "net/minecraft/example/Target", "method_2", null),
            "transformer resolver maps descriptorless selectors using named target owner");
        String actual = new String(AccessWidenerNamespaceRemapper.remap(
            AW_INTERMEDIARY.getBytes(StandardCharsets.UTF_8), mappings),
            StandardCharsets.UTF_8);
        equal(AW_NAMED, actual, "access-widener targets and descriptors remap");

        byte[] empty = "accessWidener\tv2\tintermediary".getBytes(StandardCharsets.UTF_8);
        equal("accessWidener\tv2\tnamed", new String(
            AccessWidenerNamespaceRemapper.remap(empty, mappings),
            StandardCharsets.UTF_8), "empty access widener namespace remaps");

        byte[] alreadyNamed = "accessWidener v1 named\naccessible class a/B\n"
            .getBytes(StandardCharsets.UTF_8);
        bytes(alreadyNamed, AccessWidenerNamespaceRemapper.remap(alreadyNamed, mappings),
            "named access widener remains byte-identical");
    }

    private static void remapTopLevelAndNestedArchives(
        Path root, IntermediaryNamedMappings mappings) throws Exception {
        Path source = Files.createDirectory(root.resolve("mods"));
        Path destination = root.resolve("staged");
        byte[] originalTopLevel = AW_INTERMEDIARY.getBytes(StandardCharsets.UTF_8);
        byte[] originalNested = AW_INTERMEDIARY.getBytes(StandardCharsets.UTF_8);
        byte[] nestedJar = zip(Map.of("META-INF/nested.accesswidener", originalNested));
        byte[] modJar = zip(Map.of(
            "fabric.mod.json", "{\"schemaVersion\":1,\"id\":\"probe\",\"version\":\"1\"}"
                .getBytes(StandardCharsets.UTF_8),
            "probe.accesswidener", originalTopLevel,
            "META-INF/jars/nested.jar", nestedJar));
        Path originalMod = source.resolve("probe.jar");
        Files.write(originalMod, modJar);

        ModJarStager.Result result = ModJarStager.stage(source, destination, mappings);
        equal(1, result.modCount(), "one mod staged");
        equal(AW_NAMED, new String(readZipEntry(
            destination.resolve("probe.jar"), "probe.accesswidener"),
            StandardCharsets.UTF_8), "top-level widener remapped");
        byte[] stagedNested = readZipEntry(destination.resolve("probe.jar"),
            "META-INF/jars/nested.jar");
        equal(AW_NAMED, new String(readZipEntry(stagedNested,
            "META-INF/nested.accesswidener"), StandardCharsets.UTF_8),
            "nested widener remapped");
        bytes(modJar, Files.readAllBytes(originalMod), "source mod remains unchanged");
    }

    private static void enforceExpandedSizeBudgetsAndCleanup(
        Path root, IntermediaryNamedMappings mappings) throws IOException {
        byte[] metadata = "{\"schemaVersion\":1,\"id\":\"bounded\",\"version\":\"1\"}"
            .getBytes(StandardCharsets.UTF_8);
        Path perEntrySource = Files.createDirectory(root.resolve("per-entry-limit-mods"));
        Files.write(perEntrySource.resolve("a-valid.jar"), zip(Map.of(
            "fabric.mod.json", metadata,
            "payload.dat", new byte[64])));
        Files.write(perEntrySource.resolve("z-large.jar"), zip(Map.of(
            "fabric.mod.json", metadata,
            "payload.dat", new byte[513])));
        Path perEntryDestination = root.resolve("per-entry-limit-staged");
        ModJarStager.Limits perEntryLimits = new ModJarStager.Limits(
            2_048, 512, 512, 256, 512, 100, 100);
        expectStagingFailure(perEntrySource, perEntryDestination, mappings,
            perEntryLimits, "entry-size limit rejects oversized content");
        if (Files.exists(perEntryDestination))
            throw new AssertionError("failed staging left partial destination files");

        Path totalSource = Files.createDirectory(root.resolve("total-limit-mods"));
        Files.write(totalSource.resolve("combined.jar"), zip(Map.of(
            "fabric.mod.json", metadata,
            "first.dat", new byte[300],
            "second.dat", new byte[300])));
        Path totalDestination = root.resolve("total-limit-staged");
        ModJarStager.Limits totalLimits = new ModJarStager.Limits(
            512, 384, 384, 256, 384, 100, 100);
        expectStagingFailure(totalSource, totalDestination, mappings,
            totalLimits, "cumulative expanded-size limit rejects archive bombs");
        if (Files.exists(totalDestination))
            throw new AssertionError("total-budget failure left a partial destination");
    }

    private static void countNestedJarContentsOnce(
        Path root, IntermediaryNamedMappings mappings) throws IOException {
        byte[] metadata = "{\"schemaVersion\":1,\"id\":\"nestedbudget\",\"version\":\"1\"}"
            .getBytes(StandardCharsets.UTF_8);
        byte[] payload = new byte[8_192];
        new Random(0xC0FFEE).nextBytes(payload);
        byte[] nestedJar = zip(Map.of("payload.bin", payload));
        byte[] modJar = zip(Map.of(
            "fabric.mod.json", metadata,
            "META-INF/jars/nested.jar", nestedJar));
        Path source = Files.createDirectory(root.resolve("nested-budget-mods"));
        Files.write(source.resolve("nestedbudget.jar"), modJar);
        Path destination = root.resolve("nested-budget-staged");
        ModJarStager.Limits limits = new ModJarStager.Limits(
            9_000, 8_192, 512, 256, 9_000, 100, 100);

        ModJarStager.Result result = ModJarStager.stage(source, destination, mappings, limits);
        equal(1, result.modCount(),
            "nested compressed archive bytes do not consume expanded-content budget repeatedly");
        bytes(payload, readZipEntry(readZipEntry(destination.resolve("nestedbudget.jar"),
            "META-INF/jars/nested.jar"), "payload.bin"),
            "nested archive resource remains byte-identical under expanded-content budget");
    }

    private static void expectStagingFailure(
        Path source, Path destination, IntermediaryNamedMappings mappings,
        ModJarStager.Limits limits, String message) throws IOException {
        try {
            ModJarStager.stage(source, destination, mappings, limits);
        } catch (IOException expected) {
            if (!expected.getMessage().contains("limit")) throw expected;
            System.out.println("  PASS: " + message);
            return;
        }
        throw new AssertionError(message + " (archive was accepted)");
    }

    private static void rejectUnsupportedInput(IntermediaryNamedMappings mappings)
        throws IOException {
        rejects("accessWidener v2 official\n", mappings,
            "unsupported namespace fails closed");
        rejects("accessWidener v2 intermediary\naccessible class\n", mappings,
            "malformed target fails closed");
        rejects("accessWidener v2 intermediary\naccessible class a/B\n"
            .getBytes(StandardCharsets.UTF_8), IntermediaryNamedMappings.identity(),
            "missing mapping fails closed");
    }

    private static void rejectDestinationSymlinkIntoSource(
        Path root, IntermediaryNamedMappings mappings) throws IOException {
        Path source = Files.createDirectory(root.resolve("symlink-source-mods"));
        byte[] original = zip(Map.of("fabric.mod.json",
            "{\"schemaVersion\":1,\"id\":\"symlink_probe\",\"version\":\"1\"}"
                .getBytes(StandardCharsets.UTF_8)));
        Path sourceJar = source.resolve("source.jar");
        Files.write(sourceJar, original);
        Path alias = root.resolve("mods-output-alias");
        try {
            Files.createSymbolicLink(alias, source);
        } catch (UnsupportedOperationException | IOException failure) {
            if (System.getProperty("os.name", "").startsWith("Windows")) {
                System.out.println("MOD JAR STAGER SYMLINK CONTRACT SKIP: "
                    + failure.getClass().getSimpleName());
                return;
            }
            throw failure;
        }
        try {
            ModJarStager.stage(source, alias, mappings);
        } catch (IOException expected) {
            if (!expected.getMessage().contains("resolve inside the original"))
                throw expected;
            bytes(original, Files.readAllBytes(sourceJar),
                "rejected symlink destination leaves original mod tree untouched");
            return;
        }
        throw new AssertionError("destination symlink into source tree was accepted");
    }

    private static void rejects(String source, IntermediaryNamedMappings mappings,
                                String message) {
        rejects(source.getBytes(StandardCharsets.UTF_8), mappings, message);
    }

    private static void rejects(byte[] source, IntermediaryNamedMappings mappings,
                                String message) {
        try {
            AccessWidenerNamespaceRemapper.remap(source, mappings);
        } catch (IOException expected) {
            return;
        }
        throw new AssertionError(message);
    }

    private static IntermediaryNamedMappings mapping(Path root) throws IOException {
        Path mapping = root.resolve("mappings.tiny");
        Files.writeString(mapping, TINY, StandardCharsets.UTF_8);
        return IntermediaryNamedMappings.load(mapping);
    }

    private static byte[] zip(Map<String, byte[]> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream output = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, byte[]> entry : entries.entrySet()) {
                output.putNextEntry(new ZipEntry(entry.getKey()));
                output.write(entry.getValue());
                output.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static byte[] readZipEntry(Path archive, String name) throws IOException {
        return readZipEntry(Files.readAllBytes(archive), name);
    }

    private static byte[] readZipEntry(byte[] archiveBytes, String name)
        throws IOException {
        try (ZipInputStream archive = new ZipInputStream(
            new ByteArrayInputStream(archiveBytes))) {
            for (ZipEntry entry; (entry = archive.getNextEntry()) != null; ) {
                if (entry.getName().equals(name)) return archive.readAllBytes();
            }
        }
        throw new IOException("missing archive entry " + name);
    }

    private static void bytes(byte[] expected, byte[] actual, String message) {
        if (!java.util.Arrays.equals(expected, actual))
            throw new AssertionError(message);
    }

    private static void equal(Object expected, Object actual, String message) {
        if (!expected.equals(actual))
            throw new AssertionError(message + ": expected " + expected + ", got " + actual);
    }

    private static void deleteTree(Path root) throws IOException {
        try (var paths = Files.walk(root)) {
            for (Path path : paths.sorted(Comparator.reverseOrder()).toList())
                Files.deleteIfExists(path);
        }
    }

    public static final class IntermediaryType {}

    public static final class NamedType {}
}

final class MixinStringConstantFixture {
    static final String TEXT = "method_2";
}

final class SharedDescriptorStringFixture {
    static final String TEXT =
        "Lcppfm/api_fixture/ModJarStagerContractTest$IntermediaryType;";
    ModJarStagerContractTest.IntermediaryType value;
}
