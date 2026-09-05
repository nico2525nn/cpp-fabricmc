package cppfm.transform;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Applies Fabric Access Widener directives to class-file access flags.
 *
 * <p>This transformer is deliberately independent of a loader.  A loader
 * registers it in the same ordered {@link ClassFileTransformer} chain as the
 * structural Mixin transformer.  It never guesses a namespace or silently
 * ignores a malformed matching member: strict callers get a
 * {@link TransformException}; lenient callers get the original bytes plus a
 * diagnostic.  In both cases a partially modified class is never returned.</p>
 */
public final class AccessWidenerTransformer implements ClassFileTransformer {
    private final String runtimeNamespace;
    private final DescriptorResolver resolver;
    private final boolean strict;
    private final Map<String, AccessState> classAccess = new LinkedHashMap<>();
    private final Map<MemberKey, AccessState> fieldAccess = new LinkedHashMap<>();
    private final Map<MemberKey, AccessState> methodAccess = new LinkedHashMap<>();
    private final List<String> diagnostics = Collections.synchronizedList(new ArrayList<>());

    /** A fail-closed transformer for the generated named shadow namespace. */
    public AccessWidenerTransformer() {
        this("named", DescriptorResolver.IDENTITY, true);
    }

    public AccessWidenerTransformer(boolean strict) {
        this("named", DescriptorResolver.IDENTITY, strict);
    }

    public AccessWidenerTransformer(String runtimeNamespace) {
        this(runtimeNamespace, DescriptorResolver.IDENTITY, true);
    }

    public AccessWidenerTransformer(String runtimeNamespace, boolean strict) {
        this(runtimeNamespace, DescriptorResolver.IDENTITY, strict);
    }

    public AccessWidenerTransformer(String runtimeNamespace, DescriptorResolver resolver,
                                    boolean strict) {
        if (runtimeNamespace == null || runtimeNamespace.isEmpty()) {
            throw new IllegalArgumentException("runtime namespace is empty");
        }
        if (resolver == null) throw new NullPointerException("resolver");
        this.runtimeNamespace = runtimeNamespace;
        this.resolver = resolver;
        this.strict = strict;
    }

    public String getRuntimeNamespace() {
        return runtimeNamespace;
    }

    public DescriptorResolver getResolver() {
        return resolver;
    }

    public synchronized void register(byte[] content) {
        register(AccessWidener.parse(content));
    }

    public synchronized void register(String content) {
        register(AccessWidener.parse(content));
    }

    /** Register a parsed widener after enforcing the runtime namespace boundary. */
    public synchronized void register(AccessWidener widener) {
        if (widener == null) throw new NullPointerException("widener");
        if (!runtimeNamespace.equals(widener.getNamespace())) {
            throw new TransformException("access widener namespace '" + widener.getNamespace()
                + "' does not match runtime namespace '" + runtimeNamespace + "'");
        }
        // Resolve and merge into copies first.  A bad later directive must not
        // leave an earlier directive from the same file active.
        Map<String, AccessState> nextClassAccess = copyStates(classAccess);
        Map<MemberKey, AccessState> nextFieldAccess = copyStates(fieldAccess);
        Map<MemberKey, AccessState> nextMethodAccess = copyStates(methodAccess);
        for (AccessWidener.Target raw : widener.getTargets()) {
            validateTargetKind(raw);
            AccessWidener.Target target = resolve(raw);
            switch (target.kind()) {
                case CLASS -> merge(nextClassAccess, target.owner(), target.access());
                case FIELD -> {
                    merge(nextClassAccess, target.owner(),
                        target.access() == AccessWidener.Access.ACCESSIBLE
                            ? AccessWidener.Access.ACCESSIBLE : null);
                    merge(nextFieldAccess,
                        new MemberKey(target.owner(), target.name(), target.descriptor()),
                        target.access());
                }
                case METHOD -> {
                    merge(nextClassAccess, target.owner(),
                        target.access() == AccessWidener.Access.EXTENDABLE
                            ? AccessWidener.Access.EXTENDABLE : AccessWidener.Access.ACCESSIBLE);
                    merge(nextMethodAccess,
                        new MemberKey(target.owner(), target.name(), target.descriptor()),
                        target.access());
                }
            }
        }
        classAccess.clear();
        classAccess.putAll(nextClassAccess);
        fieldAccess.clear();
        fieldAccess.putAll(nextFieldAccess);
        methodAccess.clear();
        methodAccess.putAll(nextMethodAccess);
    }

