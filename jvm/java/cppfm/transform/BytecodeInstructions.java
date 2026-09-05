package cppfm.transform;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** JVM instruction decoding, relocation and insertion utilities. */
final class BytecodeInstructions {
    private static final String ORIGIN_ATTRIBUTE = "CppFmInstructionOrigins";

    private BytecodeInstructions() { }

    static List<Instruction> decode(byte[] code) {
        ArrayList<Instruction> result = new ArrayList<>();
        int offset = 0;
        while (offset < code.length) {
            int opcode = code[offset] & 0xff;
            if (opcode == 170) {
                int padding = (4 - ((offset + 1) & 3)) & 3;
                int base = offset + 1 + padding;
                ensure(base + 12 <= code.length, offset, code.length);
                Instruction instruction = Instruction.switchInstruction(offset, opcode);
                instruction.defaultTarget = offset + readInt(code, base);
                int low = readInt(code, base + 4);
                int high = readInt(code, base + 8);
                if (high < low || high - low > 65535) throw malformed(offset, "bad tableswitch range");
                instruction.low = low;
                instruction.high = high;
                instruction.switchTargets = new int[high - low + 1];
                int position = base + 12;
                ensure(position + instruction.switchTargets.length * 4 <= code.length, offset, code.length);
                for (int i = 0; i < instruction.switchTargets.length; ++i, position += 4)
                    instruction.switchTargets[i] = offset + readInt(code, position);
                instruction.originalLength = position - offset;
                result.add(instruction);
                offset = position;
                continue;
            }
            if (opcode == 171) {
                int padding = (4 - ((offset + 1) & 3)) & 3;
                int base = offset + 1 + padding;
                ensure(base + 8 <= code.length, offset, code.length);
                Instruction instruction = Instruction.switchInstruction(offset, opcode);
                instruction.defaultTarget = offset + readInt(code, base);
                int pairs = readInt(code, base + 4);
                if (pairs < 0 || pairs > 65535) throw malformed(offset, "bad lookupswitch pair count");
                instruction.keys = new int[pairs];
                instruction.switchTargets = new int[pairs];
                int position = base + 8;
                ensure(position + pairs * 8 <= code.length, offset, code.length);
                for (int i = 0; i < pairs; ++i) {
                    instruction.keys[i] = readInt(code, position);
                    instruction.switchTargets[i] = offset + readInt(code, position + 4);
                    position += 8;
                }
                instruction.originalLength = position - offset;
                result.add(instruction);
                offset = position;
                continue;
            }
            if (opcode == 196) {
                ensure(offset + 2 <= code.length, offset, code.length);
                int widened = code[offset + 1] & 0xff;
                int length = widened == 132 ? 6 : 4;
                ensure(offset + length <= code.length, offset, code.length);
                result.add(Instruction.raw(offset, opcode, Arrays.copyOfRange(code, offset, offset + length)));
                offset += length;
                continue;
            }
            int length = fixedLength(opcode);
            ensure(length > 0 && offset + length <= code.length, offset, code.length);
            Instruction instruction = Instruction.raw(offset, opcode,
                Arrays.copyOfRange(code, offset, offset + length));
            if (isShortBranch(opcode)) instruction.branchTarget = offset + signedShort(code, offset + 1);
            else if (isWideBranch(opcode)) instruction.branchTarget = offset + readInt(code, offset + 1);
            result.add(instruction);
            offset += length;
        }
        if (offset != code.length) throw malformed(offset, "instruction stream does not end at code length");
        return result;
    }

    static int fixedLength(int opcode) {
        if (opcode < 0 || opcode > 255) throw new IllegalArgumentException("opcode " + opcode);
        if (opcode == 16 || opcode == 18 || opcode == 21 || opcode == 22 || opcode == 23
            || opcode == 24 || opcode == 25 || opcode == 54 || opcode == 55 || opcode == 56
            || opcode == 57 || opcode == 58 || opcode == 169 || opcode == 188) return 2;
        if (opcode == 17 || opcode == 19 || opcode == 20 || opcode == 132
            || (opcode >= 153 && opcode <= 168) || (opcode >= 198 && opcode <= 199)
            || (opcode >= 178 && opcode <= 184) || opcode == 187 || opcode == 189
            || opcode == 192 || opcode == 193) return 3;
        if (opcode == 185 || opcode == 186 || opcode == 197) return opcode == 197 ? 4 : 5;
        if ((opcode >= 26 && opcode <= 53) || (opcode >= 59 && opcode <= 86)
            || (opcode >= 87 && opcode <= 131) || (opcode >= 133 && opcode <= 152)
            || opcode == 177 || opcode == 172 || opcode == 173 || opcode == 174
            || opcode == 175 || opcode == 176 || opcode == 170 || opcode == 171) return 1;
        return 1;
    }

    static boolean isShortBranch(int opcode) {
        return (opcode >= 153 && opcode <= 168) || opcode == 198 || opcode == 199;
    }

    static boolean isWideBranch(int opcode) {
        return opcode == 200 || opcode == 201;
    }

