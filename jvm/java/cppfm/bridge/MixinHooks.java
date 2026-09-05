package cppfm.bridge;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.WeakHashMap;
import java.util.concurrent.atomic.AtomicLong;

import cppfm.transform.MixinDispatch;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.gen.Accessor;
import org.spongepowered.asm.mixin.gen.Invoker;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.ModifyConstant;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.Slice;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.LocalCapture;

/**
 * Mixin registration and execution contract for the Java shadow runtime.
 *
 * <p>The class has no dependency on a loader or transformer. A Knot-side
 * transformer can publish {@link MixinPlan}s before a target class is defined,
 * query {@link #handlerPlansForTarget(String, String, String)}, and emit a
 * normal {@code invokestatic}/{@code invokevirtual} call to the mod handler.
 * Generated/manual call sites can use {@link #dispatch(InvocationSite)} when
 * they need the common argument, return-value, or cancellation adapter.</p>
 *
 * <p>Class bytes are never parsed or defined here. The bytes registration
 * methods only record that an opaque class payload exists; metadata and the
 * actual handler methods are published separately. This keeps the contract
 * one-way and prevents a loader/transformer dependency cycle.</p>
 */
public final class MixinHooks {
    private static final Object LOCK = new Object();
    private static final Map<String, List<Handler>> HANDLERS = new HashMap<>();
    private static final Map<String, List<Handler>> OVERWRITES = new HashMap<>();
    private static final List<MixinPlan> MIXIN_PLANS = new ArrayList<>();
    private static final Set<String> PLAN_KEYS = new HashSet<>();
    private static final Map<String, Integer> OPAQUE_CLASS_BYTES = new HashMap<>();
    private static final List<String> WARNINGS = new ArrayList<>();
    private static final AtomicLong REGISTRATION_SEQUENCE = new AtomicLong();
    private static final ThreadLocal<Deque<ExecutionFrame>> EXECUTION_FRAMES =
        ThreadLocal.withInitial(ArrayDeque::new);

    private MixinHooks() {}

    /** Kinds of handler understood by the registration and dispatch ABI. */
    public enum HandlerType {
        INJECT, REDIRECT, MODIFY_ARG, MODIFY_CONSTANT, MODIFY_VARIABLE,
        OVERWRITE, ACCESSOR, INVOKER, SHADOW
    }

    /** Instruction topology exposed to manual/generated call sites. */
    public enum InjectionPoint {
        HEAD, TAIL, RETURN, INVOKE, FIELD, NEW, JUMP, CONSTANT, LOAD, STORE, OVERWRITE
    }

    /** Class-file-independent representation of {@code @At}. */
    public static final class AtSpec {
        private final String value;
        private final String target;
        private final int ordinal;
        private final int opcode;
        private final At.Shift shift;
        private final List<String> args;
        private final String id;

        public AtSpec(String value) {
            this(value, "", -1, -1, At.Shift.NONE, List.of(), "");
        }

        public AtSpec(String value, String target, int ordinal, int opcode) {
            this(value, target, ordinal, opcode, At.Shift.NONE, List.of(), "");
        }

        public AtSpec(String value, String target, int ordinal, int opcode,
                      At.Shift shift, List<String> args, String id) {
            this.value = normalizePoint(value);
            this.target = target == null ? "" : target;
            this.ordinal = ordinal;
            this.opcode = opcode;
            this.shift = shift == null ? At.Shift.NONE : shift;
            this.args = List.copyOf(args == null ? List.of() : args);
            this.id = id == null ? "" : id;
        }

        public static AtSpec of(At at) {
            if (at == null) return new AtSpec("HEAD");
            return new AtSpec(at.value(), at.target(), at.ordinal(), at.opcode(),
                              at.shift(), Arrays.asList(at.args()), at.id());
        }

        public static AtSpec of(InjectionPoint point) {
            return new AtSpec(point == null ? "HEAD" : point.name());
        }

        public String value() { return value; }
        public String target() { return target; }
        public int ordinal() { return ordinal; }
        public int opcode() { return opcode; }
        public At.Shift shift() { return shift; }
        public List<String> args() { return args; }
        public String id() { return id; }
        public InjectionPoint point() {
            try { return InjectionPoint.valueOf(value); }
            catch (IllegalArgumentException ignored) { return null; }
        }

        private boolean matches(InvocationSite site) {
            if (site == null || !value.equals(normalizePoint(site.at().value()))) return false;
            if (!targetMatches(target, site.at().target())) return false;
            if (ordinal >= 0) {
                int siteOrdinal = site.ordinal();
                InjectionPoint point = site.at().point();
                if ((point == InjectionPoint.LOAD || point == InjectionPoint.STORE) &&
                    site.variableOrdinal() >= 0) siteOrdinal = site.variableOrdinal();
                if (ordinal != siteOrdinal) return false;
            }
            if (opcode >= 0 && opcode != site.opcode()) return false;
            return true;
        }

        @Override public String toString() {
            return value + (target.isEmpty() ? "" : "(" + target + ")") +
                   (ordinal < 0 ? "" : "#" + ordinal);
        }
    }

    /** Class-file-independent representation of {@code @Slice}. */
    public static final class SliceSpec {
        private final AtSpec from;
        private final AtSpec to;

        public SliceSpec(AtSpec from, AtSpec to) {
            this.from = from == null ? new AtSpec("HEAD") : from;
            this.to = to == null ? new AtSpec("TAIL") : to;
        }

        public static SliceSpec of(Slice slice) {
            return slice == null ? new SliceSpec(null, null)
                                 : new SliceSpec(AtSpec.of(slice.from()), AtSpec.of(slice.to()));
        }

        public AtSpec from() { return from; }
        public AtSpec to() { return to; }

        private boolean contains(InvocationSite site) {
            if (site == null) return false;
            if (site.hasSliceBounds()) {
                if (site.sliceFromIndex() >= 0 && site.instructionIndex() >= 0 &&
                    site.instructionIndex() < site.sliceFromIndex()) return false;
                if (site.sliceToIndex() >= 0 && site.instructionIndex() >= 0 &&
                    site.instructionIndex() > site.sliceToIndex()) return false;
            }
            // A manual site without resolved indices cannot disprove a slice.
            // A class-file transformer can provide exact bounds in the site.
            return true;
        }
    }

    /** Constant selector used by {@code @ModifyConstant}. */
    public static final class ConstantSpec {
        private final Object value;
        private final Class<?> type;
        private final boolean nullValue;
        private final int ordinal;

        public ConstantSpec(Object value, Class<?> type, boolean nullValue, int ordinal) {
            this.value = value;
            this.type = type;
            this.nullValue = nullValue;
            this.ordinal = ordinal;
        }

        public ConstantSpec(Object value) { this(value, null, value == null, -1); }

        public static ConstantSpec of(Constant constant) {
            if (constant == null) return new ConstantSpec(null, null, true, -1);
            if (constant.nullValue()) return new ConstantSpec(null, null, true, constant.ordinal());
            if (constant.stringValue() != null && !constant.stringValue().isEmpty())
                return new ConstantSpec(constant.stringValue(), String.class, false, constant.ordinal());
            if (constant.classValue() != void.class)
                return new ConstantSpec(constant.classValue(), Class.class, false, constant.ordinal());
            if (constant.doubleValue() != 0.0d)
                return new ConstantSpec(constant.doubleValue(), double.class, false, constant.ordinal());
            if (constant.floatValue() != 0.0f)
                return new ConstantSpec(constant.floatValue(), float.class, false, constant.ordinal());
            if (constant.longValue() != 0L)
                return new ConstantSpec(constant.longValue(), long.class, false, constant.ordinal());
            return new ConstantSpec(constant.intValue(), int.class, false, constant.ordinal());
        }

        public Object value() { return value; }
        public Class<?> type() { return type; }
        public boolean nullValue() { return nullValue; }
        public int ordinal() { return ordinal; }

        private boolean matches(Object candidate, int siteOrdinal) {
            if (ordinal >= 0 && ordinal != siteOrdinal) return false;
            if (nullValue) return candidate == null;
            if (candidate == null || value == null) return false;
            if (type != null && type.isPrimitive()) {
                if (!numericTypeMatches(type, candidate.getClass())) return false;
            } else if (type != null && !type.isInstance(candidate)) {
                return false;
            }
            return value.equals(candidate);
        }

        private static boolean numericTypeMatches(Class<?> primitive, Class<?> boxed) {
            if (primitive == boolean.class) return boxed == Boolean.class;
            if (primitive == char.class) return boxed == Character.class;
            if (primitive == byte.class) return boxed == Byte.class;
            if (primitive == short.class) return boxed == Short.class;
            if (primitive == int.class) return boxed == Integer.class;
            if (primitive == long.class) return boxed == Long.class;
            if (primitive == float.class) return boxed == Float.class;
            if (primitive == double.class) return boxed == Double.class;
            return true;
        }
    }

    /** Method identity used by the transformer-facing lookup API. */
    public static final class MethodKey {
        private final String owner;
        private final String name;
        private final String descriptor;

        public MethodKey(String owner, String name, String descriptor) {
            this.owner = normalizeOwner(owner);
            this.name = name == null ? "" : name;
            this.descriptor = normalizeDescriptor(descriptor);
        }

        public String owner() { return owner; }
        public String name() { return name; }
        public String descriptor() { return descriptor; }
        public String getOwner() { return owner; }
        public String getName() { return name; }
        public String getDescriptor() { return descriptor; }

        @Override public boolean equals(Object other) {
            if (!(other instanceof MethodKey key)) return false;
            return owner.equals(key.owner) && name.equals(key.name) && descriptor.equals(key.descriptor);
        }

        @Override public int hashCode() {
            return 31 * (31 * owner.hashCode() + name.hashCode()) + descriptor.hashCode();
        }

        @Override public String toString() { return owner + "#" + name + descriptor; }
    }

    /**
     * Immutable metadata for one handler. It can be published before either
     * the mixin or target class is defined.
     */
    public static final class HandlerPlan {
        private final String mixinClassName;
        private final String targetOwner;
        private final String targetMethod;
        private final String targetDescriptor;
        private final HandlerType type;
        private final AtSpec at;
        private final String handlerMethodName;
        private final String handlerDescriptor;
        private final int priority;
        private final int order;
        private final boolean cancellable;
        private final LocalCapture locals;
        private final int require;
        private final int expect;
        private final int allow;
        private final int argumentIndex;
        private final boolean argsOnly;
        private final List<SliceSpec> slices;
        private final List<ConstantSpec> constants;
        private final long sequence;

        public HandlerPlan(String mixinClassName, String targetOwner, String targetMethod,
                           String targetDescriptor, HandlerType type, AtSpec at,
                           String handlerMethodName, String handlerDescriptor,
                           int priority, int order, boolean cancellable,
                           LocalCapture locals, int require, int expect, int allow,
                           int argumentIndex, List<SliceSpec> slices,
                           List<ConstantSpec> constants) {
            this(mixinClassName, targetOwner, targetMethod, targetDescriptor, type, at,
                 handlerMethodName, handlerDescriptor, priority, order, cancellable, locals,
                 require, expect, allow, argumentIndex, false, slices, constants);
        }

