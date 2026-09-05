package cppfm.transform;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;

/**
 * Conservative operand-stack analysis for injection sites.
 *
 * <p>It intentionally returns an error rather than inventing a stack shape.
 * The transformer uses the result to spill/reload values around callbacks;
 * this is what lets INVOKE/FIELD/JUMP/LOAD/STORE injections remain balanced
 * when the target has real control flow.</p>
 */
final class StackAnalyzer {
    private StackAnalyzer() { }

    static Analysis analyze(ClassFileModel owner, MemberModel method, CodeModel code) {
        List<BytecodeInstructions.Instruction> instructions = BytecodeInstructions.decode(code.code);
        if (instructions.isEmpty()) throw new TransformException("empty method Code");
        HashMap<Integer, Integer> byOffset = new HashMap<>();
        for (int i = 0; i < instructions.size(); ++i) byOffset.put(instructions.get(i).oldOffset, i);
        ArrayList<State> states = new ArrayList<>();
        for (int i = 0; i < instructions.size(); ++i) states.add(null);
        State entry = new State();
        initializeLocals(entry, owner, method);
        states.set(0, entry);
        ArrayDeque<Integer> queue = new ArrayDeque<>();
        queue.add(0);
        for (CodeModel.ExceptionHandler handler : code.exceptionHandlers) {
            Integer index = byOffset.get(handler.handlerPc);
            if (index != null) {
                State exception = new State();
                exception.stack.add(ref("java/lang/Throwable"));
                merge(states, queue, index, exception);
            }
        }
        IdentityHashMap<BytecodeInstructions.Instruction, List<Value>> before = new IdentityHashMap<>();
        IdentityHashMap<BytecodeInstructions.Instruction, List<Value>> after = new IdentityHashMap<>();
        while (!queue.isEmpty()) {
            int index = queue.removeFirst();
            State state = states.get(index);
            BytecodeInstructions.Instruction instruction = instructions.get(index);
            before.put(instruction, List.copyOf(state.stack));
            State next = state.copy();
            execute(owner.pool, instruction, next);
            after.put(instruction, List.copyOf(next.stack));
            for (int successor : successors(instructions, byOffset, index, instruction))
                merge(states, queue, successor, next);
        }
        for (BytecodeInstructions.Instruction instruction : instructions)
            if (!before.containsKey(instruction)) throw new TransformException("unreachable/unknown stack state at " + instruction.oldOffset);
        return new Analysis(before, after);
    }

    private static void initializeLocals(State state, ClassFileModel owner, MemberModel method) {
        Descriptor.MethodDesc descriptor = Descriptor.method(method.descriptor(owner.pool));
        int slot = 0;
        if ((method.access & ClassFileModel.ACC_STATIC) == 0) state.locals.put(slot++, ref(owner.internalName()));
        for (Descriptor.Type argument : descriptor.arguments) {
            state.locals.put(slot, value(argument));
            slot += argument.slots;
        }
    }