    static boolean isReturn(int opcode) {
        return opcode >= 172 && opcode <= 177;
    }

    static boolean isInvoke(int opcode) {
        return opcode >= 182 && opcode <= 186;
    }

    static boolean isField(int opcode) {
        return opcode >= 178 && opcode <= 181;
    }

    static int cpIndex(Instruction instruction) {
        int opcode = instruction.opcode;
        if ((opcode >= 178 && opcode <= 184) || opcode == 187 || opcode == 189
            || opcode == 192 || opcode == 193 || opcode == 18 || opcode == 19 || opcode == 20
            || opcode == 185 || opcode == 186 || opcode == 197) {
            return opcode == 18 ? instruction.raw[1] & 0xff : readUnsignedShort(instruction.raw, 1);
        }
        return -1;
    }

    static Instruction withCpIndex(Instruction source, int cpIndex) {
        Instruction result = source.copy();
        if (source.opcode == 18 && cpIndex <= 255) result.raw[1] = (byte) cpIndex;
        else if (source.opcode == 18) {
            result.opcode = 19;
            result.raw = new byte[] { 19, (byte) (cpIndex >>> 8), (byte) cpIndex };
        } else {
            result.raw[1] = (byte) (cpIndex >>> 8);
            result.raw[2] = (byte) cpIndex;
        }
        return result;
    }

    static List<Instruction> copyWithConstantPool(List<Instruction> source, ConstantPool sourcePool,
                                                   ConstantPool targetPool, String sourceOwner,
                                                   String targetOwner, Map<String, String> methodRenames,
                                                   Map<String, String> fieldRenames) {
        return copyWithConstantPool(source, sourcePool, targetPool, sourceOwner, targetOwner,
            methodRenames, fieldRenames, 0);
    }

    static List<Instruction> copyWithConstantPool(List<Instruction> source, ConstantPool sourcePool,
                                                   ConstantPool targetPool, String sourceOwner,
                                                   String targetOwner, Map<String, String> methodRenames,
                                                   Map<String, String> fieldRenames, int bootstrapOffset) {
        ArrayList<Instruction> result = new ArrayList<>();
        for (Instruction instruction : source) {
            Instruction copy = instruction.copy();
            int sourceCp = cpIndex(instruction);
            if (sourceCp > 0) {
                int targetCp = targetPool.importEntry(sourcePool, sourceCp, sourceOwner, targetOwner,
                    methodRenames, fieldRenames, bootstrapOffset);
                copy = withCpIndex(copy, targetCp);
            }
            result.add(copy);
        }
        return result;
    }

    static final class Editor {
        final List<Instruction> instructions;
        final CodeModel code;
        final int originalLength;
        int nextLocal;

        Editor(CodeModel code) {
            this(code, null);
        }

        Editor(CodeModel code, ConstantPool pool) {
            this.code = code;
            this.originalLength = code.code.length;
            this.instructions = new ArrayList<>(decode(code.code));
            this.nextLocal = code.maxLocals;
            if (pool != null) restoreOrigins(pool);
        }

        /** Restore provenance after a previous edit was serialized. */
        private void restoreOrigins(ConstantPool pool) {
            AttributeModel provenance = null;
            for (AttributeModel candidate : code.attributes) {
                if (!candidate.name(pool).equals(ORIGIN_ATTRIBUTE)) continue;
                if (provenance != null) throw new TransformException("duplicate " + ORIGIN_ATTRIBUTE);
                provenance = candidate;
            }
            if (provenance == null) return;
            java.nio.ByteBuffer input = java.nio.ByteBuffer.wrap(provenance.info)
                .order(java.nio.ByteOrder.BIG_ENDIAN);
            if (input.remaining() < 2) throw new TransformException("truncated " + ORIGIN_ATTRIBUTE);
            int count = input.getShort() & 0xffff;
            ArrayList<Instruction> encoded = new ArrayList<>();
            for (Instruction instruction : instructions) if (!instruction.label) encoded.add(instruction);
            if (count != encoded.size())
                throw new TransformException(ORIGIN_ATTRIBUTE + " instruction count mismatch");
            for (Instruction instruction : encoded) {
                if (input.remaining() < 4) throw new TransformException("truncated " + ORIGIN_ATTRIBUTE);
                int offset = input.getShort() & 0xffff;
                int origin = input.getShort() & 0xffff;
                if (offset != instruction.oldOffset)
                    throw new TransformException(ORIGIN_ATTRIBUTE + " offset mismatch at " + instruction.oldOffset);
                instruction.original = origin != 0xffff;
                instruction.originalOffset = instruction.original ? origin : -1;
            }
            if (input.hasRemaining()) throw new TransformException("trailing " + ORIGIN_ATTRIBUTE + " data");
        }

        int allocateLocal(Descriptor.Type type) {
            int result = nextLocal;
            nextLocal += type.slots;
            return result;
        }

        /** Allocate a label which can be used as a generated branch target. */
        Instruction newLabel() {
            return Instruction.label(-1);
        }