        public HandlerPlan(String mixinClassName, String targetOwner, String targetMethod,
                           String targetDescriptor, HandlerType type, AtSpec at,
                           String handlerMethodName, String handlerDescriptor,
                           int priority, int order, boolean cancellable,
                           LocalCapture locals, int require, int expect, int allow,
                           int argumentIndex, boolean argsOnly, List<SliceSpec> slices,
                           List<ConstantSpec> constants) {
            this.mixinClassName = mixinClassName == null ? "" : mixinClassName;
            this.targetOwner = normalizeOwner(targetOwner);
            this.targetMethod = targetMethod == null ? "" : targetMethod;
            this.targetDescriptor = normalizeDescriptor(targetDescriptor);
            this.type = type == null ? HandlerType.INJECT : type;
            this.at = at == null ? new AtSpec(defaultPoint(this.type)) : at;
            this.handlerMethodName = handlerMethodName == null ? "" : handlerMethodName;
            this.handlerDescriptor = normalizeDescriptor(handlerDescriptor);
            this.priority = priority;
            this.order = order;
            this.cancellable = cancellable;
            this.locals = locals == null ? LocalCapture.NO_CAPTURE : locals;
            this.require = require;
            this.expect = expect;
            this.allow = allow;
            this.argumentIndex = argumentIndex;
            this.argsOnly = argsOnly;
            this.slices = List.copyOf(slices == null ? List.of() : slices);
            this.constants = List.copyOf(constants == null ? List.of() : constants);
            this.sequence = REGISTRATION_SEQUENCE.getAndIncrement();
        }

        public HandlerPlan(String mixinClassName, String targetOwner, String targetMethod,
                           String targetDescriptor, HandlerType type, AtSpec at,
                           String handlerMethodName, String handlerDescriptor,
                           int priority, int order) {
            this(mixinClassName, targetOwner, targetMethod, targetDescriptor, type, at,
                 handlerMethodName, handlerDescriptor, priority, order, false,
                 LocalCapture.NO_CAPTURE, -1, 1, -1, -1, List.of(), List.of());
        }

        public String mixinClassName() { return mixinClassName; }
        public String targetOwner() { return targetOwner; }
        public String targetMethod() { return targetMethod; }
        public String targetDescriptor() { return targetDescriptor; }
        public HandlerType type() { return type; }
        public AtSpec at() { return at; }
        public String handlerMethodName() { return handlerMethodName; }
        public String handlerDescriptor() { return handlerDescriptor; }
        public int priority() { return priority; }
        public int order() { return order; }
        public boolean cancellable() { return cancellable; }
        public LocalCapture locals() { return locals; }
        public int require() { return require; }
        public int expect() { return expect; }
        public int allow() { return allow; }
        public int argumentIndex() { return argumentIndex; }
        public boolean argsOnly() { return argsOnly; }
        public List<SliceSpec> slices() { return slices; }
        public List<ConstantSpec> constants() { return constants; }
        public long sequence() { return sequence; }
        public MethodKey methodKey() { return new MethodKey(targetOwner, targetMethod, targetDescriptor); }

        private HandlerPlan withOwner(String owner) {
            return new HandlerPlan(mixinClassName, owner, targetMethod, targetDescriptor, type, at,
                                   handlerMethodName, handlerDescriptor, priority, order,
                                   cancellable, locals, require, expect, allow, argumentIndex,
                                   argsOnly, slices, constants);
        }

        private boolean matchesMethod(InvocationSite site) {
            return site != null && targetMethod.equals(site.method()) &&
                   descriptorMatches(targetDescriptor, site.descriptor());
        }

        private boolean matchesSite(InvocationSite site) {
            if (!matchesMethod(site) || !at.matches(site)) return false;
            for (SliceSpec slice : slices) if (!slice.contains(site)) return false;
            if (type == HandlerType.MODIFY_CONSTANT && !constantMatches(site)) return false;
            if (type == HandlerType.MODIFY_VARIABLE) {
                if (argsOnly && !site.variableIsArgument()) return false;
                if (argumentIndex >= 0 && argumentIndex != site.variableIndex()) return false;
                if (at.ordinal() >= 0) {
                    int siteOrdinal = site.variableOrdinal() >= 0 ? site.variableOrdinal() : site.ordinal();
                    if (at.ordinal() != siteOrdinal) return false;
                }
            }
            return true;
        }

        private boolean constantMatches(InvocationSite site) {
            if (constants.isEmpty()) return true;
            Object candidate = site.hasConstant() ? site.constant() : site.value();
            for (ConstantSpec constant : constants)
                if (constant.matches(candidate, site.ordinal())) return true;
            return false;
        }

        private String key() {
            return mixinClassName + "|" + targetOwner + "|" + targetMethod + "|" +
                   targetDescriptor + "|" + type + "|" + at + "|" + handlerMethodName +
                   "|" + handlerDescriptor;
        }

        @Override public String toString() {
            return type + " " + methodKey() + " <- " + mixinClassName + "." + handlerMethodName;
        }
    }

    /** A metadata group for one mixin class. */
    public static final class MixinPlan {
        private final String mixinClassName;
        private final int priority;
        private final List<String> targetOwners;
        private final List<HandlerPlan> handlers = new ArrayList<>();

        public MixinPlan(String mixinClassName, int priority, String... targetOwners) {
            this.mixinClassName = mixinClassName == null ? "" : mixinClassName;
            this.priority = priority;
            List<String> owners = new ArrayList<>();
            if (targetOwners != null)
                for (String owner : targetOwners)
                    if (owner != null && !owner.isEmpty()) owners.add(normalizeOwner(owner));
            this.targetOwners = List.copyOf(owners);
        }

        public String mixinClassName() { return mixinClassName; }
        public int priority() { return priority; }
        public List<String> targetOwners() { return targetOwners; }
        public List<HandlerPlan> handlers() { return List.copyOf(handlers); }

        /** Add a handler without loading either class. */
        public MixinPlan addHandler(HandlerPlan handler) {
            if (handler != null) handlers.add(handler);
            return this;
        }
    }

    /**
     * Per-transformed-method execution marker. A transformer should surround
     * one target-method invocation with this token. Manual hooks called inside
     * that invocation then reuse the first dispatch result instead of firing a
     * second callback. Tokens are thread-local and nestable.
     */
    public static final class ExecutionToken implements AutoCloseable {
        private final ExecutionFrame frame;
        private boolean closed;

        private ExecutionToken(ExecutionFrame frame) { this.frame = frame; }

        /** Claim a handler/site pair before invoking the handler directly. */
        public boolean claim(HandlerPlan plan, InvocationSite site) {
            if (plan == null || site == null) return true;
            return frame.claim(handlerExecutionKey(plan, site));
        }

        /** Claim an arbitrary stable marker chosen by generated bytecode. */
        public boolean claim(String marker) { return frame.claim(marker == null ? "" : marker); }
        public boolean isClaimed(HandlerPlan plan, InvocationSite site) {
            return plan != null && site != null && frame.isClaimed(handlerExecutionKey(plan, site));
        }

        /** Record a result when a transformer calls a handler directly. */
        public void record(InvocationSite site, DispatchResult result) {
            if (site != null && result != null) frame.record(site.marker(), result);
        }

        public boolean isClosed() { return closed; }

        @Override public void close() {
            if (closed) return;
            closed = true;
            Deque<ExecutionFrame> frames = EXECUTION_FRAMES.get();
            if (!frames.isEmpty() && frames.peek() == frame) frames.pop();
            else frames.remove(frame);
            if (frames.isEmpty()) EXECUTION_FRAMES.remove();
        }
    }

    /**
     * Description of one generated/manual call site. Arguments exclude the
     * target method receiver; {@link Builder#receiver(Object)} supplies an
     * INVOKE/FIELD receiver for redirect handlers.
     */
    public static final class InvocationSite {
        private final Object target;
        private final String owner;
        private final String method;
        private final String descriptor;
        private final AtSpec at;
        private final Object[] arguments;
        private final Object[] locals;
        private final boolean localsPresent;
        private final boolean returnPresent;
        private final Object returnValue;
        private final boolean valuePresent;
        private final Object value;
        private final boolean constantPresent;
        private final Object constant;
        private final boolean variablePresent;
        private final Object variable;
        private final int variableIndex;
        private final int variableOrdinal;
        private final boolean variableIsArgument;
        private final Object receiver;
        private final int ordinal;
        private final int opcode;
        private final int instructionIndex;
        private final int sliceFromIndex;
        private final int sliceToIndex;
        private final String eventId;

        private InvocationSite(Builder builder) {
            this.target = builder.target;
            this.owner = normalizeOwner(builder.owner != null ? builder.owner : ownerOf(builder.target));
            this.method = builder.method == null ? "" : builder.method;
            this.descriptor = normalizeDescriptor(builder.descriptor);
            this.at = builder.at == null ? new AtSpec("HEAD") : builder.at;
            this.arguments = copy(builder.arguments);
            this.locals = copy(builder.locals);
            this.localsPresent = builder.localsPresent;
            this.returnPresent = builder.returnPresent;
            this.returnValue = builder.returnValue;
            this.valuePresent = builder.valuePresent;
            this.value = builder.value;
            this.constantPresent = builder.constantPresent;
            this.constant = builder.constant;
            this.variablePresent = builder.variablePresent;
            this.variable = builder.variable;
            this.variableIndex = builder.variableIndex;
            this.variableOrdinal = builder.variableOrdinal;
            this.variableIsArgument = builder.variableIsArgument;
            this.receiver = builder.receiver;
            this.ordinal = builder.ordinal;
            this.opcode = builder.opcode;
            this.instructionIndex = builder.instructionIndex;
            this.sliceFromIndex = builder.sliceFromIndex;
            this.sliceToIndex = builder.sliceToIndex;
            this.eventId = builder.eventId == null ? "" : builder.eventId;
        }

        public static Builder builder(Object target, String method) { return new Builder(target, method); }
        public static Builder builder(String owner, String method, String descriptor) {
            return new Builder(null, method).owner(owner).descriptor(descriptor);
        }

        public static InvocationSite of(Object target, String method, String descriptor,
                                        AtSpec at, Object... arguments) {
            return builder(target, method).descriptor(descriptor).at(at).arguments(arguments).build();
        }

        public Object target() { return target; }
        public String owner() { return owner; }
        public String method() { return method; }
        public String descriptor() { return descriptor; }
        public AtSpec at() { return at; }
        public Object[] arguments() { return arguments; }
        public Object[] locals() { return locals; }
        public boolean hasLocals() { return localsPresent; }
        public boolean hasReturnValue() { return returnPresent; }
        public Object returnValue() { return returnValue; }
        public boolean hasValue() { return valuePresent; }
        public Object value() { return value; }
        public boolean hasConstant() { return constantPresent; }
        public Object constant() { return constant; }
        public boolean hasVariable() { return variablePresent; }
        public Object variable() { return variable; }
        public int variableIndex() { return variableIndex; }
        public int variableOrdinal() { return variableOrdinal; }
        public boolean variableIsArgument() { return variableIsArgument; }
        public Object receiver() { return receiver; }
        public int ordinal() { return ordinal; }
        public int opcode() { return opcode; }
        public int instructionIndex() { return instructionIndex; }
        public boolean hasSliceBounds() { return sliceFromIndex >= 0 || sliceToIndex >= 0; }
        public int sliceFromIndex() { return sliceFromIndex; }
        public int sliceToIndex() { return sliceToIndex; }
        public String eventId() { return eventId; }
        public String marker() { return executionMarker(this); }