    private static void execute(ConstantPool pool, BytecodeInstructions.Instruction instruction, State state) {
        int opcode = instruction.opcode;
        switch (opcode) {
            case 0, 132, 167, 168, 169, 177, 200, 201 -> { }
            case 1 -> state.stack.add(ref("java/lang/Object"));
            case 2, 3, 4, 5, 6, 7, 8, 16, 17 -> state.stack.add(INT);
            case 9, 10 -> state.stack.add(LONG);
            case 11, 12, 13 -> state.stack.add(FLOAT);
            case 14, 15 -> state.stack.add(DOUBLE);
            case 18, 19, 20 -> state.stack.add(constant(pool, instruction));
            case 21, 26, 27, 28, 29 -> state.stack.add(local(state, instruction, INT));
            case 22, 30, 31, 32, 33 -> state.stack.add(local(state, instruction, LONG));
            case 23, 34, 35, 36, 37 -> state.stack.add(local(state, instruction, FLOAT));
            case 24, 38, 39, 40, 41 -> state.stack.add(local(state, instruction, DOUBLE));
            case 25, 42, 43, 44, 45 -> state.stack.add(local(state, instruction, ref("java/lang/Object")));
            case 46 -> { pop(state, 2); state.stack.add(INT); }
            case 47 -> { pop(state, 2); state.stack.add(LONG); }
            case 48 -> { pop(state, 2); state.stack.add(FLOAT); }
            case 49 -> { pop(state, 2); state.stack.add(DOUBLE); }
            case 50 -> { pop(state, 2); state.stack.add(ref("java/lang/Object")); }
            case 51, 52, 53 -> { pop(state, 2); state.stack.add(INT); }
            case 54, 59, 60, 61, 62 -> store(state, instruction, INT);
            case 55, 63, 64, 65, 66 -> store(state, instruction, LONG);
            case 56, 67, 68, 69, 70 -> store(state, instruction, FLOAT);
            case 57, 71, 72, 73, 74 -> store(state, instruction, DOUBLE);
            case 58, 75, 76, 77, 78 -> store(state, instruction, ref("java/lang/Object"));
            case 79, 80, 81, 82, 83, 84, 85, 86 -> pop(state, opcode == 80 || opcode == 82 ? 4 : 3);
            case 87 -> pop(state, 1);
            case 88 -> pop(state, 2);
            case 89 -> dup(state);
            case 90 -> dupX1(state);
            case 91 -> dupX2(state);
            case 92 -> dup2(state);
            case 93 -> dup2X1(state);
            case 94 -> dup2X2(state);
            case 95 -> swap(state);
            case 96, 100, 104, 108, 112, 120, 122, 124, 126, 128, 130 -> { pop(state, 2); state.stack.add(INT); }
            case 97, 101, 105, 109, 113, 127, 129, 131 -> { pop(state, 4); state.stack.add(LONG); }
            case 98, 102, 106, 110, 114 -> { pop(state, 2); state.stack.add(FLOAT); }
            case 99, 103, 107, 111, 115 -> { pop(state, 4); state.stack.add(DOUBLE); }
            case 116 -> { pop(state, 1); state.stack.add(INT); }
            case 117 -> { pop(state, 2); state.stack.add(LONG); }
            case 118 -> { pop(state, 1); state.stack.add(FLOAT); }
            case 119 -> { pop(state, 2); state.stack.add(DOUBLE); }
            case 121, 123, 125 -> { pop(state, 3); state.stack.add(LONG); }
            case 133, 134, 135, 145, 146, 147 -> { pop(state, 1); state.stack.add(opcode == 135 ? DOUBLE : opcode == 134 ? FLOAT : INT); }
            case 136, 137, 138 -> { pop(state, 2); state.stack.add(opcode == 138 ? DOUBLE : opcode == 137 ? FLOAT : INT); }
            case 139, 140, 141 -> { pop(state, 1); state.stack.add(opcode == 141 ? DOUBLE : opcode == 140 ? LONG : FLOAT); }
            case 142, 143, 144 -> { pop(state, 2); state.stack.add(opcode == 143 ? LONG : opcode == 144 ? FLOAT : INT); }
            case 148, 149, 150, 151, 152 -> { pop(state, 4); state.stack.add(INT); }
            case 153, 154, 155, 156, 157, 158, 198, 199 -> pop(state, 1);
            case 159, 160, 161, 162, 163, 164, 165, 166 -> pop(state, 2);
            case 170, 171 -> pop(state, 1);
            case 172, 174, 176, 191 -> pop(state, 1);
            case 173, 175 -> pop(state, 2);
            case 178 -> state.stack.add(field(pool, instruction));
            case 179 -> pop(state, field(pool, instruction).slots);
            case 180 -> { pop(state, 1); state.stack.add(field(pool, instruction)); }
            case 181 -> { Value field = field(pool, instruction); pop(state, field.slots + 1); }
            case 182, 183, 184, 185, 186 -> invoke(pool, instruction, state);
            case 187 -> state.stack.add(ref(pool.className(BytecodeInstructions.cpIndex(instruction))));
            case 188, 189 -> { pop(state, 1); state.stack.add(ref("java/lang/Object")); }
            case 190 -> { pop(state, 1); state.stack.add(INT); }
            case 192 -> { pop(state, 1); state.stack.add(ref(pool.className(BytecodeInstructions.cpIndex(instruction)))); }
            case 193 -> { pop(state, 1); state.stack.add(INT); }
            case 194, 195 -> pop(state, 1);
            case 196 -> executeWide(pool, instruction, state);
            case 197 -> { int dimensions = instruction.raw[3] & 0xff; pop(state, dimensions); state.stack.add(ref("java/lang/Object")); }
            default -> throw new TransformException("stack analyzer does not support opcode " + opcode);
        }
    }