        void insertBefore(Instruction anchor, List<Instruction> additions) {
            int index = instructions.indexOf(anchor);
            if (index < 0) throw new TransformException("instruction anchor disappeared");
            instructions.addAll(index, attachOldOffset(additions, -1));
        }

        void insertAfter(Instruction anchor, List<Instruction> additions) {
            int index = instructions.indexOf(anchor);
            if (index < 0) throw new TransformException("instruction anchor disappeared");
            instructions.addAll(index + 1, attachOldOffset(additions, -1));
        }

        void replace(Instruction old, List<Instruction> additions) {
            int index = instructions.indexOf(old);
            if (index < 0) throw new TransformException("instruction anchor disappeared");
            List<Instruction> attached = attachOldOffset(additions, old.oldOffset);
            instructions.set(index, attached.isEmpty() ? Instruction.label(old.oldOffset) : attached.get(0));
            if (attached.size() > 1) instructions.addAll(index + 1, attached.subList(1, attached.size()));
        }

        void replaceRange(int start, int endInclusive, List<Instruction> additions) {
            int first = instructions.indexOf(instructions.get(start));
            int last = instructions.indexOf(instructions.get(endInclusive));
            int oldOffset = instructions.get(start).oldOffset;
            List<Instruction> attached = attachOldOffset(additions, oldOffset);
            instructions.subList(first, last + 1).clear();
            instructions.addAll(first, attached);
        }

        Assembly assemble() {
            IdentityHashMap<Instruction, Integer> offsets = new IdentityHashMap<>();
            HashMap<Integer, Integer> oldOffsets = new HashMap<>();
            int offset = 0;
            for (Instruction instruction : instructions) {
                offsets.put(instruction, offset);
                if (instruction.oldOffset >= 0) oldOffsets.putIfAbsent(instruction.oldOffset, offset);
                offset += instruction.encodedLength(offset);
            }
            oldOffsets.put(originalLength, offset);
            ByteArrayOutputStream output = new ByteArrayOutputStream(offset);
            for (Instruction instruction : instructions) instruction.encode(output, offsets, oldOffsets);
            return new Assembly(output.toByteArray(), oldOffsets, offsets, nextLocal);
        }

        static byte[] assembleGenerated(List<Instruction> generated) {
            CodeModel empty = new CodeModel();
            empty.code = new byte[0];
            Editor editor = new Editor(empty);
            editor.instructions.clear();
            editor.instructions.addAll(copyList(generated));
            return editor.assemble().bytes;
        }

        void finish(ConstantPool pool) {
            Assembly assembly = assemble();
            code.code = assembly.bytes;
            code.maxLocals = Math.max(code.maxLocals, assembly.maxLocals);
            code.maxStack = Math.min(65535, Math.max(code.maxStack + 16, 32));
            for (CodeModel.ExceptionHandler handler : code.exceptionHandlers) {
                handler.startPc = mapOffset(assembly.oldOffsets, handler.startPc);
                handler.endPc = mapOffset(assembly.oldOffsets, handler.endPc);
                handler.handlerPc = mapOffset(assembly.oldOffsets, handler.handlerPc);
            }
            NestedAttributeRelocator.relocate(code.attributes, pool, assembly.oldOffsets);
            writeOrigins(pool, assembly);
        }

        /**
         * Recompute full StackMapTable frames after introducing conditional
         * control flow.  Existing frame encodings are relative to one
         * another, so inserting a single full frame into the old table is not
         * sufficient: every later append/chop frame would use the wrong
         * predecessor.  The conservative analyzer already has the exact
         * state at each branch target, therefore rebuilding every target as a
         * full frame is both simpler and verifier-safe.
         */
        void rebuildStackMapFrames(ClassFileModel owner, MemberModel method) {
            List<Instruction> decoded = decode(code.code);
            Map<Integer, Instruction> byOffset = new HashMap<>();
            Set<Integer> targets = new java.util.TreeSet<>();
            for (Instruction instruction : decoded) {
                byOffset.put(instruction.oldOffset, instruction);
                if (instruction.branchTarget >= 0) targets.add(instruction.branchTarget);
                if (instruction.defaultTarget >= 0) targets.add(instruction.defaultTarget);
                if (instruction.switchTargets != null)
                    for (int target : instruction.switchTargets) targets.add(target);
            }
            for (CodeModel.ExceptionHandler handler : code.exceptionHandlers)
                targets.add(handler.handlerPc);
            targets.remove(0);
            if (targets.isEmpty()) return;
            StackAnalyzer.Analysis analysis = StackAnalyzer.analyze(owner, method, code);
            ArrayList<StackMapFrame> frames = new ArrayList<>();
            for (int target : targets) {
                Instruction instruction = byOffset.get(target);
                if (instruction == null) throw new TransformException("stack-map target has no instruction: " + target);
                frames.add(StackMapFrame.full(target,
                    verificationLocals(analysis.localsBefore(instruction), owner.pool),
                    verificationStack(analysis.before(instruction), owner.pool)));
            }
            try {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                DataOutputStream output = new DataOutputStream(bytes);
                output.writeShort(frames.size());
                int previous = -1;
                for (StackMapFrame frame : frames) {
                    int delta = frame.offset - previous - 1;
                    if (delta < 0 || delta > 65535)
                        throw new TransformException("invalid rebuilt StackMapTable frame order");
                    writeStackMapFrame(output, frame, delta);
                    previous = frame.offset;
                }
                output.flush();
                code.attributes.removeIf(attribute -> {
                    String name = attribute.name(owner.pool);
                    return name.equals("StackMapTable") || name.equals("StackMap");
                });
                code.attributes.add(new AttributeModel(owner.pool.addUtf8("StackMapTable"), bytes.toByteArray()));
            } catch (IOException failure) {
                throw new TransformException("cannot rebuild StackMapTable", failure);
            }
        }