        public static final class Builder {
            private final Object target;
            private final String method;
            private String owner;
            private String descriptor = "*";
            private AtSpec at;
            private Object[] arguments = new Object[0];
            private Object[] locals = new Object[0];
            private boolean localsPresent;
            private boolean returnPresent;
            private Object returnValue;
            private boolean valuePresent;
            private Object value;
            private boolean constantPresent;
            private Object constant;
            private boolean variablePresent;
            private Object variable;
            private int variableIndex = -1;
            private int variableOrdinal = -1;
            private boolean variableIsArgument;
            private Object receiver;
            private int ordinal = -1;
            private int opcode = -1;
            private int instructionIndex = -1;
            private int sliceFromIndex = -1;
            private int sliceToIndex = -1;
            private String eventId = "";

            private Builder(Object target, String method) { this.target = target; this.method = method; }
            public Builder owner(String owner) { this.owner = owner; return this; }
            public Builder descriptor(String descriptor) { this.descriptor = descriptor; return this; }
            public Builder at(AtSpec at) { this.at = at; return this; }
            public Builder at(String at) { this.at = new AtSpec(at); return this; }
            public Builder arguments(Object... arguments) { this.arguments = copy(arguments); return this; }
            public Builder locals(Object... locals) { this.locals = copy(locals); this.localsPresent = true; return this; }
            public Builder returnValue(Object value) { this.returnPresent = true; this.returnValue = value; return this; }
            public Builder value(Object value) { this.valuePresent = true; this.value = value; return this; }
            public Builder constant(Object value) { this.constantPresent = true; this.constant = value; this.valuePresent = true; this.value = value; return this; }
            public Builder variable(Object value) { this.variablePresent = true; this.variable = value; this.valuePresent = true; this.value = value; return this; }
            public Builder variableIndex(int index) { this.variableIndex = index; return this; }
            public Builder variableOrdinal(int ordinal) { this.variableOrdinal = ordinal; return this; }
            public Builder variableIsArgument(boolean value) { this.variableIsArgument = value; return this; }
            public Builder receiver(Object receiver) { this.receiver = receiver; return this; }
            public Builder ordinal(int ordinal) { this.ordinal = ordinal; return this; }
            public Builder opcode(int opcode) { this.opcode = opcode; return this; }
            public Builder instructionIndex(int index) { this.instructionIndex = index; return this; }
            public Builder slice(int from, int to) { this.sliceFromIndex = from; this.sliceToIndex = to; return this; }
            public Builder eventId(String eventId) { this.eventId = eventId; return this; }
            public InvocationSite build() { return new InvocationSite(this); }
        }
    }

    /** Mutable output of {@link #dispatch(InvocationSite)}. */
    public static final class DispatchResult {
        private final InvocationSite site;
        private Object[] arguments;
        private Object[] locals;
        private Object returnValue;
        private boolean returnPresent;
        private Object value;
        private boolean valuePresent;
        private Object constant;
        private boolean constantPresent;
        private Object variable;
        private boolean variablePresent;
        private Object redirectValue;
        private boolean handled;
        private boolean cancelled;
        private CallbackInfo callbackInfo;
        private int invokedHandlers;
        private final List<String> invoked = new ArrayList<>();

        private DispatchResult(InvocationSite site, boolean returnable) {
            this.site = site;
            this.arguments = site.arguments().clone();
            this.locals = site.locals().clone();
            this.returnPresent = site.hasReturnValue();
            this.returnValue = site.returnValue();
            this.valuePresent = site.hasValue();
            this.value = site.value();
            this.constantPresent = site.hasConstant();
            this.constant = site.constant();
            this.variablePresent = site.hasVariable();
            this.variable = site.variable();
            this.callbackInfo = returnable
                ? new CallbackInfoReturnable<>(site.method() + ":" + site.at().value(), true, returnValue)
                : new CallbackInfo(site.method() + ":" + site.at().value(), true);
        }

        public InvocationSite site() { return site; }
        public Object[] arguments() { return arguments; }
        public Object[] getArguments() { return arguments; }
        public Object[] locals() { return locals; }
        public Object[] getLocals() { return locals; }
        public boolean hasReturnValue() { return returnPresent; }
        public Object returnValue() { return returnValue; }
        public Object getReturnValue() { return returnValue; }
        public void setReturnValue(Object value) {
            returnPresent = true;
            returnValue = value;
            if (callbackInfo instanceof CallbackInfoReturnable<?> info && !info.isCancelled()) {
                @SuppressWarnings("unchecked") CallbackInfoReturnable<Object> mutable =
                    (CallbackInfoReturnable<Object>) info;
                mutable.setReturnValue(value);
            }
        }
        public boolean hasValue() { return valuePresent; }
        public Object value() { return value; }
        public Object getValue() { return value; }
        public void setValue(Object value) { valuePresent = true; this.value = value; }
        public boolean hasConstant() { return constantPresent; }
        public Object constant() { return constant; }
        public Object getConstant() { return constant; }
        public void setConstant(Object value) { constantPresent = true; valuePresent = true; constant = value; this.value = value; }
        public boolean hasVariable() { return variablePresent; }
        public Object variable() { return variable; }
        public Object getVariable() { return variable; }
        public void setVariable(Object value) { variablePresent = true; valuePresent = true; variable = value; this.value = value; }
        public Object redirectValue() { return redirectValue; }
        public Object getRedirectValue() { return redirectValue; }
        public boolean isHandled() { return handled; }
        public boolean isCancelled() { return cancelled || callbackInfo.isCancelled(); }
        public CallbackInfo callbackInfo() { return callbackInfo; }
        public CallbackInfo getCallbackInfo() { return callbackInfo; }
        public int invokedHandlers() { return invokedHandlers; }
        public List<String> invokedHandlersNames() { return List.copyOf(invoked); }
        public void setArgument(int index, Object value) { arguments[index] = value; }

        private void invoked(Handler handler) {
            handled = true;
            invokedHandlers++;
            invoked.add(handler.plan.handlerMethodName());
        }

        private void redirect(Object value) { redirectValue = value; }

        private void cancel(CallbackInfo local) {
            cancelled = true;
            if (local instanceof CallbackInfoReturnable<?> localReturn &&
                callbackInfo instanceof CallbackInfoReturnable<?>) {
                returnPresent = true;
                returnValue = localReturn.getReturnValue();
                @SuppressWarnings("unchecked") CallbackInfoReturnable<Object> outer =
                    (CallbackInfoReturnable<Object>) callbackInfo;
                if (!outer.isCancelled()) outer.setReturnValue(localReturn.getReturnValue());
            } else if (!callbackInfo.isCancelled()) {
                callbackInfo.cancel();
            }
        }
    }

    /** Remove all loaded and pre-definition metadata. */
    public static void clear() {
        synchronized (LOCK) {
            HANDLERS.clear();
            OVERWRITES.clear();
            MIXIN_PLANS.clear();
            PLAN_KEYS.clear();
            OPAQUE_CLASS_BYTES.clear();
            WARNINGS.clear();
            REGISTRATION_SEQUENCE.set(0L);
        }
    }

    public static List<String> warnings() {
        synchronized (LOCK) { return List.copyOf(WARNINGS); }
    }

    /** Begin one transformed target-method invocation. Use try-with-resources. */
    public static ExecutionToken beginExecution(Object target, String owner, String method,
                                                 String descriptor) {
        ExecutionFrame frame = new ExecutionFrame(target, new MethodKey(owner, method, descriptor));
        EXECUTION_FRAMES.get().push(frame);
        return new ExecutionToken(frame);
    }

    public static ExecutionToken beginExecution(Object target, String method) {
        return beginExecution(target, ownerOf(target), method, "*");
    }

    public static ExecutionToken beginExecution(MethodKey method, Object target) {
        if (method == null) throw new NullPointerException("method");
        return beginExecution(target, method.owner(), method.name(), method.descriptor());
    }

    /** Alias intended for generated transformed bytecode. */
    public static ExecutionToken beginTransformedExecution(Object target, String owner,
                                                            String method, String descriptor) {
        return beginExecution(target, owner, method, descriptor);
    }

    public static boolean isTransformedExecutionActive(Object target, String owner,
                                                        String method, String descriptor) {
        InvocationSite site = InvocationSite.builder(target, method).owner(owner)
            .descriptor(descriptor).at("HEAD").build();
        return activeFrame(site) != null;
    }

    /** True when metadata says the method has a Mixin-generated execution path. */
    public static boolean isTransformedHook(String owner, String method, String descriptor) {
        return hasTransformPlan(owner, method, descriptor);
    }

    public static boolean isTransformedHook(MethodKey method) {
        return method != null && isTransformedHook(method.owner(), method.name(), method.descriptor());
    }

    public static boolean isTransformedHook(Object target, String method, String descriptor) {
        if (target == null) return false;
        for (String owner : ownerCandidates(target, ""))
            if (isTransformedHook(owner, method, descriptor)) return true;
        return false;
    }

    public static boolean shouldUseJvmPath(String owner, String method, String descriptor) {
        return isTransformedHook(owner, method, descriptor);
    }

    /**
     * Claim a direct handler invocation. This is for generated code that calls
     * a normal mod method itself instead of going through {@link #dispatch}.
     */
    public static boolean claimHandler(HandlerPlan plan, InvocationSite site) {
        ExecutionFrame frame = activeFrame(site);
        return frame == null || frame.claim(handlerExecutionKey(plan, site));
    }

    public static boolean isHandlerClaimed(HandlerPlan plan, InvocationSite site) {
        ExecutionFrame frame = activeFrame(site);
        return frame != null && frame.isClaimed(handlerExecutionKey(plan, site));
    }

    /** Create a result object for a transformer that invokes a handler directly. */
    public static DispatchResult newDispatchResult(InvocationSite site) {
        return new DispatchResult(site, site != null && site.hasReturnValue());
    }

    /** Convert a callback invoked directly by generated bytecode into a cached result. */
    public static DispatchResult resultFromCallback(InvocationSite site, CallbackInfo callback) {
        if (site == null) throw new NullPointerException("site");
        if (callback == null) throw new NullPointerException("callback");
        DispatchResult result = new DispatchResult(site, callback instanceof CallbackInfoReturnable<?>);
        result.callbackInfo = callback;
        if (callback instanceof CallbackInfoReturnable<?> returnable) {
            result.returnPresent = true;
            result.returnValue = returnable.getReturnValue();
        }
        if (callback.isCancelled()) result.cancel(callback);
        return result;
    }