    private static void executeWide(ConstantPool pool, BytecodeInstructions.Instruction instruction, State state) {
        int widened = instruction.raw[1] & 0xff;
        Value type = switch (widened) {
            case 21, 54 -> INT; case 22, 55 -> LONG; case 23, 56 -> FLOAT;
            case 24, 57 -> DOUBLE; case 25, 58 -> ref("java/lang/Object");
            default -> throw new TransformException("unsupported WIDE opcode " + widened);
        };
        if (widened >= 54) store(state, instruction, type);
        else state.stack.add(local(state, instruction, type));
    }

    private static void invoke(ConstantPool pool, BytecodeInstructions.Instruction instruction, State state) {
        int cp = BytecodeInstructions.cpIndex(instruction);
        Descriptor.MethodDesc descriptor = Descriptor.method(pool.memberDescriptor(cp));
        for (int i = descriptor.arguments.size() - 1; i >= 0; --i) pop(state, descriptor.arguments.get(i).slots);
        if (instruction.opcode != 184 && instruction.opcode != 186) pop(state, 1);
        if (!descriptor.returnType.voidType) state.stack.add(value(descriptor.returnType));
    }

    private static Value field(ConstantPool pool, BytecodeInstructions.Instruction instruction) {
        return value(Descriptor.type(pool.memberDescriptor(BytecodeInstructions.cpIndex(instruction))));
    }

    private static Value constant(ConstantPool pool, BytecodeInstructions.Instruction instruction) {
        Object value = pool.constantValue(BytecodeInstructions.cpIndex(instruction));
        if (value instanceof Integer) return INT;
        if (value instanceof Long) return LONG;
        if (value instanceof Float) return FLOAT;
        if (value instanceof Double) return DOUBLE;
        return ref("java/lang/Object");
    }

    private static Value local(State state, BytecodeInstructions.Instruction instruction, Value fallback) {
        int index = localIndex(instruction);
        return state.locals.getOrDefault(index, fallback);
    }

    private static void store(State state, BytecodeInstructions.Instruction instruction, Value expected) {
        Value actual = popValue(state, expected.slots);
        state.locals.put(localIndex(instruction), actual);
    }

    private static int localIndex(BytecodeInstructions.Instruction instruction) {
        int opcode = instruction.opcode;
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
        if (opcode == 196) return ((instruction.raw[2] & 0xff) << 8) | (instruction.raw[3] & 0xff);
        return instruction.raw.length > 1 ? instruction.raw[1] & 0xff : -1;
    }

    private static void pop(State state, int slots) {
        while (slots > 0) {
            Value value = popValue(state, 1);
            slots -= value.slots;
        }
        if (slots != 0) throw new TransformException("operand stack category mismatch");
    }

    private static Value popValue(State state, int minimumSlots) {
        if (state.stack.isEmpty()) throw new TransformException("operand stack underflow");
        Value value = state.stack.remove(state.stack.size() - 1);
        if (minimumSlots == 2 && value.slots != 2) throw new TransformException("expected category-2 stack value");
        return value;
    }

    private static void dup(State state) {
        Value top = popValue(state, 1); state.stack.add(top); state.stack.add(top);
    }
    private static void dupX1(State state) {
        Value a = popValue(state, 1), b = popValue(state, 1); state.stack.add(a); state.stack.add(b); state.stack.add(a);
    }
    private static void dupX2(State state) {
        Value a = popValue(state, 1), b = popValue(state, 1), c = popValue(state, 1); state.stack.add(a); state.stack.add(c); state.stack.add(b); state.stack.add(a);
    }
    private static void dup2(State state) {
        Value a = popValue(state, 1);
        if (a.slots == 2) { state.stack.add(a); state.stack.add(a); return; }
        Value b = popValue(state, 1); state.stack.add(b); state.stack.add(a); state.stack.add(b); state.stack.add(a);
    }
    private static void dup2X1(State state) {
        Value a = popValue(state, 1), b = popValue(state, 1), c = popValue(state, 1);
        state.stack.add(b); state.stack.add(a); state.stack.add(c); state.stack.add(b); state.stack.add(a);
    }
    private static void dup2X2(State state) {
        Value a = popValue(state, 1), b = popValue(state, 1), c = popValue(state, 1), d = popValue(state, 1);
        state.stack.add(b); state.stack.add(a); state.stack.add(d); state.stack.add(c); state.stack.add(b); state.stack.add(a);
    }
    private static void swap(State state) {
        Value a = popValue(state, 1), b = popValue(state, 1); state.stack.add(a); state.stack.add(b);
    }