        private static VerificationType[] verificationLocals(Map<Integer, StackAnalyzer.Value> values,
                                                              ConstantPool pool) {
            if (values.isEmpty()) return new VerificationType[0];
            int max = 0;
            for (Map.Entry<Integer, StackAnalyzer.Value> entry : values.entrySet()) {
                if (entry.getKey() < 0 || entry.getValue() == null)
                    throw new TransformException("invalid generated frame local");
                max = Math.max(max, entry.getKey() + entry.getValue().slots);
            }
            VerificationType[] slots = new VerificationType[max];
            boolean[] wideTail = new boolean[max];
            for (Map.Entry<Integer, StackAnalyzer.Value> entry : values.entrySet()) {
                int slot = entry.getKey();
                StackAnalyzer.Value value = entry.getValue();
                if (value.isUninitialized())
                    throw new TransformException("uninitialized local in generated stack-map frame");
                slots[slot] = verification(value.descriptor, pool);
                if (value.slots == 2) wideTail[slot + 1] = true;
            }
            int last = slots.length - 1;
            while (last >= 0 && slots[last] == null && !wideTail[last]) --last;
            if (last < 0) return new VerificationType[0];
            ArrayList<VerificationType> result = new ArrayList<>();
            for (int slot = 0; slot <= last; ++slot) {
                if (wideTail[slot]) continue;
                result.add(slots[slot] == null ? new VerificationType(0, -1) : slots[slot]);
            }
            return result.toArray(VerificationType[]::new);
        }

        private static VerificationType[] verificationStack(List<StackAnalyzer.Value> values,
                                                             ConstantPool pool) {
            ArrayList<VerificationType> result = new ArrayList<>();
            for (StackAnalyzer.Value value : values) {
                if (value == null || value.isUninitialized())
                    throw new TransformException("uninitialized value in generated stack-map frame");
                result.add(verification(value.descriptor, pool));
            }
            return result.toArray(VerificationType[]::new);
        }

        private static VerificationType verification(String descriptor, ConstantPool pool) {
            if (descriptor == null || descriptor.isEmpty())
                throw new TransformException("empty generated frame descriptor");
            return switch (descriptor.charAt(0)) {
                case 'Z', 'B', 'C', 'S', 'I' -> new VerificationType(1, -1);
                case 'F' -> new VerificationType(2, -1);
                case 'D' -> new VerificationType(3, -1);
                case 'J' -> new VerificationType(4, -1);
                case 'L' -> {
                    if (!descriptor.endsWith(";")) throw new TransformException("invalid frame reference " + descriptor);
                    yield new VerificationType(7, pool.addClass(descriptor.substring(1, descriptor.length() - 1)));
                }
                case '[' -> new VerificationType(7, pool.addClass(descriptor));
                default -> throw new TransformException("invalid generated frame descriptor " + descriptor);
            };
        }

        private static void writeStackMapFrame(DataOutputStream output, StackMapFrame frame,
                                               int delta) throws IOException {
            if (frame.kind == FrameKind.SAME) {
                if (delta <= 63) output.writeByte(delta);
                else { output.writeByte(251); output.writeShort(delta); }
            } else if (frame.kind == FrameKind.SAME_ONE) {
                if (delta <= 63 && frame.stack.length == 1) {
                    output.writeByte(64 + delta);
                    writeVerification(output, frame.stack[0]);
                } else {
                    output.writeByte(247); output.writeShort(delta);
                    for (VerificationType value : frame.stack) writeVerification(output, value);
                }
            } else if (frame.kind == FrameKind.CHOP) {
                output.writeByte(251 - frame.chop); output.writeShort(delta);
            } else if (frame.kind == FrameKind.APPEND && frame.locals.length >= 1 && frame.locals.length <= 3) {
                output.writeByte(251 + frame.locals.length); output.writeShort(delta);
                for (VerificationType value : frame.locals) writeVerification(output, value);
            } else {
                output.writeByte(255); output.writeShort(delta);
                output.writeShort(frame.locals.length);
                for (VerificationType value : frame.locals) writeVerification(output, value);
                output.writeShort(frame.stack.length);
                for (VerificationType value : frame.stack) writeVerification(output, value);
            }
        }