    /** Cache a direct-call result so a nested manual hook can replay cancellation/value. */
    public static void recordExecutionResult(ExecutionToken token, InvocationSite site,
                                             DispatchResult result) {
        if (token == null) throw new NullPointerException("token");
        token.record(site, result);
    }

    /**
     * Register only a class name. No class loader is touched; this is safe
     * before Knot defines either side of the transformation.
     */
    public static void registerMixinClassName(String mixinClassName, String... targetOwners) {
        publishMixinPlan(new MixinPlan(mixinClassName, 1000, targetOwners));
    }

    public static void registerMixinClassName(String mixinClassName, int priority,
                                              String... targetOwners) {
        publishMixinPlan(new MixinPlan(mixinClassName, priority, targetOwners));
    }

    /** Register bytes opaquely without parsing or defining them. */
    public static void registerMixinClassBytes(String mixinClassName, byte[] classBytes,
                                               String... targetOwners) {
        registerMixinClassName(mixinClassName, targetOwners);
        synchronized (LOCK) {
            OPAQUE_CLASS_BYTES.put(mixinClassName == null ? "" : mixinClassName,
                                   classBytes == null ? 0 : classBytes.length);
        }
    }

    public static List<String> registeredMixinClassNames() {
        synchronized (LOCK) {
            List<String> result = new ArrayList<>();
            for (MixinPlan plan : MIXIN_PLANS)
                if (!result.contains(plan.mixinClassName())) result.add(plan.mixinClassName());
            return List.copyOf(result);
        }
    }

    /** Publish metadata without loading any class or referencing a transformer. */
    public static void publishMixinPlan(MixinPlan plan) {
        if (plan == null) return;
        synchronized (LOCK) {
            List<HandlerPlan> expanded = new ArrayList<>();
            for (HandlerPlan handler : plan.handlers()) {
                if (!handler.targetOwner().isEmpty()) expanded.add(handler);
                else for (String owner : plan.targetOwners()) expanded.add(handler.withOwner(owner));
            }
            MixinPlan normalized = new MixinPlan(plan.mixinClassName(), plan.priority(),
                                                 plan.targetOwners().toArray(String[]::new));
            for (HandlerPlan handler : expanded) normalized.addHandler(handler);
            addPlanMetadata(normalized);
            for (HandlerPlan handler : expanded) registerPlanHandler(handler, null);
        }
    }

    public static void registerMixinPlan(MixinPlan plan) { publishMixinPlan(plan); }

    /** Publish a single transformer-produced handler plan. */
    public static void publishHandlerPlan(HandlerPlan plan) {
        if (plan == null) return;
        synchronized (LOCK) {
            MixinPlan group = new MixinPlan(plan.mixinClassName(), plan.priority(), plan.targetOwner());
            group.addHandler(plan);
            addPlanMetadata(group);
            registerPlanHandler(plan, null);
        }
    }

    public static void publishHandlerPlans(Iterable<HandlerPlan> plans) {
        if (plans == null) return;
        for (HandlerPlan plan : plans) publishHandlerPlan(plan);
    }

    /**
     * Exact-owner metadata lookup for a target that may not be defined yet.
     * The transformer can call this once for the class and each hierarchy
     * owner it is transforming.
     */
    public static List<HandlerPlan> handlerPlansForTarget(String owner, String method,
                                                           String descriptor) {
        String normalizedOwner = normalizeOwner(owner);
        List<HandlerPlan> result = new ArrayList<>();
        synchronized (LOCK) {
            for (MixinPlan group : MIXIN_PLANS) {
                for (HandlerPlan handler : group.handlers()) {
                    if (normalizedOwner.equals(handler.targetOwner()) &&
                        method != null && method.equals(handler.targetMethod()) &&
                        descriptorMatches(handler.targetDescriptor(), descriptor)) result.add(handler);
                }
            }
        }
        result.sort(PLAN_COMPARATOR);
        return List.copyOf(result);
    }

    public static List<HandlerPlan> getHandlerPlans(String owner, String method,
                                                    String descriptor) {
        return handlerPlansForTarget(owner, method, descriptor);
    }

    /** Hierarchy-aware metadata lookup when the target class is already defined. */
    public static List<HandlerPlan> handlerPlansForTarget(Class<?> target, String method,
                                                          String descriptor) {
        if (target == null) return List.of();
        List<HandlerPlan> result = new ArrayList<>();
        for (String owner : ownerCandidates(target, ""))
            result.addAll(handlerPlansForTarget(owner, method, descriptor));
        result.sort(PLAN_COMPARATOR);
        return List.copyOf(result);
    }

    public static boolean hasTransformPlan(String owner, String method, String descriptor) {
        return !handlerPlansForTarget(owner, method, descriptor).isEmpty();
    }

    /** Bind the loaded class to metadata; additive and safe after pre-registration. */
    public static void bindMixinClass(Class<?> mixinClass) { registerMixinClass(mixinClass); }

    /** Register annotation-bearing loaded mixin methods for manual dispatch. */
    public static void registerMixinClass(Class<?> mixinClass) {
        if (mixinClass == null) return;
        Mixin mixin = mixinClass.getAnnotation(Mixin.class);
        if (mixin == null) return;
        List<String> targets = new ArrayList<>();
        for (Class<?> target : mixin.value()) if (target != null) targets.add(target.getName());
        for (String target : mixin.targets())
            if (target != null && !target.isEmpty()) targets.add(normalizeOwner(target));
        if (targets.isEmpty()) {
            warn("mixin has no target: " + mixinClass.getName());
            return;
        }

        MixinPlan plan = new MixinPlan(mixinClass.getName(), mixin.priority(), targets.toArray(String[]::new));
        Method[] methods = mixinClass.getDeclaredMethods();
        Arrays.sort(methods, Comparator.comparing(Method::getName).thenComparing(MixinHooks::descriptor));
        for (Method method : methods) collectAnnotatedPlans(plan, method, targets);
        for (Field field : mixinClass.getDeclaredFields()) collectShadowFieldPlan(plan, field, targets);
        publishMixinPlan(plan);
        for (HandlerPlan handler : plan.handlers()) {
            Method method = findDeclaredMethodByDescriptor(mixinClass, handler.handlerMethodName(),
                                                           handler.handlerDescriptor());
            if (method != null) bindRuntimeHandler(handler, method);
        }
    }

    private static void collectAnnotatedPlans(MixinPlan plan, Method method,
                                               List<String> defaultTargets) {
        Inject inject = method.getAnnotation(Inject.class);
        if (inject != null) {
            if (inject.method().length == 0) warn("@Inject has no method: " + method);
            for (String methodSpec : inject.method()) {
                TargetMethod target = parseTargetMethod(methodSpec, first(defaultTargets));
                List<String> owners = target.owner.isEmpty() ? defaultTargets : List.of(target.owner);
                for (String owner : owners) for (At point : inject.at())
                    plan.addHandler(new HandlerPlan(plan.mixinClassName(), owner, target.name,
                        target.descriptor, HandlerType.INJECT, AtSpec.of(point), method.getName(),
                        descriptor(method), plan.priority(), inject.order(), inject.cancellable(),
                        inject.locals(), inject.require(), inject.expect(), -1, -1,
                        slices(inject.slice()), List.of()));
            }
        }

        Redirect redirect = method.getAnnotation(Redirect.class);
        if (redirect != null)
            collectAdapterPlans(plan, method, redirect.method(), AtSpec.of(redirect.at()), HandlerType.REDIRECT,
                -1, -1, false, redirect.require(), redirect.expect(), redirect.allow(), slices(redirect.slice()),
                List.of(), defaultTargets);

        ModifyArg modifyArg = method.getAnnotation(ModifyArg.class);
        if (modifyArg != null)
            collectAdapterPlans(plan, method, modifyArg.method(), AtSpec.of(modifyArg.at()), HandlerType.MODIFY_ARG,
                modifyArg.index(), -1, false, modifyArg.require(), modifyArg.expect(), modifyArg.allow(),
                slices(modifyArg.slice()), List.of(), defaultTargets);

        ModifyVariable modifyVariable = method.getAnnotation(ModifyVariable.class);
        if (modifyVariable != null)
            collectAdapterPlans(plan, method, modifyVariable.method(), AtSpec.of(modifyVariable.at()),
                HandlerType.MODIFY_VARIABLE, modifyVariable.index(), modifyVariable.ordinal(), modifyVariable.argsOnly(),
                modifyVariable.require(), modifyVariable.expect(), modifyVariable.allow(),
                slices(modifyVariable.slice()), List.of(), defaultTargets);

        ModifyConstant modifyConstant = method.getAnnotation(ModifyConstant.class);
        if (modifyConstant != null) {
            List<ConstantSpec> constants = new ArrayList<>();
            for (Constant constant : modifyConstant.constant()) constants.add(ConstantSpec.of(constant));
            collectAdapterPlans(plan, method, modifyConstant.method(), new AtSpec("CONSTANT"),
                HandlerType.MODIFY_CONSTANT, -1, -1, false, modifyConstant.require(), modifyConstant.expect(),
                modifyConstant.allow(), slices(modifyConstant.slice()), constants, defaultTargets);
        }

        Overwrite overwrite = method.getAnnotation(Overwrite.class);
        if (overwrite != null) for (String owner : defaultTargets)
            plan.addHandler(new HandlerPlan(plan.mixinClassName(), owner, method.getName(), descriptor(method),
                HandlerType.OVERWRITE, new AtSpec("OVERWRITE"), method.getName(), descriptor(method),
                plan.priority(), 1000, false, LocalCapture.NO_CAPTURE, overwrite.require(), 1, -1,
                -1, List.of(), List.of()));

        Accessor accessor = method.getAnnotation(Accessor.class);
        Invoker invoker = method.getAnnotation(Invoker.class);
        Shadow shadow = method.getAnnotation(Shadow.class);
        if (accessor != null || invoker != null || shadow != null) {
            HandlerType type = accessor != null ? HandlerType.ACCESSOR
                : invoker != null ? HandlerType.INVOKER : HandlerType.SHADOW;
            String targetMethod = type == HandlerType.ACCESSOR ? accessorName(method)
                : type == HandlerType.INVOKER ? invokerName(method) : shadowName(method.getName(), shadow.prefix());
            for (String owner : defaultTargets)
                plan.addHandler(new HandlerPlan(plan.mixinClassName(), owner, targetMethod, "*", type,
                    new AtSpec("HEAD"), method.getName(), descriptor(method), plan.priority(), 1000));
        }
    }

    private static void collectShadowFieldPlan(MixinPlan plan, Field field, List<String> targets) {
        Shadow shadow = field.getAnnotation(Shadow.class);
        if (shadow == null) return;
        String name = shadow.aliases().length == 0 ? shadowName(field.getName(), shadow.prefix()) : shadow.aliases()[0];
        for (String owner : targets)
            plan.addHandler(new HandlerPlan(plan.mixinClassName(), owner, name, descriptor(field.getType()),
                HandlerType.SHADOW, new AtSpec("HEAD"), field.getName(), descriptor(field.getType()),
                plan.priority(), 1000));
    }

