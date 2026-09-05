package cppfm.transform;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Self-contained structural Mixin transformer for pre-definition class bytes.
 *
 * <p>This is intentionally a class-file implementation rather than an
 * annotation-reflection shim.  It parses Mixin metadata, copies handler/helper
 * methods into the target class, relocates Code/exception/debug/frame tables,
 * and edits real JVM instructions.  Supported sites include HEAD, TAIL,
 * RETURN, INVOKE, FIELD, NEW, CONSTANT, JUMP, LOAD and STORE; supported
 * operations include Inject, Overwrite, Redirect, ModifyArg,
 * ModifyConstant, ModifyVariable, Accessor, Invoker and Shadow reference
 * remapping.</p>
 *
 * <p>When an operation would require a frame computation this implementation
 * cannot prove, the whole class is left byte-for-byte unchanged in non-strict
 * mode and a diagnostic is published.  Strict mode throws
 * {@link TransformException}.  This fail-closed behavior is part of the
 * launcher contract.</p>
 */
public final class MixinClassTransformer implements ClassFileTransformer {
    private final Map<String, List<MixinDefinition>> definitions = new ConcurrentHashMap<>();
    private final Set<String> mixinClassNames = ConcurrentHashMap.newKeySet();
    private final List<String> diagnostics = Collections.synchronizedList(new ArrayList<>());
    private final AtomicLong nextRegistrationOrder = new AtomicLong();
    private volatile boolean strict;
    private volatile DescriptorResolver resolver = DescriptorResolver.IDENTITY;
    private volatile ClassLoader mixinClassLoader;

    public MixinClassTransformer() {
        this(false);
    }

    public MixinClassTransformer(boolean strict) {
        this.strict = strict;
    }

    public void setStrict(boolean strict) {
        this.strict = strict;
    }

    public boolean isStrict() {
        return strict;
    }

    public void setDescriptorResolver(DescriptorResolver resolver) {
        this.resolver = resolver == null ? DescriptorResolver.IDENTITY : resolver;
    }

    /** The one mod loader used to resolve all registered mixin classes. */
    public void setMixinClassLoader(ClassLoader loader) {
        this.mixinClassLoader = loader;
    }

    /** Resolve a registered mod/mixin class through the single custom loader. */
    public Class<?> resolveMixinClass(String binaryName) throws ClassNotFoundException {
        ClassLoader loader = mixinClassLoader;
        if (loader == null) throw new ClassNotFoundException("mixin class loader is not configured");
        return loader.loadClass(binaryName.replace('/', '.'));
    }

    /** Register class bytes before the target class is defined. */
    public void registerMixinClass(String binaryName, byte[] bytes) {
        registerMixin(binaryName, bytes);
    }

    /** Alias used by launchers that call the registration operation directly. */
    public void registerMixin(String binaryName, byte[] bytes) {
        if (bytes == null) throw new NullPointerException("bytes");
        ClassFileModel model = ClassFileModel.parse(bytes);
        String mixinName = model.binaryName();
        if (binaryName != null && !binaryName.isEmpty()) mixinName = binaryName.replace('/', '.');
        mixinClassNames.add(normalizeInternal(mixinName));
        AnnotationModel annotation = AnnotationModel.first(model.attributes, model.pool, "Mixin");
        if (annotation == null) {
            report("class is not annotated with @Mixin: " + mixinName, false);
            return;
        }
        ArrayList<String> targets = new ArrayList<>();
        for (AnnotationModel.ElementValue value : annotation.array("value")) {
            if (value.value instanceof String descriptor) {
                String target = classDescriptorToInternal(descriptor);
                if (!target.isEmpty()) targets.add(target);
            }
        }
        for (String target : annotation.strings("targets")) {
            String normalized = target.replace('.', '/');
            if (normalized.startsWith("L") && normalized.endsWith(";")) normalized = classDescriptorToInternal(normalized);
            if (!normalized.isEmpty()) targets.add(normalized);
        }
        if (targets.isEmpty()) {
            report("mixin has no target: " + mixinName, false);
            return;
        }
        MixinDefinition definition = new MixinDefinition(mixinName, model, targets,
            annotation.integer("priority", 1000), nextRegistrationOrder.getAndIncrement());
        for (String target : targets) {
            definitions.computeIfAbsent(normalizeInternal(target), ignored ->
                Collections.synchronizedList(new ArrayList<>())).add(definition);
            definitions.get(normalizeInternal(target)).sort(definitionComparator());
        }
    }

    /** Register every server/common entry from a parsed Mixin JSON object. */
    public void registerConfiguration(MixinConfiguration configuration, ClassLoader loader) {
        if (configuration == null) throw new NullPointerException("configuration");
        if (loader != null) mixinClassLoader = loader;
        for (String mixin : configuration.serverMixins()) {
            String resource = mixin.replace('.', '/') + ".class";
            try (InputStream stream = (loader == null ? ClassLoader.getSystemResourceAsStream(resource)
                                      : loader.getResourceAsStream(resource))) {
                if (stream == null) {
                    report("mixin class not found: " + mixin, configuration.isRequired());
                    continue;
                }
                registerMixin(mixin, readAll(stream));
            } catch (IOException failure) {
                report("cannot read mixin class " + mixin + ": " + failure, configuration.isRequired());
            }
        }
    }

    public List<String> getDiagnostics() {
        synchronized (diagnostics) {
            return Collections.unmodifiableList(new ArrayList<>(diagnostics));
        }
    }

    public Map<String, List<String>> registeredTargets() {
        LinkedHashMap<String, List<String>> output = new LinkedHashMap<>();
        for (Map.Entry<String, List<MixinDefinition>> entry : definitions.entrySet()) {
            ArrayList<String> names = new ArrayList<>();
            for (MixinDefinition definition : entry.getValue()) names.add(definition.name);
            output.put(entry.getKey(), Collections.unmodifiableList(names));
        }
        return Collections.unmodifiableMap(output);
    }

    @Override
    public byte[] transform(String binaryName, byte[] originalBytes, TransformContext context) {
        String internalName = normalizeInternal(binaryName);
        if (mixinClassNames.contains(internalName)) return exposeMixinHelperFields(originalBytes);
        List<MixinDefinition> matching = definitions.get(internalName);
        if (matching == null || matching.isEmpty()) return originalBytes;
        try {
            ClassFileModel target = ClassFileModel.parse(originalBytes);
            if (!normalizeInternal(target.internalName()).equals(internalName))
                throw new TransformException("class name does not match transform request: " + binaryName);
            boolean changed = false;
            // Mixin applies lower-priority definitions first.  At a shared
            // injection point, insertion-before preserves that callback order;
            // later operations still see the already transformed instruction
            // stream and can deliberately replace it.
            List<MixinDefinition> ordered = new ArrayList<>(matching);
            ordered.sort(applicationComparator());
            List<PreparedMixin> prepared = new ArrayList<>();
            for (MixinDefinition definition : ordered) prepared.add(prepare(target, definition));
            List<MixinOperation> operations = new ArrayList<>();
            for (PreparedMixin mixin : prepared) {
                List<MemberModel> methods = new ArrayList<>(mixin.definition.model.methods);
                for (int index = 0; index < methods.size(); ++index) {
                    MemberModel method = methods.get(index);
                    String name = method.name(mixin.definition.model.pool);
                    if (!name.equals("<init>") && !name.equals("<clinit>"))
                        operations.add(new MixinOperation(mixin, method, index));
                }
            }
            operations.sort(Comparator.comparingInt((MixinOperation item) -> injectorOrder(
                    item.method, item.mixin.definition.model))
                .thenComparingInt(item -> item.mixin.definition.priority)
                .thenComparingLong(item -> item.mixin.definition.registrationOrder)
                .thenComparingInt(item -> item.declarationOrder));
            for (MixinOperation operation : operations)
                changed |= applyMixinMethod(target, operation.mixin, operation.method, context);
            if (!changed) return originalBytes;
            ClassFileSafety.validate(target);
            byte[] transformed = target.write();
            ClassFileSafety.validateBytes(transformed);
            return transformed;
        } catch (TransformException failure) {
            String message = "left " + binaryName + " untransformed: " + failure.getMessage();
            context.diagnostic(message);
            report(message, false);
            if (strict || context.isStrict()) throw failure;
            return originalBytes;
        } catch (RuntimeException failure) {
            String message = "left " + binaryName + " untransformed after parser failure: " + failure;
            context.diagnostic(message);
            report(message, false);
            if (strict || context.isStrict()) throw new TransformException(message, failure);
            return originalBytes;
        }
    }

