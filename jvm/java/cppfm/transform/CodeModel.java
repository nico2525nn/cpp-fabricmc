package cppfm.transform;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Parsed Code attribute with opaque nested attributes and relocatable handlers. */
final class CodeModel {
    int maxStack;
    int maxLocals;
    byte[] code;
    final List<ExceptionHandler> exceptionHandlers = new ArrayList<>();
    final List<AttributeModel> attributes = new ArrayList<>();

    static CodeModel parse(byte[] info, ConstantPool pool) {
        try {
            DataInputStream input = new DataInputStream(new ByteArrayInputStream(info));
            CodeModel result = new CodeModel();
            result.maxStack = input.readUnsignedShort();
            result.maxLocals = input.readUnsignedShort();
            result.code = input.readNBytes(input.readInt());
            int exceptionCount = input.readUnsignedShort();
            for (int i = 0; i < exceptionCount; ++i) {
                ExceptionHandler handler = new ExceptionHandler();
                handler.startPc = input.readUnsignedShort();
                handler.endPc = input.readUnsignedShort();
                handler.handlerPc = input.readUnsignedShort();
                handler.catchType = input.readUnsignedShort();
                result.exceptionHandlers.add(handler);
            }
            result.attributes.addAll(ClassFileModel.readAttributes(input, pool));
            return result;
        } catch (IOException | IndexOutOfBoundsException failure) {
            throw new TransformException("invalid Code attribute", failure);
        }
    }

    byte[] write(ConstantPool pool) {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            output.writeShort(Math.min(65535, Math.max(0, maxStack)));
            output.writeShort(Math.min(65535, Math.max(0, maxLocals)));
            output.writeInt(code.length);
            output.write(code);
            output.writeShort(exceptionHandlers.size());
            for (ExceptionHandler handler : exceptionHandlers) {
                output.writeShort(handler.startPc);
                output.writeShort(handler.endPc);
                output.writeShort(handler.handlerPc);
                output.writeShort(handler.catchType);
            }
            ClassFileModel.writeAttributes(output, attributes);
            output.flush();
            return bytes.toByteArray();
        } catch (IOException failure) {
            throw new TransformException("cannot write Code attribute", failure);
        }
    }

    void stripDebugAndFrames(ConstantPool pool) {
        attributes.removeIf(attribute -> {
            String name = attribute.name(pool);
            return name.equals("StackMap") || name.equals("StackMapTable")
                || name.equals("LineNumberTable") || name.equals("LocalVariableTable")
                || name.equals("LocalVariableTypeTable");
        });
    }

    static final class ExceptionHandler {
        int startPc;
        int endPc;
        int handlerPc;
        int catchType;
    }
}