    private static void collectAdapterPlans(MixinPlan plan, Method method, String[] methodSpecs,
                                             AtSpec point, HandlerType type, int argumentIndex,
                                             int ordinal, boolean argsOnly, int require, int expect, int allow,
                                             List<SliceSpec> slices, List<ConstantSpec> constants,
                                             List<String> defaultTargets) {
        if (methodSpecs.length == 0) warn("@" + type + " has no method: " + method);
        for (String methodSpec : methodSpecs) {
            TargetMethod target = parseTargetMethod(methodSpec, first(defaultTargets));
            List<String> owners = target.owner.isEmpty() ? defaultTargets : List.of(target.owner);
            AtSpec selected = point;
            if (type == HandlerType.MODIFY_CONSTANT)
                selected = new AtSpec("CONSTANT", point.target(), point.ordinal(), point.opcode(),
                                      point.shift(), point.args(), point.id());
            if (type == HandlerType.MODIFY_VARIABLE && ordinal >= 0)
                selected = new AtSpec(point.value(), point.target(), ordinal, point.opcode(),
                                      point.shift(), point.args(), point.id());
            for (String owner : owners)
                plan.addHandler(new HandlerPlan(plan.mixinClassName(), owner, target.name,
                    target.descriptor, type, selected, method.getName(), descriptor(method),
                    plan.priority(), 1000, false, LocalCapture.NO_CAPTURE, require, expect, allow,
                    argumentIndex, argsOnly, slices, constants));
        }
    }

    private static List<SliceSpec> slices(Slice[] slices) {
        List<SliceSpec> result = new ArrayList<>();
        if (slices != null) for (Slice slice : slices) result.add(SliceSpec.of(slice));
        return result;
    }

    private static void addPlanMetadata(MixinPlan plan) {
        String groupKey = plan.mixinClassName() + "|" + plan.priority() + "|" + plan.targetOwners();
        for (MixinPlan existing : MIXIN_PLANS) {
            String existingKey = existing.mixinClassName() + "|" + existing.priority() + "|" + existing.targetOwners();
            if (!existingKey.equals(groupKey)) continue;
            for (HandlerPlan handler : plan.handlers()) {
                boolean present = false;
                for (HandlerPlan current : existing.handlers())
                    if (current.key().equals(handler.key())) { present = true; break; }
                if (!present) existing.addHandler(handler);
            }
            PLAN_KEYS.add(groupKey);
            return;
        }
        if (PLAN_KEYS.add(groupKey)) MIXIN_PLANS.add(plan);
    }

    private static void registerPlanHandler(HandlerPlan plan, Method method) {
        if (plan == null || plan.targetOwner().isEmpty() || plan.targetMethod().isEmpty()) return;
        if (method != null) bindRuntimeHandler(plan, method);
        if (plan.type() == HandlerType.INJECT || plan.type() == HandlerType.REDIRECT ||
            plan.type() == HandlerType.MODIFY_ARG || plan.type() == HandlerType.MODIFY_CONSTANT ||
            plan.type() == HandlerType.MODIFY_VARIABLE || plan.type() == HandlerType.OVERWRITE)
            registerTransformed(plan);
    }

    private static void bindRuntimeHandler(HandlerPlan plan, Method method) {
        try { method.setAccessible(true); }
        catch (RuntimeException failure) {
            warn("cannot access mixin handler " + method + ": " + failure);
            return;
        }
        Handler handler = new Handler(plan, method);
        Map<String, List<Handler>> map = plan.type() == HandlerType.OVERWRITE ? OVERWRITES : HANDLERS;
        synchronized (LOCK) {
            String key = ownerMethodKey(plan.targetOwner(), plan.targetMethod());
            List<Handler> handlers = map.computeIfAbsent(key, ignored -> new ArrayList<>());
            for (Handler existing : handlers) if (existing.sameHandler(handler)) return;
            handlers.add(handler);
            handlers.sort(HANDLER_COMPARATOR);
        }
    }

    private static void registerTransformed(HandlerPlan plan) {
        try {
            NativeBridge.nativeRegisterTransformedMethod(plan.targetOwner().replace('.', '/'),
                                                         plan.targetMethod(), plan.targetDescriptor());
        } catch (Throwable ignored) {
            // Standalone Java direct tests may not have the JNI library.
        }
    }

    /** Existing manual HEAD hook. */
    public static CallbackInfo invokeHead(Object target, String method, Object... args) {
        return dispatch(InvocationSite.builder(target, method).at("HEAD").arguments(args).build()).callbackInfo();
    }

    /** Existing manual HEAD hook with a return value. */
    public static <T> CallbackInfoReturnable<T> invokeHeadReturn(Object target, String method,
                                                                  T value, Object... args) {
        DispatchResult result = dispatch(InvocationSite.builder(target, method).at("HEAD")
            .returnValue(value).arguments(args).build());
        return returnable(result, method + ":HEAD", value);
    }

    public static <T> CallbackInfoReturnable<T> invokeReturn(Object target, String method,
                                                               T value, Object... args) {
        DispatchResult result = dispatch(InvocationSite.builder(target, method).at("RETURN")
            .returnValue(value).arguments(args).build());
        return returnable(result, method + ":RETURN", value);
    }

    public static CallbackInfo invokeTail(Object target, String method, Object... args) {
        return dispatch(InvocationSite.builder(target, method).at("TAIL").arguments(args).build()).callbackInfo();
    }

    public static <T> CallbackInfoReturnable<T> invokeTailReturn(Object target, String method,
                                                                  T value, Object... args) {
        DispatchResult result = dispatch(InvocationSite.builder(target, method).at("TAIL")
            .returnValue(value).arguments(args).build());
        return returnable(result, method + ":TAIL", value);
    }

    /**
     * Tail hook carrying the verifier-visible locals at the injection site.
     * This is the manual/shadow equivalent of a structural TAIL injection
     * with {@code LocalCapture}; keeping the values explicit also covers a
     * target class that was defined before its mixin configuration arrived.
     */
    public static <T> CallbackInfoReturnable<T> invokeTailReturnWithLocals(
            Object target, String method, T value, Object[] locals, Object... args) {
        InvocationSite.Builder builder = InvocationSite.builder(target, method).at("TAIL")
            .returnValue(value).arguments(args);
        if (locals != null) builder.locals(locals);
        return returnable(dispatch(builder.build()), method + ":TAIL", value);
    }

    /** Invoke the highest-priority overwrite, returning null when none exists. */
    public static <T> T invokeOverwrite(Object target, String method, Object... args) {
        return invokeOverwriteWithDescriptor(target, method, "*", args);
    }

    public static <T> T invokeOverwriteWithDescriptor(Object target, String method,
                                                      String descriptor, Object... args) {
        DispatchResult result = invokeOverwriteResult(target, method, descriptor, args);
        if (!result.isHandled()) return null;
        @SuppressWarnings("unchecked") T value = (T) result.getValue();
        return value;
    }

    public static DispatchResult invokeOverwriteResult(Object target, String method,
                                                        String descriptor, Object... args) {
        InvocationSite site = InvocationSite.builder(target, method).descriptor(descriptor)
            .at("OVERWRITE").arguments(args).build();
        if (isStructurallyTransformed(site)) return new DispatchResult(site, false);
        ExecutionFrame frame = activeFrame(site);
        if (frame != null) {
            DispatchResult cached = frame.result(site.marker());
            if (cached != null) return cached;
        }
        List<Handler> handlers = lookup(OVERWRITES, site, HandlerType.OVERWRITE);
        DispatchResult result = new DispatchResult(site, false);
        if (frame != null) frame.record(site.marker(), result);
        if (handlers.isEmpty()) return result;
        Handler handler = handlers.get(0);
        if (frame != null && !frame.claim(handlerExecutionKey(handler.plan, site))) return result;
        try {
            Object value = invokeHandlerChecked(handler, target, args);
            result.invoked(handler);
            result.setValue(value);
            return result;
        } catch (InvocationTargetException failure) {
            throw sneaky(failure.getCause());
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("mixin overwrite failed: " + handler.method, failure);
        }
    }

    /** Dispatch any generated/manual instruction site. */
    public static DispatchResult dispatch(InvocationSite site) {
        if (site == null) throw new NullPointerException("site");
        // A pre-definition class-file transform is the authoritative path.
        // The shadow classes still contain manual hook calls for standalone
        // fallback mode, but running both paths would invoke every handler
        // twice and cannot provide structural locals to @Inject.
        if (isStructurallyTransformed(site)) return new DispatchResult(site, site.hasReturnValue());
        ExecutionFrame frame = activeFrame(site);
        if (frame != null) {
            DispatchResult cached = frame.result(site.marker());
            if (cached != null) return cached;
        }
        List<Handler> handlers = lookup(HANDLERS, site, null);
        boolean returnable = site.hasReturnValue();
        for (Handler handler : handlers) {
            if (handler.plan.type() == HandlerType.INJECT && expectsReturnable(handler.method)) {
                returnable = true;
                break;
            }
        }
        DispatchResult result = new DispatchResult(site, returnable);
        if (frame != null) frame.record(site.marker(), result);
        for (Handler handler : handlers) {
            if (!handler.plan.matchesSite(site)) continue;
            if (frame != null && !frame.claim(handlerExecutionKey(handler.plan, site))) continue;
            try {
                dispatchOne(handler, result);
                result.invoked(handler);
                if (result.isCancelled() && handler.plan.type() == HandlerType.INJECT) break;
            } catch (InvocationTargetException failure) {
                throw sneaky(failure.getCause());
            } catch (ReflectiveOperationException failure) {
                throw new IllegalStateException("mixin handler failed: " + handler.method, failure);
            }
        }
        return result;
    }

    public static DispatchResult invokeAt(InvocationSite site) { return dispatch(site); }

    public static DispatchResult invokeAt(Object target, String method, String descriptor,
                                          AtSpec at, Object... args) {
        return dispatch(InvocationSite.builder(target, method).descriptor(descriptor).at(at)
            .arguments(args).build());
    }

    /** Convenience API for a transformer with explicit topology metadata. */
    public static DispatchResult invokePoint(Object target, String method, String descriptor,
                                             String point, String siteTarget, int ordinal,
                                             int opcode, Object[] args, Object[] locals) {
        AtSpec at = new AtSpec(point, siteTarget, ordinal, opcode);
        InvocationSite.Builder builder = InvocationSite.builder(target, method).descriptor(descriptor)
            .at(at).arguments(args);
        if (locals != null) builder.locals(locals);
        builder.ordinal(ordinal).opcode(opcode);
        return dispatch(builder.build());
    }

    /** Convenience API carrying original args, locals, return, and receiver. */
    public static DispatchResult invokePoint(Object target, String owner, String method,
                                             String descriptor, String point, String siteTarget,
                                             int ordinal, int opcode, Object[] args, Object[] locals,
                                             Object returnValue, boolean hasReturnValue,
                                             Object receiver) {
        AtSpec at = new AtSpec(point, siteTarget, ordinal, opcode);
        InvocationSite.Builder builder = InvocationSite.builder(target, method).owner(owner)
            .descriptor(descriptor).at(at).arguments(args);
        if (locals != null) builder.locals(locals);
        if (hasReturnValue) builder.returnValue(returnValue);
        if (receiver != null) builder.receiver(receiver);
        builder.ordinal(ordinal).opcode(opcode);
        return dispatch(builder.build());
    }