    /** Remove all registered directives while keeping the transformer reusable. */
    public synchronized void clear() {
        classAccess.clear();
        fieldAccess.clear();
        methodAccess.clear();
        diagnostics.clear();
    }

    public List<String> getDiagnostics() {
        synchronized (diagnostics) {
            return Collections.unmodifiableList(new ArrayList<>(diagnostics));
        }
    }

    public synchronized int getTargetCount() {
        return classAccess.size() + fieldAccess.size() + methodAccess.size();
    }

    @Override
    public byte[] transform(String binaryName, byte[] originalBytes, TransformContext context) {
        if (binaryName == null || originalBytes == null) {
            throw new TransformException("access widener received null class input");
        }
        String internalName = binaryName.replace('.', '/');
        Map<MemberKey, AccessState> fields;
        Map<MemberKey, AccessState> methods;
        AccessState classState;
        synchronized (this) {
            classState = copy(classAccess.get(internalName));
            fields = matching(fieldAccess, internalName);
            methods = matching(methodAccess, internalName);
        }
        if (classState == null && fields.isEmpty() && methods.isEmpty()) {
            return originalBytes.clone();
        }

        try {
            ClassFileModel model = ClassFileModel.parse(originalBytes);
            if (!internalName.equals(model.internalName())) {
                throw new TransformException("class name mismatch: requested " + internalName
                    + " but bytes contain " + model.internalName());
            }
            boolean changed = false;
            if (classState != null) {
                int widened = classState.applyClass(model.access);
                changed |= widened != model.access;
                model.access = widened;
            }
            for (Map.Entry<MemberKey, AccessState> entry : fields.entrySet()) {
                MemberKey key = entry.getKey();
                MemberModel member = model.field(key.name(), key.descriptor());
                if (member == null) throw missing("field", key);
                int widened = entry.getValue().applyField(member.access, model.access);
                changed |= widened != member.access;
                member.access = widened;
            }
            for (Map.Entry<MemberKey, AccessState> entry : methods.entrySet()) {
                MemberKey key = entry.getKey();
                MemberModel member = model.method(key.name(), key.descriptor());
                if (member == null) throw missing("method", key);
                int widened = entry.getValue().applyMethod(member.access, model.access, key.name());
                boolean memberChanged = widened != member.access;
                changed |= memberChanged;
                member.access = widened;
                if (memberChanged && context != null) {
                    context.recordMethod(key.name() + key.descriptor());
                }
            }
            if (!changed) return originalBytes.clone();
            byte[] transformed = model.write();
            // Access changes do not require frame rewriting, but the same
            // structural validation gate as Mixin output still applies.
            ClassFileSafety.validateBytes(transformed);
            return transformed;
        } catch (RuntimeException failure) {
            String message = "access widener failed for " + binaryName + ": " + failure.getMessage();
            if (strict || (context != null && context.isStrict())) throw failure instanceof TransformException
                ? (TransformException) failure : new TransformException(message, failure);
            diagnostics.add(message);
            if (context != null) context.diagnostic(message);
            return originalBytes.clone();
        }
    }

    private AccessWidener.Target resolve(AccessWidener.Target raw) {
        String owner = AccessWidener.validateResolvedName(
            resolver.resolveOwner(raw.owner()), "owner");
        if (raw.kind() == AccessWidener.TargetKind.CLASS) {
            return new AccessWidener.Target(raw.access(), raw.kind(), owner, "", "", raw.transitive());
        }
        String descriptor = AccessWidener.validateResolvedDescriptor(
            resolver.resolveDescriptor(raw.descriptor()), raw.kind(), raw.name());
        String name = raw.kind() == AccessWidener.TargetKind.FIELD
            ? resolver.resolveField(owner, raw.name(), descriptor)
            : resolver.resolveMethod(owner, raw.name(), descriptor);
        name = AccessWidener.validateResolvedMemberName(name, raw.kind(),
            raw.kind().getName() + " name for " + raw.owner());
        return new AccessWidener.Target(raw.access(), raw.kind(), owner, name, descriptor, raw.transitive());
    }

    private static <K> void merge(Map<K, AccessState> map, K key, AccessWidener.Access access) {
        if (access == null) return;
        map.computeIfAbsent(key, ignored -> new AccessState()).merge(access);
    }