        private static void writeVerification(DataOutputStream output, VerificationType value)
                throws IOException {
            output.writeByte(value.tag);
            if (value.tag == 7 || value.tag == 8) output.writeShort(value.index);
        }

        private enum FrameKind { SAME, SAME_ONE, CHOP, APPEND, FULL }

        private static final class StackMapFrame {
            int offset;
            final FrameKind kind;
            final int chop;
            final VerificationType[] locals;
            final VerificationType[] stack;

            private StackMapFrame(int offset, FrameKind kind, int chop,
                                  VerificationType[] locals, VerificationType[] stack) {
                this.offset = offset; this.kind = kind; this.chop = chop;
                this.locals = locals; this.stack = stack;
            }
            static StackMapFrame same(int offset) {
                return new StackMapFrame(offset, FrameKind.SAME, 0,
                    new VerificationType[0], new VerificationType[0]);
            }
            static StackMapFrame sameOne(int offset, VerificationType stack) {
                return new StackMapFrame(offset, FrameKind.SAME_ONE, 0,
                    new VerificationType[0], new VerificationType[] { stack });
            }
            static StackMapFrame chop(int offset, int count) {
                return new StackMapFrame(offset, FrameKind.CHOP, count,
                    new VerificationType[0], new VerificationType[0]);
            }
            static StackMapFrame append(int offset, VerificationType[] locals) {
                return new StackMapFrame(offset, FrameKind.APPEND, 0, locals,
                    new VerificationType[0]);
            }
            static StackMapFrame full(int offset, VerificationType[] locals, VerificationType[] stack) {
                return new StackMapFrame(offset, FrameKind.FULL, 0, locals, stack);
            }
        }

        private record VerificationType(int tag, int index) { }

        private void writeOrigins(ConstantPool pool, Assembly assembly) {
            ArrayList<Instruction> encoded = new ArrayList<>();
            for (Instruction instruction : instructions) if (!instruction.label) encoded.add(instruction);
            if (encoded.size() > 65535) throw new TransformException("too many instructions for " + ORIGIN_ATTRIBUTE);
            java.nio.ByteBuffer output = java.nio.ByteBuffer.allocate(2 + encoded.size() * 4)
                .order(java.nio.ByteOrder.BIG_ENDIAN);
            output.putShort((short) encoded.size());
            for (Instruction instruction : encoded) {
                Integer current = assembly.offsets.get(instruction);
                if (current == null || current < 0 || current > 65535)
                    throw new TransformException("invalid instruction offset for " + ORIGIN_ATTRIBUTE);
                int origin = instruction.original && instruction.originalOffset >= 0
                    ? instruction.originalOffset : 0xffff;
                if (origin != 0xffff && origin > 65534)
                    throw new TransformException("invalid original instruction offset");
                output.putShort((short) (int) current);
                output.putShort((short) origin);
            }
            AttributeModel provenance = null;
            for (AttributeModel candidate : code.attributes) {
                if (candidate.name(pool).equals(ORIGIN_ATTRIBUTE)) { provenance = candidate; break; }
            }
            if (provenance == null)
                code.attributes.add(new AttributeModel(pool.addUtf8(ORIGIN_ATTRIBUTE), output.array()));
            else provenance.info = output.array();
        }

        private static int mapOffset(Map<Integer, Integer> offsets, int oldOffset) {
            Integer mapped = offsets.get(oldOffset);
            if (mapped != null) return mapped;
            int closest = Integer.MAX_VALUE;
            int mappedClosest = 0;
            for (Map.Entry<Integer, Integer> entry : offsets.entrySet()) {
                if (entry.getKey() >= oldOffset && entry.getKey() < closest) {
                    closest = entry.getKey();
                    mappedClosest = entry.getValue();
                }
            }
            return mappedClosest;
        }

        private static List<Instruction> attachOldOffset(List<Instruction> additions, int oldOffset) {
            ArrayList<Instruction> result = copyList(additions);
            if (oldOffset >= 0) {
                for (Instruction instruction : result) {
                    if (!instruction.label) {
                        instruction.oldOffset = oldOffset;
                        break;
                    }
                }
            }
            return result;
        }

        private static ArrayList<Instruction> copyList(List<Instruction> source) {
            ArrayList<Instruction> result = new ArrayList<>();
            IdentityHashMap<Instruction, Instruction> copies = new IdentityHashMap<>();
            for (Instruction instruction : source) {
                Instruction copy = instruction.copy();
                result.add(copy);
                copies.put(instruction, copy);
            }
            for (Instruction copy : result) {
                if (copy.branchTargetInstruction != null)
                    copy.branchTargetInstruction = copies.getOrDefault(copy.branchTargetInstruction,
                        copy.branchTargetInstruction);
            }
            return result;
        }
    }

    static final class Assembly {
        final byte[] bytes;
        final Map<Integer, Integer> oldOffsets;
        final IdentityHashMap<Instruction, Integer> offsets;
        final int maxLocals;