    private static void dispatchOne(Handler handler, DispatchResult result)
        throws ReflectiveOperationException {
        switch (handler.plan.type()) {
            case INJECT -> dispatchInject(handler, result);
            case REDIRECT -> dispatchRedirect(handler, result);
            case MODIFY_ARG -> dispatchModifyArg(handler, result);
            case MODIFY_CONSTANT -> dispatchModifyConstant(handler, result);
            case MODIFY_VARIABLE -> dispatchModifyVariable(handler, result);
            case ACCESSOR, INVOKER, SHADOW, OVERWRITE -> { }
        }
    }

    private static boolean isStructurallyTransformed(InvocationSite site) {
        String owner = site.owner();
        if (owner != null && !owner.isEmpty() && MixinDispatch.isAnyTransformed(owner, site.method())) return true;
        Object target = site.target();
        if (target != null) {
            for (String candidate : ownerCandidates(target, ""))
                if (MixinDispatch.isAnyTransformed(candidate, site.method())) return true;
        }
        return false;
    }

    private static void dispatchInject(Handler handler, DispatchResult result)
        throws ReflectiveOperationException {
        CallbackInfo callback = newCallback(handler.plan, result.site(), result.returnValue(),
                                             result.hasReturnValue());
        Object[] callArgs = resolveCallbackArguments(handler.plan, handler.method,
                                                     result.arguments(), result.locals(), callback);
        if (callArgs == null) return;
        invokeHandlerChecked(handler, result.site().target(), callArgs);
        if (callback.isCancelled()) result.cancel(callback);
    }

    private static void dispatchRedirect(Handler handler, DispatchResult result)
        throws ReflectiveOperationException {
        Object value = invokeHandlerChecked(handler, result.site().target(),
                                            resolveRedirectArguments(handler.method, result.site()));
        result.redirect(value);
        result.setValue(value);
    }

    private static void dispatchModifyArg(Handler handler, DispatchResult result)
        throws ReflectiveOperationException {
        Object[] args = result.arguments();
        if (args.length == 0) return;
        int index = handler.plan.argumentIndex() < 0 ? 0 : handler.plan.argumentIndex();
        if (index < 0 || index >= args.length) return;
        Object[] callArgs = handler.method.getParameterCount() == 1
            ? new Object[] { args[index] } : resolveRedirectArguments(handler.method, result.site());
        args[index] = invokeHandlerChecked(handler, result.site().target(), callArgs);
    }

    private static void dispatchModifyConstant(Handler handler, DispatchResult result)
        throws ReflectiveOperationException {
        Object value = result.hasConstant() ? result.constant() : result.value();
        if (!result.hasConstant() && !result.hasValue()) return;
        Object replacement = invokeHandlerChecked(handler, result.site().target(), new Object[] { value });
        result.setConstant(replacement);
    }

    private static void dispatchModifyVariable(Handler handler, DispatchResult result)
        throws ReflectiveOperationException {
        Object value;
        if (result.hasVariable()) value = result.variable();
        else if (handler.plan.argumentIndex() >= 0 && handler.plan.argumentIndex() < result.locals().length)
            value = result.locals()[handler.plan.argumentIndex()];
        else if (handler.plan.at().ordinal() >= 0 && handler.plan.at().ordinal() < result.locals().length)
            value = result.locals()[handler.plan.at().ordinal()];
        else if (result.hasValue()) value = result.value();
        else return;
        Object replacement = invokeHandlerChecked(handler, result.site().target(), new Object[] { value });
        result.setVariable(replacement);
        int index = handler.plan.argumentIndex();
        if (index >= 0 && index < result.locals().length) result.locals()[index] = replacement;
    }

    /** Create the callback object the transformed handler must receive. */
    public static CallbackInfo newCallback(HandlerPlan plan, InvocationSite site,
                                           Object returnValue, boolean hasReturnValue) {
        boolean returnable = hasReturnValue;
        if (plan != null && plan.handlerDescriptor().contains("CallbackInfoReturnable")) returnable = true;
        return returnable
            ? new CallbackInfoReturnable<>(site.method() + ":" + site.at().value(),
                                           plan == null || plan.cancellable(), returnValue)
            : new CallbackInfo(site.method() + ":" + site.at().value(),
                               plan == null || plan.cancellable());
    }

    /**
     * Assemble the exact argument vector for an Inject handler: original
     * method arguments, captured locals, then CallbackInfo where present.
     */
    public static Object[] resolveCallbackArguments(HandlerPlan plan, Method handlerMethod,
                                                    Object[] originalArgs, Object[] locals,
                                                    CallbackInfo callback) {
        Class<?>[] types = handlerMethod.getParameterTypes();
        int callbackIndex = callbackParameterIndex(types, callback);
        Object[] candidates = concat(originalArgs, locals);
        Object[] output = new Object[types.length];
        int candidateIndex = 0;
        for (int i = 0; i < types.length; ++i) {
            if (i == callbackIndex) {
                output[i] = callback;
                continue;
            }
            if (candidateIndex >= candidates.length) return missingLocalArguments(plan, handlerMethod);
            Object candidate = candidates[candidateIndex++];
            if (!accepts(types[i], candidate)) return missingLocalArguments(plan, handlerMethod);
            output[i] = candidate;
        }
        return output;
    }

    /** Build the receiver/argument vector expected by Redirect/ModifyArg. */
    public static Object[] resolveRedirectArguments(Method handlerMethod, InvocationSite site) {
        Object[] args = site.arguments();
        Object receiver = site.receiver();
        InjectionPoint point = site.at().point();
        if (receiver == null && (point == InjectionPoint.INVOKE || point == InjectionPoint.FIELD))
            receiver = site.target();
        if (receiver == null || handlerMethod.getParameterCount() == args.length) return args.clone();
        if (handlerMethod.getParameterCount() == args.length + 1) {
            Object[] output = new Object[args.length + 1];
            output[0] = receiver;
            System.arraycopy(args, 0, output, 1, args.length);
            return output;
        }
        return args.clone();
    }

    /** Resolve a handler from the ordinary mod classloader by JVM descriptor. */
    public static Method resolveHandlerMethod(String mixinClassName, String handlerName,
                                              String handlerDescriptor, ClassLoader loader) {
        if (loader == null) loader = MixinHooks.class.getClassLoader();
        try {
            Class<?> mixinClass = Class.forName(normalizeOwner(mixinClassName), true, loader);
            return resolveHandlerMethod(mixinClass, handlerName, handlerDescriptor);
        } catch (ClassNotFoundException failure) {
            throw new IllegalStateException("mixin handler class not found: " + mixinClassName, failure);
        }
    }

    public static Method resolveHandlerMethod(Class<?> mixinClass, String handlerName,
                                              String handlerDescriptor) {
        Method method = findDeclaredMethodByDescriptor(mixinClass, handlerName, handlerDescriptor);
        if (method == null)
            throw new IllegalStateException("mixin handler not found: " + mixinClass.getName() + "." + handlerName + handlerDescriptor);
        try { method.setAccessible(true); }
        catch (RuntimeException failure) { throw new IllegalStateException("mixin handler inaccessible: " + method, failure); }
        return method;
    }

    /**
     * Call a handler using the same receiver convention as invokevirtual for
     * instance mixins and invokestatic for static handlers. This is the direct
     * Java-side equivalent used by manual tests; transformed bytecode may call
     * the resolved method directly with the same argument vector.
     */
    public static Object callHandler(Method handlerMethod, Object targetInstance,
                                     Object... args) {
        if (handlerMethod == null) throw new NullPointerException("handlerMethod");
        try {
            Object receiver = Modifier.isStatic(handlerMethod.getModifiers()) ? null : targetInstance;
            if (receiver == null && !Modifier.isStatic(handlerMethod.getModifiers())) {
                Constructor<?> constructor = handlerMethod.getDeclaringClass().getDeclaredConstructor();
                constructor.setAccessible(true);
                receiver = constructor.newInstance();
            }
            handlerMethod.setAccessible(true);
            return handlerMethod.invoke(receiver, args == null ? new Object[0] : args);
        } catch (InvocationTargetException failure) {
            throw sneaky(failure.getCause());
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("mixin handler call failed: " + handlerMethod, failure);
        }
    }

    public static Object callHandler(String mixinClassName, String handlerName,
                                     String handlerDescriptor, ClassLoader loader,
                                     Object targetInstance, Object... args) {
        return callHandler(resolveHandlerMethod(mixinClassName, handlerName, handlerDescriptor, loader),
                           targetInstance, args);
    }

    private static Object invokeHandlerChecked(Handler handler, Object target, Object[] args)
        throws ReflectiveOperationException {
        Object receiver = handler.receiverFor(target);
        return handler.method.invoke(receiver, args == null ? new Object[0] : args);
    }

    /** Execute an @Accessor method against the transformed target instance. */
    public static Object invokeAccessor(Object target, Method accessorMethod, Object... args) {
        if (accessorMethod == null || !accessorMethod.isAnnotationPresent(Accessor.class))
            throw new IllegalArgumentException("method is not @Accessor: " + accessorMethod);
        Field field = resolveField(targetClass(target), accessorName(accessorMethod), List.of());
        try {
            field.setAccessible(true);
            Object receiver = target instanceof Class<?> ? null : target;
            if (args == null || args.length == 0) return field.get(receiver);
            field.set(receiver, args[0]);
            return null;
        } catch (IllegalAccessException failure) {
            throw new IllegalStateException("accessor failed: " + accessorMethod, failure);
        }
    }

    public static Object invokeAccessor(Object target, String accessorMethodName,
                                        Class<?> mixinClass, Object... args) {
        return invokeAccessor(target, resolveHandlerMethod(mixinClass, accessorMethodName, "*"), args);
    }

    public static Object readAccessor(Object target, Method accessorMethod) {
        return invokeAccessor(target, accessorMethod);
    }

    public static void writeAccessor(Object target, Method accessorMethod, Object value) {
        invokeAccessor(target, accessorMethod, value);
    }

    /** Execute an @Invoker method against the transformed target instance. */
    public static Object invokeInvoker(Object target, Method invokerMethod, Object... args) {
        if (invokerMethod == null || !invokerMethod.isAnnotationPresent(Invoker.class))
            throw new IllegalArgumentException("method is not @Invoker: " + invokerMethod);
        Invoker annotation = invokerMethod.getAnnotation(Invoker.class);
        String specified = annotation.value().isEmpty() ? invokerName(invokerMethod) : annotation.value();
        TargetMethod targetMethod = parseTargetMethod(specified, targetClass(target).getName());
        Method method = resolveTargetMethod(targetClass(target), targetMethod.name, targetMethod.descriptor,
                                            args == null ? new Object[0] : args, invokerMethod.getReturnType());
        try {
            method.setAccessible(true);
            return method.invoke(target instanceof Class<?> ? null : target, args == null ? new Object[0] : args);
        } catch (InvocationTargetException failure) {
            throw sneaky(failure.getCause());
        } catch (IllegalAccessException failure) {
            throw new IllegalStateException("invoker failed: " + invokerMethod, failure);
        }
    }

