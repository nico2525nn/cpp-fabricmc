package cppfm.transform;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;

/** JVM instruction decoding, relocation and insertion utilities. */
final class BytecodeInstructions {
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
        ArrayList<Instruction> result = new ArrayList<>();
        for (Instruction instruction : source) {
            Instruction copy = instruction.copy();
            int sourceCp = cpIndex(instruction);
            if (sourceCp > 0) {
                int targetCp = targetPool.importEntry(sourcePool, sourceCp, sourceOwner, targetOwner,
                    methodRenames, fieldRenames);
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
            this.code = code;
            this.originalLength = code.code.length;
            this.instructions = new ArrayList<>(decode(code.code));
            this.nextLocal = code.maxLocals;
        }

        int allocateLocal(Descriptor.Type type) {
            int result = nextLocal;
            nextLocal += type.slots;
            return result;
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
            result.label = true;
            result.opcode = -1;
            result.raw = new byte[0];
            return result;
        }

        static Instruction branch(int opcode, Instruction target) {
            Instruction result = new Instruction();
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