        Assembly(byte[] bytes, Map<Integer, Integer> oldOffsets,
                 IdentityHashMap<Instruction, Integer> offsets, int maxLocals) {
            this.bytes = bytes;
            this.oldOffsets = oldOffsets;
            this.offsets = offsets;
            this.maxLocals = maxLocals;
        }
    }

    static final class Instruction {
        int oldOffset;
        int opcode;
        byte[] raw;
        int originalLength;
        boolean original;
        int originalOffset = -1;
        int branchTarget = -1;
        Instruction branchTargetInstruction;
        boolean label;
        int defaultTarget = -1;
        int[] switchTargets;
        int[] keys;
        int low;
        int high;

        static Instruction raw(int oldOffset, int opcode, byte[] raw) {
            Instruction result = new Instruction();
            result.oldOffset = oldOffset;
            result.original = oldOffset >= 0;
            result.originalOffset = oldOffset >= 0 ? oldOffset : -1;
            result.opcode = opcode;
            result.raw = raw;
            result.originalLength = raw.length;
            return result;
        }

        static Instruction switchInstruction(int oldOffset, int opcode) {
            Instruction result = raw(oldOffset, opcode, new byte[] { (byte) opcode });
            result.switchTargets = new int[0];
            return result;
        }

        static Instruction bytes(byte... bytes) {
            return raw(-1, bytes[0] & 0xff, bytes);
        }

        static Instruction label(int oldOffset) {
            Instruction result = new Instruction();
            result.oldOffset = oldOffset;
            result.originalOffset = -1;
            result.label = true;
            result.opcode = -1;
            result.raw = new byte[0];
            return result;
        }

        static Instruction branch(int opcode, Instruction target) {
            Instruction result = new Instruction();
            result.originalOffset = -1;
            result.opcode = opcode;
            result.raw = new byte[opcode == 200 || opcode == 201 ? 5 : 3];
            result.raw[0] = (byte) opcode;
            result.branchTargetInstruction = target;
            result.originalLength = result.raw.length;
            return result;
        }

        Instruction copy() {
            Instruction result = new Instruction();
            result.oldOffset = oldOffset;
            result.opcode = opcode;
            result.raw = raw == null ? null : raw.clone();
            result.originalLength = originalLength;
            result.original = original;
            result.originalOffset = originalOffset;
            result.branchTarget = branchTarget;
            result.branchTargetInstruction = branchTargetInstruction;
            result.label = label;
            result.defaultTarget = defaultTarget;
            result.switchTargets = switchTargets == null ? null : switchTargets.clone();
            result.keys = keys == null ? null : keys.clone();
            result.low = low;
            result.high = high;
            return result;
        }

        int encodedLength(int offset) {
            if (label) return 0;
            if (opcode == 170) return 1 + ((4 - ((offset + 1) & 3)) & 3) + 12 + switchTargets.length * 4;
            if (opcode == 171) return 1 + ((4 - ((offset + 1) & 3)) & 3) + 8 + switchTargets.length * 8;
            return raw.length;
        }

        void encode(ByteArrayOutputStream output, IdentityHashMap<Instruction, Integer> offsets,
                    Map<Integer, Integer> oldOffsets) {
            if (label) return;
            int offset = offsets.get(this);
            if (opcode == 170 || opcode == 171) {
                int padding = (4 - ((offset + 1) & 3)) & 3;
                output.write(opcode);
                for (int i = 0; i < padding; ++i) output.write(0);
                writeInt(output, mappedTarget(defaultTarget, offsets, oldOffsets) - offset);
                if (opcode == 170) {
                    writeInt(output, low);
                    writeInt(output, high);
                    for (int target : switchTargets)
                        writeInt(output, mappedTarget(target, offsets, oldOffsets) - offset);
                } else {
                    writeInt(output, switchTargets.length);
                    for (int i = 0; i < switchTargets.length; ++i) {
                        writeInt(output, keys[i]);
                        writeInt(output, mappedTarget(switchTargets[i], offsets, oldOffsets) - offset);
                    }
                }
                return;
            }
            if (branchTargetInstruction != null || branchTarget >= 0) {
                int target = branchTargetInstruction == null
                    ? mappedTarget(branchTarget, offsets, oldOffsets)
                    : offsets.get(branchTargetInstruction);
                int delta = target - offset;
                output.write(opcode);
                if (isWideBranch(opcode)) writeInt(output, delta);
                else {
                    if (delta < Short.MIN_VALUE || delta > Short.MAX_VALUE)
                        throw new TransformException("short branch overflow after transformation at " + offset);
                    output.write((delta >>> 8) & 0xff);
                    output.write(delta & 0xff);
                }
                return;
            }
            output.write(raw, 0, raw.length);
        }

        private int mappedTarget(int oldTarget, IdentityHashMap<Instruction, Integer> offsets,
                                 Map<Integer, Integer> oldOffsets) {
            Integer result = oldOffsets.get(oldTarget);
            if (result == null) throw new TransformException("branch target has no relocation: " + oldTarget);
            return result;
        }
    }