    public static Object invokeInvoker(Object target, String invokerMethodName,
                                       Class<?> mixinClass, Object... args) {
        return invokeInvoker(target, resolveHandlerMethod(mixinClass, invokerMethodName, "*"), args);
    }

    /** Invoke an @Shadow method or access an @Shadow field on the target. */
    public static Object invokeShadow(Object target, Method shadowMethod, Object... args) {
        if (shadowMethod == null || !shadowMethod.isAnnotationPresent(Shadow.class))
            throw new IllegalArgumentException("method is not @Shadow: " + shadowMethod);
        Shadow annotation = shadowMethod.getAnnotation(Shadow.class);
        String name = annotation.aliases().length == 0
            ? shadowName(shadowMethod.getName(), annotation.prefix()) : annotation.aliases()[0];
        Object[] actualArgs = args == null ? new Object[0] : args;
        try {
            Method targetMethod = resolveTargetMethod(targetClass(target), name, descriptor(shadowMethod),
                                                     actualArgs, shadowMethod.getReturnType());
            targetMethod.setAccessible(true);
            return targetMethod.invoke(target instanceof Class<?> ? null : target, actualArgs);
        } catch (IllegalStateException missingMethod) {
            if (shadowMethod.getParameterCount() > 1)
                throw missingMethod;
            Field field = resolveField(targetClass(target), name, Arrays.asList(annotation.aliases()));
            try {
                field.setAccessible(true);
                Object receiver = target instanceof Class<?> ? null : target;
                if (actualArgs.length == 0) return field.get(receiver);
                field.set(receiver, actualArgs[0]);
                return null;
            } catch (IllegalAccessException failure) {
                throw new IllegalStateException("shadow field failed: " + shadowMethod, failure);
            }
        } catch (InvocationTargetException failure) {
            throw sneaky(failure.getCause());
        } catch (IllegalAccessException failure) {
            throw new IllegalStateException("shadow method failed: " + shadowMethod, failure);
        }
    }

    public static Object readShadow(Object target, Field shadowField) {
        if (shadowField == null || !shadowField.isAnnotationPresent(Shadow.class))
            throw new IllegalArgumentException("field is not @Shadow: " + shadowField);
        Shadow shadow = shadowField.getAnnotation(Shadow.class);
        String name = shadow.aliases().length == 0 ? shadowName(shadowField.getName(), shadow.prefix()) : shadow.aliases()[0];
        return readField(target, name, Arrays.asList(shadow.aliases()));
    }

    public static void writeShadow(Object target, Field shadowField, Object value) {
        if (shadowField == null || !shadowField.isAnnotationPresent(Shadow.class))
            throw new IllegalArgumentException("field is not @Shadow: " + shadowField);
        Shadow shadow = shadowField.getAnnotation(Shadow.class);
        String name = shadow.aliases().length == 0 ? shadowName(shadowField.getName(), shadow.prefix()) : shadow.aliases()[0];
        writeField(target, name, value, Arrays.asList(shadow.aliases()));
    }

    public static Object readShadow(Object target, String name) { return readField(target, name, List.of()); }
    public static void writeShadow(Object target, String name, Object value) { writeField(target, name, value, List.of()); }

    /** Explicit post-transformation entrance for Accessor/Invoker/Shadow. */
    public static Object invokeTransformedMember(Object target, Method generatedMethod,
                                                 Object... args) {
        if (generatedMethod.isAnnotationPresent(Accessor.class)) return invokeAccessor(target, generatedMethod, args);
        if (generatedMethod.isAnnotationPresent(Invoker.class)) return invokeInvoker(target, generatedMethod, args);
        if (generatedMethod.isAnnotationPresent(Shadow.class)) return invokeShadow(target, generatedMethod, args);
        throw new IllegalArgumentException("not an accessor/invoker/shadow method: " + generatedMethod);
    }

    public static Object invokeGeneratedMember(Object target, Method generatedMethod,
                                               Object... args) {
        return invokeTransformedMember(target, generatedMethod, args);
    }

    public static Method resolveTargetMethod(Class<?> owner, String name, String descriptor) {
        return resolveTargetMethod(owner, name, descriptor, new Object[0], null);
    }

    public static Method resolveTargetMethod(Object target, String name, String descriptor) {
        return resolveTargetMethod(targetClass(target), name, descriptor);
    }

    private static Method resolveTargetMethod(Class<?> owner, String name, String descriptor,
                                              Object[] args, Class<?> returnType) {
        List<Method> methods = methodsOf(owner);
        methods.sort(Comparator.comparing(Method::getName).thenComparing(MixinHooks::descriptor));
        for (Method method : methods) {
            if (!method.getName().equals(name) || !descriptorMatches(descriptor, descriptor(method))) continue;
            if (returnType != null && !returnType.isAssignableFrom(method.getReturnType()) &&
                !(returnType.isPrimitive() && box(returnType) == box(method.getReturnType()))) continue;
            if (args == null || compatibleParameters(method.getParameterTypes(), args)) return method;
        }
        throw new IllegalStateException("missing target method " + owner.getName() + "." + name + descriptor);
    }

    /** Resolve a descriptor without loading a target class. */
    public static String resolveTargetDescriptor(String owner, String name, String descriptor) {
        return normalizeDescriptor(descriptor);
    }

    public static String resolveTargetDescriptor(Class<?> owner, String name,
                                                 Class<?>... parameterTypes) {
        Class<?>[] parameters = parameterTypes == null ? new Class<?>[0] : parameterTypes;
        for (Method method : methodsOf(owner)) {
            if (method.getName().equals(name) && Arrays.equals(method.getParameterTypes(), parameters))
                return descriptor(method);
        }
        throw new IllegalStateException("missing target descriptor " + owner.getName() + "." + name);
    }

    public static String descriptor(Method method) {
        if (method == null) return "*";
        StringBuilder result = new StringBuilder("(");
        for (Class<?> parameter : method.getParameterTypes()) result.append(descriptor(parameter));
        return result.append(')').append(descriptor(method.getReturnType())).toString();
    }

    public static String descriptor(Field field) { return field == null ? "*" : descriptor(field.getType()); }

    public static String descriptor(Class<?> type) {
        if (type.isPrimitive()) {
            if (type == void.class) return "V";
            if (type == boolean.class) return "Z";
            if (type == byte.class) return "B";
            if (type == char.class) return "C";
            if (type == short.class) return "S";
            if (type == int.class) return "I";
            if (type == long.class) return "J";
            if (type == float.class) return "F";
            if (type == double.class) return "D";
        }
        if (type.isArray()) return type.getName().replace('.', '/');
        return "L" + type.getName().replace('.', '/') + ";";
    }

    private static Method findDeclaredMethodByDescriptor(Class<?> type, String name, String methodDescriptor) {
        if (type == null) return null;
        for (Method method : type.getDeclaredMethods())
            if (method.getName().equals(name) && descriptorMatches(methodDescriptor, descriptor(method))) return method;
        return null;
    }

    private static Method findDeclaredMethodByName(Class<?> type, String name) {
        if (type == null) throw new IllegalArgumentException("missing mixin class");
        Method[] methods = type.getDeclaredMethods();
        Arrays.sort(methods, Comparator.comparing(Method::getName).thenComparing(MixinHooks::descriptor));
        for (Method method : methods) if (method.getName().equals(name)) return method;
        throw new IllegalArgumentException("missing mixin method " + type.getName() + "." + name);
    }

    private static List<Method> methodsOf(Class<?> type) {
        List<Method> methods = new ArrayList<>();
        Set<Class<?>> visited = new HashSet<>();
        for (Class<?> current = type; current != null && visited.add(current); current = current.getSuperclass()) {
            methods.addAll(Arrays.asList(current.getDeclaredMethods()));
            for (Class<?> iface : current.getInterfaces()) methods.addAll(Arrays.asList(iface.getMethods()));
        }
        return methods;
    }

    private static List<Handler> lookup(Map<String, List<Handler>> map, InvocationSite site,
                                        HandlerType type) {
        List<Handler> result = new ArrayList<>();
        Set<Handler> seen = Collections.newSetFromMap(new IdentityHashMap<>());
        synchronized (LOCK) {
            for (String owner : ownerCandidates(site.target(), site.owner())) {
                List<Handler> candidates = map.get(ownerMethodKey(owner, site.method()));
                if (candidates == null) continue;
                for (Handler handler : candidates) {
                    if (seen.add(handler) && (type == null || handler.plan.type() == type) &&
                        descriptorMatches(handler.plan.targetDescriptor(), site.descriptor())) result.add(handler);
                }
            }
        }
        result.sort(HANDLER_COMPARATOR);
        return result;
    }

    private static List<String> ownerCandidates(Object target, String ownerHint) {
        List<String> owners = new ArrayList<>();
        if (ownerHint != null && !ownerHint.isEmpty()) owners.add(normalizeOwner(ownerHint));
        if (target == null) return owners;
        Class<?> type = target instanceof Class<?> clazz ? clazz : target.getClass();
        Deque<Class<?>> queue = new ArrayDeque<>();
        Set<Class<?>> visited = new HashSet<>();
        queue.add(type);
        while (!queue.isEmpty()) {
            Class<?> current = queue.removeFirst();
            if (current == null || !visited.add(current)) continue;
            String name = current.getName();
            if (!owners.contains(name)) owners.add(name);
            for (Class<?> iface : current.getInterfaces()) queue.addLast(iface);
            Class<?> parent = current.getSuperclass();
            if (parent != null) queue.addLast(parent);
        }
        return owners;
    }

    private static Object invokeHandler(Handler handler, Object target, Object[] args)
        throws ReflectiveOperationException {
        return invokeHandlerChecked(handler, target, args);
    }

    private static Object readField(Object target, String name, List<String> aliases) {
        Field field = resolveField(targetClass(target), name, aliases);
        try {
            field.setAccessible(true);
            return field.get(target instanceof Class<?> ? null : target);
        } catch (IllegalAccessException failure) {
            throw new IllegalStateException("field read failed: " + field, failure);
        }
    }

    private static void writeField(Object target, String name, Object value, List<String> aliases) {
        Field field = resolveField(targetClass(target), name, aliases);
        try {
            field.setAccessible(true);
            field.set(target instanceof Class<?> ? null : target, value);
        } catch (IllegalAccessException failure) {
            throw new IllegalStateException("field write failed: " + field, failure);
        }
    }

