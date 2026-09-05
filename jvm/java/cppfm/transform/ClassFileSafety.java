package cppfm.transform;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Fail-closed structural verifier used before transformed bytes are defined. */
final class ClassFileSafety {
    private ClassFileSafety() { }

    static void validateBytes(byte[] bytes) {
        if (bytes == null) throw new TransformException("null transformed class bytes");
        validate(ClassFileModel.parse(bytes));
    }

    static void validate(ClassFileModel model) {
        if (model.major < 45 || model.major > 65)
            throw new TransformException("unsupported class-file major version " + model.major);
        validateMembers(model, model.fields, false);
        validateMembers(model, model.methods, true);
    }

    private static void validateMembers(ClassFileModel model, List<MemberModel> members, boolean methods) {
        for (MemberModel member : members) {
            for (AttributeModel attribute : member.attributes) {
                if (!attribute.name(model.pool).equals("Code")) continue;
                CodeModel code = CodeModel.parse(attribute.info, model.pool);
                if (!methods) throw new TransformException("field contains Code attribute");
                if (code.code.length == 0 || code.code.length > 65535)
                    throw new TransformException("invalid Code length for " + member.name(model.pool));
                if (code.maxStack > 65535 || code.maxLocals > 65535)
                    throw new TransformException("invalid Code limits for " + member.name(model.pool));
                validateInstructions(code.code);
                validateExceptions(code, member.name(model.pool));
                validateCodeAttributes(code.attributes, model.pool, code.code.length);
            }
        }
    }

    private static void validateInstructions(byte[] code) {
        List<BytecodeInstructions.Instruction> instructions = BytecodeInstructions.decode(code);
        Set<Integer> offsets = new HashSet<>();
        for (BytecodeInstructions.Instruction instruction : instructions) offsets.add(instruction.oldOffset);
        offsets.add(code.length);
        for (BytecodeInstructions.Instruction instruction : instructions) {
            if (instruction.branchTarget >= 0 && !offsets.contains(instruction.branchTarget))
                throw new TransformException("branch target is not an instruction: " + instruction.branchTarget);
            if (instruction.defaultTarget >= 0 && !offsets.contains(instruction.defaultTarget))
                throw new TransformException("switch default target is not an instruction: " + instruction.defaultTarget);
            if (instruction.switchTargets != null)
                for (int target : instruction.switchTargets)
                    if (!offsets.contains(target)) throw new TransformException("switch target is not an instruction: " + target);
        }
    }

    private static void validateExceptions(CodeModel code, String method) {
        for (CodeModel.ExceptionHandler handler : code.exceptionHandlers) {
            if (handler.startPc < 0 || handler.endPc > code.code.length || handler.startPc >= handler.endPc
                || handler.handlerPc < 0 || handler.handlerPc >= code.code.length)
                throw new TransformException("invalid exception table in " + method);
        }
    }

    private static void validateCodeAttributes(List<AttributeModel> attributes, ConstantPool pool, int codeLength) {
        for (AttributeModel attribute : attributes) {
            String name = attribute.name(pool);
            try {
                switch (name) {
                    case "LineNumberTable" -> validateLines(attribute.info, codeLength);
                    case "LocalVariableTable", "LocalVariableTypeTable" -> validateLocals(attribute.info, codeLength, pool);
                    case "StackMapTable" -> validateFrames(attribute.info, pool, codeLength);
                    case "StackMap" -> validateLegacyFrames(attribute.info, pool, codeLength);
                    default -> { }
                }
            } catch (IOException | RuntimeException failure) {
                if (failure instanceof TransformException exception) throw exception;
                throw new TransformException("invalid " + name + " attribute", failure);
            }
        }
    }

    private static void validateLines(byte[] info, int codeLength) throws IOException {
        DataInputStream input = input(info);
        int count = input.readUnsignedShort();
        for (int i = 0; i < count; ++i) {
            if (input.readUnsignedShort() >= codeLength) throw new TransformException("LineNumberTable offset outside Code");
            input.readUnsignedShort();
        }
        requireEnd(input);
    }

    private static void validateLocals(byte[] info, int codeLength, ConstantPool pool) throws IOException {
        DataInputStream input = input(info);
        int count = input.readUnsignedShort();
        for (int i = 0; i < count; ++i) {
            int start = input.readUnsignedShort();
            int length = input.readUnsignedShort();
            if (start > codeLength || start + length > codeLength) throw new TransformException("LocalVariableTable range outside Code");
            pool.utf8(input.readUnsignedShort());
            pool.utf8(input.readUnsignedShort());
            input.readUnsignedShort();
        }
        requireEnd(input);
    }

    private static void validateFrames(byte[] info, ConstantPool pool, int codeLength) throws IOException {
        DataInputStream input = input(info);
        int count = input.readUnsignedShort();
        int offset = -1;
        for (int i = 0; i < count; ++i) {
            int type = input.readUnsignedByte();
            int delta;
            if (type <= 63) delta = type;
            else if (type <= 127) { delta = type - 64; verification(input, pool); }
            else if (type == 247) { delta = input.readUnsignedShort(); verification(input, pool); }
            else if (type >= 248 && type <= 251) delta = input.readUnsignedShort();
            else if (type >= 252 && type <= 254) {
                delta = input.readUnsignedShort();
                for (int j = 0; j < type - 251; ++j) verification(input, pool);
            } else if (type == 255) {
                delta = input.readUnsignedShort();
                int locals = input.readUnsignedShort();
                for (int j = 0; j < locals; ++j) verification(input, pool);
                int stack = input.readUnsignedShort();
                for (int j = 0; j < stack; ++j) verification(input, pool);
            } else throw new TransformException("bad StackMapTable frame type " + type);
            offset += delta + 1;
            if (offset < 0 || offset >= codeLength) throw new TransformException("StackMapTable frame outside Code");
        }
        requireEnd(input);
    }

    private static void validateLegacyFrames(byte[] info, ConstantPool pool, int codeLength) throws IOException {
        DataInputStream input = input(info);
        int count = input.readUnsignedShort();
        for (int i = 0; i < count; ++i) {
            if (input.readUnsignedShort() >= codeLength) throw new TransformException("StackMap frame outside Code");
            verificationList(input, pool);
            verificationList(input, pool);
        }
        requireEnd(input);
    }

    private static void verificationList(DataInputStream input, ConstantPool pool) throws IOException {
        int count = input.readUnsignedShort();
        for (int i = 0; i < count; ++i) verification(input, pool);
    }

    private static void verification(DataInputStream input, ConstantPool pool) throws IOException {
        int tag = input.readUnsignedByte();
        if (tag < 0 || tag > 8) throw new TransformException("bad verification_type_info tag " + tag);
        if (tag == 7) pool.className(input.readUnsignedShort());
        else if (tag == 8) input.readUnsignedShort();
    }

    private static DataInputStream input(byte[] info) {
        return new DataInputStream(new ByteArrayInputStream(info));
    }

    private static void requireEnd(DataInputStream input) throws IOException {
        if (input.available() != 0) throw new TransformException("trailing Code attribute data");
    }
}