    static final class NestedAttributeRelocator {
        private NestedAttributeRelocator() { }

        static void relocate(List<AttributeModel> attributes, ConstantPool pool, Map<Integer, Integer> offsets) {
            for (AttributeModel attribute : attributes) {
                String name = attribute.name(pool);
                if (name.equals("LineNumberTable")) attribute.info = relocateLines(attribute.info, offsets);
                else if (name.equals("LocalVariableTable") || name.equals("LocalVariableTypeTable"))
                    attribute.info = relocateLocals(attribute.info, offsets);
                else if (name.equals("StackMapTable")) attribute.info = relocateFrames(attribute.info, offsets);
                else if (name.equals("StackMap")) attribute.info = relocateLegacyFrames(attribute.info, offsets);
            }
        }

        private static byte[] relocateLines(byte[] info, Map<Integer, Integer> offsets) {
            ByteBuffer input = ByteBuffer.wrap(info).order(ByteOrder.BIG_ENDIAN);
            int count = input.getShort() & 0xffff;
            byte[] body = new byte[2 + count * 4];
            ByteBuffer bodyBuffer = ByteBuffer.wrap(body).order(ByteOrder.BIG_ENDIAN);
            bodyBuffer.putShort((short) count);
            for (int i = 0; i < count; ++i) {
                int start = input.getShort() & 0xffff;
                bodyBuffer.putShort((short) map(offsets, start));
                bodyBuffer.putShort(input.getShort());
            }
            return body;
        }

        private static byte[] relocateLocals(byte[] info, Map<Integer, Integer> offsets) {
            ByteBuffer input = ByteBuffer.wrap(info).order(ByteOrder.BIG_ENDIAN);
            int count = input.getShort() & 0xffff;
            ByteArrayOutputStream output = new ByteArrayOutputStream(info.length);
            writeShort(output, count);
            for (int i = 0; i < count; ++i) {
                int start = input.getShort() & 0xffff;
                int length = input.getShort() & 0xffff;
                int end = map(offsets, start + length);
                writeShort(output, map(offsets, start));
                writeShort(output, Math.max(0, end - map(offsets, start)));
                writeShort(output, input.getShort() & 0xffff);
                writeShort(output, input.getShort() & 0xffff);
                writeShort(output, input.getShort() & 0xffff);
            }
            return output.toByteArray();
        }

        private static byte[] relocateFrames(byte[] info, Map<Integer, Integer> offsets) {
            // Decode/re-encode all frame forms, retaining their locals/stack
            // payloads.  Inserted transformer snippets are stack balanced.
            ByteBuffer input = ByteBuffer.wrap(info).order(ByteOrder.BIG_ENDIAN);
            int count = input.getShort() & 0xffff;
            ByteArrayOutputStream output = new ByteArrayOutputStream(info.length);
            writeShort(output, count);
            int oldPrevious = -1;
            int newPrevious = -1;
            for (int i = 0; i < count; ++i) {
                int frameType = input.get() & 0xff;
                FrameData frame = new FrameData(frameType);
                int oldDelta;
                if (frameType <= 63) oldDelta = frameType;
                else if (frameType <= 127) {
                    oldDelta = frameType - 64;
                    frame.stack = new VerificationType[] { readVerification(input) };
                } else if (frameType == 247) {
                    oldDelta = input.getShort() & 0xffff;
                    frame.stack = new VerificationType[] { readVerification(input) };
                    frame.kind = FrameKind.SAME_ONE;
                } else if (frameType >= 248 && frameType <= 250) {
                    oldDelta = input.getShort() & 0xffff;
                    frame.kind = FrameKind.CHOP;
                    frame.chop = 251 - frameType;
                } else if (frameType == 251) {
                    oldDelta = input.getShort() & 0xffff;
                    frame.kind = FrameKind.SAME;
                } else if (frameType >= 252 && frameType <= 254) {
                    oldDelta = input.getShort() & 0xffff;
                    frame.kind = FrameKind.APPEND;
                    frame.locals = new VerificationType[frameType - 251];
                    for (int j = 0; j < frame.locals.length; ++j) frame.locals[j] = readVerification(input);
                } else if (frameType == 255) {
                    oldDelta = input.getShort() & 0xffff;
                    frame.kind = FrameKind.FULL;
                    int locals = input.getShort() & 0xffff;
                    frame.locals = new VerificationType[locals];
                    for (int j = 0; j < locals; ++j) frame.locals[j] = readVerification(input);
                    int stack = input.getShort() & 0xffff;
                    frame.stack = new VerificationType[stack];
                    for (int j = 0; j < stack; ++j) frame.stack[j] = readVerification(input);
                } else throw new TransformException("bad stack-map frame type " + frameType);
                oldPrevious = oldPrevious + oldDelta + 1;
                int mapped = map(offsets, oldPrevious);
                int newDelta = mapped - newPrevious - 1;
                if (newDelta < 0 || newDelta > 65535)
                    throw new TransformException("StackMapTable offset_delta overflow at " + mapped);
                writeFrame(output, frame, newDelta);
                newPrevious = mapped;
            }
            return output.toByteArray();
        }

