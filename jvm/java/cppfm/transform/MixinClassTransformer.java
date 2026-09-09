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
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Self-contained structural Mixin transformer for pre-definition class bytes.
 *
 * <p>This is intentionally a class-file implementation rather than an
 * annotation-reflection shim.  It parses Mixin metadata, copies handler/helper
 * methods into the target class, relocates Code/exception/debug/frame tables,
 * and edits real JVM instructions.  Supported sites include HEAD, TAIL,
 * RETURN, INVOKE, INVOKE_ASSIGN, FIELD, NEW, CONSTANT, JUMP, LOAD and STORE; supported
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
    private final List<String> appliedMixins = Collections.synchronizedList(new ArrayList<>());
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
        registerMixin(binaryName, bytes, null);
    }

    /** Register class bytes while retaining the originating Mixin resource. */
    public void registerMixin(String binaryName, byte[] bytes, String configName) {
        registerMixin(binaryName, bytes, configName, MixinReferenceMap.EMPTY);
    }

    /** Register class bytes with the config's named-to-intermediary refmap. */
    public void registerMixin(String binaryName, byte[] bytes, String configName,
                              MixinReferenceMap referenceMap) {
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
        LinkedHashSet<String> targetSet = new LinkedHashSet<>();
        for (AnnotationModel.ElementValue value : annotation.array("value")) {
            if (value.value instanceof String descriptor) {
                String target = classDescriptorToInternal(descriptor);
                if (!target.isEmpty()) targetSet.add(target);
            }
        }
        for (String target : annotation.strings("targets")) {
            String normalized = target.replace('.', '/');
            if (normalized.startsWith("L") && normalized.endsWith(";")) normalized = classDescriptorToInternal(normalized);
            if (!normalized.isEmpty()) targetSet.add(normalized);
        }
        if (targetSet.isEmpty()) {
            report("mixin has no target: " + mixinName, false);
            return;
        }
        List<String> targets = List.copyOf(targetSet);
        MixinDefinition definition = new MixinDefinition(mixinName, model, targets,
            annotation.integer("priority", 1000), nextRegistrationOrder.getAndIncrement(), configName,
            referenceMap == null ? MixinReferenceMap.EMPTY : referenceMap);
        for (String target : targets) {
            List<MixinDefinition> targetDefinitions = definitions.computeIfAbsent(
                normalizeInternal(target), ignored -> new CopyOnWriteArrayList<>());
            targetDefinitions.add(definition);
            targetDefinitions.sort(mixinOrder());
        }
    }

    /** Register every server/common entry from a parsed Mixin JSON object. */
    public void registerConfiguration(MixinConfiguration configuration, ClassLoader loader) {
        if (configuration == null) throw new NullPointerException("configuration");
        if (loader != null) mixinClassLoader = loader;
        MixinReferenceMap referenceMap = MixinReferenceMap.EMPTY;
        String refmap = configuration.refmap();
        if (!refmap.isEmpty() && loader != null) {
            try (InputStream stream = loader.getResourceAsStream(refmap)) {
                if (stream != null) referenceMap = MixinReferenceMap.parse(
                    new String(readAll(stream), java.nio.charset.StandardCharsets.UTF_8));
                else reportConfigurationResource("mixin refmap not found: " + refmap,
                    configuration.isRequired());
            } catch (IOException failure) {
                reportConfigurationResource("cannot read mixin refmap " + refmap + ": " + failure,
                    configuration.isRequired());
            }
        }
        for (String mixin : configuration.serverMixins()) {
            String resource = mixin.replace('.', '/') + ".class";
            try (InputStream stream = (loader == null ? ClassLoader.getSystemResourceAsStream(resource)
                                      : loader.getResourceAsStream(resource))) {
                if (stream == null) {
                    reportConfigurationResource("mixin class not found: " + mixin, configuration.isRequired());
                    continue;
                }
                registerMixin(mixin, readAll(stream), null, referenceMap);
            } catch (IOException failure) {
                reportConfigurationResource("cannot read mixin class " + mixin + ": " + failure,
                    configuration.isRequired());
            }
        }
    }

    public List<String> getDiagnostics() {
        synchronized (diagnostics) {
            return Collections.unmodifiableList(new ArrayList<>(diagnostics));
        }
    }

    /** Record a non-fatal loader diagnostic without changing strict policy. */
    public void addDiagnostic(String message) {
        if (message != null && !message.isEmpty()) diagnostics.add(message);
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

    /** Successful Mixin applications, in deterministic transformation order. */
    public List<String> getAppliedMixins() {
        synchronized (appliedMixins) {
            return Collections.unmodifiableList(new ArrayList<>(appliedMixins));
        }
    }

    @Override
    public byte[] transform(String binaryName, byte[] originalBytes, TransformContext context) {
        String internalName = normalizeInternal(binaryName);
        if (mixinClassNames.contains(internalName))
            return exposeMixinHelperFields(originalBytes, internalName);
        List<MixinDefinition> matching = definitions.get(internalName);
        if (matching == null || matching.isEmpty()) return originalBytes;
        Set<String> dispatchCheckpoint = MixinDispatch.transformedMethods();
        Set<String> changedMethodCheckpoint = context.getChangedMethods();
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
            ordered.sort(mixinOrder());
            List<PreparedMixin> prepared = new ArrayList<>();
            for (MixinDefinition definition : ordered) {
                // A normal class mixin may implement a public helper
                // interface (for example ServerCore's IMobCategory).  The
                // copied methods alone are not enough: JVM instanceof and
                // invokeinterface resolution require the target class to
                // advertise that interface in its class file.
                changed |= addMixinInterfaces(target, definition);
                prepared.add(prepare(target, definition));
            }
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
            // MixinExtras @Share references live for the duration of one
            // target-method invocation, not for one injector operation.  Keep
            // the bindings across the ordered operations so a ModifyVariable
            // and a Redirect/Inject using the same key see the same object.
            Map<String, SharedRefBinding> sharedRefs = new HashMap<>();
            List<String> pendingApplications = new ArrayList<>();
            for (MixinOperation operation : operations) {
                boolean operationChanged = applyMixinMethod(target, operation.mixin, operation.method, context,
                    sharedRefs);
                changed |= operationChanged;
                if (operationChanged) {
                    pendingApplications.add("Mixing " + simpleMixinName(operation.mixin.definition.name)
                        + " from " + configName(operation.mixin.definition)
                        + " into " + target.binaryName());
                }
            }
            if (!changed) return originalBytes;
            ClassFileSafety.validate(target);
            byte[] transformed = target.write();
            ClassFileSafety.validateBytes(transformed);
            appliedMixins.addAll(pendingApplications);
            return transformed;
        } catch (TransformException failure) {
            MixinDispatch.rollbackTo(dispatchCheckpoint);
            context.rollbackChangedMethods(changedMethodCheckpoint);
            String message = "left " + binaryName + " untransformed: " + failure.getMessage();
            context.diagnostic(message);
            if (strict || context.isStrict()) throw failure;
            report(message, false);
            return originalBytes;
        } catch (RuntimeException failure) {
            MixinDispatch.rollbackTo(dispatchCheckpoint);
            context.rollbackChangedMethods(changedMethodCheckpoint);
            String message = "left " + binaryName + " untransformed after parser failure: " + failure;
            context.diagnostic(message);
            if (strict || context.isStrict()) throw new TransformException(message, failure);
            report(message, false);
            return originalBytes;
        }
    }

    private boolean applyMixinMethod(ClassFileModel target, PreparedMixin prepared,
                                     MemberModel mixinMethod, TransformContext context,
                                     Map<String, SharedRefBinding> sharedRefs) {
        boolean changed = false;
        ClassFileModel mixin = prepared.definition.model;
        AnnotationModel overwrite = annotation(mixinMethod, mixin, "Overwrite");
        if (overwrite != null) changed |= applyOverwrite(target, prepared, mixinMethod, context);
        AnnotationModel accessor = annotation(mixinMethod, mixin, "Accessor");
        if (accessor != null) changed |= applyAccessor(target, prepared, mixinMethod, accessor, context);
        AnnotationModel invoker = annotation(mixinMethod, mixin, "Invoker");
        if (invoker != null) changed |= applyInvoker(target, prepared, mixinMethod, invoker, context);
        AnnotationModel inject = annotation(mixinMethod, mixin, "Inject");
        if (inject != null) changed |= applyInject(target, prepared, mixinMethod, inject, context, sharedRefs);
        AnnotationModel redirect = annotation(mixinMethod, mixin, "Redirect");
        if (redirect != null) changed |= applyRedirect(target, prepared, mixinMethod, redirect, context, sharedRefs);
        AnnotationModel modifyArg = annotation(mixinMethod, mixin, "ModifyArg");
        if (modifyArg != null) changed |= applyModifyArg(target, prepared, mixinMethod, modifyArg, context);
        AnnotationModel modifyConstant = annotation(mixinMethod, mixin, "ModifyConstant");
        if (modifyConstant != null) changed |= applyModifyConstant(target, prepared, mixinMethod, modifyConstant, context);
        AnnotationModel modifyVariable = annotation(mixinMethod, mixin, "ModifyVariable");
        if (modifyVariable != null) changed |= applyModifyVariable(target, prepared, mixinMethod, modifyVariable,
            context, sharedRefs);
        AnnotationModel modifyReturnValue = annotation(mixinMethod, mixin, "ModifyReturnValue");
        if (modifyReturnValue != null)
            changed |= applyModifyReturnValue(target, prepared, mixinMethod, modifyReturnValue, context, sharedRefs);
        AnnotationModel modifyExpressionValue = annotation(mixinMethod, mixin, "ModifyExpressionValue");
        if (modifyExpressionValue != null)
            changed |= applyModifyExpressionValue(target, prepared, mixinMethod, modifyExpressionValue, context, sharedRefs);
        AnnotationModel wrapWithCondition = annotation(mixinMethod, mixin, "WrapWithCondition");
        if (wrapWithCondition != null)
            changed |= applyWrapWithCondition(target, prepared, mixinMethod, wrapWithCondition, context);
        AnnotationModel wrapOperation = annotation(mixinMethod, mixin, "WrapOperation");
        if (wrapOperation != null)
            changed |= applyWrapOperation(target, prepared, mixinMethod, wrapOperation, context);
        AnnotationModel wrapMethod = annotation(mixinMethod, mixin, "WrapMethod");
        if (wrapMethod != null)
            changed |= applyWrapMethod(target, prepared, mixinMethod, wrapMethod, context);
        return changed;
    }

    private static AnnotationModel annotation(MemberModel method, ClassFileModel owner,
                                              String simpleName) {
        return AnnotationModel.first(method.attributes, owner.pool, simpleName);
    }

    private static Comparator<MixinDefinition> mixinOrder() {
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
                String targetFieldDescriptor = targetDescriptor(fieldDescriptor,
                    definition.model.internalName(), target.internalName());
                // A target may already provide the same uniquely named
                // compatibility field.  Reuse it rather than adding a
                // second, uninitialised copy of the mixin field.  This is
                // also how the Java shadow layer can supply constructor
                // state for a Mixin field whose initializer is not copied by
                // the structural transformer.
                if (target.field(desired, targetFieldDescriptor) != null) {
                    prepared.fieldRenames.put(key, desired);
                    continue;
                }
                int suffix = 0;
                while (target.field(desired, targetFieldDescriptor) != null)
                    desired = "$cppfm$mixin$" + Integer.toHexString(definition.name.hashCode())
                        + "$" + sourceName + "$" + (++suffix);
                prepared.fieldRenames.put(key, desired);
                MemberModel copy = new MemberModel();
                copy.access = field.access;
                copy.nameIndex = target.pool.addUtf8(desired);
                copy.descriptorIndex = target.pool.addUtf8(targetFieldDescriptor);
                target.fields.add(copy);
                continue;
            }
            String prefix = shadow.string("prefix", "shadow$");
            String targetName = sourceName.startsWith(prefix) ? sourceName.substring(prefix.length()) : sourceName;
            targetName = prepared.fieldName(targetName, targetOwner,
                targetDescriptor(fieldDescriptor, sourceOwner, targetOwner), resolver);
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
            if (AnnotationModel.first(field.attributes, definition.model.pool, "Mutable") != null) {
                MemberModel targetField = target.field(targetName,
                    targetDescriptor(fieldDescriptor, sourceOwner, targetOwner));
                if (targetField != null) targetField.access &= ~ClassFileModel.ACC_FINAL;
            }
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
                String targetName = name;
                if (shadow != null && overwrite == null && accessor == null && invoker == null) {
                    String mapped = prepared.memberToken(name + descriptor, targetOwner, resolver);
                    int open = mapped == null ? -1 : mapped.indexOf('(');
                    if (open >= 0) targetName = mapped.substring(0, open);
                }
                prepared.methodRenames.put(name + descriptor, targetName);
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
        result.descriptorIndex = target.pool.addUtf8(targetDescriptor(
            source.descriptor(definition.model.pool), definition.model.internalName(), target.internalName()));
        CodeModel sourceCode = source.code(definition.model.pool);
        if (sourceCode != null) {
            CodeModel code = copyCode(sourceCode, definition.model.pool, target.pool,
                definition.model.internalName(), target.internalName(), prepared.methodRenames,
                prepared.fieldRenames, prepared.bootstrapOffset, definition.model, source);
            result.attributes.add(new AttributeModel(target.pool.addUtf8("Code"), code.write(target.pool)));
        }
        return result;
    }

    private CodeModel copyCode(CodeModel source, ConstantPool sourcePool, ConstantPool targetPool,
                               String sourceOwner, String targetOwner,
                               Map<String, String> methodRenames, Map<String, String> fieldRenames,
                               int bootstrapOffset, ClassFileModel sourceModel, MemberModel sourceMethod) {
        CodeModel copy = CodeModel.parse(source.write(sourcePool), sourcePool);
        // Exception-table catch_type is a CONSTANT_Class index, just like
        // stack-map object types.  CodeModel keeps the raw index, so import
        // it into the target pool before the copied method is serialized;
        // leaving the source-pool index in place can make the JVM interpret
        // an unrelated target-pool entry as the catch class.
        for (CodeModel.ExceptionHandler handler : copy.exceptionHandlers) {
            if (handler.catchType == 0) continue; // finally / catch-all
            handler.catchType = targetPool.importEntry(sourcePool, handler.catchType,
                sourceOwner, targetOwner, methodRenames, fieldRenames);
        }
        remapCodeAttributeConstants(copy.attributes, sourcePool, targetPool, sourceOwner, targetOwner,
            methodRenames, fieldRenames);
        List<BytecodeInstructions.Instruction> sourceInstructions = BytecodeInstructions.decode(copy.code);
        List<BytecodeInstructions.Instruction> mapped = BytecodeInstructions.copyWithConstantPool(
            sourceInstructions, sourcePool, targetPool, sourceOwner, targetOwner,
            methodRenames, fieldRenames, bootstrapOffset);
        BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(copy, targetPool);
        editor.instructions.clear();
        editor.instructions.addAll(mapped);
        insertVerifierCasts(editor, sourceInstructions, sourceModel, sourceMethod,
            targetPool, sourceOwner, targetOwner);
        editor.finish(targetPool);
        return copy;
    }

    /**
     * Reconcile the verifier types exposed by a shadow API with the types
     * expected by a copied Mixin call site.  Real Mixin can use the target
     * hierarchy while transforming a method; a structural class-file copy
     * otherwise preserves the mixin method's return descriptor and can leave
     * a superclass value on the stack where a shadow subclass is required.
     *
     * <p>The adaptation is deliberately local to invoke operands.  Values are
     * spilled and reloaded in their original order, so casts work for any
     * argument position without changing the surrounding control flow.  A
     * failed conservative analysis leaves the old copy path intact; strict
     * transformation still reports the JVM verifier result if that class is
     * not otherwise valid.</p>
     */
    private void insertVerifierCasts(BytecodeInstructions.Editor editor,
                                     List<BytecodeInstructions.Instruction> sourceInstructions,
                                     ClassFileModel sourceModel, MemberModel sourceMethod,
                                     ConstantPool targetPool, String sourceOwner, String targetOwner) {
        if (sourceModel == null || sourceMethod == null) return;
        StackAnalyzer.Analysis analysis;
        try {
            analysis = StackAnalyzer.analyze(sourceModel, sourceMethod,
                sourceMethod.code(sourceModel.pool));
        } catch (TransformException unsupportedAnalysis) {
            return;
        }
        if (sourceInstructions.size() != editor.instructions.size()) return;
        // Walk backwards because insertBefore changes list indexes.  The
        // original instruction identity remains stable, while all earlier
        // indexes still refer to their corresponding source instruction.
        for (int i = sourceInstructions.size() - 1; i >= 0; --i) {
            BytecodeInstructions.Instruction sourceInstruction = sourceInstructions.get(i);
            BytecodeInstructions.Instruction targetInstruction = editor.instructions.get(i);
            int opcode = targetInstruction.opcode;
            if (opcode < 182 || opcode > 185) continue;
            int constant = BytecodeInstructions.cpIndex(targetInstruction);
            if (constant <= 0) continue;
            Descriptor.MethodDesc descriptor;
            try {
                descriptor = Descriptor.method(targetPool.memberDescriptor(constant));
            } catch (TransformException unsupportedDescriptor) {
                continue;
            }
            boolean hasReceiver = opcode != 184;
            int operandCount = descriptor.arguments.size() + (hasReceiver ? 1 : 0);
            List<StackAnalyzer.Value> before = analysis.before(sourceInstruction);
            if (before.size() < operandCount) continue;
            int base = before.size() - operandCount;
            ArrayList<Descriptor.Type> expected = new ArrayList<>(operandCount);
            if (hasReceiver) {
                String owner = targetPool.memberOwner(constant);
                expected.add(ownerType(owner));
            }
            expected.addAll(descriptor.arguments);

            ArrayList<Descriptor.Type> actual = new ArrayList<>(operandCount);
            boolean needsAdaptation = false;
            boolean hasUninitialized = false;
            for (int operand = 0; operand < operandCount; ++operand) {
                StackAnalyzer.Value value = before.get(base + operand);
                if (value == null) continue;
                if (value.isUninitialized()) hasUninitialized = true;
                Descriptor.Type actualType;
                try {
                    actualType = stackType(value);
                } catch (TransformException invalidValue) {
                    actualType = null;
                }
                if (actualType == null) break;
                actual.add(actualType);
                // INVOKESPECIAL uses the current class (or the uninitialized
                // object being constructed) as its verifier receiver even
                // when the constant-pool owner names a superclass.  Casting
                // `this` to that owner would turn a valid super call into an
                // invalid invokespecial stack shape.
                boolean specialReceiver = opcode == 183 && operand == 0;
                if (!specialReceiver && !value.isUninitialized() && expected.get(operand).reference
                    && actualType.reference
                    && !isVerifierAssignable(expected.get(operand).descriptor,
                        ConstantPool.remapDescriptor(actualType.descriptor, sourceOwner, targetOwner)))
                    needsAdaptation = true;
            }
            if (actual.size() != operandCount || !needsAdaptation || hasUninitialized) continue;

            int[] locals = new int[operandCount];
            ArrayList<BytecodeInstructions.Instruction> additions = new ArrayList<>();
            for (int operand = operandCount - 1; operand >= 0; --operand) {
                locals[operand] = editor.allocateLocal(actual.get(operand));
                additions.addAll(storeLocal(actual.get(operand), locals[operand]));
            }
            for (int operand = 0; operand < operandCount; ++operand) {
                additions.addAll(loadLocal(actual.get(operand), locals[operand]));
                Descriptor.Type expectedType = expected.get(operand);
                boolean specialReceiver = opcode == 183 && operand == 0;
                if (!specialReceiver && expectedType.reference && actual.get(operand).reference
                    && !isVerifierAssignable(expectedType.descriptor, ConstantPool.remapDescriptor(
                        actual.get(operand).descriptor, sourceOwner, targetOwner))) {
                    String castType = expectedType.array ? expectedType.descriptor
                        : descriptorOwner(expectedType.descriptor);
                    additions.add(memberInstruction(192, targetPool.addClass(castType)));
                }
            }
            editor.insertBefore(targetInstruction, additions);
        }
    }

    private boolean applyOverwrite(ClassFileModel target, PreparedMixin prepared,
                                   MemberModel source, TransformContext context) {
        String sourceName = source.name(prepared.definition.model.pool);
        String sourceDescriptor = source.descriptor(prepared.definition.model.pool);
        String destinationName = sourceName;
        String descriptor = targetDescriptor(sourceDescriptor,
            prepared.definition.model.internalName(), target.internalName());
        String mapped = prepared.memberToken(sourceName + sourceDescriptor, target.internalName(), resolver);
        int mappedOpen = mapped == null ? -1 : mapped.indexOf('(');
        if (mappedOpen >= 0) {
            destinationName = mapped.substring(0, mappedOpen);
            descriptor = targetDescriptor(mapped.substring(mappedOpen),
                prepared.definition.model.internalName(), target.internalName());
        }
        MemberModel destination = target.method(destinationName, descriptor);
        if (destination == null) throw unsupported("@Overwrite target not found: "
            + destinationName + descriptor);
        CodeModel sourceCode = source.code(prepared.definition.model.pool);
        if (sourceCode == null) throw unsupported("@Overwrite has no Code: " + source.name(prepared.definition.model.pool));
        CodeModel code = copyCode(sourceCode, prepared.definition.model.pool, target.pool,
            prepared.definition.model.internalName(), target.internalName(), prepared.methodRenames,
            prepared.fieldRenames, prepared.bootstrapOffset, prepared.definition.model, source);
        destination.access &= ~(ClassFileModel.ACC_ABSTRACT | ClassFileModel.ACC_NATIVE);
        destination.replaceCode(target.pool, code);
        mark(context, destination, target);
        return true;
    }

    private boolean applyAccessor(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                  AnnotationModel annotation, TransformContext context) {
        String descriptor = targetDescriptor(source.descriptor(prepared.definition.model.pool),
            prepared.definition.model.internalName(), target.internalName());
        Descriptor.MethodDesc method = Descriptor.method(descriptor);
        String methodName = source.name(prepared.definition.model.pool);
        String fieldName = annotation.string("value", "");
        if (fieldName.isEmpty()) fieldName = inferMemberName(methodName, "get", "is", "set");
        fieldName = prepared.fieldName(fieldName, target.internalName(),
            targetDescriptor(source.descriptor(prepared.definition.model.pool),
                prepared.definition.model.internalName(), target.internalName()), resolver);
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
        String descriptor = targetDescriptor(source.descriptor(prepared.definition.model.pool),
            prepared.definition.model.internalName(), target.internalName());
        Descriptor.MethodDesc method = Descriptor.method(descriptor);
        String sourceName = source.name(prepared.definition.model.pool);
        String invokedName = annotation.string("value", "");
        if (invokedName.isEmpty()) {
            String mapped = prepared.memberToken(sourceName, target.internalName(), resolver);
            if (mapped != null && !mapped.equals(sourceName)) {
                int open = mapped.indexOf('(');
                invokedName = open < 0 ? mapped : mapped.substring(0, open);
            } else {
                invokedName = inferInvokerName(sourceName);
            }
        } else {
            String mapped = prepared.memberToken(invokedName, target.internalName(), resolver);
            if (mapped != null && !mapped.equals(invokedName)) {
                int open = mapped.indexOf('(');
                invokedName = open < 0 ? mapped : mapped.substring(0, open);
            }
        }
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
                                 AnnotationModel annotation, TransformContext context,
                                 Map<String, SharedRefBinding> sharedRefs) {
        Handler handler = prepared.handler(source, target);
        List<String> methodNames = targetMethodTokens(annotation, prepared, target);
        if (methodNames.isEmpty()) throw unsupported("@Inject has no target method: " + source.name(prepared.definition.model.pool));
        List<AtSpec> atSpecs = readAtSpecs(annotation, "at", prepared, target);
        if (atSpecs.isEmpty()) throw unsupported("@Inject has no @At");
        List<AnnotationModel> slices = nestedAnnotations(annotation, "slice");
        LocalCaptureMode localCapture = LocalCaptureMode.read(annotation);
        boolean changed = false;
        int matchedSites = 0;
        for (AtSpec at : atSpecs) {
            for (String methodName : methodNames) {
                for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, true)) {
                    CodeModel code = destination.code(target.pool);
                    if (code == null) throw unsupported("cannot inject into abstract/native method: " + destination.name(target.pool));
                    BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                    List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at, slices);
                    if (sites.isEmpty()) continue;
                    ensureSharedRefs(target, destination, handler, editor, sharedRefs);
                    validateConstructorInjectionSites(target, destination, at, editor.instructions, sites);
                    boolean returnSite = at.value.equals("RETURN");
                    boolean cancellable = annotation.bool("cancellable", false);
                    boolean needsLocalCapture = localCapture.requiresAnalysis(handler, target, destination);
                    StackAnalyzer.Analysis stackAnalysis;
                    try {
                        stackAnalysis = (!returnSite
                        || needsLocalCapture
                        || cancellable)
                        ? StackAnalyzer.analyze(target, destination, code) : null;
                    } catch (TransformException failure) {
                        if (needsLocalCapture && localCapture.isSoft()) {
                            context.diagnostic("skipped soft local capture for " + handler.name + ": " + failure.getMessage());
                            continue;
                        }
                        throw failure;
                    }
                    LocalVariableTable localTable = stackAnalysis == null ? null : LocalVariableTable.read(code, target.pool);
                    int count = 0;
                    for (int i = sites.size() - 1; i >= 0; --i) {
                        BytecodeInstructions.Instruction site = sites.get(i);
                        List<CapturedLocal> captured;
                        try {
                            captured = stackAnalysis == null ? List.of()
                                : capturedLocals(target, destination, handler, localCapture, stackAnalysis,
                                    localTable, site, at.shift == Shift.AFTER);
                        } catch (TransformException failure) {
                            if (localCapture.isSoft()) {
                                context.diagnostic("skipped soft local capture for " + handler.name + " at "
                                    + site.oldOffset + ": " + failure.getMessage());
                                continue;
                            }
                            throw failure;
                        }
                        List<BytecodeInstructions.Instruction> addition = returnSite
                            ? buildReturnInjection(target, destination, handler, editor, site, cancellable, captured,
                                sharedRefs)
                            : buildCallbackInjection(target, destination, handler, editor, cancellable,
                                at.value.equals("TAIL"),
                                at.shift == Shift.AFTER ? stackAnalysis.after(site) : stackAnalysis.before(site),
                                captured, sharedRefs);
                        if (at.shift == Shift.AFTER) editor.insertAfter(site, addition);
                        else editor.insertBefore(site, addition);
                        count++;
                    }
                    matchedSites += count;
                    if (count == 0) continue;
                    editor.finish(target.pool);
                    // Any insertion changes the byte offsets used by the
                    // existing StackMapTable, including a non-cancellable
                    // callback inserted immediately before RETURN.  Rebuild
                    // frames for every @Inject path; restricting this to
                    // cancellable callbacks leaves valid source methods such
                    // as MathHelper.<clinit> unverifiable after a tail hook.
                    try {
                        editor.rebuildStackMapFrames(target, destination);
                    } catch (TransformException frameFailure) {
                        // A conservative stack analysis may refuse an
                        // optional library call in a generated callback.  A
                        // stale StackMapTable is never safe after insertion;
                        // removing it lets HotSpot perform the complete
                        // verifier pass over the otherwise valid bytecode.
                        code.stripDebugAndFrames(target.pool);
                        context.diagnostic("removed stale StackMapTable after @Inject from "
                            + prepared.definition.name + " at " + destination.name(target.pool)
                            + " in " + target.binaryName() + ": " + frameFailure.getMessage());
                    }
                    destination.replaceCode(target.pool, code);
                    mark(context, destination, target);
                    changed = true;
                }
            }
        }
        validateMatchCount(annotation, matchedSites, "@Inject " + source.name(prepared.definition.model.pool));
        return changed;
    }

    private List<BytecodeInstructions.Instruction> buildCallbackInjection(ClassFileModel target,
            MemberModel destination, Handler handler, BytecodeInstructions.Editor editor,
            boolean cancellable, boolean returnBoundary,
            List<StackAnalyzer.Value> preservedStack, List<CapturedLocal> capturedLocals,
            Map<String, SharedRefBinding> sharedRefs) {
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
            callbackIndex, callbackLocal, capturedTypes(capturedLocals), capturedSlots(capturedLocals), editor,
            sharedRefs));
        if (cancellable) {
            output.addAll(loadLocal(callbackType, callbackLocal));
            int cancelled = target.pool.addMethodRef(
                "org/spongepowered/asm/mixin/injection/callback/CallbackInfo",
                "isCancelled", "()Z", false);
            output.add(memberInstruction(182, cancelled));
            BytecodeInstructions.Instruction continueLabel = editor.newLabel();
            output.add(BytecodeInstructions.Instruction.branch(153, continueLabel)); // IFEQ
            if (targetDescriptor.returnType.voidType) {
                output.add(returnInstruction(targetDescriptor.returnType));
            } else if (returnable) {
                output.addAll(loadReturnValue(target.pool, targetDescriptor.returnType, callbackLocal));
                output.add(returnInstruction(targetDescriptor.returnType));
            } else if (returnBoundary && !stackTypes.isEmpty()) {
                Descriptor.Type original = stackTypes.get(stackTypes.size() - 1);
                if (!compatible(targetDescriptor.returnType, original))
                    throw unsupported("cancellable TAIL return value does not match target: " + handler.name);
                output.addAll(loadLocal(original, stackLocals.get(stackLocals.size() - 1)));
                output.add(returnInstruction(targetDescriptor.returnType));
            } else {
                throw unsupported("cancellable non-returnable injection targets a non-void method: "
                    + destination.name(target.pool));
            }
            output.add(continueLabel);
        }
        for (int i = 0; i < stackTypes.size(); ++i)
            output.addAll(loadLocal(stackTypes.get(i), stackLocals.get(i)));
        return output;
    }

    private List<BytecodeInstructions.Instruction> buildReturnInjection(ClassFileModel target,
            MemberModel destination, Handler handler, BytecodeInstructions.Editor editor,
            BytecodeInstructions.Instruction site, boolean cancellable,
            List<CapturedLocal> capturedLocals, Map<String, SharedRefBinding> sharedRefs) {
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
                callbackIndex, callbackLocal, capturedTypes(capturedLocals), capturedSlots(capturedLocals), editor,
                sharedRefs));
            if (cancellable) {
                output.addAll(loadLocal(callbackType, callbackLocal));
                int cancelled = target.pool.addMethodRef(
                    "org/spongepowered/asm/mixin/injection/callback/CallbackInfo",
                    "isCancelled", "()Z", false);
                output.add(memberInstruction(182, cancelled));
                BytecodeInstructions.Instruction continueLabel = editor.newLabel();
                output.add(BytecodeInstructions.Instruction.branch(153, continueLabel)); // IFEQ
                output.add(returnInstruction(returnType));
                output.add(continueLabel);
            }
            return output;
        }
        int returnLocal = editor.allocateLocal(returnType);
        output.addAll(storeLocal(returnType, returnLocal));
        int callbackLocal = editor.allocateLocal(new Descriptor.Type(callbackType.descriptor, 1, true, false, false, false));
        boolean returnable = callbackType.descriptor.endsWith("CallbackInfoReturnable;");
        output.addAll(makeCallbackObject(target, callbackType, destination.name(target.pool) + ":RETURN",
            cancellable, returnable ? returnType : null, editor, callbackLocal, returnLocal));
        output.addAll(callHandler(target, destination, handler, targetDescriptor, handlerDescriptor,
            callbackIndex, callbackLocal, capturedTypes(capturedLocals), capturedSlots(capturedLocals), editor,
            sharedRefs));
        if (cancellable) {
            output.addAll(loadLocal(callbackType, callbackLocal));
            int cancelled = target.pool.addMethodRef(
                "org/spongepowered/asm/mixin/injection/callback/CallbackInfo",
                "isCancelled", "()Z", false);
            output.add(memberInstruction(182, cancelled));
            BytecodeInstructions.Instruction continueLabel = editor.newLabel();
            output.add(BytecodeInstructions.Instruction.branch(153, continueLabel)); // IFEQ
            if (returnType.voidType) {
                output.add(returnInstruction(returnType));
            } else if (returnable) {
                output.addAll(loadReturnValue(target.pool, returnType, callbackLocal));
                output.add(returnInstruction(returnType));
            } else {
                output.addAll(loadLocal(returnType, returnLocal));
                output.add(returnInstruction(returnType));
            }
            output.add(continueLabel);
        }
        if (returnable) output.addAll(loadReturnValue(target.pool, returnType, callbackLocal));
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
            int callbackIndex, int callbackLocal, List<Descriptor.Type> capturedTypes,
            List<Integer> capturedLocals, BytecodeInstructions.Editor editor,
            Map<String, SharedRefBinding> sharedRefs) {
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        if (capturedTypes.size() != capturedLocals.size())
            throw unsupported("captured local bookkeeping mismatch");
        if (!handler.isStatic) output.add(bytes(42));
        int slot = (destination.access & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
        int[] targetSlots = new int[targetDescriptor.arguments.size()];
        for (int i = 0; i < targetDescriptor.arguments.size(); ++i) {
            targetSlots[i] = slot;
            slot += targetDescriptor.arguments.get(i).slots;
        }
        int targetArgument = 0;
        int captured = 0;
        for (int i = 0; i < handlerDescriptor.arguments.size(); ++i) {
            Descriptor.Type expected = handlerDescriptor.arguments.get(i);
            if (i == callbackIndex) {
                output.addAll(loadLocal(expected, callbackLocal));
                continue;
            }
            if (isSharedRefType(expected)) {
                SharedRefBinding binding = sharedRefs.get(sharedBindingKey(destination, target.pool,
                    shareKey(handler, i)));
                if (binding == null) throw unsupported("missing shared ref binding: " + handler.name);
                output.addAll(loadLocal(binding.type(), binding.local()));
                continue;
            }
            if (isLocalParameter(handler, i)) {
                if (captured >= capturedTypes.size())
                    throw unsupported("handler requests an unavailable @Local: " + handler.name);
                Descriptor.Type actual = capturedTypes.get(captured);
                int local = capturedLocals.get(captured++);
                if (!compatible(expected, actual))
                    throw unsupported("@Local handler argument does not match captured local: " + handler.name);
                output.addAll(loadLocal(actual, local));
                continue;
            }
            Descriptor.Type actual;
            int local;
            if (targetArgument < targetDescriptor.arguments.size()) {
                actual = targetDescriptor.arguments.get(targetArgument);
                local = targetSlots[targetArgument++];
            } else if (captured < capturedTypes.size()) {
                actual = capturedTypes.get(captured);
                local = capturedLocals.get(captured++);
            } else {
                throw unsupported("handler captures unsupported locals: " + handler.name + handler.descriptor);
            }
            if (!compatible(expected, actual)) throw unsupported("handler argument does not match target argument: " + handler.name);
            output.addAll(loadLocal(actual, local));
        }
        // An @Inject handler may intentionally omit trailing target
        // parameters (the common CallbackInfo-only form).  Every captured
        // local, however, must have a corresponding handler parameter.
        if (captured < capturedTypes.size())
            throw unsupported("handler argument layout does not match target: " + handler.name + handler.descriptor);
        int ref = target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false);
        output.add(memberInstruction(handler.isStatic ? 184 : 182, ref));
        return output;
    }

    private boolean applyRedirect(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                  AnnotationModel annotation, TransformContext context,
                                  Map<String, SharedRefBinding> sharedRefs) {
        Handler handler = prepared.handler(source, target);
        AtSpec at = readAtSpec(nestedAnnotation(annotation, "at"), prepared, target);
        if (!(at.value.equals("INVOKE") || at.value.equals("FIELD") || at.value.equals("NEW")))
            throw unsupported("@Redirect requires INVOKE, FIELD or NEW, got " + at.value);
        boolean changed = false;
        int matchedSites = 0;
        for (String methodName : targetMethodTokens(annotation, prepared, target)) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot redirect abstract/native method: " + methodName);
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                    nestedAnnotations(annotation, "slice"));
                if (sites.isEmpty()) continue;
                ensureSharedRefs(target, destination, handler, editor, sharedRefs);
                StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                for (int i = sites.size() - 1; i >= 0; --i) {
                    BytecodeInstructions.Instruction site = sites.get(i);
                    int index = editor.instructions.indexOf(site);
                    List<BytecodeInstructions.Instruction> replacement;
                    int constructorEnd = -1;
                    if (at.value.equals("NEW")) {
                        constructorEnd = findConstructorEnd(editor.instructions, index, target.pool, analysis);
                        replacement = buildNewRedirect(target, editor, index, constructorEnd, handler, analysis,
                            destination, sharedRefs);
                    } else {
                        replacement = buildMemberRedirect(target, editor, site, handler, analysis, destination,
                            sharedRefs);
                    }
                    if (at.value.equals("NEW")) {
                        editor.replaceRange(index, constructorEnd, replacement);
                    } else editor.replace(site, replacement);
                    matchedSites++;
                }
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        validateMatchCount(annotation, matchedSites, "@Redirect " + source.name(prepared.definition.model.pool));
        return changed;
    }

    private List<BytecodeInstructions.Instruction> buildMemberRedirect(ClassFileModel target,
            BytecodeInstructions.Editor editor, BytecodeInstructions.Instruction site, Handler handler,
            StackAnalyzer.Analysis analysis, MemberModel destination,
            Map<String, SharedRefBinding> sharedRefs) {
        int cp = BytecodeInstructions.cpIndex(site);
        if (cp < 0) throw unsupported("member redirect site has no reference");
        String owner = target.pool.memberOwner(cp);
        String name = target.pool.memberName(cp);
        String descriptor = target.pool.memberDescriptor(cp);
        List<Descriptor.Type> stackTypes = new ArrayList<>();
        boolean isField = BytecodeInstructions.isField(site.opcode);
        boolean staticMember = site.opcode == 178 || site.opcode == 179 || site.opcode == 184;
        if (!staticMember && (isField || BytecodeInstructions.isInvoke(site.opcode)))
            stackTypes.add(ownerType(owner));
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
        requireStackSuffix(analysis.before(site), stackTypes,
            "@Redirect stack shape does not match " + owner + "." + name + descriptor);
        return spillAndCall(target, editor, stackTypes, handler, expectedReturn, destination, sharedRefs);
    }

    private List<BytecodeInstructions.Instruction> buildNewRedirect(ClassFileModel target,
            BytecodeInstructions.Editor editor, int index, int end, Handler handler,
            StackAnalyzer.Analysis analysis, MemberModel destination,
            Map<String, SharedRefBinding> sharedRefs) {
        BytecodeInstructions.Instruction newInstruction = editor.instructions.get(index);
        int cp = BytecodeInstructions.cpIndex(newInstruction);
        String owner = target.pool.className(cp);
        if (index + 1 >= editor.instructions.size() || editor.instructions.get(index + 1).opcode != 89)
            throw unsupported("NEW redirect requires DUP immediately after NEW: " + owner);
        BytecodeInstructions.Instruction initInstruction = editor.instructions.get(end);
        Descriptor.MethodDesc constructor = Descriptor.method(target.pool.memberDescriptor(BytecodeInstructions.cpIndex(initInstruction)));
        List<StackAnalyzer.Value> beforeConstructor = analysis.before(initInstruction);
        int receiverIndex = beforeConstructor.size() - constructor.arguments.size() - 1;
        if (receiverIndex < 0 || !beforeConstructor.get(receiverIndex).isUninitialized())
            throw unsupported("NEW redirect constructor receiver is not uninitialized: " + owner);
        List<BytecodeInstructions.Instruction> argumentInstructions = new ArrayList<>();
        for (int i = index + 2; i < end; ++i) argumentInstructions.add(editor.instructions.get(i).copy());
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>(argumentInstructions);
        output.addAll(spillAndCall(target, editor, constructor.arguments, handler, ownerType(owner), destination,
            sharedRefs));
        return output;
    }

    private int findConstructorEnd(List<BytecodeInstructions.Instruction> instructions, int start,
                                   ConstantPool pool, StackAnalyzer.Analysis analysis) {
        int newCp = BytecodeInstructions.cpIndex(instructions.get(start));
        String owner = pool.className(newCp);
        if (start + 1 >= instructions.size() || instructions.get(start + 1).opcode != 89)
            throw unsupported("NEW has no immediate DUP: " + owner);
        List<StackAnalyzer.Value> createdStack = analysis.after(instructions.get(start));
        StackAnalyzer.Value created = lastValue(createdStack);
        if (created == null || !created.isUninitialized())
            throw unsupported("NEW does not produce an uninitialized value: " + owner);
        for (int i = start + 1; i < instructions.size(); ++i) {
            BytecodeInstructions.Instruction instruction = instructions.get(i);
            if (instruction.opcode != 183) continue;
            int cp = BytecodeInstructions.cpIndex(instruction);
            if (cp < 0 || !pool.memberOwner(cp).equals(owner) || !pool.memberName(cp).equals("<init>")) continue;
            Descriptor.MethodDesc constructor = Descriptor.method(pool.memberDescriptor(cp));
            List<StackAnalyzer.Value> before = analysis.before(instruction);
            int receiverIndex = before.size() - constructor.arguments.size() - 1;
            if (receiverIndex >= 0) {
                StackAnalyzer.Value receiver = before.get(receiverIndex);
                if (receiver.isUninitialized() && receiver.uninitializedToken == created.uninitializedToken)
                    return i;
            }
        }
        throw unsupported("NEW has no matching invokespecial <init>: " + owner);
    }

    private List<BytecodeInstructions.Instruction> spillAndCall(ClassFileModel target,
            BytecodeInstructions.Editor editor, List<Descriptor.Type> stackTypes,
            Handler handler, Descriptor.Type expectedReturn, MemberModel destination,
            Map<String, SharedRefBinding> sharedRefs) {
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        if (handlerDescriptor.arguments.size() < stackTypes.size())
            throw unsupported("handler descriptor does not match redirected member: "
                + handler.name + handler.descriptor);
        if (expectedReturn != null && !compatible(expectedReturn, handlerDescriptor.returnType))
            throw unsupported("NEW redirect return type mismatch: " + handler.name);
        ArrayList<Integer> locals = new ArrayList<>();
        for (Descriptor.Type type : stackTypes) locals.add(editor.allocateLocal(type));
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        for (int i = stackTypes.size() - 1; i >= 0; --i) output.addAll(storeLocal(stackTypes.get(i), locals.get(i)));
        if (!handler.isStatic) output.add(bytes(42));
        int targetSlot = (destination.access & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
        int targetArgument = 0;
        for (int i = 0; i < handlerDescriptor.arguments.size(); ++i) {
            Descriptor.Type expected = handlerDescriptor.arguments.get(i);
            if (i < stackTypes.size()) {
                Descriptor.Type actual = stackTypes.get(i);
                if (!compatible(expected, actual))
                    throw unsupported("handler descriptor does not match redirected member: "
                        + handler.name + handler.descriptor);
                output.addAll(loadLocal(actual, locals.get(i)));
            } else if (isSharedRefType(expected)) {
                SharedRefBinding binding = sharedRefs.get(sharedBindingKey(destination, target.pool,
                    shareKey(handler, i)));
                if (binding == null) throw unsupported("missing shared ref binding: " + handler.name);
                output.addAll(loadLocal(binding.type(), binding.local()));
            } else {
                Descriptor.MethodDesc targetDescriptor = Descriptor.method(destination.descriptor(target.pool));
                if (targetArgument >= targetDescriptor.arguments.size())
                    throw unsupported("handler descriptor does not match redirected member: "
                        + handler.name + handler.descriptor);
                Descriptor.Type actual = targetDescriptor.arguments.get(targetArgument);
                if (!compatible(expected, actual))
                    throw unsupported("handler context argument does not match target method: "
                        + handler.name + handler.descriptor);
                output.addAll(loadLocal(actual, targetSlot));
                targetSlot += actual.slots;
                targetArgument++;
            }
        }
        Descriptor.MethodDesc destinationDescriptor = Descriptor.method(destination.descriptor(target.pool));
        if (targetArgument != destinationDescriptor.arguments.size()
            && handlerDescriptor.arguments.size() > stackTypes.size()) {
            // Context arguments are optional for redirect handlers, but once
            // one ordinary context parameter is present all supplied context
            // parameters must be consumed in declaration order.
            boolean hasOrdinaryContext = false;
            for (int i = stackTypes.size(); i < handlerDescriptor.arguments.size(); ++i)
                hasOrdinaryContext |= !isSharedRefType(handlerDescriptor.arguments.get(i));
            if (hasOrdinaryContext)
                throw unsupported("handler context argument layout does not match target method: "
                    + handler.name + handler.descriptor);
        }
        int ref = target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false);
        output.add(memberInstruction(handler.isStatic ? 184 : 182, ref));
        return output;
    }

    /** Apply MixinExtras' value-at-return operation without a callback object. */
    private boolean applyModifyReturnValue(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                           AnnotationModel annotation, TransformContext context,
                                           Map<String, SharedRefBinding> sharedRefs) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc modifier = Descriptor.method(handler.descriptor);
        if (modifier.arguments.isEmpty() || modifier.returnType.voidType)
            throw unsupported("@ModifyReturnValue handler must accept and return the modified value: "
                + handler.name + handler.descriptor);
        List<AtSpec> atSpecs = readAtSpecs(annotation, "at", prepared, target);
        if (atSpecs.isEmpty()) throw unsupported("@ModifyReturnValue has no @At");
        boolean changed = false;
        int matchedSites = 0;
        for (AtSpec at : atSpecs) {
            if (!at.value.equals("RETURN"))
                throw unsupported("@ModifyReturnValue requires RETURN, got " + at.value);
            for (String methodName : targetMethodTokens(annotation, prepared, target)) {
                for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                    Descriptor.MethodDesc destinationDescriptor = Descriptor.method(destination.descriptor(target.pool));
                    if (destinationDescriptor.returnType.voidType)
                        throw unsupported("@ModifyReturnValue cannot target a void method: "
                            + destination.name(target.pool));
                    if (!compatible(destinationDescriptor.returnType, modifier.arguments.get(0))
                        || !compatible(destinationDescriptor.returnType, modifier.returnType))
                        throw unsupported("@ModifyReturnValue type mismatch: " + handler.name + handler.descriptor);
                    if (!handler.isStatic && (destination.access & ClassFileModel.ACC_STATIC) != 0)
                        throw unsupported("non-static @ModifyReturnValue handler targets a static method: " + handler.name);
                    CodeModel code = destination.code(target.pool);
                    if (code == null) throw unsupported("cannot modify return value in abstract/native method");
                    BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                    ensureSharedRefs(target, destination, handler, editor, sharedRefs);
                    List<CapturedLocal> targetLocals = targetArgumentLocals(target, destination, handler);
                    List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                        nestedAnnotations(annotation, "slice"));
                    if (sites.isEmpty()) continue;
                    for (int i = sites.size() - 1; i >= 0; --i) {
                        BytecodeInstructions.Instruction site = sites.get(i);
                        int local = editor.allocateLocal(destinationDescriptor.returnType);
                        ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                        replacement.addAll(storeLocal(destinationDescriptor.returnType, local));
                        replacement.addAll(callHandler(target, destination, handler, destinationDescriptor, modifier,
                            0, local, capturedTypes(targetLocals), capturedSlots(targetLocals), editor, sharedRefs));
                        replacement.add(returnInstruction(destinationDescriptor.returnType));
                        editor.replace(site, replacement);
                    }
                    editor.finish(target.pool);
                    destination.replaceCode(target.pool, code);
                    mark(context, destination, target);
                    matchedSites += sites.size();
                    changed = true;
                }
            }
        }
        validateMatchCount(annotation, matchedSites,
            "@ModifyReturnValue " + source.name(prepared.definition.model.pool));
        return changed;
    }

    /** Apply MixinExtras' expression modifier to an INVOKE or field read. */
    private boolean applyModifyExpressionValue(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                               AnnotationModel annotation, TransformContext context,
                                               Map<String, SharedRefBinding> sharedRefs) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc modifier = Descriptor.method(handler.descriptor);
        if (modifier.arguments.isEmpty() || modifier.returnType.voidType)
            throw unsupported("@ModifyExpressionValue handler must accept and return the modified value: "
                + handler.name + handler.descriptor);
        List<AtSpec> atSpecs = readAtSpecs(annotation, "at", prepared, target);
        if (atSpecs.isEmpty()) throw unsupported("@ModifyExpressionValue has no @At");
        boolean changed = false;
        int matchedSites = 0;
        for (AtSpec at : atSpecs) {
            if (!at.value.equals("INVOKE") && !at.value.equals("FIELD"))
                throw unsupported("@ModifyExpressionValue requires INVOKE or FIELD, got " + at.value);
            for (String methodName : targetMethodTokens(annotation, prepared, target)) {
                for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                    if (!handler.isStatic && (destination.access & ClassFileModel.ACC_STATIC) != 0)
                        throw unsupported("non-static @ModifyExpressionValue handler targets a static method: " + handler.name);
                    CodeModel code = destination.code(target.pool);
                    if (code == null) throw unsupported("cannot modify expression in abstract/native method");
                    BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                    ensureSharedRefs(target, destination, handler, editor, sharedRefs);
                    List<CapturedLocal> targetLocals = targetArgumentLocals(target, destination, handler);
                    List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                        nestedAnnotations(annotation, "slice"));
                    if (sites.isEmpty()) continue;
                    StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                    for (int i = sites.size() - 1; i >= 0; --i) {
                        BytecodeInstructions.Instruction site = sites.get(i);
                        Descriptor.Type result = memberResultType(target, site);
                        if (result == null || result.voidType)
                            throw unsupported("@ModifyExpressionValue site has no value at " + site.oldOffset);
                        if (!compatible(result, modifier.arguments.get(0))
                            || !compatible(result, modifier.returnType))
                            throw unsupported("@ModifyExpressionValue type mismatch at " + site.oldOffset);
                        List<Descriptor.Type> operands = memberOperandTypes(target, site);
                        requireStackSuffix(analysis.before(site), operands,
                            "@ModifyExpressionValue stack shape does not match site at " + site.oldOffset);
                        int local = editor.allocateLocal(result);
                        ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                        BytecodeInstructions.Instruction original = generatedMember(site);
                        replacement.add(original);
                        replacement.addAll(storeLocal(result, local));
                        replacement.addAll(callHandler(target, destination, handler,
                            Descriptor.method(destination.descriptor(target.pool)), modifier,
                            0, local, capturedTypes(targetLocals), capturedSlots(targetLocals), editor, sharedRefs));
                        editor.replace(site, replacement);
                    }
                    editor.finish(target.pool);
                    destination.replaceCode(target.pool, code);
                    mark(context, destination, target);
                    matchedSites += sites.size();
                    changed = true;
                }
            }
        }
        validateMatchCount(annotation, matchedSites,
            "@ModifyExpressionValue " + source.name(prepared.definition.model.pool));
        return changed;
    }

    /** Conditionally execute an invocation/field operation, preserving its stack result. */
    private boolean applyWrapWithCondition(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                           AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        if (!handlerDescriptor.returnType.descriptor.equals("Z"))
            throw unsupported("@WrapWithCondition handler must return boolean: " + handler.name);
        List<AtSpec> atSpecs = readAtSpecs(annotation, "at", prepared, target);
        if (atSpecs.isEmpty()) throw unsupported("@WrapWithCondition has no @At");
        boolean changed = false;
        int matchedSites = 0;
        for (AtSpec at : atSpecs) {
            if (!at.value.equals("INVOKE") && !at.value.equals("FIELD"))
                throw unsupported("@WrapWithCondition requires INVOKE or FIELD, got " + at.value);
            for (String methodName : targetMethodTokens(annotation, prepared, target)) {
                for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                    if (!handler.isStatic && (destination.access & ClassFileModel.ACC_STATIC) != 0)
                        throw unsupported("non-static @WrapWithCondition handler targets a static method: " + handler.name);
                    CodeModel code = destination.code(target.pool);
                    if (code == null) throw unsupported("cannot wrap operation in abstract/native method");
                    BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                    List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                        nestedAnnotations(annotation, "slice"));
                    if (sites.isEmpty()) continue;
                    StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                    for (int i = sites.size() - 1; i >= 0; --i) {
                        BytecodeInstructions.Instruction site = sites.get(i);
                        List<Descriptor.Type> operands = memberOperandTypes(target, site);
                        Descriptor.Type result = memberResultType(target, site);
                        if (handlerDescriptor.arguments.size() != operands.size())
                            throw unsupported("@WrapWithCondition handler argument layout does not match site: "
                                + handler.name + handler.descriptor);
                        for (int operand = 0; operand < operands.size(); ++operand)
                            if (!compatible(operands.get(operand), handlerDescriptor.arguments.get(operand)))
                                throw unsupported("@WrapWithCondition operand type mismatch at " + site.oldOffset);
                        requireStackSuffix(analysis.before(site), operands,
                            "@WrapWithCondition stack shape does not match site at " + site.oldOffset);
                        ArrayList<Integer> locals = allocateLocals(editor, operands);
                        ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                        for (int operand = operands.size() - 1; operand >= 0; --operand)
                            replacement.addAll(storeLocal(operands.get(operand), locals.get(operand)));
                        if (!handler.isStatic) replacement.add(bytes(42));
                        for (int operand = 0; operand < operands.size(); ++operand)
                            replacement.addAll(loadLocal(operands.get(operand), locals.get(operand)));
                        replacement.add(memberInstruction(handler.isStatic ? 184 : 182,
                            target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                        BytecodeInstructions.Instruction skip = editor.newLabel();
                        replacement.add(BytecodeInstructions.Instruction.branch(153, skip));
                        for (int operand = 0; operand < operands.size(); ++operand)
                            replacement.addAll(loadLocal(operands.get(operand), locals.get(operand)));
                        replacement.add(generatedMember(site));
                        if (result != null && !result.voidType) {
                            BytecodeInstructions.Instruction join = editor.newLabel();
                            replacement.add(BytecodeInstructions.Instruction.branch(167, join));
                            replacement.add(skip);
                            replacement.addAll(defaultValue(result));
                            replacement.add(join);
                        } else {
                            replacement.add(skip);
                        }
                        editor.replace(site, replacement);
                    }
                    editor.finish(target.pool);
                    finishControlFlowEdit(target, destination, code, editor, context,
                        "@WrapWithCondition from " + prepared.definition.name);
                    destination.replaceCode(target.pool, code);
                    mark(context, destination, target);
                    matchedSites += sites.size();
                    changed = true;
                }
            }
        }
        validateMatchCount(annotation,
            matchedSites, "@WrapWithCondition " + source.name(prepared.definition.model.pool));
        return changed;
    }

    /** Wrap an operation with the erased MixinExtras Operation interface. */
    private boolean applyWrapOperation(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                       AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        List<AtSpec> atSpecs = readAtSpecs(annotation, "at", prepared, target);
        if (atSpecs.isEmpty()) throw unsupported("@WrapOperation has no @At");
        boolean changed = false;
        int matchedSites = 0;
        for (AtSpec at : atSpecs) {
            if (!at.value.equals("INVOKE") && !at.value.equals("FIELD"))
                throw unsupported("@WrapOperation requires INVOKE or FIELD, got " + at.value);
            for (String methodName : targetMethodTokens(annotation, prepared, target)) {
                for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                    if (!handler.isStatic && (destination.access & ClassFileModel.ACC_STATIC) != 0)
                        throw unsupported("non-static @WrapOperation handler targets a static method: " + handler.name);
                    CodeModel code = destination.code(target.pool);
                    if (code == null) throw unsupported("cannot wrap operation in abstract/native method");
                    BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                    List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                        nestedAnnotations(annotation, "slice"));
                    if (sites.isEmpty()) continue;
                    StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                    for (int i = sites.size() - 1; i >= 0; --i) {
                        BytecodeInstructions.Instruction site = sites.get(i);
                        List<Descriptor.Type> operands = memberOperandTypes(target, site);
                        Descriptor.Type result = memberResultType(target, site);
                        if (handlerDescriptor.arguments.size() != operands.size() + 1
                            || !isOperationType(handlerDescriptor.arguments.get(operands.size())))
                            throw unsupported("@WrapOperation handler must end with Operation and match site: "
                                + handler.name + handler.descriptor);
                        for (int operand = 0; operand < operands.size(); ++operand)
                            if (!compatible(operands.get(operand), handlerDescriptor.arguments.get(operand)))
                                throw unsupported("@WrapOperation operand type mismatch at " + site.oldOffset);
                        if (result == null || !compatible(result, handlerDescriptor.returnType))
                            throw unsupported("@WrapOperation return type mismatch at " + site.oldOffset);
                        requireStackSuffix(analysis.before(site), operands,
                            "@WrapOperation stack shape does not match site at " + site.oldOffset);
                        ArrayList<Integer> locals = allocateLocals(editor, operands);
                        ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                        for (int operand = operands.size() - 1; operand >= 0; --operand)
                            replacement.addAll(storeLocal(operands.get(operand), locals.get(operand)));
                        if (!handler.isStatic) replacement.add(bytes(42));
                        for (int operand = 0; operand < operands.size(); ++operand)
                            replacement.addAll(loadLocal(operands.get(operand), locals.get(operand)));
                        replacement.addAll(makeOriginalOperation(target, site));
                        replacement.add(memberInstruction(handler.isStatic ? 184 : 182,
                            target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                        appendReferenceCast(target.pool, result, handlerDescriptor.returnType, replacement);
                        editor.replace(site, replacement);
                    }
                    editor.finish(target.pool);
                    destination.replaceCode(target.pool, code);
                    mark(context, destination, target);
                    matchedSites += sites.size();
                    changed = true;
                }
            }
        }
        validateMatchCount(annotation,
            matchedSites, "@WrapOperation " + source.name(prepared.definition.model.pool));
        return changed;
    }

    /** Wrap a complete target method and bind the original implementation. */
    private boolean applyWrapMethod(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                    AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        if (handlerDescriptor.arguments.isEmpty()
            || !isOperationType(handlerDescriptor.arguments.get(handlerDescriptor.arguments.size() - 1)))
            throw unsupported("@WrapMethod handler must end with Operation: " + handler.name + handler.descriptor);
        boolean changed = false;
        int matchedMethods = 0;
        for (String methodName : targetMethodTokens(annotation, prepared, target)) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                String destinationName = destination.name(target.pool);
                String destinationDescriptorString = destination.descriptor(target.pool);
                Descriptor.MethodDesc destinationDescriptor = Descriptor.method(destinationDescriptorString);
                int argumentCount = handlerDescriptor.arguments.size() - 1;
                if (argumentCount != destinationDescriptor.arguments.size()) {
                    if (methodName.indexOf('(') < 0) continue;
                    throw unsupported("@WrapMethod argument layout does not match " + destinationName
                        + destinationDescriptorString);
                }
                if (!compatible(destinationDescriptor.returnType, handlerDescriptor.returnType))
                    throw unsupported("@WrapMethod return type mismatch: " + handler.name);
                if (!handler.isStatic && (destination.access & ClassFileModel.ACC_STATIC) != 0)
                    throw unsupported("non-static @WrapMethod handler targets a static method: " + handler.name);
                for (int argument = 0; argument < argumentCount; ++argument) {
                    if (!compatible(destinationDescriptor.arguments.get(argument),
                                    handlerDescriptor.arguments.get(argument)))
                        throw unsupported("@WrapMethod argument type mismatch: " + handler.name);
                }

                String originalName = uniqueWrappedMethodName(target, destinationName,
                    destinationDescriptorString, handler.name);
                int originalAccess = destination.access;
                destination.nameIndex = target.pool.addUtf8(originalName);

                MemberModel wrapper = new MemberModel();
                wrapper.access = originalAccess & ~(ClassFileModel.ACC_ABSTRACT | ClassFileModel.ACC_NATIVE);
                wrapper.nameIndex = target.pool.addUtf8(destinationName);
                wrapper.descriptorIndex = target.pool.addUtf8(destinationDescriptorString);
                CodeModel wrapperCode = new CodeModel();
                wrapperCode.maxLocals = (originalAccess & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
                for (Descriptor.Type argument : destinationDescriptor.arguments)
                    wrapperCode.maxLocals += argument.slots;
                ArrayList<BytecodeInstructions.Instruction> instructions = new ArrayList<>();
                if (!handler.isStatic) instructions.add(bytes(42));
                int slot = (originalAccess & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
                for (Descriptor.Type argument : destinationDescriptor.arguments) {
                    instructions.addAll(loadLocal(argument, slot));
                    slot += argument.slots;
                }
                instructions.addAll(makeBoundOriginalOperation(target, originalName,
                    destinationDescriptorString, originalAccess));
                instructions.add(memberInstruction(handler.isStatic ? 184 : 182,
                    target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                instructions.add(returnInstruction(destinationDescriptor.returnType));
                wrapperCode.maxStack = 32;
                wrapperCode.code = BytecodeInstructions.Editor.assembleGenerated(instructions);
                wrapper.replaceCode(target.pool, wrapperCode);
                target.methods.add(wrapper);
                mark(context, destinationName + destinationDescriptorString, target);
                matchedMethods++;
                changed = true;
            }
        }
        validateMatchCount(annotation, matchedMethods,
            "@WrapMethod " + source.name(prepared.definition.model.pool));
        return changed;
    }

    private String uniqueWrappedMethodName(ClassFileModel target, String name, String descriptor,
                                           String handlerName) {
        String base = "$cppfm$original$" + Integer.toHexString((name + descriptor + handlerName).hashCode());
        String result = base;
        int suffix = 0;
        while (target.method(result, descriptor) != null) result = base + "$" + (++suffix);
        return result;
    }

    private List<BytecodeInstructions.Instruction> makeBoundOriginalOperation(ClassFileModel target,
                                                                               String name,
                                                                               String descriptor,
                                                                               int access) {
        String helper = "cppfm/transform/OriginalOperation";
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        output.add(memberInstruction(187, target.pool.addClass(helper)));
        output.add(bytes(89));
        if ((access & ClassFileModel.ACC_STATIC) != 0) output.add(bytes(1));
        else output.add(bytes(42));
        output.add(ldcString(target.pool, target.internalName()));
        output.add(ldcString(target.pool, name));
        output.add(ldcString(target.pool, descriptor));
        output.add(iconst((access & ClassFileModel.ACC_STATIC) != 0 ? 184 : 182));
        output.add(memberInstruction(183, target.pool.addMethodRef(helper, "<init>",
            "(Ljava/lang/Object;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V", false)));
        return output;
    }

    private List<Descriptor.Type> memberOperandTypes(ClassFileModel target,
                                                      BytecodeInstructions.Instruction instruction) {
        int cp = BytecodeInstructions.cpIndex(instruction);
        if (cp < 0) throw unsupported("member site has no constant-pool reference");
        boolean field = BytecodeInstructions.isField(instruction.opcode);
        boolean staticMember = instruction.opcode == 178 || instruction.opcode == 179 || instruction.opcode == 184;
        ArrayList<Descriptor.Type> output = new ArrayList<>();
        String owner = target.pool.memberOwner(cp);
        if (!staticMember) output.add(ownerType(owner));
        if (field) {
            if (instruction.opcode == 179 || instruction.opcode == 181)
                output.add(Descriptor.type(target.pool.memberDescriptor(cp)));
        } else {
            output.addAll(Descriptor.method(target.pool.memberDescriptor(cp)).arguments);
        }
        return output;
    }

    private Descriptor.Type memberResultType(ClassFileModel target,
                                              BytecodeInstructions.Instruction instruction) {
        int cp = BytecodeInstructions.cpIndex(instruction);
        if (cp < 0) throw unsupported("member site has no constant-pool reference");
        if (BytecodeInstructions.isField(instruction.opcode)) {
            if (instruction.opcode == 179 || instruction.opcode == 181) return Descriptor.type("V");
            return Descriptor.type(target.pool.memberDescriptor(cp));
        }
        return Descriptor.method(target.pool.memberDescriptor(cp)).returnType;
    }

    private static ArrayList<Integer> allocateLocals(BytecodeInstructions.Editor editor,
                                                      List<Descriptor.Type> types) {
        ArrayList<Integer> output = new ArrayList<>();
        for (Descriptor.Type type : types) output.add(editor.allocateLocal(type));
        return output;
    }

    private static BytecodeInstructions.Instruction generatedMember(BytecodeInstructions.Instruction source) {
        BytecodeInstructions.Instruction result = source.copy();
        result.oldOffset = -1;
        result.original = false;
        result.originalOffset = -1;
        result.branchTarget = -1;
        result.branchTargetInstruction = null;
        return result;
    }

    private static boolean isOperationType(Descriptor.Type type) {
        return type != null && descriptorOwner(type.descriptor).equals(
            "com/llamalad7/mixinextras/injector/wrapoperation/Operation");
    }

    private List<BytecodeInstructions.Instruction> makeOriginalOperation(ClassFileModel target,
                                                                          BytecodeInstructions.Instruction site) {
        int cp = BytecodeInstructions.cpIndex(site);
        if (cp < 0) throw unsupported("wrapped member site has no constant-pool reference");
        String owner = target.pool.memberOwner(cp);
        String name = target.pool.memberName(cp);
        String descriptor = target.pool.memberDescriptor(cp);
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        String helper = "cppfm/transform/OriginalOperation";
        output.add(memberInstruction(187, target.pool.addClass(helper)));
        output.add(bytes(89));
        output.add(ldcString(target.pool, owner));
        output.add(ldcString(target.pool, name));
        output.add(ldcString(target.pool, descriptor));
        output.add(iconst(site.opcode));
        output.add(memberInstruction(183, target.pool.addMethodRef(helper, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V", false)));
        return output;
    }

    private static void appendReferenceCast(ConstantPool pool, Descriptor.Type expected,
                                            Descriptor.Type actual,
                                            List<BytecodeInstructions.Instruction> output) {
        if (expected == null || expected.voidType || !expected.reference || actual == null || !actual.reference)
            return;
        if (expected.descriptor.equals("Ljava/lang/Object;") || expected.descriptor.equals(actual.descriptor)) return;
        String cast = expected.array ? expected.descriptor : descriptorOwner(expected.descriptor);
        output.add(memberInstruction(192, pool.addClass(cast)));
    }

    private static List<BytecodeInstructions.Instruction> defaultValue(Descriptor.Type type) {
        if (type.reference) return List.of(bytes(1));
        if (type.descriptor.equals("J")) return List.of(bytes(9));
        if (type.descriptor.equals("F")) return List.of(bytes(11));
        if (type.descriptor.equals("D")) return List.of(bytes(14));
        return List.of(iconst(0));
    }

    private void finishControlFlowEdit(ClassFileModel target, MemberModel destination, CodeModel code,
                                       BytecodeInstructions.Editor editor, TransformContext context,
                                       String origin) {
        try {
            editor.rebuildStackMapFrames(target, destination);
        } catch (TransformException frameFailure) {
            code.stripDebugAndFrames(target.pool);
            context.diagnostic("removed stale StackMapTable after " + origin + " at "
                + destination.name(target.pool) + " in " + target.binaryName() + ": "
                + frameFailure.getMessage());
        }
    }

    private boolean applyModifyArg(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                   AnnotationModel annotation, TransformContext context) {
        Handler handler = prepared.handler(source, target);
        AtSpec at = readAtSpec(nestedAnnotation(annotation, "at"), prepared, target);
        if (!at.value.equals("INVOKE")) throw unsupported("@ModifyArg requires INVOKE");
        Descriptor.MethodDesc modifier = Descriptor.method(handler.descriptor);
        if (modifier.arguments.isEmpty() || modifier.returnType.voidType)
            throw unsupported("@ModifyArg handler must return the modified argument: "
                + handler.name + handler.descriptor);
        for (int parameter = 1; parameter < modifier.arguments.size(); ++parameter) {
            if (!isArgsOnlyLocal(handler, parameter))
                throw unsupported("@ModifyArg only supports extra @Local(argsOnly=true) parameters: "
                    + handler.name + handler.descriptor);
        }
        boolean changed = false;
        int matchedSites = 0;
        for (String methodName : targetMethodTokens(annotation, prepared, target)) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot modify abstract/native method");
                Descriptor.MethodDesc targetDescriptor = Descriptor.method(destination.descriptor(target.pool));
                int[] targetArgumentSlots = methodArgumentSlots(destination, target.pool);
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                    nestedAnnotations(annotation, "slice"));
                if (sites.isEmpty()) continue;
                StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                int index = annotation.integer("index", -1);
                for (int i = sites.size() - 1; i >= 0; --i) {
                    BytecodeInstructions.Instruction site = sites.get(i);
                    int cp = BytecodeInstructions.cpIndex(site);
                    if (cp < 0 || !BytecodeInstructions.isInvoke(site.opcode)) throw unsupported("@ModifyArg site is not INVOKE");
                    String owner = target.pool.memberOwner(cp);
                    boolean staticCall = site.opcode == 184;
                    Descriptor.MethodDesc invocation = Descriptor.method(target.pool.memberDescriptor(cp));
                    ArrayList<Descriptor.Type> stackTypes = new ArrayList<>();
                    if (!staticCall) stackTypes.add(ownerType(owner));
                    stackTypes.addAll(invocation.arguments);
                    int argumentOffset = staticCall ? 0 : 1;
                    int selected = index >= 0 ? index + argumentOffset : selectArgument(invocation.arguments, modifier.arguments.get(0), target.pool, argumentOffset);
                    if (selected < argumentOffset || selected >= stackTypes.size()) throw unsupported("@ModifyArg index out of range");
                    if (!compatible(stackTypes.get(selected), modifier.arguments.get(0))
                        || !compatible(stackTypes.get(selected), modifier.returnType))
                        throw unsupported("@ModifyArg type mismatch");
                    requireStackSuffix(analysis.before(site), stackTypes,
                        "@ModifyArg stack shape does not match " + owner + "." + target.pool.memberName(cp));
                    ArrayList<Integer> locals = new ArrayList<>();
                    for (Descriptor.Type type : stackTypes) locals.add(editor.allocateLocal(type));
                    ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                    for (int j = stackTypes.size() - 1; j >= 0; --j) replacement.addAll(storeLocal(stackTypes.get(j), locals.get(j)));
                    if (!handler.isStatic) replacement.add(bytes(42));
                    replacement.addAll(loadLocal(stackTypes.get(selected), locals.get(selected)));
                    Set<Integer> usedTargetArguments = new HashSet<>();
                    for (int parameter = 1; parameter < modifier.arguments.size(); ++parameter) {
                        Descriptor.Type wanted = modifier.arguments.get(parameter);
                        int matchingArgument = -1;
                        for (int argument = 0; argument < targetDescriptor.arguments.size(); ++argument) {
                            if (usedTargetArguments.contains(argument)) continue;
                            Descriptor.Type actual = targetDescriptor.arguments.get(argument);
                            if (compatible(actual, wanted)) {
                                matchingArgument = argument;
                                break;
                            }
                        }
                        if (matchingArgument < 0)
                            throw unsupported("@ModifyArg @Local argument has no matching target argument: "
                                + handler.name + handler.descriptor);
                        usedTargetArguments.add(matchingArgument);
                        replacement.addAll(loadLocal(targetDescriptor.arguments.get(matchingArgument),
                            targetArgumentSlots[matchingArgument]));
                    }
                    int handlerRef = target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false);
                    replacement.add(memberInstruction(handler.isStatic ? 184 : 182, handlerRef));
                    replacement.addAll(storeLocal(stackTypes.get(selected), locals.get(selected)));
                    for (int j = 0; j < stackTypes.size(); ++j) replacement.addAll(loadLocal(stackTypes.get(j), locals.get(j)));
                    BytecodeInstructions.Instruction original = site.copy();
                    original.oldOffset = -1;
                    replacement.add(original);
                    editor.replace(site, replacement);
                }
                matchedSites += sites.size();
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        validateMatchCount(annotation, matchedSites, "@ModifyArg " + source.name(prepared.definition.model.pool));
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
        int matchedSites = 0;
        for (String methodName : targetMethodTokens(annotation, prepared, target)) {
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
                if (sites.isEmpty()) continue;
                matchedSites += sites.size();
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        validateMatchCount(annotation, matchedSites, "@ModifyConstant " + source.name(prepared.definition.model.pool));
        return changed;
    }

    private boolean applyModifyVariable(ClassFileModel target, PreparedMixin prepared, MemberModel source,
                                        AnnotationModel annotation, TransformContext context,
                                        Map<String, SharedRefBinding> sharedRefs) {
        Handler handler = prepared.handler(source, target);
        Descriptor.MethodDesc modifier = Descriptor.method(handler.descriptor);
        boolean sharedReference = modifier.arguments.size() == 2
            && isSharedRefType(modifier.arguments.get(1));
        if ((!sharedReference && modifier.arguments.isEmpty())
            || modifier.returnType.voidType
            || !compatible(modifier.arguments.get(0), modifier.returnType))
            throw unsupported("@ModifyVariable handler must be (T)T: " + handler.name + handler.descriptor);
        AtSpec at = readAtSpec(nestedAnnotation(annotation, "at"), prepared, target);
        boolean argsOnly = annotation.bool("argsOnly", false);
        if (at.value.equals("HEAD")) {
            if (!argsOnly) throw unsupported("@ModifyVariable HEAD requires argsOnly=true");
            boolean changed = false;
            int matchedSites = 0;
            int explicitIndex = annotation.integer("index", -1);
            for (String methodName : targetMethodTokens(annotation, prepared, target)) {
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
                    if (!handler.isStatic) replacement.add(bytes(42));
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
                    matchedSites++;
                    changed = true;
                }
            }
            validateMatchCount(annotation, matchedSites,
                "@ModifyVariable " + source.name(prepared.definition.model.pool));
            return changed;
        }
        if (sharedReference && at.value.equals("INVOKE"))
            return applySharedModifyVariable(target, prepared, source, handler, modifier, annotation, at, context,
                sharedRefs);
        if (at.value.equals("INVOKE_ASSIGN"))
            return applyInvokeAssignModifyVariable(target, prepared, source, handler, modifier, annotation, at,
                context);
        if (!at.value.equals("LOAD") && !at.value.equals("STORE"))
            throw unsupported("@ModifyVariable requires LOAD or STORE");
        boolean changed = false;
        int matchedSites = 0;
        for (String methodName : targetMethodTokens(annotation, prepared, target)) {
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
                if (sites.isEmpty()) continue;
                matchedSites += sites.size();
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        validateMatchCount(annotation, matchedSites, "@ModifyVariable " + source.name(prepared.definition.model.pool));
        return changed;
    }

    /**
     * Apply a regular Mixin {@code @ModifyVariable} to an
     * {@code @At("INVOKE_ASSIGN")} site.  The injection point is the local
     * store which consumes the value returned by the matched invocation.  A
     * modifier receives that value first and may optionally receive the
     * target method's arguments after it, which is the form used by Fabric's
     * entity sleep events in 1.21.4.
     */
    private boolean applyInvokeAssignModifyVariable(ClassFileModel target, PreparedMixin prepared,
                                                    MemberModel source, Handler handler,
                                                    Descriptor.MethodDesc modifier,
                                                    AnnotationModel annotation, AtSpec at,
                                                    TransformContext context) {
        if (modifier.arguments.size() < 1)
            throw unsupported("@ModifyVariable INVOKE_ASSIGN handler has no variable argument: " + handler.name);
        boolean changed = false;
        int matchedSites = 0;
        for (String methodName : targetMethodTokens(annotation, prepared, target)) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                if (!handler.isStatic && (destination.access & ClassFileModel.ACC_STATIC) != 0)
                    throw unsupported("non-static @ModifyVariable handler targets a static method: " + handler.name);
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot modify a variable in abstract/native method");
                rejectUnsafeConstructorInjection(target, destination, at);
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                LocalVariableTable localTable = LocalVariableTable.read(code, target.pool);
                List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                    nestedAnnotations(annotation, "slice"));
                if (sites.isEmpty()) continue;

                Descriptor.MethodDesc destinationDescriptor = Descriptor.method(destination.descriptor(target.pool));
                int targetArgumentCount = modifier.arguments.size() - 1;
                if (targetArgumentCount > destinationDescriptor.arguments.size())
                    throw unsupported("@ModifyVariable handler has too many target arguments: "
                        + handler.name + handler.descriptor);
                int targetSlot = (destination.access & ClassFileModel.ACC_STATIC) != 0 ? 0 : 1;
                int[] targetSlots = new int[destinationDescriptor.arguments.size()];
                for (int argument = 0; argument < destinationDescriptor.arguments.size(); ++argument) {
                    targetSlots[argument] = targetSlot;
                    targetSlot += destinationDescriptor.arguments.get(argument).slots;
                }
                for (int argument = 0; argument < targetArgumentCount; ++argument) {
                    Descriptor.Type expected = modifier.arguments.get(argument + 1);
                    Descriptor.Type actual = destinationDescriptor.arguments.get(argument);
                    if (!compatible(expected, actual))
                        throw unsupported("@ModifyVariable target argument type mismatch at " + argument + ": "
                            + handler.name + handler.descriptor);
                }

                for (int index = sites.size() - 1; index >= 0; --index) {
                    BytecodeInstructions.Instruction site = sites.get(index);
                    Descriptor.Type type = variableType(target.pool, site, "STORE", analysis, localTable);
                    if (type == null || !compatible(type, modifier.arguments.get(0))
                        || !compatible(type, modifier.returnType))
                        throw unsupported("@ModifyVariable INVOKE_ASSIGN type mismatch at " + site.oldOffset);

                    int temporary = editor.allocateLocal(type);
                    ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                    // The invocation result is on the operand stack immediately
                    // before the original store.  Spill it while preparing the
                    // receiver and the optional target-method arguments.
                    replacement.addAll(storeLocal(type, temporary));
                    if (!handler.isStatic) replacement.add(bytes(42));
                    replacement.addAll(loadLocal(type, temporary));
                    for (int argument = 0; argument < targetArgumentCount; ++argument)
                        replacement.addAll(loadLocal(destinationDescriptor.arguments.get(argument), targetSlots[argument]));
                    replacement.add(memberInstruction(handler.isStatic ? 184 : 182,
                        target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                    BytecodeInstructions.Instruction original = site.copy();
                    original.oldOffset = -1;
                    replacement.add(original);
                    editor.replace(site, replacement);
                }
                matchedSites += sites.size();
                editor.finish(target.pool);
                finishControlFlowEdit(target, destination, code, editor, context,
                    "@ModifyVariable INVOKE_ASSIGN from " + source.name(prepared.definition.model.pool));
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        validateMatchCount(annotation, matchedSites, "@ModifyVariable " + source.name(prepared.definition.model.pool));
        return changed;
    }

    /**
     * MixinExtras extends ModifyVariable with an optional {@code @Share}
     * reference parameter.  At an INVOKE point the call operands are already
     * on the operand stack, so the implementation spills that complete stack,
     * updates the selected live local, substitutes its value in the matching
     * invocation operand, and restores the remaining operands in order.
     */
    private boolean applySharedModifyVariable(ClassFileModel target, PreparedMixin prepared,
                                              MemberModel source, Handler handler,
                                              Descriptor.MethodDesc modifier,
                                              AnnotationModel annotation, AtSpec at,
                                              TransformContext context,
                                              Map<String, SharedRefBinding> sharedRefs) {
        int sharedParameter = -1;
        for (int index = 0; index < modifier.arguments.size(); ++index) {
            if (!isSharedRefType(modifier.arguments.get(index))) continue;
            if (sharedParameter >= 0)
                throw unsupported("@ModifyVariable supports one @Share parameter: " + handler.name);
            sharedParameter = index;
        }
        if (sharedParameter < 0)
            throw unsupported("@ModifyVariable shared handler has no reference parameter: " + handler.name);
        boolean changed = false;
        int matchedSites = 0;
        for (String methodName : targetMethodTokens(annotation, prepared, target)) {
            for (MemberModel destination : selectMethods(target, methodName, handler.descriptor, false)) {
                CodeModel code = destination.code(target.pool);
                if (code == null) throw unsupported("cannot modify variable in abstract/native method");
                rejectUnsafeConstructorInjection(target, destination, at);
                BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, target.pool);
                StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(target, destination, code);
                List<BytecodeInstructions.Instruction> sites = findSites(editor.instructions, target.pool, at,
                    nestedAnnotations(annotation, "slice"));
                if (sites.isEmpty()) continue;
                String shareKey = shareKey(handler, sharedParameter);
                Descriptor.Type sharedType = modifier.arguments.get(sharedParameter);
                ensureSharedRefs(target, destination, handler, editor, sharedRefs);
                SharedRefBinding sharedBinding = sharedRefs.get(sharedBindingKey(destination, target.pool, shareKey));
                if (sharedBinding == null || !sharedType.descriptor.equals(sharedBinding.type().descriptor))
                    throw unsupported("shared ref type mismatch: " + handler.name);
                int sharedLocal = sharedBinding.local();

                for (int i = sites.size() - 1; i >= 0; --i) {
                    BytecodeInstructions.Instruction site = sites.get(i);
                    List<StackAnalyzer.Value> before = analysis.before(site);
                    ArrayList<Descriptor.Type> stackTypes = new ArrayList<>();
                    for (StackAnalyzer.Value value : before) stackTypes.add(stackType(value));
                    int cp = BytecodeInstructions.cpIndex(site);
                    if (cp < 0 || !BytecodeInstructions.isInvoke(site.opcode))
                        throw unsupported("@ModifyVariable INVOKE site is not an invocation");
                    ArrayList<Descriptor.Type> invocationTypes = new ArrayList<>();
                    if (site.opcode != 184) invocationTypes.add(ownerType(target.pool.memberOwner(cp)));
                    invocationTypes.addAll(Descriptor.method(target.pool.memberDescriptor(cp)).arguments);
                    if (stackTypes.size() < invocationTypes.size())
                        throw unsupported("@ModifyVariable INVOKE stack is too shallow at " + site.oldOffset);
                    int invocationOffset = stackTypes.size() - invocationTypes.size();
                    int operand = -1;
                    int compatibleOperand = -1;
                    for (int position = 0; position < invocationTypes.size(); ++position) {
                        if (modifier.arguments.get(0).descriptor.equals(invocationTypes.get(position).descriptor)) {
                            if (operand >= 0)
                                throw unsupported("@ModifyVariable INVOKE variable operand is ambiguous at " + site.oldOffset);
                            operand = invocationOffset + position;
                        } else if (compatibleOperand < 0
                                   && compatible(modifier.arguments.get(0), invocationTypes.get(position))) {
                            compatibleOperand = invocationOffset + position;
                        }
                    }
                    if (operand < 0) operand = compatibleOperand;
                    if (operand < 0)
                        throw unsupported("@ModifyVariable INVOKE has no matching operand at " + site.oldOffset);
                    int variable = findLiveLocal(analysis.localsBefore(site), modifier.arguments.get(0),
                        argumentSlots(destination, target.pool));
                    if (variable < 0)
                        throw unsupported("@ModifyVariable cannot find a live local for " + handler.name);

                    ArrayList<Integer> stackLocals = new ArrayList<>();
                    for (Descriptor.Type type : stackTypes) stackLocals.add(editor.allocateLocal(type));
                    ArrayList<BytecodeInstructions.Instruction> replacement = new ArrayList<>();
                    for (int position = stackTypes.size() - 1; position >= 0; --position)
                        replacement.addAll(storeLocal(stackTypes.get(position), stackLocals.get(position)));
                    if (!handler.isStatic) replacement.add(bytes(42));
                    replacement.addAll(loadLocal(modifier.arguments.get(0), variable));
                    replacement.addAll(loadLocal(sharedType, sharedLocal));
                    replacement.add(memberInstruction(handler.isStatic ? 184 : 182,
                        target.pool.addMethodRef(target.internalName(), handler.name, handler.descriptor, false)));
                    replacement.addAll(storeLocal(modifier.arguments.get(0), variable));
                    for (int position = 0; position < stackTypes.size(); ++position) {
                        if (position == operand) replacement.addAll(loadLocal(modifier.arguments.get(0), variable));
                        else replacement.addAll(loadLocal(stackTypes.get(position), stackLocals.get(position)));
                    }
                    editor.insertBefore(site, replacement);
                    matchedSites++;
                }
                editor.finish(target.pool);
                destination.replaceCode(target.pool, code);
                mark(context, destination, target);
                changed = true;
            }
        }
        validateMatchCount(annotation, matchedSites, "@ModifyVariable " + source.name(prepared.definition.model.pool));
        return changed;
    }

    private int findLiveLocal(Map<Integer, StackAnalyzer.Value> locals, Descriptor.Type wanted,
                              Set<Integer> argumentSlots) {
        ArrayList<Integer> slots = new ArrayList<>(locals.keySet());
        Collections.sort(slots);
        for (int slot : slots) {
            if (argumentSlots.contains(slot)) continue;
            StackAnalyzer.Value value = locals.get(slot);
            if (value == null || value.isUninitialized()) continue;
            if (compatible(wanted, stackType(value))) return slot;
        }
        return -1;
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
        if (output.isEmpty())
            throw unsupported("target method not found in " + target.internalName() + ": " + token);
        return output;
    }

    private List<String> targetMethodTokens(AnnotationModel annotation, PreparedMixin prepared,
                                            ClassFileModel target) {
        ArrayList<String> output = new ArrayList<>();
        for (String source : annotation.strings("method")) {
            String mapped = prepared.methodToken(source, target.internalName(), resolver);
            if (mapped != null && !mapped.isEmpty() && !output.contains(mapped)) output.add(mapped);
        }
        return output;
    }

    private AtSpec readAtSpec(AnnotationModel annotation, PreparedMixin prepared,
                              ClassFileModel target) {
        AtSpec parsed = AtSpec.read(annotation);
        if (parsed.target.isEmpty()) return parsed;
        AtSpec mapped = parsed.withTarget(prepared.atTarget(parsed.target, target.internalName(), resolver));
        return mapAtTargetNamespace(mapped);
    }

    /**
     * Mixin target strings are normally remapped by a refmap.  Intermediary
     * mixins, however, intentionally omit a refmap and rely on the launcher's
     * intermediary-to-named edge.  Normalize both forms here so FIELD/INVOKE
     * matching compares the same namespace as the target class pool.
     */
    private AtSpec mapAtTargetNamespace(AtSpec spec) {
        if (spec.owner.isEmpty()) return spec;
        String owner = resolver.resolveOwner(spec.owner);
        String descriptor = spec.descriptor.isEmpty() ? "" : resolver.resolveDescriptor(spec.descriptor);
        String member = spec.member;
        if (!member.isEmpty()) {
            String sourceDescriptor = spec.descriptor.isEmpty() ? "" : spec.descriptor;
            member = spec.value.equals("FIELD")
                ? resolver.resolveField(spec.owner, member, sourceDescriptor)
                : resolver.resolveMethod(spec.owner, member, sourceDescriptor);
        }
        String target;
        if (member.isEmpty()) target = "L" + owner + ";";
        else if (spec.value.equals("FIELD")) target = "L" + owner + ";" + member
            + (descriptor.isEmpty() ? "" : ":" + descriptor);
        else target = "L" + owner + ";" + member + descriptor;
        return spec.withTarget(target);
    }

    private List<BytecodeInstructions.Instruction> findSites(List<BytecodeInstructions.Instruction> instructions,
                                                              ConstantPool pool, AtSpec at,
                                                              List<AnnotationModel> slices) {
        if (at.value.equals("INVOKE_ASSIGN"))
            return findInvokeAssignSites(instructions, pool, at, slices);
        int[] range = sliceBounds(instructions, pool, at, slices);
        ArrayList<BytecodeInstructions.Instruction> candidates = new ArrayList<>();
        for (int index = 0; index < instructions.size(); ++index) {
            BytecodeInstructions.Instruction instruction = instructions.get(index);
            if (!isOriginalInstruction(instruction) || index < range[0] || index > range[1]) continue;
            boolean match = switch (at.value) {
                case "HEAD" -> instruction == firstInstruction(instructions);
                case "TAIL" -> instruction == lastReturn(instructions);
                case "RETURN" -> BytecodeInstructions.isReturn(instruction.opcode);
                case "INVOKE" -> memberMatches(instruction, pool, at, false);
                case "FIELD" -> memberMatches(instruction, pool, at, true);
                case "NEW" -> instruction.opcode == 187 && classMatches(instruction, pool, at);
                case "JUMP" -> isJump(instruction) && (at.opcode < 0 || instruction.opcode == at.opcode);
                case "CONSTANT" -> constantMatches(instruction, pool, at);
                case "LOAD", "STORE" -> variableType(instruction) != null && isLoadOrStore(instruction, at.value);
                default -> false;
            };
            if (match) candidates.add(instruction);
        }
        if (at.ordinal >= 0) {
            if (at.ordinal >= candidates.size()) return List.of();
            candidates = new ArrayList<>(List.of(candidates.get(at.ordinal)));
        }
        return shiftSites(instructions, candidates, at);
    }

    /**
     * {@code INVOKE_ASSIGN} is an after-invoke injection point.  Mixin places
     * the anchor at the first local store which consumes the invocation
     * result, so a callback inserted before that anchor observes the result
     * on the operand stack while one shifted AFTER it observes the assigned
     * local.  Returning the store rather than the invoke is important: using
     * the invoke itself silently changes the callback to a before-invoke
     * injection.
     */
    private List<BytecodeInstructions.Instruction> findInvokeAssignSites(
            List<BytecodeInstructions.Instruction> instructions, ConstantPool pool,
            AtSpec at, List<AnnotationModel> slices) {
        int[] range = sliceBounds(instructions, pool, at, slices);
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        int seen = 0;
        for (int index = range[0]; index <= range[1]; ++index) {
            BytecodeInstructions.Instruction invoke = instructions.get(index);
            if (!isOriginalInstruction(invoke) || !BytecodeInstructions.isInvoke(invoke.opcode)
                || !memberMatches(invoke, pool, at, false)) continue;
            int cp = BytecodeInstructions.cpIndex(invoke);
            if (cp < 0) continue;
            Descriptor.MethodDesc descriptor = Descriptor.method(pool.memberDescriptor(cp));
            if (descriptor.returnType.voidType) continue;
            BytecodeInstructions.Instruction assignment = findInvokeAssignment(
                instructions, index, descriptor.returnType);
            if (assignment == null) continue;
            if (at.ordinal >= 0 && seen != at.ordinal) {
                seen++;
                continue;
            }
            seen++;
            output.add(assignment);
        }
        return shiftSites(instructions, output, at);
    }

    /**
     * Apply Mixin's explicit instruction-count shift after the injection point
     * has resolved its ordinal.  The shift is deliberately performed against
     * the original instruction list: generated instructions are not present
     * while a mixin's injection points are being resolved, and allowing them
     * here would make the result dependent on mixin ordering.
     */
    private List<BytecodeInstructions.Instruction> shiftSites(
            List<BytecodeInstructions.Instruction> instructions,
            List<BytecodeInstructions.Instruction> sites, AtSpec at) {
        if (at.shift != Shift.BY || at.by == 0 || sites.isEmpty()) return sites;
        ArrayList<BytecodeInstructions.Instruction> shifted = new ArrayList<>(sites.size());
        for (BytecodeInstructions.Instruction site : sites) {
            int index = instructions.indexOf(site);
            if (index < 0) throw unsupported("@At(shift=BY) anchor disappeared");
            int shiftedIndex = index + at.by;
            if (shiftedIndex < 0 || shiftedIndex >= instructions.size()) {
                throw unsupported("@At(shift=BY) moves outside the target method: " + at.by);
            }
            BytecodeInstructions.Instruction destination = instructions.get(shiftedIndex);
            if (!isOriginalInstruction(destination)) {
                throw unsupported("@At(shift=BY) resolved to a generated instruction");
            }
            shifted.add(destination);
        }
        return shifted;
    }

    private BytecodeInstructions.Instruction findInvokeAssignment(
            List<BytecodeInstructions.Instruction> instructions, int invokeIndex,
            Descriptor.Type returnType) {
        // The official AfterInvoke point permits a small set of stack-neutral
        // instructions (most notably CHECKCAST) between the call and the
        // store.  Keep the accepted window deliberately narrow so a result
        // which is consumed by another operation is never mistaken for an
        // assignment.  Unknown topology is fail-closed and simply yields no
        // injection site.
        int limit = Math.min(instructions.size(), invokeIndex + 9);
        for (int index = invokeIndex + 1; index < limit; ++index) {
            BytecodeInstructions.Instruction instruction = instructions.get(index);
            if (instruction.label || !isOriginalInstruction(instruction)) continue;
            if (isLoadOrStore(instruction, "STORE")) {
                Descriptor.Type actual = variableType(instruction);
                return actual != null && compatible(returnType, actual) ? instruction : null;
            }
            if (instruction.opcode == 0 || instruction.opcode == 192) continue;
            return null;
        }
        return null;
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

    private int[] methodArgumentSlots(MemberModel method, ConstantPool pool) {
        List<Descriptor.Type> arguments = Descriptor.method(method.descriptor(pool)).arguments;
        int[] slots = new int[arguments.size()];
        int slot = (method.access & ClassFileModel.ACC_STATIC) == 0 ? 1 : 0;
        for (int index = 0; index < arguments.size(); ++index) {
            slots[index] = slot;
            slot += arguments.get(index).slots;
        }
        return slots;
    }

    /**
     * Resolve {@code @Local} parameters for MixinExtras value modifiers.
     *
     * <p>Unlike a callback injection, a return/expression modifier has no
     * callback-local analysis list.  Method arguments are nevertheless live
     * JVM locals and are the most common (and deterministic) form of
     * {@code @Local} context used by Fabric's modifiers.  Resolve those
     * arguments explicitly, honoring the same type/index/ordinal/name
     * selectors as the general local-capture path.</p>
     */
    private List<CapturedLocal> targetArgumentLocals(ClassFileModel target, MemberModel destination,
                                                     Handler handler) {
        Descriptor.MethodDesc destinationDescriptor = Descriptor.method(destination.descriptor(target.pool));
        int[] slots = methodArgumentSlots(destination, target.pool);
        LocalVariableTable table = LocalVariableTable.read(destination.code(target.pool), target.pool);
        ArrayList<CapturedLocal> output = new ArrayList<>();
        Set<Integer> used = new HashSet<>();
        for (int parameter = 0; parameter < handler.parameterAnnotations.size(); ++parameter) {
            if (!isLocalParameter(handler, parameter)) continue;
            Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
            Descriptor.Type expected = handlerDescriptor.arguments.get(parameter);
            AnnotationModel localSpec = parameterAnnotation(handler, parameter, "Local");
            int requestedIndex = localSpec == null ? -1 : localSpec.integer("index", -1);
            int requestedOrdinal = localSpec == null ? -1 : localSpec.integer("ordinal", -1);
            String requestedName = localSpec == null ? "" : localSpec.string("name", "");
            String requestedType = localSpec == null ? "" : localSpec.string("type", "");
            boolean argsOnly = isArgsOnlyLocal(handler, parameter);
            ArrayList<CapturedLocal> matching = new ArrayList<>();
            for (int argument = 0; argument < destinationDescriptor.arguments.size(); ++argument) {
                int slot = slots[argument];
                if (used.contains(slot) || (requestedIndex >= 0 && requestedIndex != slot)) continue;
                Descriptor.Type actual = destinationDescriptor.arguments.get(argument);
                if (!compatible(expected, actual)) continue;
                LocalVariableTable.Entry entry = table.at(0, slot);
                String name = entry == null ? "" : entry.name();
                if (!requestedName.isEmpty() && !requestedName.equals(name)) continue;
                if (!requestedType.isEmpty() && !requestedType.equals("java.lang.Object")
                    && !localTypeMatches(requestedType, actual)) continue;
                matching.add(new CapturedLocal(actual, slot, name, true));
            }
            CapturedLocal selected = null;
            if (requestedOrdinal >= 0) {
                if (requestedOrdinal < matching.size()) selected = matching.get(requestedOrdinal);
            } else if (!matching.isEmpty()) {
                selected = matching.get(0);
            }
            if (selected == null)
                throw unsupported("@Local does not match a live target argument: "
                    + handler.name + handler.descriptor + " parameter " + parameter
                    + (argsOnly ? " (argsOnly)" : ""));
            output.add(selected);
            used.add(selected.slot());
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
        for (int index = 0; index < descriptor.arguments.size(); ++index)
            if (isCallbackInfoType(descriptor.arguments.get(index))) return index;
        return -1;
    }

    private static boolean isCallbackInfoType(Descriptor.Type type) {
        return type != null && descriptorOwner(type.descriptor)
            .startsWith("org/spongepowered/asm/mixin/injection/callback/");
    }

    private static boolean isSharedRefType(Descriptor.Type type) {
        if (type == null) return false;
        String owner = descriptorOwner(type.descriptor);
        return owner.startsWith("com/llamalad7/mixinextras/sugar/ref/")
            && owner.endsWith("Ref");
    }

    private static boolean isLocalParameter(Handler handler, int parameterIndex) {
        if (parameterIndex < 0 || parameterIndex >= handler.parameterAnnotations.size()) return false;
        for (AnnotationModel annotation : handler.parameterAnnotations.get(parameterIndex))
            if (annotation.simpleName().equals("Local")) return true;
        return false;
    }

    private static boolean isArgsOnlyLocal(Handler handler, int parameterIndex) {
        if (parameterIndex < 0 || parameterIndex >= handler.parameterAnnotations.size()) return false;
        for (AnnotationModel annotation : handler.parameterAnnotations.get(parameterIndex))
            if (annotation.simpleName().equals("Local")) return annotation.bool("argsOnly", false);
        return false;
    }

    private String shareKey(Handler handler, int parameterIndex) {
        if (parameterIndex >= 0 && parameterIndex < handler.parameterAnnotations.size()) {
            for (AnnotationModel annotation : handler.parameterAnnotations.get(parameterIndex)) {
                if (!annotation.simpleName().equals("Share")) continue;
                String explicit = annotation.string("value", "");
                if (!explicit.isEmpty()) return explicit;
            }
        }
        if (parameterIndex < handler.parameterNames.size()) {
            String name = handler.parameterNames.get(parameterIndex);
            if (name != null && !name.isEmpty()) return name;
        }
        return "$parameter" + parameterIndex;
    }

    private String sharedBindingKey(MemberModel destination, ConstantPool pool, String shareKey) {
        return destination.name(pool) + destination.descriptor(pool) + "\u0000" + shareKey;
    }

    /** Ensure one @Share object exists for this target method and key. */
    private void ensureSharedRefs(ClassFileModel target, MemberModel destination, Handler handler,
                                  BytecodeInstructions.Editor editor,
                                  Map<String, SharedRefBinding> sharedRefs) {
        Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
        for (int index = 0; index < handlerDescriptor.arguments.size(); ++index) {
            Descriptor.Type type = handlerDescriptor.arguments.get(index);
            if (!isSharedRefType(type)) continue;
            String key = sharedBindingKey(destination, target.pool, shareKey(handler, index));
            SharedRefBinding existing = sharedRefs.get(key);
            if (existing != null) {
                if (!existing.type().descriptor.equals(type.descriptor))
                    throw unsupported("shared ref type mismatch: " + handler.name);
                continue;
            }
            BytecodeInstructions.Instruction first = firstInstruction(editor.instructions);
            if (first == null) throw unsupported("cannot create a shared ref in an empty method: "
                + destination.name(target.pool));
            int local = editor.allocateLocal(type);
            editor.insertBefore(first, makeSharedRef(target, type, local));
            sharedRefs.put(key, new SharedRefBinding(type, local));
        }
    }

    /** Create the concrete MixinExtras reference object for one target call. */
    private List<BytecodeInstructions.Instruction> makeSharedRef(ClassFileModel target,
                                                                  Descriptor.Type interfaceType,
                                                                  int local) {
        String interfaceOwner = descriptorOwner(interfaceType.descriptor);
        String helperOwner;
        String constructor;
        BytecodeInstructions.Instruction initial;
        if (interfaceOwner.endsWith("LocalBooleanRef")) {
            helperOwner = "com/llamalad7/mixinextras/sugar/ref/SimpleLocalBooleanRef";
            constructor = "(Z)V";
            initial = iconst(0);
        } else if (interfaceOwner.endsWith("LocalIntRef")) {
            helperOwner = "com/llamalad7/mixinextras/sugar/ref/SimpleLocalIntRef";
            constructor = "(I)V";
            initial = iconst(0);
        } else if (interfaceOwner.endsWith("LocalLongRef")) {
            helperOwner = "com/llamalad7/mixinextras/sugar/ref/SimpleLocalLongRef";
            constructor = "(J)V";
            initial = bytes(9); // LCONST_0
        } else if (interfaceOwner.endsWith("LocalFloatRef")) {
            helperOwner = "com/llamalad7/mixinextras/sugar/ref/SimpleLocalFloatRef";
            constructor = "(F)V";
            initial = bytes(11); // FCONST_0
        } else if (interfaceOwner.endsWith("LocalDoubleRef")) {
            helperOwner = "com/llamalad7/mixinextras/sugar/ref/SimpleLocalDoubleRef";
            constructor = "(D)V";
            initial = bytes(14); // DCONST_0
        } else if (interfaceOwner.endsWith("LocalRef")) {
            helperOwner = "com/llamalad7/mixinextras/sugar/ref/SimpleLocalRef";
            constructor = "(Ljava/lang/Object;)V";
            initial = bytes(1); // ACONST_NULL
        } else {
            throw unsupported("unsupported MixinExtras shared reference: " + interfaceType.descriptor);
        }
        ArrayList<BytecodeInstructions.Instruction> output = new ArrayList<>();
        output.add(memberInstruction(187, target.pool.addClass(helperOwner)));
        output.add(bytes(89)); // DUP
        output.add(initial);
        output.add(memberInstruction(183, target.pool.addMethodRef(helperOwner, "<init>", constructor, false)));
        output.addAll(storeLocal(interfaceType, local));
        return output;
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

    private List<AtSpec> readAtSpecs(AnnotationModel parent, String name,
                                     PreparedMixin prepared, ClassFileModel target) {
        ArrayList<AtSpec> output = new ArrayList<>();
        for (AnnotationModel annotation : nestedAnnotations(parent, name))
            output.add(readAtSpec(annotation, prepared, target));
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

    /**
     * Constructor callbacks are legal once the mandatory super/this call has
     * completed.  The old blanket rejection also discarded real Mixin points
     * such as WorldChunk's post-allocation INVOKE_ASSIGN.  Validate the
     * selected anchors against that JVM initialization boundary instead.
     */
    private void validateConstructorInjectionSites(ClassFileModel target, MemberModel destination,
                                                   AtSpec at,
                                                   List<BytecodeInstructions.Instruction> instructions,
                                                   List<BytecodeInstructions.Instruction> sites) {
        if (!destination.name(target.pool).equals("<init>")
            || at.value.equals("RETURN") || at.value.equals("TAIL")) return;
        int initialization = -1;
        for (int index = 0; index < instructions.size(); ++index) {
            BytecodeInstructions.Instruction instruction = instructions.get(index);
            if (instruction.opcode != 183) continue;
            int cp = BytecodeInstructions.cpIndex(instruction);
            if (cp < 0 || !target.pool.memberName(cp).equals("<init>")) continue;
            String owner = target.pool.memberOwner(cp);
            if (owner.equals(target.internalName())
                || owner.equals(target.pool.className(target.superClass))) {
                initialization = index;
                break;
            }
        }
        if (initialization < 0)
            throw unsupported("constructor has no super/this initialization boundary: @" + at.value);
        for (BytecodeInstructions.Instruction site : sites) {
            if (instructions.indexOf(site) <= initialization)
                throw unsupported("constructor injection crosses uninitialized this: @" + at.value);
        }
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
        ArrayList<Integer> targetParameters = new ArrayList<>();
        ArrayList<Integer> explicitLocalParameters = new ArrayList<>();
        for (int index = 0; index < handlerDescriptor.arguments.size(); ++index) {
            Descriptor.Type argument = handlerDescriptor.arguments.get(index);
            if (index == callback || isSharedRefType(argument)) continue;
            if (isLocalParameter(handler, index)) explicitLocalParameters.add(index);
            else targetParameters.add(index);
        }
        ArrayList<Integer> captureParameters = new ArrayList<>(explicitLocalParameters);
        for (int i = targetDescriptor.arguments.size(); i < targetParameters.size(); ++i)
            captureParameters.add(targetParameters.get(i));
        if (captureParameters.isEmpty()) return List.of();
        if (mode == LocalCaptureMode.NO_CAPTURE && explicitLocalParameters.isEmpty())
            throw unsupported("handler requests local capture without locals=LocalCapture mode: " + handler.name);

        Map<Integer, StackAnalyzer.Value> state = after
            ? analysis.localsAfter(site) : analysis.localsBefore(site);
        ArrayList<Integer> slots = new ArrayList<>(state.keySet());
        Collections.sort(slots);
        int argumentStart = (destination.access & ClassFileModel.ACC_STATIC) == 0 ? 1 : 0;
        int argumentEnd = argumentStart;
        for (Descriptor.Type argument : targetDescriptor.arguments) argumentEnd += argument.slots;
        boolean includeArguments = !explicitLocalParameters.isEmpty();
        ArrayList<CapturedLocal> available = new ArrayList<>();
        for (int slot : slots) {
            if (slot < argumentStart) continue;
            boolean argument = slot < argumentEnd;
            if (argument && !includeArguments) continue;
            StackAnalyzer.Value value = state.get(slot);
            if (value == null) continue;
            if (value.isUninitialized()) throw unsupported("local capture crosses an uninitialized value at " + site.oldOffset);
            Descriptor.Type type = stackType(value);
            String localName = "";
            if (localTable != null) {
                LocalVariableTable.Entry entry = localTable.at(site.oldOffset, slot);
                if (entry != null) {
                    localName = entry.name();
                    Descriptor.Type debugType = Descriptor.type(entry.descriptor());
                    if (type.reference && debugType.reference) type = debugType;
                    else if (!compatible(type, debugType))
                        throw unsupported("LVT type disagrees with verifier at local " + slot);
                }
            }
            available.add(new CapturedLocal(type, slot, localName, argument));
        }
        if (captureParameters.size() > available.size())
            throw unsupported("local capture requested " + captureParameters.size()
                + " value(s), only " + available.size() + " live in "
                + target.internalName() + "." + destination.name(target.pool)
                + destination.descriptor(target.pool));
        ArrayList<CapturedLocal> output = new ArrayList<>();
        for (int i = 0; i < captureParameters.size(); ++i) {
            int parameterIndex = captureParameters.get(i);
            Descriptor.Type expected = handlerDescriptor.arguments.get(parameterIndex);
            String parameterName = parameterIndex < handler.parameterNames.size()
                ? handler.parameterNames.get(parameterIndex) : "";
            CapturedLocal captured = null;
            boolean explicitLocal = isLocalParameter(handler, parameterIndex);
            boolean argsOnly = isArgsOnlyLocal(handler, parameterIndex);
            if (!parameterName.isEmpty()) {
                for (CapturedLocal candidate : available) {
                    if (!explicitLocal && candidate.argument) continue;
                    if (argsOnly && !candidate.argument) continue;
                    if (parameterName.equals(candidate.name) && compatible(expected, candidate.type)) {
                        captured = candidate;
                        break;
                    }
                }
            }
            if (captured == null) {
                AnnotationModel localSpec = parameterAnnotation(handler, parameterIndex, "Local");
                int requestedIndex = localSpec == null ? -1 : localSpec.integer("index", -1);
                int requestedOrdinal = localSpec == null ? -1 : localSpec.integer("ordinal", -1);
                String requestedName = localSpec == null ? "" : localSpec.string("name", "");
                String requestedType = localSpec == null ? "" : localSpec.string("type", "");
                ArrayList<CapturedLocal> matching = new ArrayList<>();
                for (CapturedLocal candidate : available) {
                    if (!explicitLocal && candidate.argument) continue;
                    if (argsOnly && !candidate.argument) continue;
                    if (requestedIndex >= 0 && candidate.slot != requestedIndex) continue;
                    if (!requestedName.isEmpty() && !requestedName.equals(candidate.name)) continue;
                    if (!requestedType.isEmpty() && !localTypeMatches(requestedType, candidate.type)) continue;
                    if (compatible(expected, candidate.type)) matching.add(candidate);
                }
                if (requestedOrdinal >= 0 && requestedOrdinal < matching.size())
                    captured = matching.get(requestedOrdinal);
                else if (requestedOrdinal < 0 && !matching.isEmpty()) captured = matching.get(0);
            }
            if (captured == null) {
                StringBuilder live = new StringBuilder();
                for (Map.Entry<Integer, StackAnalyzer.Value> entry : state.entrySet()) {
                    if (live.length() != 0) live.append(", ");
                    live.append(entry.getKey()).append(':').append(entry.getValue().descriptor);
                }
                throw unsupported("no captured local matches handler argument " + i
                    + " (expected " + expected.descriptor + ", site=" + site.oldOffset
                    + ", locals=" + live + ')');
            }
            output.add(captured);
            available.remove(captured);
        }
        return output;
    }

    private static AnnotationModel parameterAnnotation(Handler handler, int parameterIndex, String simpleName) {
        if (parameterIndex < 0 || parameterIndex >= handler.parameterAnnotations.size()) return null;
        for (AnnotationModel annotation : handler.parameterAnnotations.get(parameterIndex))
            if (annotation.simpleName().equals(simpleName)) return annotation;
        return null;
    }

    private static boolean localTypeMatches(String requested, Descriptor.Type actual) {
        if (requested.equals(actual.descriptor)) return true;
        if (requested.startsWith("L") && requested.endsWith(";"))
            return requested.substring(1, requested.length() - 1).equals(descriptorOwner(actual.descriptor));
        if (requested.startsWith("[")) return requested.equals(actual.descriptor);
        return requested.replace('.', '/').equals(descriptorOwner(actual.descriptor));
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

    private boolean addMixinInterfaces(ClassFileModel target, MixinDefinition definition) {
        if ((definition.model.access & ClassFileModel.ACC_INTERFACE) != 0)
            return false;
        boolean changed = false;
        for (int interfaceIndex : definition.model.interfaces) {
            String interfaceName = definition.model.pool.className(interfaceIndex);
            int targetIndex = target.pool.addClass(interfaceName);
            if (!target.interfaces.contains(targetIndex)) {
                target.interfaces.add(targetIndex);
                changed = true;
            }
        }
        return changed;
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

    private static String simpleMixinName(String binaryName) {
        if (binaryName == null || binaryName.isEmpty()) return "<unknown>";
        int separator = Math.max(binaryName.lastIndexOf('/'), binaryName.lastIndexOf('.'));
        return separator < 0 ? binaryName : binaryName.substring(separator + 1);
    }

    private static String configName(MixinDefinition definition) {
        if (definition.configName != null && !definition.configName.isEmpty()) return definition.configName;
        return "<direct-registration>.mixins.json";
    }

    /**
     * A missing class in an optional config is a bounded capability gap, not
     * evidence that an unrelated target should be transformed incorrectly.
     * Required configs retain the normal fatal/strict behavior; optional
     * entries are recorded and skipped even when the transformer is strict.
     */
    private void reportConfigurationResource(String message, boolean required) {
        if (required) report(message, true);
        else diagnostics.add(message);
    }

    /** Enforce the hard injector bounds; Mixin's {@code expect} is advisory. */
    private void validateMatchCount(AnnotationModel annotation, int matched, String operation) {
        int required = annotation.integer("require", -1);
        if (required >= 0 && matched < required)
            throw unsupported(operation + " matched " + matched + " site(s), require=" + required);
        int allowed = annotation.integer("allow", -1);
        if (allowed >= 0 && matched > allowed)
            throw unsupported(operation + " matched " + matched + " site(s), allow=" + allowed);
    }

    /**
     * Convert a member descriptor from the mixin namespace into the target
     * class namespace.  ConstantPool relocation only changes the mixin owner;
     * the resolver supplies the intermediary-to-named mapping for Minecraft
     * types used by Accessor, Invoker, and Overwrite declarations.
     */
    private String targetDescriptor(String descriptor, String sourceOwner, String targetOwner) {
        String relocated = ConstantPool.remapDescriptor(descriptor, sourceOwner, targetOwner);
        return resolver.resolveDescriptor(relocated);
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
    private byte[] exposeMixinHelperFields(byte[] originalBytes, String internalName) {
        ClassFileModel model = ClassFileModel.parse(originalBytes);
        boolean changed = false;
        MixinDefinition definition = definitionForMixin(internalName);
        String targetOwner = definition == null || definition.targets.isEmpty()
            ? "" : resolver.resolveOwner(definition.targets.get(0));
        for (MemberModel field : model.fields) {
            if ((field.access & 0x0008) == 0
                || AnnotationModel.first(field.attributes, model.pool, "Shadow") != null) continue;
            int exposed = (field.access | 0x0001) & ~0x0006;
            if (exposed != field.access) {
                field.access = exposed;
                changed = true;
            }
        }
        // Mixin classes are loaded by HotSpot before their handlers are
        // copied into a target.  A structural transformer must therefore
        // perform the same local verifier adaptation on the handler class
        // itself; otherwise a shadow return type mismatch fails class
        // definition before target injection can even begin.
        for (MemberModel method : model.methods) {
            AnnotationModel accessor = AnnotationModel.first(method.attributes, model.pool, "Accessor");
            AnnotationModel invoker = AnnotationModel.first(method.attributes, model.pool, "Invoker");
            if ((method.access & ClassFileModel.ACC_STATIC) != 0
                && (accessor != null || invoker != null) && !targetOwner.isEmpty()) {
                // Static accessor/invoker methods are called directly on the
                // mixin interface by real Fabric code.  Mixin normally
                // replaces their throwing placeholder with a bridge to the
                // generated static method on the target class; do that before
                // the interface is defined so the call site sees the same
                // behavior without depending on a reflective fallback.
                String sourceName = method.name(model.pool);
                String descriptor = resolver.resolveDescriptor(method.descriptor(model.pool));
                String bridgeName = sourceName;
                int reference = model.pool.addMethodRef(targetOwner, bridgeName, descriptor, false);
                CodeModel bridge = new CodeModel();
                bridge.maxStack = 8;
                Descriptor.MethodDesc bridgeDescriptor = Descriptor.method(descriptor);
                bridge.maxLocals = 0;
                for (Descriptor.Type argument : bridgeDescriptor.arguments)
                    bridge.maxLocals += argument.slots;
                ArrayList<BytecodeInstructions.Instruction> bridgeInstructions = new ArrayList<>();
                int argumentSlot = 0;
                for (Descriptor.Type argument : bridgeDescriptor.arguments) {
                    bridgeInstructions.addAll(loadLocal(argument, argumentSlot));
                    argumentSlot += argument.slots;
                }
                bridgeInstructions.add(memberInstruction(184, reference));
                bridgeInstructions.add(returnInstruction(bridgeDescriptor.returnType));
                bridge.code = BytecodeInstructions.Editor.assembleGenerated(bridgeInstructions);
                method.replaceCode(model.pool, bridge);
                changed = true;
            }
            CodeModel code = method.code(model.pool);
            if (code == null) continue;
            List<BytecodeInstructions.Instruction> sourceInstructions =
                BytecodeInstructions.decode(code.code);
            BytecodeInstructions.Editor editor = new BytecodeInstructions.Editor(code, model.pool);
            int instructionCount = editor.instructions.size();
            insertVerifierCasts(editor, sourceInstructions, model, method,
                model.pool, model.internalName(), model.internalName());
            if (editor.instructions.size() != instructionCount) {
                editor.finish(model.pool);
                method.replaceCode(model.pool, code);
                changed = true;
            }
        }
        if (!changed) return originalBytes;
        byte[] output = model.write();
        ClassFileSafety.validateBytes(output);
        return output;
    }

    private MixinDefinition definitionForMixin(String internalName) {
        for (List<MixinDefinition> definitionsForTarget : definitions.values()) {
            for (MixinDefinition definition : definitionsForTarget) {
                if (normalizeInternal(definition.model.internalName()).equals(internalName)) return definition;
            }
        }
        return null;
    }

    private static String descriptorOwner(String descriptor) {
        return descriptor.startsWith("L") && descriptor.endsWith(";")
            ? descriptor.substring(1, descriptor.length() - 1) : "";
    }

    private static Descriptor.Type ownerType(String owner) {
        return Descriptor.type(owner != null && owner.startsWith("[")
            ? owner : "L" + owner + ";");
    }

    /**
     * Test the verifier's reference widening relation before emitting a
     * CHECKCAST.  A cast from a known subtype to its supertype is unnecessary
     * and, when an existing StackMapTable still names the subtype, can make a
     * copied helper unverifiable.  Reflection is non-initializing and only a
     * fallback: class definition must remain usable while a target class is
     * still being defined.
     */
    private boolean isVerifierAssignable(String expectedDescriptor, String actualDescriptor) {
        if (expectedDescriptor == null || actualDescriptor == null) return false;
        if (expectedDescriptor.equals(actualDescriptor) || "Ljava/lang/Object;".equals(expectedDescriptor)) return true;
        try {
            ClassLoader loader = mixinClassLoader == null
                ? MixinClassTransformer.class.getClassLoader() : mixinClassLoader;
            Class<?> expected = Class.forName(binaryClassName(expectedDescriptor), false, loader);
            Class<?> actual = Class.forName(binaryClassName(actualDescriptor), false, loader);
            return expected.isAssignableFrom(actual);
        } catch (LinkageError | ClassNotFoundException ignored) {
            // Keep a small hierarchy fallback for the bootstrap window.  It
            // covers the common shadow Entity/LivingEntity path without
            // guessing arbitrary mod class relationships.
            String expected = descriptorOwner(expectedDescriptor);
            String actual = descriptorOwner(actualDescriptor);
            if ("net/minecraft/entity/Entity".equals(expected)
                && actual.startsWith("net/minecraft/entity/")) return true;
            if ("net/minecraft/entity/LivingEntity".equals(expected)
                && (actual.endsWith("/LivingEntity") || actual.endsWith("/MobEntity")
                    || actual.endsWith("/PlayerEntity") || actual.endsWith("/HostileEntity")
                    || actual.endsWith("/MerchantEntity"))) return true;
            return false;
        }
    }

    private static String binaryClassName(String descriptor) {
        if (descriptor.startsWith("[")) return descriptor.replace('/', '.');
        String owner = descriptorOwner(descriptor);
        if (owner.isEmpty()) throw new IllegalArgumentException("not a reference descriptor: " + descriptor);
        return owner.replace('/', '.');
    }

    private static String inferMemberName(String name, String... prefixes) {
        for (String prefix : prefixes) if (name.startsWith(prefix) && name.length() > prefix.length()) {
            String tail = name.substring(prefix.length());
            // Match Mixin's inflection rule: a normal camel-case tail loses
            // only its first capital (getXSize -> xSize), while a completely
            // uppercase tail remains intact (getURL/getROOT -> URL/ROOT).
            // The latter is required by Fabric's registry-sync accessor for
            // Registries.ROOT.  This is the same rule used by Mixin's
            // AccessorName, including its locale-independent case check.
            if (tail.equals(tail.toUpperCase(Locale.ROOT))) return tail;
            return Character.toLowerCase(tail.charAt(0)) + tail.substring(1);
        }
        return name;
    }

    private static String inferInvokerName(String name) {
        for (String prefix : new String[] { "call", "invoke", "access$" })
            if (name.startsWith(prefix) && name.length() > prefix.length()) {
                String suffix = name.substring(prefix.length());
                return Character.toLowerCase(suffix.charAt(0)) + suffix.substring(1);
            }
        return name;
    }

    /** Converts the analyzer's verified stack value back to the descriptor model used by emitters. */
    private static Descriptor.Type stackType(StackAnalyzer.Value value) {
        if (value == null || value.descriptor == null || value.descriptor.isEmpty())
            throw new TransformException("stack analyzer returned an invalid value");
        return Descriptor.type(value.descriptor);
    }

    private static void requireStackSuffix(List<StackAnalyzer.Value> actual,
                                           List<Descriptor.Type> expected, String description) {
        if (actual.size() < expected.size())
            throw new TransformException(description + ": operand stack is too shallow");
        int offset = actual.size() - expected.size();
        for (int i = 0; i < expected.size(); ++i) {
            StackAnalyzer.Value value = actual.get(offset + i);
            if (value == null || value.isUninitialized()
                || !compatible(expected.get(i), stackType(value)))
                throw new TransformException(description + ": operand stack type mismatch at argument " + i);
        }
    }

    private static boolean compatible(Descriptor.Type expected, Descriptor.Type actual) {
        if (expected.descriptor.equals(actual.descriptor)) return true;
        // JVM bytecode uses the INTEGER verification type for boolean, byte,
        // char, and short values.  javac still records the source-level
        // descriptor as Z/B/C/S, so a stack-site comparison must accept any
        // two of those int-like primitive forms.
        if (expected.primitive || actual.primitive || expected.voidType || actual.voidType)
            return expected.primitive && actual.primitive
                && expected.isIntLike() && actual.isIntLike();
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
        final String configName;
        final MixinReferenceMap referenceMap;
        MixinDefinition(String name, ClassFileModel model, List<String> targets, int priority,
                        long registrationOrder, String configName, MixinReferenceMap referenceMap) {
            this.name = name; this.model = model; this.targets = List.copyOf(new LinkedHashSet<>(targets));
            this.priority = priority; this.registrationOrder = registrationOrder; this.configName = configName;
            this.referenceMap = referenceMap == null ? MixinReferenceMap.EMPTY : referenceMap;
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
        String methodToken(String source, String targetOwner, DescriptorResolver resolver) {
            return definition.referenceMap.methodToken(definition.name, source, targetOwner, resolver);
        }
        String memberToken(String source, String targetOwner, DescriptorResolver resolver) {
            return definition.referenceMap.memberToken(definition.name, source, targetOwner, resolver);
        }
        String fieldName(String source, String targetOwner, String descriptor, DescriptorResolver resolver) {
            return definition.referenceMap.fieldName(definition.name, source, targetOwner, descriptor, resolver);
        }
        String atTarget(String source, String targetOwner, DescriptorResolver resolver) {
            return definition.referenceMap.atTarget(definition.name, source, targetOwner, resolver);
        }
        boolean hasCopy(String name, String descriptor) { return copiedMethods.containsKey(name + descriptor); }
        Handler handler(MemberModel source, ClassFileModel target) {
            String key = source.name(definition.model.pool) + source.descriptor(definition.model.pool);
            MemberModel copy = copiedMethods.get(key);
            if (copy == null) throw new TransformException("Mixin handler was not copied: " + key);
            return new Handler(copy.name(target.pool), copy.descriptor(target.pool),
                (copy.access & ClassFileModel.ACC_STATIC) != 0,
                handlerParameterNames(source, definition.model),
                handlerParameterAnnotations(source, definition.model));
        }
    }

    private static List<String> handlerParameterNames(MemberModel source, ClassFileModel owner) {
        CodeModel code = source.code(owner.pool);
        if (code == null) return List.of();
        LocalVariableTable table = LocalVariableTable.read(code, owner.pool);
        Descriptor.MethodDesc descriptor = Descriptor.method(source.descriptor(owner.pool));
        ArrayList<String> names = new ArrayList<>();
        int slot = (source.access & ClassFileModel.ACC_STATIC) == 0 ? 1 : 0;
        for (Descriptor.Type argument : descriptor.arguments) {
            LocalVariableTable.Entry entry = table.at(0, slot);
            names.add(entry == null ? "" : entry.name());
            slot += argument.slots;
        }
        return List.copyOf(names);
    }

    private static List<List<AnnotationModel>> handlerParameterAnnotations(MemberModel source,
                                                                            ClassFileModel owner) {
        Descriptor.MethodDesc descriptor = Descriptor.method(source.descriptor(owner.pool));
        return AnnotationModel.parameterAnnotations(source.attributes, owner.pool,
            descriptor.arguments.size());
    }

    private record Handler(String name, String descriptor, boolean isStatic,
                           List<String> parameterNames,
                           List<List<AnnotationModel>> parameterAnnotations) {
        Handler {
            parameterNames = List.copyOf(parameterNames == null ? List.of() : parameterNames);
            parameterAnnotations = List.copyOf(parameterAnnotations == null ? List.of()
                : parameterAnnotations.stream().map(List::copyOf).toList());
        }
    }

    /** One per-injected-method implementation of a MixinExtras @Share value. */
    private record SharedRefBinding(Descriptor.Type type, int local) { }

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
                case "FAILHARD", "CAPTURE_FAILHARD", "CAPTURE_FAILEXCEPTION" -> HARD;
                default -> throw new TransformException("unsupported LocalCapture value " + enumValue.name());
            };
        }

        boolean requiresAnalysis(Handler handler, ClassFileModel target, MemberModel destination) {
            Descriptor.MethodDesc targetDescriptor = Descriptor.method(destination.descriptor(target.pool));
            Descriptor.MethodDesc handlerDescriptor = Descriptor.method(handler.descriptor);
            int callback = callbackIndexOf(handlerDescriptor);
            if (callback < 0) return false;
            int targetParameters = 0;
            int explicitLocals = 0;
            for (int index = 0; index < handlerDescriptor.arguments.size(); ++index) {
                Descriptor.Type argument = handlerDescriptor.arguments.get(index);
                if (index == callback || isSharedRefType(argument)) continue;
                if (isLocalParameter(handler, index)) explicitLocals++;
                else targetParameters++;
            }
            return explicitLocals > 0 || targetParameters > targetDescriptor.arguments.size();
        }

        boolean isSoft() {
            return this == SOFT;
        }

        private static int callbackIndexOf(Descriptor.MethodDesc descriptor) {
            for (int index = 0; index < descriptor.arguments.size(); ++index)
                if (isCallbackInfoType(descriptor.arguments.get(index))) return index;
            return -1;
        }
    }

    private record CapturedLocal(Descriptor.Type type, int slot, String name, boolean argument) { }

    private static final class AtSpec {
        final String value;
        final String target;
        final String owner;
        final String member;
        final String descriptor;
        final int ordinal;
        final int opcode;
        final Shift shift;
        final int by;
        final List<String> args;
        /** Reserved for loaders which expose an explicit slice selector. */
        final String sliceId;

        private AtSpec(String value, String target, int ordinal, int opcode, Shift shift, List<String> args) {
            this(value, target, ordinal, opcode, shift, 0, args, "");
        }

        private AtSpec(String value, String target, int ordinal, int opcode, Shift shift,
                       List<String> args, String sliceId) {
            this(value, target, ordinal, opcode, shift, 0, args, sliceId);
        }

        private AtSpec(String value, String target, int ordinal, int opcode, Shift shift,
                       int by, List<String> args, String sliceId) {
            this.value = value;
            this.target = target;
            this.ordinal = ordinal;
            this.opcode = opcode;
            this.shift = shift;
            this.by = by;
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
                Shift.valueOf(shiftName), annotation.integer("by", 0), annotation.strings("args"),
                annotation.string("slice", ""));
        }

        AtSpec withTarget(String mappedTarget) {
            return new AtSpec(value, mappedTarget == null ? target : mappedTarget,
                ordinal, opcode, shift, by, args, sliceId);
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