    private static List<Integer> successors(List<BytecodeInstructions.Instruction> instructions,
                                            Map<Integer, Integer> byOffset, int index,
                                            BytecodeInstructions.Instruction instruction) {
        ArrayList<Integer> result = new ArrayList<>();
        int opcode = instruction.opcode;
        if (BytecodeInstructions.isReturn(opcode) || opcode == 191 || opcode == 169) return result;
        if (opcode == 170 || opcode == 171) {
            add(byOffset, result, instruction.defaultTarget);
            if (instruction.switchTargets != null) for (int target : instruction.switchTargets) add(byOffset, result, target);
            return result;
        }
        if (BytecodeInstructions.isShortBranch(opcode) || BytecodeInstructions.isWideBranch(opcode)) {
            add(byOffset, result, instruction.branchTarget);
            if (opcode >= 153 && opcode <= 166 || opcode == 198 || opcode == 199)
                if (index + 1 < instructions.size()) result.add(index + 1);
            return result;
        }
        if (index + 1 < instructions.size()) result.add(index + 1);
        return result;
    }

    private static void add(Map<Integer, Integer> byOffset, List<Integer> result, int offset) {
        Integer index = byOffset.get(offset);
        if (index == null) throw new TransformException("branch target has no instruction: " + offset);
        result.add(index);
    }

    private static void merge(List<State> states, ArrayDeque<Integer> queue, int index, State incoming) {
        State existing = states.get(index);
        if (existing == null) {
            states.set(index, incoming.copy());
            queue.add(index);
            return;
        }
        State merged = existing.merge(incoming);
        if (!existing.equalsState(merged)) {
            states.set(index, merged);
            queue.add(index);
        }
    }

    static final class Analysis {
        final IdentityHashMap<BytecodeInstructions.Instruction, List<Value>> before;
        final IdentityHashMap<BytecodeInstructions.Instruction, List<Value>> after;
        Analysis(IdentityHashMap<BytecodeInstructions.Instruction, List<Value>> before,
                 IdentityHashMap<BytecodeInstructions.Instruction, List<Value>> after) {
            this.before = before; this.after = after;
        }
        List<Value> before(BytecodeInstructions.Instruction instruction) { return before.getOrDefault(instruction, List.of()); }
        List<Value> after(BytecodeInstructions.Instruction instruction) { return after.getOrDefault(instruction, List.of()); }
    }

    static final class Value {
        final String descriptor;
        final int slots;
        final boolean reference;
        final boolean primitive;
        Value(String descriptor, int slots, boolean reference, boolean primitive) {
            this.descriptor = descriptor; this.slots = slots; this.reference = reference; this.primitive = primitive;
        }
    }

    private static final Value INT = new Value("I", 1, false, true);
    private static final Value LONG = new Value("J", 2, false, true);
    private static final Value FLOAT = new Value("F", 1, false, true);
    private static final Value DOUBLE = new Value("D", 2, false, true);

    private static Value value(Descriptor.Type type) {
        return new Value(type.descriptor, type.slots, type.reference, type.primitive);
    }
    private static Value ref(String owner) { return new Value("L" + owner + ";", 1, true, false); }

    private static final class State {
        final ArrayList<Value> stack = new ArrayList<>();
        final HashMap<Integer, Value> locals = new HashMap<>();
        State copy() { State result = new State(); result.stack.addAll(stack); result.locals.putAll(locals); return result; }
        State merge(State other) {
            if (stack.size() != other.stack.size()) throw new TransformException("incompatible operand stack heights at control-flow merge");
            State result = new State();
            for (int i = 0; i < stack.size(); ++i) result.stack.add(join(stack.get(i), other.stack.get(i)));
            java.util.HashSet<Integer> keys = new java.util.HashSet<>(locals.keySet()); keys.retainAll(other.locals.keySet());
            for (int key : keys) result.locals.put(key, join(locals.get(key), other.locals.get(key)));
            return result;
        }
        boolean equalsState(State other) { return stackEquals(stack, other.stack) && locals.equals(other.locals); }
        private static boolean stackEquals(List<Value> a, List<Value> b) {
            if (a.size() != b.size()) return false;
            for (int i = 0; i < a.size(); ++i) if (!same(a.get(i), b.get(i))) return false;
            return true;
        }
        private static boolean same(Value a, Value b) { return a.descriptor.equals(b.descriptor) && a.slots == b.slots; }
        private static Value join(Value a, Value b) {
            if (same(a, b)) return a;
            if (a.reference && b.reference) return ref("java/lang/Object");
            throw new TransformException("incompatible stack/local types at control-flow merge");
        }
    }
}