    private static Field resolveField(Class<?> type, String name, List<String> aliases) {
        List<String> names = new ArrayList<>();
        if (name != null && !name.isEmpty()) names.add(name);
        if (aliases != null) for (String alias : aliases)
            if (alias != null && !alias.isEmpty() && !names.contains(alias)) names.add(alias);
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            for (String candidate : names) {
                try { return current.getDeclaredField(candidate); }
                catch (NoSuchFieldException ignored) {}
            }
        }
        for (Class<?> iface : type.getInterfaces()) for (String candidate : names) {
            try { return iface.getField(candidate); }
            catch (NoSuchFieldException ignored) {}
        }
        throw new IllegalStateException("missing shadow/accessor field " + type.getName() + "." + name);
    }

    private static boolean compatibleParameters(Class<?>[] types, Object[] args) {
        if (args == null || types.length != args.length) return false;
        for (int i = 0; i < types.length; ++i) if (!accepts(types[i], args[i])) return false;
        return true;
    }

    private static boolean accepts(Class<?> type, Object value) {
        if (value == null) return !type.isPrimitive();
        return box(type).isInstance(value);
    }

    private static Class<?> box(Class<?> type) {
        if (!type.isPrimitive()) return type;
        if (type == boolean.class) return Boolean.class;
        if (type == byte.class) return Byte.class;
        if (type == char.class) return Character.class;
        if (type == short.class) return Short.class;
        if (type == int.class) return Integer.class;
        if (type == long.class) return Long.class;
        if (type == float.class) return Float.class;
        if (type == double.class) return Double.class;
        return Void.class;
    }

    private static int callbackParameterIndex(Class<?>[] types, CallbackInfo callback) {
        for (int i = types.length - 1; i >= 0; --i)
            if (CallbackInfo.class.isAssignableFrom(types[i]) && types[i].isAssignableFrom(callback.getClass())) return i;
        return -1;
    }

    private static Object[] missingLocalArguments(HandlerPlan plan, Method handler) {
        LocalCapture mode = plan == null ? LocalCapture.FAILHARD : plan.locals();
        if (mode == LocalCapture.FAILSOFT || mode == LocalCapture.CAPTURE_FAILSOFT || mode == LocalCapture.PRINT) {
            warn("local capture unavailable for " + handler);
            return null;
        }
        throw new IllegalStateException("local capture failed for " + handler);
    }

    private static Object[] concat(Object[] first, Object[] second) {
        Object[] left = first == null ? new Object[0] : first;
        Object[] right = second == null ? new Object[0] : second;
        Object[] output = new Object[left.length + right.length];
        System.arraycopy(left, 0, output, 0, left.length);
        System.arraycopy(right, 0, output, left.length, right.length);
        return output;
    }

    private static Object[] copy(Object[] values) {
        return values == null ? new Object[0] : values.clone();
    }

    @SuppressWarnings("unchecked")
    private static <T> CallbackInfoReturnable<T> returnable(DispatchResult result, String id, T fallback) {
        if (result.callbackInfo() instanceof CallbackInfoReturnable<?> info)
            return (CallbackInfoReturnable<T>) info;
        return new CallbackInfoReturnable<>(id, true,
            result.hasReturnValue() ? (T) result.returnValue() : fallback);
    }

    private static String accessorName(Method method) {
        Accessor accessor = method.getAnnotation(Accessor.class);
        return accessor != null && !accessor.value().isEmpty() ? accessor.value() : inferredMemberName(method.getName());
    }

    private static String invokerName(Method method) {
        Invoker invoker = method.getAnnotation(Invoker.class);
        return invoker != null && !invoker.value().isEmpty() ? invoker.value() : inferredMemberName(method.getName());
    }

    private static String inferredMemberName(String name) {
        String value = name;
        for (String prefix : List.of("get", "set", "is", "call", "invoke", "shadow$")) {
            if (value.startsWith(prefix) && value.length() > prefix.length()) {
                value = value.substring(prefix.length());
                break;
            }
        }
        if (value.isEmpty()) return name;
        return Character.toLowerCase(value.charAt(0)) + value.substring(1);
    }

    private static String shadowName(String name, String prefix) {
        if (prefix != null && !prefix.isEmpty() && name.startsWith(prefix)) return name.substring(prefix.length());
        return name;
    }

    private static String first(List<String> values) { return values.isEmpty() ? "" : values.get(0); }

    private static String ownerMethodKey(String owner, String method) { return normalizeOwner(owner) + "#" + method; }

    private static final Comparator<Handler> HANDLER_COMPARATOR = Comparator
        .comparingInt((Handler handler) -> handler.plan.priority()).reversed()
        .thenComparingInt(handler -> handler.plan.order())
        .thenComparingInt(handler -> handler.plan.targetDescriptor().equals("*") ? 1 : 0)
        .thenComparingLong(handler -> handler.plan.sequence());

    private static final Comparator<HandlerPlan> PLAN_COMPARATOR = Comparator
        .comparingInt(HandlerPlan::priority).reversed()
        .thenComparingInt(HandlerPlan::order)
        .thenComparingInt(plan -> plan.targetDescriptor().equals("*") ? 1 : 0)
        .thenComparingLong(HandlerPlan::sequence);

    private static boolean expectsReturnable(Method method) {
        for (Class<?> parameter : method.getParameterTypes())
            if (CallbackInfoReturnable.class.isAssignableFrom(parameter)) return true;
        return false;
    }

    private static String normalizePoint(String point) {
        if (point == null || point.isEmpty()) return "HEAD";
        String value = point.trim().toUpperCase(java.util.Locale.ROOT);
        return value.startsWith("INVOKE_ASSIGN") ? "INVOKE" : value;
    }

    private static String defaultPoint(HandlerType type) {
        return type == HandlerType.OVERWRITE ? "OVERWRITE" : type == HandlerType.MODIFY_CONSTANT ? "CONSTANT" : "HEAD";
    }

    private static String normalizeOwner(String owner) {
        if (owner == null || owner.isEmpty()) return "";
        String value = owner.trim();
        if (value.startsWith("L") && value.endsWith(";")) value = value.substring(1, value.length() - 1);
        return value.replace('/', '.');
    }

    private static String normalizeDescriptor(String descriptor) {
        return descriptor == null || descriptor.isEmpty() ? "*" : descriptor;
    }

    private static boolean descriptorMatches(String expected, String actual) {
        String left = normalizeDescriptor(expected);
        String right = normalizeDescriptor(actual);
        return "*".equals(left) || "*".equals(right) || left.equals(right);
    }

    private static boolean targetMatches(String expected, String actual) {
        if (expected == null || expected.isEmpty() || actual == null || actual.isEmpty()) return true;
        String left = canonicalTarget(expected);
        String right = canonicalTarget(actual);
        return left.equals(right) || left.endsWith(right) || right.endsWith(left);
    }

    private static String canonicalTarget(String target) {
        String value = target.trim().replace('.', '/');
        if (value.startsWith("L") && value.contains(";")) {
            int semicolon = value.indexOf(';');
            value = value.substring(1, semicolon) + value.substring(semicolon + 1);
        }
        return value;
    }

    private static String ownerOf(Object target) {
        if (target == null) return "";
        return (target instanceof Class<?> clazz ? clazz : target.getClass()).getName();
    }

    private static TargetMethod parseTargetMethod(String specification, String defaultOwner) {
        String value = specification == null ? "" : specification.trim();
        String owner = normalizeOwner(defaultOwner);
        int hash = value.indexOf('#');
        if (hash > 0) {
            owner = normalizeOwner(value.substring(0, hash));
            value = value.substring(hash + 1);
        } else if (value.startsWith("L")) {
            int semicolon = value.indexOf(';');
            if (semicolon > 0) {
                owner = normalizeOwner(value.substring(0, semicolon + 1));
                value = value.substring(semicolon + 1);
            }
        }
        int descriptor = value.indexOf('(');
        String name = descriptor < 0 ? value : value.substring(0, descriptor);
        String desc = descriptor < 0 ? "*" : value.substring(descriptor);
        return new TargetMethod(owner, name, desc);
    }

    private static Class<?> targetClass(Object target) {
        if (target == null) throw new NullPointerException("target");
        return target instanceof Class<?> clazz ? clazz : target.getClass();
    }

    private static void warn(String warning) {
        synchronized (LOCK) { WARNINGS.add(warning); }
        try { NativeBridge.nativeLog("WARN", warning); } catch (Throwable ignored) {}
    }

    private static RuntimeException sneaky(Throwable failure) {
        if (failure instanceof RuntimeException runtime) return runtime;
        if (failure instanceof Error error) throw error;
        return new RuntimeException(failure);
    }

    private static String executionMarker(InvocationSite site) {
        if (site.eventId() != null && !site.eventId().isEmpty()) return site.eventId();
        return site.at().value() + "|" + site.at().target() + "|" + site.ordinal() + "|" + site.opcode();
    }

    private static String handlerExecutionKey(HandlerPlan plan, InvocationSite site) {
        return plan.key() + "|" + executionMarker(site);
    }

    private static ExecutionFrame activeFrame(InvocationSite site) {
        if (site == null) return null;
        Deque<ExecutionFrame> frames = EXECUTION_FRAMES.get();
        for (ExecutionFrame frame : frames) if (frame.matches(site)) return frame;
        return null;
    }

    private static final class ExecutionFrame {
        private final Object target;
        private final MethodKey method;
        private final Set<String> claimed = new HashSet<>();
        private final Map<String, DispatchResult> results = new HashMap<>();

        private ExecutionFrame(Object target, MethodKey method) {
            this.target = target;
            this.method = method;
        }

        private boolean matches(InvocationSite site) {
            if (target != null && site.target() != null && target != site.target()) return false;
            return method.name().equals(site.method()) &&
                   descriptorMatches(method.descriptor(), site.descriptor()) &&
                   (method.owner().isEmpty() || method.owner().equals(site.owner()));
        }

        private synchronized boolean claim(String marker) { return claimed.add(marker); }
        private synchronized boolean isClaimed(String marker) { return claimed.contains(marker); }
        private synchronized DispatchResult result(String marker) { return results.get(marker); }
        private synchronized void record(String marker, DispatchResult result) {
            results.putIfAbsent(marker, result);
        }
    }

    private static final class Handler {
        private final HandlerPlan plan;
        private final Method method;
        private final Map<Object, Object> instances = new WeakHashMap<>();

        private Handler(HandlerPlan plan, Method method) { this.plan = plan; this.method = method; }

        private boolean sameHandler(Handler other) {
            return plan.key().equals(other.plan.key()) && method.equals(other.method);
        }

        private Object receiverFor(Object target) {
            if (Modifier.isStatic(method.getModifiers())) return null;
            if (target != null && method.getDeclaringClass().isInstance(target)) return target;
            synchronized (instances) {
                Object instance = instances.get(target);
                if (instance != null) return instance;
                try {
                    Constructor<?> constructor = method.getDeclaringClass().getDeclaredConstructor();
                    constructor.setAccessible(true);
                    instance = constructor.newInstance();
                    instances.put(target, instance);
                    return instance;
                } catch (ReflectiveOperationException failure) {
                    throw new IllegalStateException("cannot instantiate instance mixin " + method.getDeclaringClass().getName(), failure);
                }
            }
        }
    }

    private static final class TargetMethod {
        private final String owner;
        private final String name;
        private final String descriptor;
        private TargetMethod(String owner, String name, String descriptor) {
            this.owner = owner;
            this.name = name;
            this.descriptor = descriptor;
        }
    }
}