        private static byte[] relocateLegacyFrames(byte[] info, Map<Integer, Integer> offsets) {
            // Java 6's StackMap uses absolute offsets and verification_type
            // payloads.  The payload format can be relocated in place.
            ByteBuffer input = ByteBuffer.wrap(info).order(ByteOrder.BIG_ENDIAN);
            int count = input.getShort() & 0xffff;
            ByteArrayOutputStream output = new ByteArrayOutputStream(info.length);
            writeShort(output, count);
            for (int i = 0; i < count; ++i) {
                int old = input.getShort() & 0xffff;
                writeShort(output, map(offsets, old));
                copyVerificationList(input, output);
                copyVerificationList(input, output);
            }
            return output.toByteArray();
        }

        private static void copyVerificationList(ByteBuffer input, ByteArrayOutputStream output) {
            int count = input.getShort() & 0xffff;
            writeShort(output, count);
            for (int i = 0; i < count; ++i) copyVerification(input, output);
        }

        private static VerificationType readVerification(ByteBuffer input) {
            int tag = input.get() & 0xff;
            return new VerificationType(tag, tag == 7 || tag == 8 ? input.getShort() & 0xffff : -1);
        }

        private static void copyVerification(ByteBuffer input, ByteArrayOutputStream output) {
            VerificationType value = readVerification(input);
            output.write(value.tag);
            if (value.index >= 0) writeShort(output, value.index);
        }

        private static void writeVerification(ByteArrayOutputStream output, VerificationType value) {
            output.write(value.tag);
            if (value.index >= 0) writeShort(output, value.index);
        }

        private static void writeFrame(ByteArrayOutputStream output, FrameData frame, int delta) {
            if (frame.kind == FrameKind.SAME) {
                if (delta <= 63) output.write(delta);
                else { output.write(251); writeShort(output, delta); }
            } else if (frame.kind == FrameKind.SAME_ONE) {
                output.write(247);
                writeShort(output, delta);
                for (VerificationType value : frame.stack) writeVerification(output, value);
            } else if (frame.kind == FrameKind.CHOP) {
                output.write(251 - frame.chop);
                writeShort(output, delta);
            } else if (frame.kind == FrameKind.APPEND && frame.locals != null
                && frame.locals.length >= 1 && frame.locals.length <= 3) {
                output.write(251 + frame.locals.length);
                writeShort(output, delta);
                for (VerificationType value : frame.locals) writeVerification(output, value);
            } else {
                output.write(255);
                writeShort(output, delta);
                VerificationType[] locals = frame.locals == null ? new VerificationType[0] : frame.locals;
                VerificationType[] stack = frame.stack == null ? new VerificationType[0] : frame.stack;
                writeShort(output, locals.length);
                for (VerificationType value : locals) writeVerification(output, value);
                writeShort(output, stack.length);
                for (VerificationType value : stack) writeVerification(output, value);
            }
        }

        private static int map(Map<Integer, Integer> offsets, int old) {
            Integer result = offsets.get(old);
            if (result == null) throw new TransformException("attribute offset has no relocation: " + old);
            return result;
        }

        private enum FrameKind { SAME, SAME_ONE, CHOP, APPEND, FULL }

        private static final class FrameData {
            final int originalType;
            FrameKind kind;
            int chop;
            VerificationType[] locals;
            VerificationType[] stack;
            FrameData(int originalType) {
                this.originalType = originalType;
                this.kind = originalType <= 63 ? FrameKind.SAME : FrameKind.SAME_ONE;
                if (originalType >= 64 && originalType <= 127) {
                    this.stack = new VerificationType[1];
                }
            }
        }

        private record VerificationType(int tag, int index) { }
    }

    private static void ensure(boolean condition, int offset, int length) {
        if (!condition) throw malformed(offset, "truncated instruction (code length " + length + ")");
    }

    private static TransformException malformed(int offset, String message) {
        return new TransformException(message + " at bytecode offset " + offset);
    }

    private static int readInt(byte[] bytes, int position) {
        return ByteBuffer.wrap(bytes, position, 4).order(ByteOrder.BIG_ENDIAN).getInt();
    }

    private static int readUnsignedShort(byte[] bytes, int position) {
        return ((bytes[position] & 0xff) << 8) | (bytes[position + 1] & 0xff);
    }

    private static int signedShort(byte[] bytes, int position) {
        return (short) readUnsignedShort(bytes, position);
    }

    private static void writeInt(ByteArrayOutputStream output, int value) {
        output.write((value >>> 24) & 0xff);
        output.write((value >>> 16) & 0xff);
        output.write((value >>> 8) & 0xff);
        output.write(value & 0xff);
    }

    private static void writeShort(ByteArrayOutputStream output, int value) {
        output.write((value >>> 8) & 0xff);
        output.write(value & 0xff);
    }
}