    private static Map<MemberKey, AccessState> matching(Map<MemberKey, AccessState> source, String owner) {
        LinkedHashMap<MemberKey, AccessState> output = new LinkedHashMap<>();
        for (Map.Entry<MemberKey, AccessState> entry : source.entrySet()) {
            if (entry.getKey().owner().equals(owner)) {
                output.put(entry.getKey(), copy(entry.getValue()));
            }
        }
        return output;
    }

    private static AccessState copy(AccessState source) {
        return source == null ? null : source.copy();
    }

    private static <K> Map<K, AccessState> copyStates(Map<K, AccessState> source) {
        LinkedHashMap<K, AccessState> output = new LinkedHashMap<>();
        for (Map.Entry<K, AccessState> entry : source.entrySet()) {
            output.put(entry.getKey(), entry.getValue().copy());
        }
        return output;
    }

    private static void validateTargetKind(AccessWidener.Target target) {
        if (target.kind() == AccessWidener.TargetKind.CLASS
            && target.access() == AccessWidener.Access.MUTABLE) {
            throw new TransformException("classes cannot be mutable");
        }
        if (target.kind() == AccessWidener.TargetKind.FIELD
            && target.access() == AccessWidener.Access.EXTENDABLE) {
            throw new TransformException("fields cannot be extendable");
        }
        if (target.kind() != AccessWidener.TargetKind.FIELD
            && target.access() == AccessWidener.Access.MUTABLE) {
            throw new TransformException(target.kind().getName() + " targets cannot be mutable");
        }
    }

    private static TransformException missing(String kind, MemberKey key) {
        return new TransformException("access widener target not found: " + kind + " "
            + key.owner() + " " + key.name() + " " + key.descriptor());
    }

    private record MemberKey(String owner, String name, String descriptor) { }

    private static final class AccessState {
        private boolean accessible;
        private boolean extendable;
        private boolean mutable;

        void merge(AccessWidener.Access access) {
            switch (access) {
                case ACCESSIBLE -> accessible = true;
                case EXTENDABLE -> extendable = true;
                case MUTABLE -> mutable = true;
            }
        }

        AccessState copy() {
            AccessState output = new AccessState();
            output.accessible = accessible;
            output.extendable = extendable;
            output.mutable = mutable;
            return output;
        }

        int applyClass(int access) {
            if (extendable) return makePublic(access & ~ClassFileModel.ACC_FINAL);
            if (accessible) return makePublic(access);
            return access;
        }

        int applyField(int access, int ownerAccess) {
            boolean interfaceStatic = (ownerAccess & ClassFileModel.ACC_INTERFACE) != 0
                && (access & ClassFileModel.ACC_STATIC) != 0;
            if (accessible && mutable) {
                return interfaceStatic ? makePublic(access) : makePublic(access & ~ClassFileModel.ACC_FINAL);
            }
            if (accessible) return makePublic(access);
            if (mutable) return interfaceStatic ? access : access & ~ClassFileModel.ACC_FINAL;
            return access;
        }

        int applyMethod(int access, int ownerAccess, String name) {
            if (accessible && extendable) return makePublic(access & ~ClassFileModel.ACC_FINAL);
            if (accessible) {
                int output = makePublic(access);
                if ((access & ClassFileModel.ACC_PRIVATE) != 0
                    && !name.equals("<init>") && !name.equals("<clinit>")
                    && (ownerAccess & ClassFileModel.ACC_INTERFACE) == 0
                    && (access & ClassFileModel.ACC_STATIC) == 0) {
                    output |= ClassFileModel.ACC_FINAL;
                }
                return output;
            }
            if (extendable) {
                int output = access & ~ClassFileModel.ACC_FINAL;
                if ((access & ClassFileModel.ACC_PUBLIC) == 0) {
                    output = (output & ~ClassFileModel.ACC_PRIVATE) | ClassFileModel.ACC_PROTECTED;
                }
                return output;
            }
            if (mutable) throw new TransformException("methods cannot be mutable");
            return access;
        }

        private static int makePublic(int access) {
            return (access & ~(ClassFileModel.ACC_PRIVATE | ClassFileModel.ACC_PROTECTED))
                | ClassFileModel.ACC_PUBLIC;
        }
    }
}