    private boolean applyMixinMethod(ClassFileModel target, PreparedMixin prepared,
                                     MemberModel mixinMethod, TransformContext context) {
        boolean changed = false;
        AnnotationModel overwrite = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "Overwrite");
        if (overwrite != null) changed |= applyOverwrite(target, prepared, mixinMethod, context);
        AnnotationModel accessor = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "Accessor");
        if (accessor != null) changed |= applyAccessor(target, prepared, mixinMethod, accessor, context);
        AnnotationModel invoker = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "Invoker");
        if (invoker != null) changed |= applyInvoker(target, prepared, mixinMethod, invoker, context);
        AnnotationModel inject = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "Inject");
        if (inject != null) changed |= applyInject(target, prepared, mixinMethod, inject, context);
        AnnotationModel redirect = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "Redirect");
        if (redirect != null) changed |= applyRedirect(target, prepared, mixinMethod, redirect, context);
        AnnotationModel modifyArg = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "ModifyArg");
        if (modifyArg != null) changed |= applyModifyArg(target, prepared, mixinMethod, modifyArg, context);
        AnnotationModel modifyConstant = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "ModifyConstant");
        if (modifyConstant != null) changed |= applyModifyConstant(target, prepared, mixinMethod, modifyConstant, context);
        AnnotationModel modifyVariable = AnnotationModel.first(mixinMethod.attributes, prepared.definition.model.pool, "ModifyVariable");
        if (modifyVariable != null) changed |= applyModifyVariable(target, prepared, mixinMethod, modifyVariable, context);
        return changed;
    }

    private static Comparator<MixinDefinition> definitionComparator() {
        return Comparator.comparingInt((MixinDefinition item) -> item.priority)
            .thenComparingLong(item -> item.registrationOrder);
    }

    private static Comparator<MixinDefinition> applicationComparator() {
        return Comparator.comparingInt((MixinDefinition item) -> item.priority)
            .thenComparingLong(item -> item.registrationOrder);
    }

    /** Match Mixin's injector phases for the subset represented by this transformer. */
    private int injectorOrder(MemberModel method, ClassFileModel model) {
        if (AnnotationModel.first(method.attributes, model.pool, "Overwrite") != null
            || AnnotationModel.first(method.attributes, model.pool, "Accessor") != null
            || AnnotationModel.first(method.attributes, model.pool, "Invoker") != null) return 0;
        AnnotationModel inject = AnnotationModel.first(method.attributes, model.pool, "Inject");
        if (inject != null) return inject.integer("order", 1000);
        if (AnnotationModel.first(method.attributes, model.pool, "Redirect") != null) return 10000;
        return 1000;
    }

    private PreparedMixin prepare(ClassFileModel target, MixinDefinition definition) {
        PreparedMixin prepared = new PreparedMixin(definition);
        String sourceOwner = definition.model.internalName();
        String targetOwner = target.internalName();
        for (MemberModel field : definition.model.fields) {
            AnnotationModel shadow = AnnotationModel.first(field.attributes, definition.model.pool, "Shadow");
            String sourceName = field.name(definition.model.pool);
            String fieldDescriptor = field.descriptor(definition.model.pool);
            if (shadow == null) {
                String key = sourceName + fieldDescriptor;
                // Static helper state is already initialized by the mixin
                // class' own <clinit>.  Keep that field owner and expose the
                // field on the transformed mixin class below, rather than
                // silently copying a null AtomicBoolean/Map into the target.
                if ((field.access & 0x0008) != 0) {
                    prepared.fieldRenames.put(key, "");
                    continue;
                }
                String desired = sourceName;
                int suffix = 0;
                while (target.field(desired, ConstantPool.remapDescriptor(fieldDescriptor,
                        definition.model.internalName(), target.internalName())) != null)
                    desired = "$cppfm$mixin$" + Integer.toHexString(definition.name.hashCode())
                        + "$" + sourceName + "$" + (++suffix);
                prepared.fieldRenames.put(key, desired);
                MemberModel copy = new MemberModel();
                copy.access = field.access;
                copy.nameIndex = target.pool.addUtf8(desired);
                copy.descriptorIndex = target.pool.addUtf8(ConstantPool.remapDescriptor(
                    fieldDescriptor, definition.model.internalName(), target.internalName()));
                target.fields.add(copy);
                continue;
            }
            String prefix = shadow.string("prefix", "shadow$");
            String targetName = sourceName.startsWith(prefix) ? sourceName.substring(prefix.length()) : sourceName;
            List<String> aliases = shadow.strings("aliases");
            if (target.field(targetName, field.descriptor(definition.model.pool)) == null) {
                for (String alias : aliases) {
                    if (target.field(alias, field.descriptor(definition.model.pool)) != null) {
                        targetName = alias;
                        break;
                    }
                }
            }
            prepared.fieldRenames.put(sourceName + fieldDescriptor, targetName);
        }
        for (MemberModel method : definition.model.methods) {
            String name = method.name(definition.model.pool);
            String descriptor = method.descriptor(definition.model.pool);
            if (name.equals("<init>") || name.equals("<clinit>")) continue;
            AnnotationModel overwrite = AnnotationModel.first(method.attributes, definition.model.pool, "Overwrite");
            AnnotationModel accessor = AnnotationModel.first(method.attributes, definition.model.pool, "Accessor");
            AnnotationModel invoker = AnnotationModel.first(method.attributes, definition.model.pool, "Invoker");
            AnnotationModel shadow = AnnotationModel.first(method.attributes, definition.model.pool, "Shadow");
            if (overwrite != null || accessor != null || invoker != null || shadow != null && method.code(definition.model.pool) == null) {
                prepared.methodRenames.put(name + descriptor, name);
                continue;
            }
            String desired = name;
            if (target.method(desired, descriptor) != null || prepared.hasCopy(desired, descriptor)) {
                desired = "$cppfm$mixin$" + Integer.toHexString(Hashes.sha256(definition.name.getBytes(java.nio.charset.StandardCharsets.UTF_8)).hashCode())
                    + "$" + name;
                int suffix = 0;
                while (target.method(desired, descriptor) != null || prepared.hasCopy(desired, descriptor))
                    desired = "$cppfm$mixin$" + Integer.toHexString(definition.name.hashCode()) + "$" + name + "$" + (++suffix);
            }
            prepared.methodRenames.put(name + descriptor, desired);
        }
        prepared.bootstrapOffset = appendBootstrapMethods(target, definition.model, prepared);
        for (MemberModel method : definition.model.methods) {
            String name = method.name(definition.model.pool);
            String descriptor = method.descriptor(definition.model.pool);
            if (name.equals("<init>") || name.equals("<clinit>")) continue;
            if (AnnotationModel.first(method.attributes, definition.model.pool, "Overwrite") != null
                || AnnotationModel.first(method.attributes, definition.model.pool, "Accessor") != null
                || AnnotationModel.first(method.attributes, definition.model.pool, "Invoker") != null
                || method.code(definition.model.pool) == null) continue;
            String desired = prepared.methodRenames.get(name + descriptor);
            if (target.method(desired, ConstantPool.remapDescriptor(descriptor, sourceOwner, targetOwner)) != null) continue;
            MemberModel copy = copyMethod(target, definition, method, desired, prepared);
            target.addMethod(copy);
            prepared.copiedMethods.put(name + descriptor, copy);
        }
        return prepared;
    }

    /**
     * Merge a mixin's class-level bootstrap table before any copied method can
     * import a CONSTANT_Dynamic or CONSTANT_InvokeDynamic entry.  Bootstrap
     * indexes are local to the class file, so appending the entries and
     * retaining the returned offset keeps copied bytecode verifier-valid even
     * when the target already uses invokedynamic.
     */
    private int appendBootstrapMethods(ClassFileModel target, ClassFileModel source,
                                       PreparedMixin prepared) {
        AttributeModel sourceAttribute = source.attribute(source.pool, "BootstrapMethods");
        if (sourceAttribute == null)
            return source.pool.containsTag(17) || source.pool.containsTag(18) ? -1 : 0;
        try {
            int sourceCount = bootstrapMethodCount(sourceAttribute.info, "mixin");
            AttributeModel targetAttribute = target.attribute(target.pool, "BootstrapMethods");
            int targetCount = targetAttribute == null ? 0
                : bootstrapMethodCount(targetAttribute.info, "target");
            if (sourceCount > 65535 - targetCount)
                throw unsupported("too many BootstrapMethods entries");

            ByteArrayOutputStream bytes = new ByteArrayOutputStream(
                (targetAttribute == null ? 2 : targetAttribute.info.length) + sourceAttribute.info.length);
            java.io.DataOutputStream output = new java.io.DataOutputStream(bytes);
            output.writeShort(targetCount + sourceCount);
            if (targetAttribute != null) output.write(targetAttribute.info, 2, targetAttribute.info.length - 2);

            DataInputStream input = new DataInputStream(new ByteArrayInputStream(sourceAttribute.info));
            if (input.readUnsignedShort() != sourceCount)
                throw unsupported("BootstrapMethods count changed while reading mixin");
            for (int i = 0; i < sourceCount; ++i) {
                int methodReference = input.readUnsignedShort();
                output.writeShort(target.pool.importEntry(source.pool, methodReference,
                    source.internalName(), target.internalName(), prepared.methodRenames,
                    prepared.fieldRenames, targetCount));
                int argumentCount = input.readUnsignedShort();
                output.writeShort(argumentCount);
                for (int j = 0; j < argumentCount; ++j) {
                    int argument = input.readUnsignedShort();
                    output.writeShort(target.pool.importEntry(source.pool, argument,
                        source.internalName(), target.internalName(), prepared.methodRenames,
                        prepared.fieldRenames, targetCount));
                }
            }
            if (input.available() != 0) throw unsupported("trailing mixin BootstrapMethods data");
            output.flush();
            byte[] merged = bytes.toByteArray();
            if (targetAttribute == null)
                target.attributes.add(new AttributeModel(target.pool.addUtf8("BootstrapMethods"), merged));
            else targetAttribute.info = merged;
            return targetCount;
        } catch (IOException failure) {
            throw new TransformException("cannot merge BootstrapMethods", failure);
        }
    }

    private int bootstrapMethodCount(byte[] info, String owner) throws IOException {
        DataInputStream input = new DataInputStream(new ByteArrayInputStream(info));
        if (input.available() < 2) throw unsupported("truncated " + owner + " BootstrapMethods");
        int count = input.readUnsignedShort();
        for (int i = 0; i < count; ++i) {
            input.readUnsignedShort();
            int arguments = input.readUnsignedShort();
            for (int j = 0; j < arguments; ++j) input.readUnsignedShort();
        }
        if (input.available() != 0) throw unsupported("trailing " + owner + " BootstrapMethods data");
        return count;
    }

    private MemberModel copyMethod(ClassFileModel target, MixinDefinition definition,
                                   MemberModel source, String desired, PreparedMixin prepared) {
        MemberModel result = new MemberModel();
        result.access = source.access & ~(ClassFileModel.ACC_ABSTRACT | ClassFileModel.ACC_NATIVE);
        result.nameIndex = target.pool.addUtf8(desired);
        result.descriptorIndex = target.pool.addUtf8(ConstantPool.remapDescriptor(
            source.descriptor(definition.model.pool), definition.model.internalName(), target.internalName()));
        CodeModel sourceCode = source.code(definition.model.pool);
        if (sourceCode != null) {
            CodeModel code = copyCode(sourceCode, definition.model.pool, target.pool,
                definition.model.internalName(), target.internalName(), prepared.methodRenames,
                prepared.fieldRenames, prepared.bootstrapOffset);
            result.attributes.add(new AttributeModel(target.pool.addUtf8("Code"), code.write(target.pool)));
        }
        return result;
    }

    private CodeModel copyCode(CodeModel source, ConstantPool sourcePool, ConstantPool targetPool,
                               String sourceOwner, String targetOwner,
                               Map<String, String> methodRenames, Map<String, String> fieldRenames,
                               int bootstrapOffset) {
        CodeModel copy = CodeModel.parse(source.write(sourcePool), sourcePool);
        remapCodeAttributeConstants(copy.attributes, sourcePool, targetPool, sourceOwner, targetOwner,
            methodRenames, fieldRenames);
        List<BytecodeInstructions.Instruction> mapped = BytecodeInstructions.copyWithConstantPool(
            BytecodeInstructions.decode(copy.code), sourcePool, targetPool, sourceOwner, targetOwner,
            methodRenames, fieldRenames, bootstrapOffset);
        BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(copy, targetPool);
        editor.instructions.clear();
        editor.instructions.addAll(mapped);
        editor.finish(targetPool);
        return copy;
    }

    private boolean applyOverwrite(ClassFileModel target, PreparedMixin prepared,
                                   MemberModel source, TransformContext context) {
        String descriptor = ConstantPool.remapDescriptor(source.descriptor(prepared.definition.model.pool),
            prepared.definition.model.internalName(), target.internalName());
        MemberModel destination = target.method(source.name(prepared.definition.model.pool), descriptor);
        if (destination == null) throw unsupported("@Overwrite target not found: "
            + source.name(prepared.definition.model.pool) + descriptor);
        CodeModel sourceCode = source.code(prepared.definition.model.pool);
        if (sourceCode == null) throw unsupported("@Overwrite has no Code: " + source.name(prepared.definition.model.pool));
        CodeModel code = copyCode(sourceCode, prepared.definition.model.pool, target.pool,
            prepared.definition.model.internalName(), target.internalName(), prepared.methodRenames,
            prepared.fieldRenames, prepared.bootstrapOffset);
        destination.access &= ~(ClassFileModel.ACC_ABSTRACT | ClassFileModel.ACC_NATIVE);
        destination.replaceCode(target.pool, code);
        mark(context, destination, target);
        return true;
    }

    private boolean applyAccessor(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                  AnnotationModel annotation, TransformContext context) {
        String descriptor = ConstantPool.remapDescriptor(source.descriptor(prepared.definition.model.pool),
            prepared.definition.model.internalName(), target.internalName());
        Descriptor.MethodDesc method = Descriptor.method(descriptor);
        String methodName = source.name(prepared.definition.model.pool);
        String fieldName = annotation.string("value", "");
        if (fieldName.isEmpty()) fieldName = inferMemberName(methodName, "get", "is", "set");
        MemberModel field = target.field(fieldName, method.returnType.descriptor);
        boolean setter = method.returnType.voidType && method.arguments.size() == 1;
        if (setter) field = target.field(fieldName, method.arguments.get(0).descriptor);
        if (field == null) throw unsupported("@Accessor field not found: " + fieldName + " " + descriptor);
        addImplementedInterface(target, prepared.definition.model.internalName());
        ArrayList<BytecodeInstructions.Instruction> instructions = new ArrayList<>();
        boolean staticField = (field.access & ClassFileModel.ACC_STATIC) != 0;
        if (!staticField) instructions.add(bytes(42));
        if (setter) {
            instructions.addAll(loadLocal(method.arguments.get(0), (source.access & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1));
            int ref = target.pool.addFieldRef(target.internalName(), field.name(target.pool), field.descriptor(target.pool));
            instructions.add(memberInstruction(staticField ? 179 : 181, ref));
            instructions.add(bytes(177));
        } else {
            int ref = target.pool.addFieldRef(target.internalName(), field.name(target.pool), field.descriptor(target.pool));
            instructions.add(memberInstruction(staticField ? 178 : 180, ref));
            instructions.add(returnInstruction(method.returnType));
        }
        replaceOrAddGeneratedMethod(target, source.name(prepared.definition.model.pool), source.access,
            descriptor, instructions, method.arguments, method.returnType);
        mark(context, source.name(prepared.definition.model.pool) + descriptor, target);
        return true;
    }

    private boolean applyInvoker(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                 AnnotationModel annotation, TransformContext context) {
        String descriptor = ConstantPool.remapDescriptor(source.descriptor(prepared.definition.model.pool),
            prepared.definition.model.internalName(), target.internalName());
        Descriptor.MethodDesc method = Descriptor.method(descriptor);
        String sourceName = source.name(prepared.definition.model.pool);
        String invokedName = annotation.string("value", "");
        if (invokedName.isEmpty()) invokedName = inferInvokerName(sourceName);
        MemberModel destination = target.method(invokedName, descriptor);
        if (destination == null) {
            for (MemberModel candidate : target.methodsNamed(invokedName)) {
                if (descriptor.equals(candidate.descriptor(target.pool))) { destination = candidate; break; }
            }
        }
        if (destination == null) throw unsupported("@Invoker target not found: " + invokedName + descriptor);
        addImplementedInterface(target, prepared.definition.model.internalName());
        ArrayList<BytecodeInstructions.Instruction> instructions = new ArrayList<>();
        boolean targetStatic = (destination.access & ClassFileModel.ACC_STATIC) != 0;
        if (!targetStatic) instructions.add(bytes(42));
        int slot = (source.access & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
        for (Descriptor.Type argument : method.arguments) {
            instructions.addAll(loadLocal(argument, slot));
            slot += argument.slots;
        }
        int ref = target.pool.addMethodRef(target.internalName(), destination.name(target.pool),
            destination.descriptor(target.pool), false);
        int opcode = targetStatic ? 184 : ((destination.access & 0x0002) != 0 ? 183 : 182);
        instructions.add(memberInstruction(opcode, ref));
        instructions.add(returnInstruction(method.returnType));
        replaceOrAddGeneratedMethod(target, sourceName, source.access, descriptor, instructions,
            method.arguments, method.returnType);
        mark(context, sourceName + descriptor, target);
        return true;
    }

    private boolean applyInject(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                 AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        List<String> methodNames = annotation.strings("method");
        if (methodNames.isEmpty()) throw unsupported("@Inject has no target method: " + source.name(prepared.definition.model.pool));
        List<AtSpec> atSpecs = readAtSpecs(annotation, "at");
        if (atSpecs.isEmpty()) throw unsupported("@Inject has no @At");
        List<AnnotationModel> slices = nestedAnnotations(annotation, "slice");
        LocalCaptureMode localCapture = LocalCaptureMode.read(annotation);
        boolean changed = false;
        for (AtSpec at : atSpecs) {
            for (String methodName : methodNames) {
                for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, true)) {
                    CodeModel code = destination.code(target.pool);
                    if (code == null) throw unsupported("cannot inject into abstract/native method: " + destination.name(target.pool));
                    rejectUnsafeConstructorInjection(target, destination, at);
                    BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                    List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at, slices);
                    if (sites.isEmpty()) throw unsupported("@Inject site not found: " + methodName + " @" + at.value);
                    if (at.shift == Shift.BY) throw unsupported("@At(shift=BY) requires explicit frame recomputation");
                    boolean returnSite = at.value.equals("RETURN");
                    boolean cancellable = annotation.bool("cancellable", false);
                    if (cancellable)
                        throw unsupported("cancellable injection requires cancellation branch/frame synthesis");
                    StackAnalyzer.Analysis stackAnalysis = (!returnSite || localCapture.requiresAnalysis(handler, target, destination))
                        ? StackAnalyzer.analyze(target, destination, code) : null;
                    LocalVariableTable localTable = stackAnalysis == null ? null : LocalVariableTable.read(code, target.pool);
                    int count = 0;
                    for (int i = sites.size() - 1; i >= 0; --i) {
                        BytecodeInstructions.Instruction site = sites.get(i);
                        List<CapturedLocal> captured = stackAnalysis == null ? List.of()
                            : capturedLocals(target, destination, handler, localCapture, stackAnalysis,
                                localTable, site, at.shift == Shift.AFTER);
                        List<BytecodeInstructions.Instruction> addition = returnSite
                            ? buildReturnInjection(target, destination, handler, editor, site, cancellable, captured)
                            : buildCallbackInjection(target, destination, handler, editor, cancellable,
                                at.value.equals("TAIL"),
                                at.shift == Shift.AFTER ? stackAnalysis.after(site) : stackAnalysis.before(site),
                                captured);
                        if (at.shift == Shift.AFTER) editor.insertAfter(site, addition);
                        else editor.insertBefore(site, addition);
                        count++;
                    }
                    if (count == 0) throw unsupported("@Inject made no changes: " + methodName);
                    editor.finish(target.pool);
                    destination.replaceCode(target.pool, code);
                    mark(context, destination, target);
                    changed = true;
                }
            }
        }
        return changed;
    }

    private List<BytecodeInstructions.Instruction> buildCallbackInjection(ClassFileModel target,
            MemberModel destination, Handler handler, BytecodeInstructions.Editor editor,
            boolean cancellable, boolean returnBoundary,
            List<StackAnalyzer.Value> preservedStack, List<CapturedLocal> capturedLocals) {
        Descriptor.MethodDesc targetDescriptor = Descriptor.method(destination.descriptor(target.pool));
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        int callbackIndex = callbackIndex(handlerDescriptor);
        if (callbackIndex < 0) throw unsupported("Mixin handler has no CallbackInfo parameter: " + handler.name);
        Descriptor.Type callbackType = handlerDescriptor.arguments.get(callbackIndex);
        int callbackLocal = editor.allocateLocal(new Descriptor.Type(callbackType.descriptor, 1, true, false, false, false));
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        ArrayList<Integer> stackLocals = new ArrayList<>();
        ArrayList<Descriptor.Type> stackTypes = new ArrayList<>();
        for (StackAnalyzer.Value value : preservedStack) {
            if (value.isUninitialized())
                throw unsupported("cannot inject across an uninitialized object stack value");
            Descriptor.Type type = stackType(value);
            stackTypes.add(type);
            stackLocals.add(editor.allocateLocal(type));
        }
        for (int i = stackTypes.size() - 1; i >= 0; --i)
            output.addAll(storeLocal(stackTypes.get(i), stackLocals.get(i)));
        Descriptor.Type callbackReturnType = null;
        int callbackReturnLocal = -1;
        boolean returnable = callbackType.descriptor.endsWith("CallbackInfoReturnable;");
        if (returnBoundary && returnable && !targetDescriptor.returnType.voidType
            && !stackTypes.isEmpty()) {
            Descriptor.Type candidate = stackTypes.get(stackTypes.size() - 1);
            if (!compatible(targetDescriptor.returnType, candidate))
                throw unsupported("TAIL return value does not match CallbackInfoReturnable: " + handler.name);
            callbackReturnType = targetDescriptor.returnType;
            callbackReturnLocal = stackLocals.get(stackLocals.size() - 1);
        }
        output.addAll(makeCallbackObject(target, callbackType, destination.name(target.pool) + ":HEAD",
            cancellable, callbackReturnType, editor, callbackLocal, callbackReturnLocal));
        output.addAll(callHandler(target, destination, handler, targetDescriptor, handlerDescriptor,
            callbackIndex, callbackLocal, capturedTypes(capturedLocals), capturedSlots(capturedLocals)));
        if (cancellable) {
            throw unsupported("cancellable callback branch is intentionally fail-closed until frame synthesis");
        }
        for (int i = 0; i < stackTypes.size(); ++i)
            output.addAll(loadLocal(stackTypes.get(i), stackLocals.get(i)));
        return output;
    }

    private List<BytecodeInstructions.Instruction> buildReturnInjection(ClassFileModel target,
            MemberModel destination, Handler handler, BytecodeInstructions.Editor editor,
            BytecodeInstructions.Instruction site, boolean cancellable,
            List<CapturedLocal> capturedLocals) {
        Descriptor.MethodDesc targetDescriptor = Descriptor.method(destination.descriptor(target.pool));
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        int callbackIndex = callbackIndex(handlerDescriptor);
        if (callbackIndex < 0) throw unsupported("Mixin return handler has no CallbackInfo parameter: " + handler.name);
        Descriptor.Type callbackType = handlerDescriptor.arguments.get(callbackIndex);
        Descriptor.Type returnType = targetDescriptor.returnType;
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        if (returnType.voidType) {
            int callbackLocal = editor.allocateLocal(new Descriptor.Type(callbackType.descriptor, 1, true, false, false, false));
            output.addAll(makeCallbackObject(target, callbackType, destination.name(target.pool) + ":RETURN",
                cancellable, null, editor, callbackLocal));
            output.addAll(callHandler(target, destination, handler, targetDescriptor, handlerDescriptor,
                callbackIndex, callbackLocal, capturedTypes(capturedLocals), capturedSlots(capturedLocals)));
            return output;
        }
        int returnLocal = editor.allocateLocal(returnType);
        output.addAll(storeLocal(returnType, returnLocal));
        int callbackLocal = editor.allocateLocal(new Descriptor.Type(callbackType.descriptor, 1, true, false, false, false));
        boolean returnable = callbackType.descriptor.endsWith("CallbackInfoReturnable;");
        output.addAll(makeCallbackObject(target, callbackType, destination.name(target.pool) + ":RETURN",
            cancellable, returnable ? returnType : null, editor, callbackLocal, returnLocal));
        output.addAll(callHandler(target, destination, handler, targetDescriptor, handlerDescriptor,
            callbackIndex, callbackLocal, capturedTypes(capturedLocals), capturedSlots(capturedLocals)));
        if (returnable) output.addAll(loadReturnValue(target, returnType, callbackLocal));
        else output.addAll(loadLocal(returnType, returnLocal));
        return output;
    }

    private List<BytecodeInstructions.Instruction> makeCallbackObject(ClassFileModel target,
            Descriptor.Type callbackType, String id, boolean cancellable, Descriptor.Type returnType,
            BytecodeInstructions.Editor editor, int callbackLocal, int... returnLocal) {
        return makeCallbackObject(target, callbackType, id, cancellable, returnType, editor, callbackLocal,
            returnLocal.length == 0 ? -1 : returnLocal[0]);
    }

    private List<BytecodeInstructions.Instruction> makeCallbackObject(ClassFileModel target,
            Descriptor.Type callbackType, String id, boolean cancellable, Descriptor.Type returnType,
            BytecodeInstructions.Editor editor, int callbackLocal, int returnLocal) {
        String callbackOwner = descriptorOwner(callbackType.descriptor);
        if (callbackOwner.isEmpty()) throw unsupported("invalid CallbackInfo type: " + callbackType.descriptor);
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        int callbackClass = target.pool.addClass(callbackOwner);
        output.add(memberInstruction(187, callbackClass));
        output.add(bytes(89));
        output.add(ldcString(target.pool, id));
        output.add(iconst(cancellable ? 1 : 0));
        if (returnType != null) {
            output.addAll(loadLocal(returnType, returnLocal));
            output.addAll(box(target.pool, returnType));
            int init = target.pool.addMethodRef(callbackOwner, "<init>",
                "(Ljava/lang/String;ZLjava/lang/Object;)V", false);
            output.add(memberInstruction(183, init));
        } else {
            int init = target.pool.addMethodRef(callbackOwner, "<init>", "(Ljava/lang/String;Z)V", false);
            output.add(memberInstruction(183, init));
        }
        output.addAll(storeLocal(new Descriptor.Type(callbackType.descriptor, 1, true, false, false, false), callbackLocal));
        return output;
    }

    private List<BytecodeInstructions.Instruction> callHandler(ClassFileModel target, MemberModel destination,
            Handler handler, Descriptor.MethodDesc targetDescriptor, Descriptor.MethodDesc handlerDescriptor,
            int callbackIndex, int callbackLocal) {
        return callHandler(target, destination, handler, targetDescriptor, handlerDescriptor,
            callbackIndex, callbackLocal, List.of(), List.of());
    }

    private List<BytecodeInstructions.Instruction> callHandler(ClassFileModel target, MemberModel destination,
            Handler handler, Descriptor.MethodDesc targetDescriptor, Descriptor.MethodDesc handlerDescriptor,
            int callbackIndex, int callbackLocal, List<Descriptor.Type> capturedTypes,
            List<Integer> capturedLocals) {
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        if (!handler.isStatic) output.add(bytes(42));
        int handlerArguments = callbackIndex;
        if (capturedTypes.size() != capturedLocals.size())
            throw unsupported("captured local bookkeeping mismatch");
        if (handlerArguments > targetDescriptor.arguments.size() + capturedTypes.size())
            throw unsupported("handler captures unsupported locals: " + handler.name + handler.descriptor);
        int slot = (destination.access & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
        for (int i = 0; i < handlerArguments; ++i) {
            Descriptor.Type expected = handlerDescriptor.arguments.get(i);
            Descriptor.Type actual;
            int local;
            if (i < targetDescriptor.arguments.size()) {
                actual = targetDescriptor.arguments.get(i);
                local = slot;
                slot += actual.slots;
            } else {
                int captured = i - targetDescriptor.arguments.size();
                actual = capturedTypes.get(captured);
                local = capturedLocals.get(captured);
            }
            if (!compatible(expected, actual)) throw unsupported("handler argument does not match target argument: " + handler.name);
            output.addAll(loadLocal(actual, local));
        }
        Descriptor.Type callback = new Descriptor.Type(handlerDescriptor.arguments.get(callbackIndex).descriptor,
            1, true, false, false, false);
        output.addAll(loadLocal(callback, callbackLocal));
        int ref = target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false);
        output.add(memberInstruction(handler.isStatic ? 184 : 182, ref));
        return output;
    }

    private boolean applyRedirect(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                  AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        AtSpec at = AtSpec.read(nestedAnnotation(annotation, "at"));
        if (!(at.value.equals("INVOKE") || at.value.equals("FIELD") || at.value.equals("NEW")))
            throw unsupported("@Redirect requires INVOKE, FIELD or NEW, got " + at.value);
        boolean changed = false;
        for (String methodName : annotation.strings("method")) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot redirect abstract/native method: " + methodName);
                rejectUnsafeConstructorInjection(target, destination, at);
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                    nestedAnnotations(annotation, "slice"));
                if (sites.isEmpty()) throw unsupported("@Redirect site not found: " + methodName);
                for (int i = sites.size() - 1; i >= 0; --i) {
                    BytecodeInstructions.Instruction site = sites.get(i);
                    int index = editor.instructions.indexOf(site);
                    List<BytecodeInstructions.Instruction> replacement;
                    if (at.value.equals("NEW")) {
                        replacement = buildNewRedirect(target, editor, index, handler);
                    } else {
                        replacement = buildMemberRedirect(target, editor, site, handler);
                    }
                    if (at.value.equals("NEW")) {
                        int end = findConstructorEnd(editor.instructions, index, target.pool);
                        editor.replaceRange(index, end, replacement);
                    } else editor.replace(site, replacement);
                }
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        return changed;
    }

    private List<BytecodeInstructions.Instruction> buildMemberRedirect(ClassFileModel target,
            BytecodeInstructions.Editor editor, BytecodeInstructions.Instruction site, Handler handler) {
        int cp = BytecodeInstructions.cpIndex(site);
        if (cp < 0) throw unsupported("member redirect site has no reference");
        String owner = target.pool.memberOwner(cp);
        String name = target.pool.memberName(cp);
        String descriptor = target.pool.memberDescriptor(cp);
        List<Descriptor.Type> stackTypes = new ArrayList<>();
        boolean isField = BytecodeInstructions.isField(site.opcode);
        boolean staticMember = site.opcode == 178 || site.opcode == 179 || site.opcode == 184;
        if (!staticMember && (isField || BytecodeInstructions.isInvoke(site.opcode)))
            stackTypes.add(Descriptor.type("L" + owner + ";"));
        if (isField) {
            Descriptor.Type fieldType = Descriptor.type(descriptor);
            if (site.opcode == 179 || site.opcode == 181) stackTypes.add(fieldType);
        } else {
            Descriptor.MethodDesc invocation = Descriptor.method(descriptor);
            stackTypes.addAll(invocation.arguments);
        }
        Descriptor.Type expectedReturn = isField && (site.opcode == 179 || site.opcode == 181)
            ? Descriptor.type("V")
            : isField ? Descriptor.type(descriptor) : Descriptor.method(descriptor).returnType;
        return spillAndCall(target, editor, stackTypes, handler, stackTypes, expectedReturn);
    }

    private List<BytecodeInstructions.Instruction> buildNewRedirect(ClassFileModel target,
            BytecodeInstructions.Editor editor, int index, Handler handler) {
        int end = findConstructorEnd(editor.instructions, index, target.pool);
        BytecodeInstructions.Instruction newInstruction = editor.instructions.get(index);
        int cp = BytecodeInstructions.cpIndex(newInstruction);
        String owner = target.pool.className(cp);
        BytecodeInstructions.Instruction initInstruction = editor.instructions.get(end);
        Descriptor.MethodDesc constructor = Descriptor.method(target.pool.memberDescriptor(BytecodeInstructions.cpIndex(initInstruction)));
        List<BytecodeInstructions.Instruction> argumentInstructions = new ArrayList<>();
        for (int i = index + 2; i < end; ++i) argumentInstructions.add(editor.instructions.get(i).copy());
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>(argumentInstructions);
        output.addAll(spillAndCall(target, editor, constructor.arguments, handler, constructor.arguments,
            Descriptor.type("L" + owner + ";")));
        return output;
    }

    private int findConstructorEnd(List<BytecodeInstructions.Instruction> instructions, int start, ConstantPool pool) {
        int newCp = BytecodeInstructions.cpIndex(instructions.get(start));
        String owner = pool.className(newCp);
        for (int i = start + 1; i < instructions.size(); ++i) {
            BytecodeInstructions.Instruction instruction = instructions.get(i);
            if (instruction.opcode != 183) continue;
            int cp = BytecodeInstructions.cpIndex(instruction);
            if (cp >= 0 && pool.memberOwner(cp).equals(owner) && pool.memberName(cp).equals("<init>")) return i;
        }
        throw unsupported("NEW has no matching invokespecial <init>: " + owner);
    }

    private List<BytecodeInstructions.Instruction> spillAndCall(ClassFileModel target,
            BytecodeInstructions.Editor editor, List<Descriptor.Type> stackTypes,
            Handler handler, List<Descriptor.Type> handlerTypes, Descriptor.Type expectedReturn) {
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        int expectedArguments = handlerDescriptor.arguments.size();
        if (expectedArguments != handlerTypes.size())
            throw unsupported("handler descriptor does not match redirected member: " + handler.name + handler.descriptor);
        if (expectedReturn != null && !compatible(expectedReturn, handlerDescriptor.returnType))
            throw unsupported("NEW redirect return type mismatch: " + handler.name);
        ArrayList<Integer> locals = new ArrayList<>();
        for (Descriptor.Type type : stackTypes) locals.add(editor.allocateLocal(type));
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        for (int i = stackTypes.size() - 1; i >= 0; --i) output.addAll(storeLocal(stackTypes.get(i), locals.get(i)));
        if (!handler.isStatic) output.add(bytes(42));
        for (int i = 0; i < stackTypes.size(); ++i) output.addAll(loadLocal(stackTypes.get(i), locals.get(i)));
        int ref = target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false);
        output.add(memberInstruction(handler.isStatic ? 184 : 182, ref));
        return output;
    }

    private boolean applyModifyArg(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                   AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        AtSpec at = AtSpec.read(nestedAnnotation(annotation, "at"));
        if (!at.value.equals("INVOKE")) throw unsupported("@ModifyArg requires INVOKE");
        Descriptor.MethodDesc modifier = Descriptor.method(handler.descriptor);
        if (modifier.arguments.size() != 1 || modifier.returnType.voidType)
            throw unsupported("@ModifyArg handler must be (T)T: " + handler.name + handler.descriptor);
        boolean changed = false;
        for (String methodName : annotation.strings("method")) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot modify abstract/native method");
                rejectUnsafeConstructorInjection(target, destination, at);
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                    nestedAnnotations(annotation, "slice"));
                int index = annotation.integer("index", -1);
                for (int i = sites.size() - 1; i >= 0; --i) {
                    BytecodeInstructions.Instruction site = sites.get(i);
                    int cp = BytecodeInstructions.cpIndex(site);
                    if (cp < 0 || !BytecodeInstructions.isInvoke(site.opcode)) throw unsupported("@ModifyArg site is not INVOKE");
                    String owner = target.pool.memberOwner(cp);
                    boolean staticCall = site.opcode == 184;
                    Descriptor.MethodDesc invocation = Descriptor.method(target.pool.memberDescriptor(cp));
                    ArrayList<Descriptor.Type> stackTypes = new ArrayList<>();
                    if (!staticCall) stackTypes.add(Descriptor.type("L" + owner + ";"));
                    stackTypes.addAll(invocation.arguments);
                    int argumentOffset = staticCall ? 0 : 1;
                    int selected = index >= 0 ? index + argumentOffset : selectArgument(invocation.arguments, modifier.arguments.get(0), target.pool, argumentOffset);
                    if (selected < argumentOffset || selected >= stackTypes.size()) throw unsupported("@ModifyArg index out of range");
                    if (!compatible(stackTypes.get(selected), modifier.arguments.get(0))
                        || !compatible(stackTypes.get(selected), modifier.returnType))
                        throw unsupported("@ModifyArg type mismatch");
                    ArrayList<Integer> locals = new ArrayList<>();
                    for (Descriptor.Type type : stackTypes) locals.add(editor.allocateLocal(type));
                    ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                    for (int j = stackTypes.size() - 1; j >= 0; --j) replacement.addAll(storeLocal(stackTypes.get(j), locals.get(j)));
                    if (!handler.isStatic) replacement.add(bytes(42));
                    replacement.addAll(loadLocal(stackTypes.get(selected), locals.get(selected)));
                    int handlerRef = target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false);
                    replacement.add(memberInstruction(handler.isStatic ? 184 : 182, handlerRef));
                    replacement.addAll(storeLocal(stackTypes.get(selected), locals.get(selected)));
                    for (int j = 0; j < stackTypes.size(); ++j) replacement.addAll(loadLocal(stackTypes.get(j), locals.get(j)));
                    BytecodeInstructions.Instruction original = site.copy();
                    original.oldOffset = -1;
                    replacement.add(original);
                    editor.replace(site, replacement);
                }
                if (sites.isEmpty()) throw unsupported("@ModifyArg site not found: " + methodName);
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        return changed;
    }

    private boolean applyModifyConstant(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                        AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc modifier = Descriptor.method(handler.descriptor);
        if (modifier.arguments.size() != 1 || modifier.returnType.voidType
            || !compatible(modifier.arguments.get(0), modifier.returnType))
            throw unsupported("@ModifyConstant handler must be (T)T: " + handler.name + handler.descriptor);
        List<ConstantSpec> constants = readConstantSpecs(annotation);
        boolean changed = false;
        for (String methodName : annotation.strings("method")) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot modify constant in abstract/native method");
                rejectConstructorOperation(target, destination, "@ModifyConstant");
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                List<BytecodeInstructions.Instruction> sites = findConstantSites(editor.instructions, target.pool,
                    constants, nestedAnnotations(annotation, "slice"));
                for (int i = sites.size() - 1; i >= 0; --i) {
                    BytecodeInstructions.Instruction site = sites.get(i);
                    Descriptor.Type type = ConstantSpec.typeFor(target.pool, site);
                    if (type == null || !compatible(type, modifier.arguments.get(0)) || !compatible(type, modifier.returnType))
                        throw unsupported("@ModifyConstant type mismatch at " + site.oldOffset);
                    ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                    if (handler.isStatic) {
                        replacement.add(site.copy());
                        replacement.add(memberInstruction(184, target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                    } else {
                        int local = editor.allocateLocal(type);
                        replacement.add(site.copy());
                        replacement.addAll(storeLocal(type, local));
                        replacement.add(bytes(42));
                        replacement.addAll(loadLocal(type, local));
                        replacement.add(memberInstruction(182, target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                    }
                    editor.replace(site, replacement);
                }
                if (sites.isEmpty()) throw unsupported("@ModifyConstant site not found: " + methodName);
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        return changed;
    }

    private boolean applyModifyVariable(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                        AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc modifier = Descriptor.method(handler.descriptor);
        if (modifier.arguments.size() != 1 || modifier.returnType.voidType
            || !compatible(modifier.arguments.get(0), modifier.returnType))
            throw unsupported("@ModifyVariable handler must be (T)T: " + handler.name + handler.descriptor);
        AtSpec at = AtSpec.read(nestedAnnotation(annotation, "at"));
        boolean argsOnly = annotation.bool("argsOnly", false);
        if (at.value.equals("HEAD")) {
            if (!argsOnly) throw unsupported("@ModifyVariable HEAD requires argsOnly=true");
            boolean changed = false;
            int explicitIndex = annotation.integer("index", -1);
            for (String methodName : annotation.strings("method")) {
                for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                    rejectUnsafeConstructorInjection(target, destination, at);
                    Descriptor.MethodDesc destinationDescriptor = Descriptor.method(destination.descriptor(target.pool));
                    int argumentIndex = explicitIndex >= 0
                        ? explicitIndex : selectVariableArgument(destinationDescriptor.arguments, modifier.arguments.get(0));
                    if (argumentIndex < 0 || argumentIndex >= destinationDescriptor.arguments.size())
                        throw unsupported("@ModifyVariable argument index out of range: " + methodName);
                    Descriptor.Type argumentType = destinationDescriptor.arguments.get(argumentIndex);
                    if (!compatible(argumentType, modifier.arguments.get(0))
                        || !compatible(argumentType, modifier.returnType))
                        throw unsupported("@ModifyVariable argument type mismatch: " + methodName);
                    CodeModel code = destination.code(target.pool);
                    if (code == null) throw unsupported("cannot modify variable in abstract/native method");
                    BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                    int slot = (destination.access & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
                    for (int i = 0; i < argumentIndex; ++i) slot += destinationDescriptor.arguments.get(i).slots;
                    ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                    replacement.addAll(loadLocal(argumentType, slot));
                    replacement.add(memberInstruction(handler.isStatic ? 184 : 182,
                        target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                    replacement.addAll(storeLocal(argumentType, slot));
                    BytecodeInstructions.Instruction first = firstInstruction(editor.instructions);
                    if (first == null) throw unsupported("cannot modify empty method: " + methodName);
                    editor.insertBefore(first, replacement);
                    editor.finish(target.pool);
                    destination.replaceCode(target.pool, code);
                    mark(context, destination, target);
                    changed = true;
                }
            }
            return changed;
        }
        if (!at.value.equals("LOAD") && !at.value.equals("STORE"))
            throw unsupported("@ModifyVariable requires LOAD or STORE");
        boolean changed = false;
        for (String methodName : annotation.strings("method")) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot modify variable in abstract/native method");
                rejectUnsafeConstructorInjection(target, destination, at);
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                LocalVariableTable localTable = LocalVariableTable.read(code, target.pool);
                List<BytecodeInstructions.Instruction> sites = findVariableSites(editor.instructions, target.pool, at,
                    annotation.integer("ordinal", -1), annotation.integer("index", -1),
                    modifier.arguments.get(0), analysis, localTable, argsOnly,
                    argumentSlots(destination, target.pool), nestedAnnotations(annotation, "slice"));
                for (int i = sites.size() - 1; i >= 0; --i) {
                    BytecodeInstructions.Instruction site = sites.get(i);
                    Descriptor.Type type = variableType(site);
                    if (type == null || !compatible(type, modifier.arguments.get(0)) || !compatible(type, modifier.returnType))
                        throw unsupported("@ModifyVariable type mismatch at " + site.oldOffset);
                    ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                    if (handler.isStatic) {
                        if (at.value.equals("LOAD")) {
                            replacement.add(site.copy());
                            replacement.add(memberInstruction(184, target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                        } else {
                            replacement.add(memberInstruction(184, target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                            replacement.add(site.copy());
                        }
                    } else {
                        int local = editor.allocateLocal(type);
                        if (at.value.equals("LOAD")) replacement.add(site.copy());
                        replacement.addAll(storeLocal(type, local));
                        replacement.add(bytes(42));
                        replacement.addAll(loadLocal(type, local));
                        replacement.add(memberInstruction(182, target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                        if (at.value.equals("STORE")) replacement.add(site.copy());
                    }
                    editor.replace(site, replacement);
                }
                if (sites.isEmpty()) throw unsupported("@ModifyVariable site not found: " + methodName);
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        return changed;
    }

    private static int selectVariableArgument(List<Descriptor.Type> arguments, Descriptor.Type type) {
        int selected = -1;
        for (int i = 0; i < arguments.size(); ++i) {
            if (!compatible(arguments.get(i), type)) continue;
            if (selected >= 0) throw new TransformException("@ModifyVariable argument is ambiguous");
            selected = i;
        }
        return selected;
    }

    private List<MemberModel> selectMethods(ClassFileModel target, String token, String handlerDescriptor,
                                            boolean injection) {
        String name = token;
        String descriptor = null;
        int open = token.indexOf('(');
        if (open >= 0) {
            name = token.substring(0, open);
            descriptor = token.substring(open);
        }
        name = resolver.resolveMethod(target.internalName(), name, descriptor);
        ArrayList<MemberModel> output = new ArrayList<>();
        for (MemberModel method : target.methodsNamed(name)) {
            if (descriptor != null && descriptor.equals(method.descriptor(target.pool))) output.add(method);
            else if (descriptor == null && injection) output.add(method);
            else if (descriptor == null && Descriptor.method(method.descriptor(target.pool)).returnType != null) output.add(method);
        }
        if (descriptor == null && injection && output.size() > 1) {
            Descriptor.MethodDesc handlerMethod = Descriptor.method(handlerDescriptor);
            int callback = callbackIndex(handlerMethod);
            if (callback >= 0) {
                List<MemberModel> matching = new ArrayList<>();
                for (MemberModel method : output) {
                    Descriptor.MethodDesc targetMethod = Descriptor.method(method.descriptor(target.pool));
                    if (callback <= targetMethod.arguments.size()) {
                        boolean same = true;
                        for (int i = 0; i < callback; ++i)
                            same &= compatible(targetMethod.arguments.get(i), handlerMethod.arguments.get(i));
                        if (same) matching.add(method);
                    }
                }
                if (!matching.isEmpty()) output = new ArrayList<>(matching);
            }
        }
        if (output.isEmpty()) throw unsupported("target method not found: " + token);
        return output;
    }

    private List<BytecodeInstructions.Instruction> findSites(List<BytecodeInstructions.Instruction> instructions,
                                                              ConstantPool pool, AtSpec at,
                                                              List<AnnotationModel> slices) {
        int[] range = sliceBounds(instructions, pool, at, slices);
        ArrayList<BytecodeInstructions.Instruction> candidates = new ArrayList<>();
        for (int index = 0; index < instructions.size(); ++index) {
            BytecodeInstructions.Instruction instruction = instructions.get(index);
            if (!isOriginalInstruction(instruction) || index < range[0] || index > range[1]) continue;
            boolean match = switch (at.value) {
                case "HEAD" -> instruction == firstInstruction(instructions);
                case "TAIL" -> instruction == lastReturn(instructions);
                case "RETURN" -> BytecodeInstructions.isReturn(instruction.opcode);
                case "INVOKE", "INVOKE_ASSIGN" -> memberMatches(instruction, pool, at, false);
                case "FIELD" -> memberMatches(instruction, pool, at, true);
                case "NEW" -> instruction.opcode == 187 && classMatches(instruction, pool, at);
                case "JUMP" -> isJump(instruction) && (at.opcode < 0 || instruction.opcode == at.opcode);
                case "CONSTANT" -> constantMatches(instruction, pool, at);
                case "LOAD", "STORE" -> variableType(instruction) != null && isLoadOrStore(instruction, at.value);
                default -> false;
            };
            if (match) candidates.add(instruction);
        }
        if (at.ordinal >= 0) return at.ordinal < candidates.size()
            ? List.of(candidates.get(at.ordinal)) : List.of();
        return candidates;
    }

    private int[] sliceBounds(List<BytecodeInstructions.Instruction> instructions, ConstantPool pool,
                              AtSpec at, List<AnnotationModel> slices) {
        int first = firstOriginalIndex(instructions);
        int last = lastOriginalIndex(instructions);
        if (first < 0 || last < 0) throw unsupported("cannot select an injection site in an empty method");
        if (slices.isEmpty()) return new int[] { first, last };

        AnnotationModel selected = null;
        String requestedId = at.sliceId;
        for (AnnotationModel candidate : slices) {
            String id = candidate.string("id", "");
            if (!requestedId.equals(id)) continue;
            if (selected != null) throw unsupported("multiple @Slice entries have id '" + requestedId + "'");
            selected = candidate;
        }
        if (selected == null && requestedId.isEmpty() && slices.size() == 1) selected = slices.get(0);
        if (selected == null)
            throw unsupported("@At refers to missing @Slice '" + requestedId + "'");

        AnnotationModel fromAnnotation = nestedAnnotation(selected, "from");
        AnnotationModel toAnnotation = nestedAnnotation(selected, "to");
        AtSpec from = fromAnnotation == null ? AtSpec.defaultSpec("HEAD") : AtSpec.read(fromAnnotation);
        AtSpec to = toAnnotation == null ? AtSpec.defaultSpec("TAIL") : AtSpec.read(toAnnotation);
        List<BytecodeInstructions.Instruction> fromSites = findSites(instructions, pool, from, List.of());
        List<BytecodeInstructions.Instruction> toSites = findSites(instructions, pool, to, List.of());
        if (fromSites.isEmpty()) throw unsupported("@Slice.from site not found: @" + from.value);
        if (toSites.isEmpty()) throw unsupported("@Slice.to site not found: @" + to.value);
        int sliceFrom = instructions.indexOf(fromSites.get(0));
        int sliceTo = instructions.indexOf(toSites.get(toSites.size() - 1));
        if (sliceFrom < 0 || sliceTo < 0 || sliceFrom > sliceTo)
            throw unsupported("@Slice boundaries are inverted");
        return new int[] { sliceFrom, sliceTo };
    }

    private List<BytecodeInstructions.Instruction> findConstantSites(List<BytecodeInstructions.Instruction> instructions,
                                                                      ConstantPool pool, List<ConstantSpec> specifications,
                                                                      List<AnnotationModel> slices) {
        int[] range = sliceBounds(instructions, pool, new AtSpec("CONSTANT", "", -1, -1,
            Shift.NONE, List.of(), ""), slices);
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        Set<BytecodeInstructions.Instruction> seen = java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>());
        for (ConstantSpec specification : specifications) {
            ArrayList<BytecodeInstructions.Instruction> matches = new ArrayList<>();
            for (int index = range[0]; index <= range[1]; ++index) {
                BytecodeInstructions.Instruction instruction = instructions.get(index);
                if (isOriginalInstruction(instruction) && isConstantInstruction(instruction, pool)
                    && specification.matches(pool, instruction)) matches.add(instruction);
            }
            if (specification.ordinal >= 0)
                matches = specification.ordinal < matches.size()
                    ? new ArrayList<>(List.of(matches.get(specification.ordinal))) : new ArrayList<>();
            for (BytecodeInstructions.Instruction instruction : matches) if (seen.add(instruction)) output.add(instruction);
        }
        output.sort(Comparator.comparingInt(instructions::indexOf));
        return output;
    }

    private List<BytecodeInstructions.Instruction> findVariableSites(List<BytecodeInstructions.Instruction> instructions,
                                                                     ConstantPool pool, AtSpec at, int ordinal, int explicitIndex,
                                                                     Descriptor.Type wanted, StackAnalyzer.Analysis analysis,
                                                                     LocalVariableTable localTable, boolean argsOnly,
                                                                     Set<Integer> argumentSlots, List<AnnotationModel> slices) {
        int[] range = sliceBounds(instructions, pool, at, slices);
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        int seen = 0;
        for (int index = range[0]; index <= range[1]; ++index) {
            BytecodeInstructions.Instruction instruction = instructions.get(index);
            if (!isOriginalInstruction(instruction)) continue;
            if (!isLoadOrStore(instruction, at.value)) continue;
            int slot = localIndex(instruction);
            if (slot < 0) throw unsupported("invalid local variable instruction");
            if (explicitIndex >= 0 && explicitIndex != slot) continue;
            if (argsOnly && !argumentSlots.contains(slot)) continue;
            if (at.opcode >= 0 && at.opcode != variableOpcode(instruction)) continue;
            Descriptor.Type actual = variableType(pool, instruction, at.value, analysis, localTable);
            if (actual == null || !compatible(wanted, actual)) continue;
            int current = seen++;
            if (at.ordinal >= 0 && current != at.ordinal) continue;
            if (ordinal >= 0 && current != ordinal) continue;
            output.add(instruction);
        }
        return output;
    }

    private boolean memberMatches(BytecodeInstructions.Instruction instruction, ConstantPool pool,
                                  AtSpec at, boolean field) {
        if (field != BytecodeInstructions.isField(instruction.opcode)) return false;
        if (!field && !BytecodeInstructions.isInvoke(instruction.opcode)) return false;
        if (at.opcode >= 0 && at.opcode != instruction.opcode) return false;
        int cp = BytecodeInstructions.cpIndex(instruction);
        if (cp < 0) return false;
        String owner = resolver.resolveOwner(pool.memberOwner(cp));
        String name = field ? resolver.resolveField(owner, pool.memberName(cp), pool.memberDescriptor(cp))
                            : resolver.resolveMethod(owner, pool.memberName(cp), pool.memberDescriptor(cp));
        if (!at.owner.isEmpty() && !normalizeInternal(at.owner).equals(normalizeInternal(owner))) return false;
        if (!at.member.isEmpty() && !at.member.equals(name)) return false;
        if (!at.descriptor.isEmpty() && !at.descriptor.equals(pool.memberDescriptor(cp))) return false;
        return true;
    }

    private boolean classMatches(BytecodeInstructions.Instruction instruction, ConstantPool pool, AtSpec at) {
        int cp = BytecodeInstructions.cpIndex(instruction);
        return cp >= 0 && (at.owner.isEmpty() || normalizeInternal(at.owner).equals(normalizeInternal(pool.className(cp))));
    }

    private boolean constantMatches(BytecodeInstructions.Instruction instruction, ConstantPool pool, AtSpec at) {
        if (!isConstantInstruction(instruction, pool)) return false;
        return ConstantSpec.fromArgs(at.args).matches(pool, instruction);
    }

    private boolean isConstantInstruction(BytecodeInstructions.Instruction instruction, ConstantPool pool) {
        int opcode = instruction.opcode;
        return opcode >= 1 && opcode <= 20;
    }

    private boolean isJump(BytecodeInstructions.Instruction instruction) {
        return BytecodeInstructions.isShortBranch(instruction.opcode) || BytecodeInstructions.isWideBranch(instruction.opcode)
            || instruction.opcode == 170 || instruction.opcode == 171;
    }

    private boolean isLoadOrStore(BytecodeInstructions.Instruction instruction, String kind) {
        int opcode = variableOpcode(instruction);
        boolean load = (opcode >= 21 && opcode <= 25) || (opcode >= 26 && opcode <= 45);
        boolean store = (opcode >= 54 && opcode <= 58) || (opcode >= 59 && opcode <= 78);
        return kind.equals("LOAD") ? load : store;
    }

    private Descriptor.Type variableType(BytecodeInstructions.Instruction instruction) {
        int opcode = variableOpcode(instruction);
        if (opcode == 21 || (opcode >= 26 && opcode <= 29) || opcode == 54 || (opcode >= 59 && opcode <= 62))
            return new Descriptor.Type("I", 1, false, false, true, false);
        if (opcode == 22 || (opcode >= 30 && opcode <= 33) || opcode == 55 || (opcode >= 63 && opcode <= 66))
            return new Descriptor.Type("J", 2, false, false, true, false);
        if (opcode == 23 || (opcode >= 34 && opcode <= 37) || opcode == 56 || (opcode >= 67 && opcode <= 70))
            return new Descriptor.Type("F", 1, false, false, true, false);
        if (opcode == 24 || (opcode >= 38 && opcode <= 41) || opcode == 57 || (opcode >= 71 && opcode <= 74))
            return new Descriptor.Type("D", 2, false, false, true, false);
        if (opcode == 25 || (opcode >= 42 && opcode <= 45) || opcode == 58 || (opcode >= 75 && opcode <= 78))
            return new Descriptor.Type("Ljava/lang/Object;", 1, true, false, false, false);
        return null;
    }

    private int localIndex(BytecodeInstructions.Instruction instruction) {
        if (instruction.opcode == 196)
            return ((instruction.raw[2] & 0xff) << 8) | (instruction.raw[3] & 0xff);
        int opcode = variableOpcode(instruction);
        if (opcode >= 26 && opcode <= 29) return opcode - 26;
        if (opcode >= 30 && opcode <= 33) return opcode - 30;
        if (opcode >= 34 && opcode <= 37) return opcode - 34;
        if (opcode >= 38 && opcode <= 41) return opcode - 38;
        if (opcode >= 42 && opcode <= 45) return opcode - 42;
        if (opcode >= 59 && opcode <= 62) return opcode - 59;
        if (opcode >= 63 && opcode <= 66) return opcode - 63;
        if (opcode >= 67 && opcode <= 70) return opcode - 67;
        if (opcode >= 71 && opcode <= 74) return opcode - 71;
        if (opcode >= 75 && opcode <= 78) return opcode - 75;
        if ((opcode >= 21 && opcode <= 25) || (opcode >= 54 && opcode <= 58) || opcode == 169)
            return instruction.raw[1] & 0xff;
        return -1;
    }

    private int variableOpcode(BytecodeInstructions.Instruction instruction) {
        return instruction.opcode == 196 ? instruction.raw[1] & 0xff : instruction.opcode;
    }

    private Descriptor.Type variableType(ConstantPool pool, BytecodeInstructions.Instruction instruction,
                                         String kind, StackAnalyzer.Analysis analysis,
                                         LocalVariableTable localTable) {
        int slot = localIndex(instruction);
        if (slot < 0) return null;
        if (localTable != null) {
            LocalVariableTable.Entry entry = localTable.at(instruction.oldOffset, slot);
            if (entry != null) {
                try {
                    Descriptor.Type precise = Descriptor.type(entry.descriptor());
                    if (precise != null) return precise;
                } catch (TransformException ignored) {
                    // The verifier-derived fallback below remains authoritative.
                }
            }
        }
        StackAnalyzer.Value value = kind.equals("LOAD")
            ? analysis.localsBefore(instruction).get(slot)
            : lastValue(analysis.before(instruction));
        if (value != null && !value.isUninitialized()) return stackType(value);
        return variableType(instruction);
    }

    private static StackAnalyzer.Value lastValue(List<StackAnalyzer.Value> values) {
        return values.isEmpty() ? null : values.get(values.size() - 1);
    }

    private Set<Integer> argumentSlots(MemberModel method, ConstantPool pool) {
        Set<Integer> output = new HashSet<>();
        int slot = (method.access & ClassFileModel.ACC_STATIC) == 0 ? 1 : 0;
        for (Descriptor.Type argument : Descriptor.method(method.descriptor(pool)).arguments) {
            output.add(slot);
            slot += argument.slots;
        }
        return output;
    }

    private boolean isOriginalInstruction(BytecodeInstructions.Instruction instruction) {
        return !instruction.label && instruction.original && instruction.originalOffset >= 0;
    }

    private int firstOriginalIndex(List<BytecodeInstructions.Instruction> instructions) {
        for (int i = 0; i < instructions.size(); ++i)
            if (isOriginalInstruction(instructions.get(i))) return i;
        return -1;
    }

    private int lastOriginalIndex(List<BytecodeInstructions.Instruction> instructions) {
        for (int i = instructions.size() - 1; i >= 0; --i)
            if (isOriginalInstruction(instructions.get(i))) return i;
        return -1;
    }

    private BytecodeInstructions.Instruction firstInstruction(List<BytecodeInstructions.Instruction> instructions) {
        for (BytecodeInstructions.Instruction instruction : instructions)
            if (isOriginalInstruction(instruction)) return instruction;
        return null;
    }

    private BytecodeInstructions.Instruction lastReturn(List<BytecodeInstructions.Instruction> instructions) {
        for (int i = instructions.size() - 1; i >= 0; --i)
            if (isOriginalInstruction(instructions.get(i))
                && BytecodeInstructions.isReturn(instructions.get(i).opcode)) return instructions.get(i);
        return null;
    }

    private int callbackIndex(Descriptor.MethodDesc descriptor) {
        if (descriptor.arguments.isEmpty()) return -1;
        Descriptor.Type last = descriptor.arguments.get(descriptor.arguments.size() - 1);
        return last.descriptor.startsWith("Lorg/spongepowered/asm/mixin/injection/callback/")
            ? descriptor.arguments.size() - 1 : -1;
    }

    private List<AnnotationModel> nestedAnnotations(List<AnnotationModel.ElementValue> values) {
        ArrayList<AnnotationModel> output = new ArrayList<>();
        for (AnnotationModel.ElementValue value : values)
            if (value.value instanceof AnnotationModel annotation) output.add(annotation);
        return output;
    }

    /** Accept both javac's shorthand annotation form and the array form. */
    private List<AnnotationModel> nestedAnnotations(AnnotationModel parent, String name) {
        AnnotationModel direct = parent.annotation(name);
        if (direct != null) return List.of(direct);
        return nestedAnnotations(parent.array(name));
    }

    private List<AtSpec> readAtSpecs(AnnotationModel parent, String name) {
        ArrayList<AtSpec> output = new ArrayList<>();
        for (AnnotationModel annotation : nestedAnnotations(parent, name)) output.add(AtSpec.read(annotation));
        return output;
    }

    private List<ConstantSpec> readConstantSpecs(AnnotationModel parent) {
        List<AnnotationModel> annotations = nestedAnnotations(parent, "constant");
        if (annotations.isEmpty()) return List.of(ConstantSpec.any());
        ArrayList<ConstantSpec> output = new ArrayList<>();
        for (AnnotationModel annotation : annotations) output.add(ConstantSpec.read(annotation));
        return output;
    }

    private void rejectUnsafeConstructorInjection(ClassFileModel target, MemberModel destination, AtSpec at) {
        if (destination.name(target.pool).equals("<init>")
            && !at.value.equals("RETURN") && !at.value.equals("TAIL"))
            throw unsupported("constructor injection is only supported at RETURN/TAIL after initialization: @" + at.value);
    }

    private void rejectConstructorOperation(ClassFileModel target, MemberModel destination, String operation) {
        if (destination.name(target.pool).equals("<init>"))
            throw unsupported(operation + " cannot transform a constructor safely");
    }

    private List<CapturedLocal> capturedLocals(ClassFileModel target, MemberModel destination,
                                               Handler handler, LocalCaptureMode mode,
                                               StackAnalyzer.Analysis analysis, LocalVariableTable localTable,
                                               BytecodeInstructions.Instruction site, boolean after) {
        Descriptor.MethodDesc targetDescriptor = Descriptor.method(destination.descriptor(target.pool));
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        int callback = callbackIndex(handlerDescriptor);
        if (callback < 0) return List.of();
        int required = callback - targetDescriptor.arguments.size();
        if (required <= 0) return List.of();
        if (mode == LocalCaptureMode.NO_CAPTURE)
            throw unsupported("handler requests local capture without locals=LocalCapture mode: " + handler.name);

        Map<Integer, StackAnalyzer.Value> state = after
            ? analysis.localsAfter(site) : analysis.localsBefore(site);
        ArrayList<Integer> slots = new ArrayList<>(state.keySet());
        Collections.sort(slots);
        int argumentEnd = (destination.access & ClassFileModel.ACC_STATIC) == 0 ? 1 : 0;
        for (Descriptor.Type argument : targetDescriptor.arguments) argumentEnd += argument.slots;
        ArrayList<CapturedLocal> available = new ArrayList<>();
        for (int slot : slots) {
            if (slot < argumentEnd) continue;
            StackAnalyzer.Value value = state.get(slot);
            if (value == null) continue;
            if (value.isUninitialized()) throw unsupported("local capture crosses an uninitialized value at " + site.oldOffset);
            Descriptor.Type type = stackType(value);
            if (localTable != null) {
                LocalVariableTable.Entry entry = localTable.at(site.oldOffset, slot);
                if (entry != null) {
                    Descriptor.Type debugType = Descriptor.type(entry.descriptor());
                    if (type.reference && debugType.reference) type = debugType;
                    else if (!compatible(type, debugType))
                        throw unsupported("LVT type disagrees with verifier at local " + slot);
                }
            }
            available.add(new CapturedLocal(type, slot));
        }
        if (required > available.size())
            throw unsupported("local capture requested " + required + " value(s), only " + available.size() + " live");
        ArrayList<CapturedLocal> output = new ArrayList<>();
        for (int i = 0; i < required; ++i) {
            CapturedLocal captured = available.get(i);
            Descriptor.Type expected = handlerDescriptor.arguments.get(targetDescriptor.arguments.size() + i);
            if (!compatible(expected, captured.type))
                throw unsupported("captured local " + captured.slot + " does not match handler argument " + i);
            output.add(captured);
        }
        return output;
    }

    private static List<Descriptor.Type> capturedTypes(List<CapturedLocal> locals) {
        ArrayList<Descriptor.Type> output = new ArrayList<>();
        for (CapturedLocal local : locals) output.add(local.type);
        return output;
    }

    private static List<Integer> capturedSlots(List<CapturedLocal> locals) {
        ArrayList<Integer> output = new ArrayList<>();
        for (CapturedLocal local : locals) output.add(local.slot);
        return output;
    }

    /**
     * Mixin's {@code @Inject.at} is declared as an annotation array, while
     * Redirect/ModifyArg/ModifyVariable use a single annotation.  The class
     * file representation preserves that distinction, so accept both forms
     * at the transformer boundary.
     */
    private AnnotationModel nestedAnnotation(AnnotationModel parent, String name) {
        AnnotationModel direct = parent.annotation(name);
        if (direct != null) return direct;
        List<AnnotationModel> values = nestedAnnotations(parent.array(name));
        return values.isEmpty() ? null : values.get(0);
    }

    private void replaceOrAddGeneratedMethod(ClassFileModel target, String name, int sourceAccess,
                                             String descriptor,
                                             List<BytecodeInstructions.Instruction> instructions,
                                             List<Descriptor.Type> arguments, Descriptor.Type returnType) {
        MemberModel method = target.method(name, descriptor);
        if (method == null) {
            method = new MemberModel();
            method.nameIndex = target.pool.addUtf8(name);
            method.descriptorIndex = target.pool.addUtf8(descriptor);
            target.addMethod(method);
        }
        method.access = (sourceAccess | 0x0001) & ~(ClassFileModel.ACC_ABSTRACT | ClassFileModel.ACC_NATIVE);
        CodeModel code = new CodeModel();
        code.maxLocals = ((sourceAccess & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1);
        for (Descriptor.Type argument : arguments) code.maxLocals += argument.slots;
        code.maxStack = 16;
        code.code = BytecodeInstructions.Editor.assembleGenerated(instructions);
        method.replaceCode(target.pool, code);
    }

    /**
     * Accessor/Invoker mixins are interfaces at the Java call site.  Adding
     * only their generated methods is insufficient: an entrypoint using
     * {@code target instanceof AccessorInterface} must also see the target
     * class implement that interface.  The interface is defined by the same
     * child loader as the transformed target, so this preserves JVM identity.
     */
    private void addImplementedInterface(ClassFileModel target, String interfaceName) {
        int classIndex = target.pool.addClass(interfaceName);
        if (!target.interfaces.contains(classIndex)) target.interfaces.add(classIndex);
    }

    private void remapCodeAttributeConstants(List<AttributeModel> attributes, ConstantPool sourcePool,
                                             ConstantPool targetPool, String sourceOwner, String targetOwner,
                                             Map<String, String> methodRenames, Map<String, String> fieldRenames) {
        for (AttributeModel attribute : attributes) {
            String name = attribute.name(sourcePool);
            attribute.nameIndex = targetPool.importEntry(sourcePool, attribute.nameIndex, sourceOwner,
                targetOwner, methodRenames, fieldRenames);
            if (name.equals("StackMapTable")) attribute.info = remapStackMapConstants(attribute.info, sourcePool,
                targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames, false);
            else if (name.equals("StackMap")) attribute.info = remapStackMapConstants(attribute.info, sourcePool,
                targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames, true);
            else if (name.equals("LocalVariableTable") || name.equals("LocalVariableTypeTable"))
                attribute.info = remapLocalVariableConstants(attribute.info, sourcePool, targetPool,
                    sourceOwner, targetOwner, methodRenames, fieldRenames);
        }
    }

    private byte[] remapStackMapConstants(byte[] info, ConstantPool sourcePool, ConstantPool targetPool,
                                           String sourceOwner, String targetOwner, Map<String, String> methodRenames,
                                           Map<String, String> fieldRenames, boolean legacy) {
        try {
            DataInputStream input = new DataInputStream(new ByteArrayInputStream(info));
            ByteArrayOutputStream bytes = new ByteArrayOutputStream(info.length + 16);
            java.io.DataOutputStream output = new java.io.DataOutputStream(bytes);
            int count = input.readUnsignedShort();
            output.writeShort(count);
            for (int i = 0; i < count; ++i) {
                if (legacy) output.writeShort(input.readUnsignedShort());
                else {
                    int frame = input.readUnsignedByte();
                    output.writeByte(frame);
                    if (frame >= 64 && frame <= 127) remapVerification(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
                    else if (frame == 247) {
                        output.writeShort(input.readUnsignedShort());
                        remapVerification(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
                    } else if (frame >= 248 && frame <= 251) output.writeShort(input.readUnsignedShort());
                    else if (frame >= 252 && frame <= 254) {
                        output.writeShort(input.readUnsignedShort());
                        for (int j = 0; j < frame - 251; ++j) remapVerification(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
                    } else if (frame == 255) {
                        output.writeShort(input.readUnsignedShort());
                        int locals = input.readUnsignedShort(); output.writeShort(locals);
                        for (int j = 0; j < locals; ++j) remapVerification(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
                        int stack = input.readUnsignedShort(); output.writeShort(stack);
                        for (int j = 0; j < stack; ++j) remapVerification(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
                    }
                }
                if (legacy) {
                    remapVerificationList(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
                    remapVerificationList(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
                }
            }
            output.flush();
            return bytes.toByteArray();
        } catch (IOException failure) {
            throw new TransformException("cannot remap StackMap constants", failure);
        }
    }

    private void remapVerificationList(DataInputStream input, java.io.DataOutputStream output,
                                       ConstantPool sourcePool, ConstantPool targetPool, String sourceOwner,
                                       String targetOwner, Map<String, String> methodRenames, Map<String, String> fieldRenames) throws IOException {
        int count = input.readUnsignedShort(); output.writeShort(count);
        for (int i = 0; i < count; ++i) remapVerification(input, output, sourcePool, targetPool, sourceOwner, targetOwner, methodRenames, fieldRenames);
    }

    private void remapVerification(DataInputStream input, java.io.DataOutputStream output,
                                   ConstantPool sourcePool, ConstantPool targetPool, String sourceOwner,
                                   String targetOwner, Map<String, String> methodRenames, Map<String, String> fieldRenames) throws IOException {
        int tag = input.readUnsignedByte(); output.writeByte(tag);
        if (tag == 7) {
            int sourceIndex = input.readUnsignedShort();
            output.writeShort(targetPool.importEntry(sourcePool, sourceIndex, sourceOwner, targetOwner, methodRenames, fieldRenames));
        } else if (tag == 8) output.writeShort(input.readUnsignedShort());
    }

    private byte[] remapLocalVariableConstants(byte[] info, ConstantPool sourcePool, ConstantPool targetPool,
                                                String sourceOwner, String targetOwner, Map<String, String> methodRenames,
                                                Map<String, String> fieldRenames) {
        try {
            DataInputStream input = new DataInputStream(new ByteArrayInputStream(info));
            ByteArrayOutputStream bytes = new ByteArrayOutputStream(info.length);
            java.io.DataOutputStream output = new java.io.DataOutputStream(bytes);
            int count = input.readUnsignedShort(); output.writeShort(count);
            for (int i = 0; i < count; ++i) {
                output.writeShort(input.readUnsignedShort()); output.writeShort(input.readUnsignedShort());
                int name = input.readUnsignedShort(); int descriptor = input.readUnsignedShort();
                output.writeShort(targetPool.importEntry(sourcePool, name, sourceOwner, targetOwner, methodRenames, fieldRenames));
                output.writeShort(targetPool.importEntry(sourcePool, descriptor, sourceOwner, targetOwner, methodRenames, fieldRenames));
                output.writeShort(input.readUnsignedShort());
            }
            output.flush(); return bytes.toByteArray();
        } catch (IOException failure) {
            throw new TransformException("cannot remap local-variable constants", failure);
        }
    }

    private void mark(TransformContext context, MemberModel method, ClassFileModel target) {
        mark(context, method.name(target.pool) + method.descriptor(target.pool), target);
        MixinDispatch.markTransformed(target.internalName(), method.name(target.pool), method.descriptor(target.pool));
    }

    private void mark(TransformContext context, String descriptor, ClassFileModel target) {
        context.recordMethod(descriptor);
        int open = descriptor.indexOf('(');
        if (open > 0) MixinDispatch.markTransformed(target.internalName(), descriptor.substring(0, open), descriptor.substring(open));
    }

    private void report(String message, boolean fatal) {
        diagnostics.add(message);
        if (fatal || strict) throw new TransformException(message);
    }

    private TransformException unsupported(String message) {
        return new TransformException(message);
    }

    private static String classDescriptorToInternal(String descriptor) {
        if (descriptor == null) return "";
        if (descriptor.startsWith("L") && descriptor.endsWith(";")) return descriptor.substring(1, descriptor.length() - 1);
        return descriptor.replace('.', '/');
    }

    private static String normalizeInternal(String name) {
        return name == null ? "" : name.replace('.', '/').replace("L", "").replace(";", "");
    }

    /** Make static helper state callable from a copied target method. */
    private byte[] exposeMixinHelperFields(byte[] originalBytes) {
        ClassFileModel model = ClassFileModel.parse(originalBytes);
        boolean changed = false;
        for (MemberModel field : model.fields) {
            if ((field.access & 0x0008) == 0
                || AnnotationModel.first(field.attributes, model.pool, "Shadow") != null) continue;
            int exposed = (field.access | 0x0001) & ~0x0006;
            if (exposed != field.access) {
                field.access = exposed;
                changed = true;
            }
        }
        if (!changed) return originalBytes;
        byte[] output = model.write();
        ClassFileSafety.validateBytes(output);
        return output;
    }

    private static String descriptorOwner(String descriptor) {
        return descriptor.startsWith("L") && descriptor.endsWith(";")
            ? descriptor.substring(1, descriptor.length() - 1) : "";
    }

    private static String inferMemberName(String name, String... prefixes) {
        for (String prefix : prefixes) if (name.startsWith(prefix) && name.length() > prefix.length()) {
            String tail = name.substring(prefix.length());
            return Character.toLowerCase(tail.charAt(0)) + tail.substring(1);
        }
        return name;
    }

    private static String inferInvokerName(String name) {
        for (String prefix : new String[] { "call", "invoke", "access$" })
            if (name.startsWith(prefix) && name.length() > prefix.length()) return name.substring(prefix.length());
        return name;
    }

    /** Converts the analyzer's verified stack value back to the descriptor model used by emitters. */
    private static Descriptor.Type stackType(StackAnalyzer.Value value) {
        if (value == null || value.descriptor == null || value.descriptor.isEmpty())
            throw new TransformException("stack analyzer returned an invalid value");
        return Descriptor.type(value.descriptor);
    }

    private static boolean compatible(Descriptor.Type expected, Descriptor.Type actual) {
        if (expected.descriptor.equals(actual.descriptor)) return true;
        if (expected.primitive || actual.primitive || expected.voidType || actual.voidType) return false;
        return expected.reference && actual.reference;
    }

    private static int selectArgument(List<Descriptor.Type> arguments, Descriptor.Type type,
                                      ConstantPool ignored, int offset) {
        int selected = -1;
        for (int i = 0; i < arguments.size(); ++i) if (compatible(arguments.get(i), type)) {
            if (selected >= 0) throw new TransformException("@ModifyArg index is ambiguous");
            selected = i + offset;
        }
        return selected;
    }

    private static List<BytecodeInstructions.Instruction> loadLocal(Descriptor.Type type, int index) {
        int opcode = type.descriptor.equals("J") ? 22 : type.descriptor.equals("F") ? 23
            : type.descriptor.equals("D") ? 24 : type.reference ? 25 : 21;
        if (index >= 0 && index <= 3) {
            int base = type.descriptor.equals("J") ? 30 : type.descriptor.equals("F") ? 34
                : type.descriptor.equals("D") ? 38 : type.reference ? 42 : 26;
            return List.of(bytes(base + index));
        }
        if (index < 0 || index > 65535) throw new TransformException("local index out of range: " + index);
        if (index > 255) return List.of(bytes(196, opcode, (index >>> 8) & 0xff, index & 0xff));
        return List.of(bytes(opcode, index & 0xff));
    }

    private static List<BytecodeInstructions.Instruction> storeLocal(Descriptor.Type type, int index) {
        int opcode = type.descriptor.equals("J") ? 55 : type.descriptor.equals("F") ? 56
            : type.descriptor.equals("D") ? 57 : type.reference ? 58 : 54;
        if (index >= 0 && index <= 3) {
            int base = type.descriptor.equals("J") ? 63 : type.descriptor.equals("F") ? 67
                : type.descriptor.equals("D") ? 71 : type.reference ? 75 : 59;
            return List.of(bytes(base + index));
        }
        if (index < 0 || index > 65535) throw new TransformException("local index out of range: " + index);
        if (index > 255) return List.of(bytes(196, opcode, (index >>> 8) & 0xff, index & 0xff));
        return List.of(bytes(opcode, index & 0xff));
    }

    private static BytecodeInstructions.Instruction returnInstruction(Descriptor.Type type) {
        return type.voidType ? bytes(177) : type.reference ? bytes(176)
            : type.descriptor.equals("J") ? bytes(173) : type.descriptor.equals("F") ? bytes(174)
            : type.descriptor.equals("D") ? bytes(175) : bytes(172);
    }

    private static BytecodeInstructions.Instruction memberInstruction(int opcode, int cp) {
        return bytes(opcode, (cp >>> 8) & 0xff, cp & 0xff);
    }

    private static BytecodeInstructions.Instruction bytes(int... values) {
        byte[] result = new byte[values.length];
        for (int i = 0; i < values.length; ++i) result[i] = (byte) values[i];
        return BytecodeInstructions.Instruction.raw(-1, values[0] & 0xff, result);
    }

    private static BytecodeInstructions.Instruction ldcString(ConstantPool pool, String value) {
        int cp = pool.addString(value);
        return cp <= 255 ? bytes(18, cp) : bytes(19, (cp >>> 8) & 0xff, cp & 0xff);
    }

    private static BytecodeInstructions.Instruction iconst(int value) {
        if (value == -1) return bytes(2);
        if (value >= 0 && value <= 5) return bytes(3 + value);
        if (value >= Byte.MIN_VALUE && value <= Byte.MAX_VALUE) return bytes(16, value);
        return bytes(17, (value >>> 8) & 0xff, value & 0xff);
    }

    private static List<BytecodeInstructions.Instruction> box(ConstantPool pool, Descriptor.Type type) {
        if (!type.primitive) return List.of();
        String owner = switch (type.descriptor) {
            case "Z" -> "java/lang/Boolean";
            case "B" -> "java/lang/Byte";
            case "C" -> "java/lang/Character";
            case "S" -> "java/lang/Short";
            case "I" -> "java/lang/Integer";
            case "J" -> "java/lang/Long";
            case "F" -> "java/lang/Float";
            case "D" -> "java/lang/Double";
            default -> throw new TransformException("cannot box " + type.descriptor);
        };
        int ref = pool.addMethodRef(owner, "valueOf", "(" + type.descriptor + ")L" + owner + ";", false);
        return List.of(memberInstruction(184, ref));
    }

    private static List<BytecodeInstructions.Instruction> loadReturnValue(ConstantPool pool, Descriptor.Type type, int callbackLocal) {
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        Descriptor.Type callback = new Descriptor.Type("Lorg/spongepowered/asm/mixin/injection/callback/CallbackInfoReturnable;", 1, true, false, false, false);
        output.addAll(loadLocal(callback, callbackLocal));
        if (type.primitive) {
            String suffix = switch (type.descriptor) {
                case "Z" -> "Z"; case "B" -> "B"; case "C" -> "C"; case "S" -> "S";
                case "I" -> "I"; case "J" -> "J"; case "F" -> "F"; case "D" -> "D";
                default -> throw new TransformException("bad primitive return type");
            };
            int ref = pool.addMethodRef("org/spongepowered/asm/mixin/injection/callback/CallbackInfoReturnable",
                "getReturnValue" + suffix, "()" + type.descriptor, false);
            output.add(memberInstruction(182, ref));
        } else {
            int ref = pool.addMethodRef("org/spongepowered/asm/mixin/injection/callback/CallbackInfoReturnable",
                "getReturnValue", "()Ljava/lang/Object;", false);
            output.add(memberInstruction(182, ref));
            String cast = type.descriptor.startsWith("L") ? descriptorOwner(type.descriptor) : type.descriptor;
            if (!type.descriptor.equals("Ljava/lang/Object;")) output.add(memberInstruction(192, pool.addClass(cast)));
        }
        return output;
    }

    private static List<BytecodeInstructions.Instruction> loadReturnValue(ClassFileModel target, Descriptor.Type type, int callbackLocal) {
        return loadReturnValue(target.pool, type, callbackLocal);
    }

    private static List<BytecodeInstructions.Instruction> readAllAsList() { return List.of(); }

    private static byte[] readAll(InputStream input) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        for (int count; (count = input.read(buffer)) >= 0; ) if (count > 0) output.write(buffer, 0, count);
        return output.toByteArray();
    }

    private static final class MixinDefinition {
        final String name;
        final ClassFileModel model;
        final List<String> targets;
        final int priority;
        final long registrationOrder;
        MixinDefinition(String name, ClassFileModel model, List<String> targets, int priority,
                        long registrationOrder) {
            this.name = name; this.model = model; this.targets = List.copyOf(new LinkedHashSet<>(targets));
            this.priority = priority; this.registrationOrder = registrationOrder;
        }
    }

    private record MixinOperation(PreparedMixin mixin, MemberModel method, int declarationOrder) { }

    private static final class PreparedMixin {
        final MixinDefinition definition;
        final Map<String, String> methodRenames = new LinkedHashMap<>();
        final Map<String, String> fieldRenames = new LinkedHashMap<>();
        final Map<String, MemberModel> copiedMethods = new LinkedHashMap<>();
        int bootstrapOffset;
        PreparedMixin(MixinDefinition definition) { this.definition = definition; }
        boolean hasCopy(String name, String descriptor) { return copiedMethods.containsKey(name + descriptor); }
        Handler handler(MemberModel source, ClassFileModel target) {
            String key = source.name(definition.model.pool) + source.descriptor(definition.model.pool);
            MemberModel copy = copiedMethods.get(key);
            if (copy == null) throw new TransformException("Mixin handler was not copied: " + key);
            return new Handler(copy.name(target.pool), copy.descriptor(target.pool),
                (copy.access & ClassFileModel.ACC_STATIC) != 0);
        }
    }

    private record Handler(String name, String descriptor, boolean isStatic) { }

    private enum Shift { NONE, BEFORE, AFTER, BY }

    private enum LocalCaptureMode {
        NO_CAPTURE, SOFT, HARD;

        static LocalCaptureMode read(AnnotationModel annotation) {
            AnnotationModel.ElementValue value = annotation.value("locals");
            if (value == null) return NO_CAPTURE;
            if (!(value.value instanceof AnnotationModel.EnumValue enumValue))
                throw new TransformException("invalid @Inject locals value");
            return switch (enumValue.name()) {
                case "NO_CAPTURE" -> NO_CAPTURE;
                case "PRINT", "FAILSOFT", "CAPTURE_FAILSOFT" -> SOFT;
                case "FAILHARD", "CAPTURE_FAILHARD" -> HARD;
                default -> throw new TransformException("unsupported LocalCapture value " + enumValue.name());
            };
        }

        boolean requiresAnalysis(Handler handler, ClassFileModel target, MemberModel destination) {
            Descriptor.MethodDesc targetDescriptor = Descriptor.method(destination.descriptor(target.pool));
            Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
            int callback = callbackIndexOf(handlerDescriptor);
            return callback >= 0 && callback > targetDescriptor.arguments.size();
        }

        private static int callbackIndexOf(Descriptor.MethodDesc descriptor) {
            if (descriptor.arguments.isEmpty()) return -1;
            int index = descriptor.arguments.size() - 1;
            return descriptor.arguments.get(index).descriptor.startsWith(
                "Lorg/spongepowered/asm/mixin/injection/callback/") ? index : -1;
        }
    }

    private record CapturedLocal(Descriptor.Type type, int slot) { }

    private static final class AtSpec {
        final String value;
        final String target;
        final String owner;
        final String member;
        final String descriptor;
        final int ordinal;
        final int opcode;
        final Shift shift;
        final List<String> args;
        /** Reserved for loaders which expose an explicit slice selector. */
        final String sliceId;

        private AtSpec(String value, String target, int ordinal, int opcode, Shift shift, List<String> args) {
            this(value, target, ordinal, opcode, shift, args, "");
        }

        private AtSpec(String value, String target, int ordinal, int opcode, Shift shift,
                       List<String> args, String sliceId) {
            this.value = value;
            this.target = target;
            this.ordinal = ordinal;
            this.opcode = opcode;
            this.shift = shift;
            this.args = args;
            this.sliceId = sliceId;
            TargetParts parts = TargetParts.parse(target);
            this.owner = parts.owner; this.member = parts.member; this.descriptor = parts.descriptor;
        }

        static AtSpec defaultSpec(String value) {
            return new AtSpec(value, "", -1, -1, Shift.NONE, List.of(), "");
        }

        static AtSpec read(AnnotationModel annotation) {
            if (annotation == null) throw new TransformException("Mixin operation has no @At");
            String shiftName = "NONE";
            AnnotationModel.ElementValue shiftValue = annotation.value("shift");
            if (shiftValue != null && shiftValue.value instanceof AnnotationModel.EnumValue enumValue) shiftName = enumValue.name();
            return new AtSpec(annotation.string("value", "HEAD"), annotation.string("target", ""),
                annotation.integer("ordinal", -1), annotation.integer("opcode", -1),
                Shift.valueOf(shiftName), annotation.strings("args"), annotation.string("slice", ""));
        }
    }

    private record TargetParts(String owner, String member, String descriptor) {
        static TargetParts parse(String target) {
            if (target == null || target.isEmpty()) return new TargetParts("", "", "");
            String value = target;
            if (value.startsWith("L")) value = value.substring(1);
            int descriptorStart = value.indexOf('(');
            int semicolon = descriptorStart >= 0 ? value.lastIndexOf(';', descriptorStart) : value.lastIndexOf(';');
            if (semicolon >= 0) {
                int fieldSeparator = descriptorStart < 0 ? value.indexOf(':', semicolon + 1) : -1;
                int memberEnd = descriptorStart >= 0 ? descriptorStart
                    : fieldSeparator >= 0 ? fieldSeparator : value.length();
                return new TargetParts(value.substring(0, semicolon),
                    value.substring(semicolon + 1, memberEnd),
                    descriptorStart >= 0 ? value.substring(descriptorStart)
                        : fieldSeparator >= 0 ? value.substring(fieldSeparator + 1) : "");
            }
            int split = descriptorStart >= 0 ? value.lastIndexOf('/', descriptorStart) : value.lastIndexOf('/');
            if (split < 0) return new TargetParts("", value, descriptorStart >= 0 ? value.substring(descriptorStart) : "");
            return new TargetParts(value.substring(0, split), value.substring(split + 1, descriptorStart < 0 ? value.length() : descriptorStart),
                descriptorStart < 0 ? "" : value.substring(descriptorStart));
        }
    }

    private static final class ConstantSpec {
        final boolean hasValue;
        final Object value;
        final int ordinal;
        private ConstantSpec(boolean hasValue, Object value, int ordinal) {
            this.hasValue = hasValue; this.value = value; this.ordinal = ordinal;
        }
        static ConstantSpec any() { return new ConstantSpec(false, null, -1); }

        static ConstantSpec fromArgs(List<String> args) {
            if (args == null || args.isEmpty()) return any();
            if (args.size() != 1) throw new TransformException("CONSTANT At accepts one selector argument");
            String argument = args.get(0).trim();
            if (argument.equals("null")) return new ConstantSpec(true, null, -1);
            int separator = argument.indexOf('=');
            if (separator <= 0 || separator == argument.length() - 1)
                throw new TransformException("invalid CONSTANT At argument: " + argument);
            String key = argument.substring(0, separator).trim();
            String literal = argument.substring(separator + 1).trim();
            try {
                Object value = switch (key) {
                    case "intValue" -> Integer.valueOf(literal);
                    case "longValue" -> Long.valueOf(literal.endsWith("L") || literal.endsWith("l")
                        ? literal.substring(0, literal.length() - 1) : literal);
                    case "floatValue" -> Float.valueOf(stripSuffix(literal, 'f', 'F'));
                    case "doubleValue" -> Double.valueOf(stripSuffix(literal, 'd', 'D'));
                    case "stringValue" -> literal;
                    case "classValue" -> new ClassConstant(literal);
                    case "nullValue" -> {
                        if (!Boolean.parseBoolean(literal)) throw new TransformException("nullValue must be true");
                        yield null;
                    }
                    default -> throw new TransformException("unsupported CONSTANT At argument: " + key);
                };
                return new ConstantSpec(true, value, -1);
            } catch (NumberFormatException failure) {
                throw new TransformException("invalid CONSTANT At value: " + argument, failure);
            }
        }

        private static String stripSuffix(String value, char... suffixes) {
            if (value.length() > 1) {
                char last = value.charAt(value.length() - 1);
                for (char suffix : suffixes) if (last == suffix) return value.substring(0, value.length() - 1);
            }
            return value;
        }

        static ConstantSpec read(AnnotationModel annotation) {
            if (annotation == null) return any();
            Object value = null;
            boolean hasValue = false;
            if (annotation.value("nullValue") != null && annotation.bool("nullValue", false)) {
                value = null; hasValue = true;
            } else if (annotation.value("stringValue") != null) {
                value = annotation.string("stringValue", ""); hasValue = true;
            } else if (annotation.value("longValue") != null) {
                value = annotation.value("longValue").value; hasValue = true;
            } else if (annotation.value("doubleValue") != null) {
                value = annotation.value("doubleValue").value; hasValue = true;
            } else if (annotation.value("floatValue") != null) {
                value = annotation.value("floatValue").value; hasValue = true;
            } else if (annotation.value("intValue") != null) {
                value = annotation.value("intValue").value; hasValue = true;
            } else if (annotation.value("classValue") != null) {
                value = new ClassConstant(String.valueOf(annotation.value("classValue").value)); hasValue = true;
            }
            return new ConstantSpec(hasValue, value, annotation.integer("ordinal", -1));
        }

        boolean matches(ConstantPool pool, BytecodeInstructions.Instruction instruction) {
            if (!hasValue) return true;
            Object actual = actualValue(pool, instruction);
            if (value == null) return actual == null;
            if (value instanceof ClassConstant wanted && actual instanceof String found)
                return normalizeInternal(wanted.name).equals(normalizeInternal(found));
            if (value instanceof Number wanted && actual instanceof Number found) {
                if (value instanceof Float || value instanceof Double || actual instanceof Float || actual instanceof Double)
                    return Double.compare(wanted.doubleValue(), found.doubleValue()) == 0;
                return wanted.longValue() == found.longValue();
            }
            return value.equals(actual);
        }

        private record ClassConstant(String name) { }

        private static Object actualValue(ConstantPool pool, BytecodeInstructions.Instruction instruction) {
            int opcode = instruction.opcode;
            if (opcode == 1) return null;
            if (opcode >= 2 && opcode <= 8) return opcode - 3 == -1 ? -1 : opcode - 3;
            if (opcode == 9 || opcode == 10) return (long) (opcode - 9);
            if (opcode >= 11 && opcode <= 13) return (float) (opcode - 11);
            if (opcode == 14 || opcode == 15) return (double) (opcode - 14);
            if (opcode == 16) return (int) (byte) instruction.raw[1];
            if (opcode == 17) return (int) (short) (((instruction.raw[1] & 0xff) << 8) | (instruction.raw[2] & 0xff));
            if (opcode == 18 || opcode == 19 || opcode == 20) return pool.constantValue(BytecodeInstructions.cpIndex(instruction));
            return null;
        }
        static Descriptor.Type typeFor(ConstantPool pool, BytecodeInstructions.Instruction instruction) {
            int opcode = instruction.opcode;
            if (opcode == 1) return new Descriptor.Type("Ljava/lang/Object;", 1, true, false, false, false);
            if (opcode >= 2 && opcode <= 8 || opcode == 16 || opcode == 17)
                return new Descriptor.Type("I", 1, false, false, true, false);
            if (opcode == 9 || opcode == 10) return new Descriptor.Type("J", 2, false, false, true, false);
            if (opcode >= 11 && opcode <= 13) return new Descriptor.Type("F", 1, false, false, true, false);
            if (opcode == 14 || opcode == 15) return new Descriptor.Type("D", 2, false, false, true, false);
            if (opcode == 18 || opcode == 19 || opcode == 20) {
                Object constant = pool.constantValue(BytecodeInstructions.cpIndex(instruction));
                if (constant instanceof Integer) return new Descriptor.Type("I", 1, false, false, true, false);
                if (constant instanceof Float) return new Descriptor.Type("F", 1, false, false, true, false);
                if (constant instanceof Long) return new Descriptor.Type("J", 2, false, false, true, false);
                if (constant instanceof Double) return new Descriptor.Type("D", 2, false, false, true, false);
                return new Descriptor.Type("Ljava/lang/Object;", 1, true, false, false, false);
            }
            return null;
        }
    }
}
