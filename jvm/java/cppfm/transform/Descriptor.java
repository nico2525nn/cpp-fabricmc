package cppfm.transform;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** JVM descriptor parsing shared by the transformer and its public extension points. */
final class Descriptor {
    private Descriptor() { }

    static MethodDesc method(String descriptor) {
        if (descriptor == null || descriptor.length() < 3 || descriptor.charAt(0) != '(')
            throw new TransformException("invalid method descriptor: " + descriptor);
        ArrayList<Type> arguments = new ArrayList<>();
        int position = 1;
        while (descriptor.charAt(position) != ')') {
            ParseResult parsed = type(descriptor, position);
            arguments.add(parsed.type);
            position = parsed.next;
            if (position >= descriptor.length()) throw new TransformException("unterminated method descriptor");
        }
        ParseResult result = type(descriptor, position + 1);
        if (result.next != descriptor.length()) throw new TransformException("trailing descriptor data: " + descriptor);
        return new MethodDesc(arguments, result.type);
    }

    static Type type(String descriptor) {
        ParseResult result = type(descriptor, 0);
        if (result.next != descriptor.length()) throw new TransformException("trailing type descriptor: " + descriptor);
        return result.type;
    }

    private static ParseResult type(String descriptor, int position) {
        if (position < 0 || position >= descriptor.length()) throw new TransformException("invalid descriptor: " + descriptor);
        char kind = descriptor.charAt(position);
        if (kind == '[') {
            ParseResult component = type(descriptor, position + 1);
            return new ParseResult(new Type(descriptor.substring(position, component.next), 1,
                false, false, false, true), component.next);
        }
        if (kind == 'L') {
            int end = descriptor.indexOf(';', position + 1);
            if (end < 0) throw new TransformException("unterminated object descriptor: " + descriptor);
            return new ParseResult(new Type(descriptor.substring(position, end + 1), 1,
                true, false, false, false), end + 1);
        }
        if (kind == 'V') return new ParseResult(new Type("V", 0, false, true, false, false), position + 1);
        if ("ZBCSIF".indexOf(kind) >= 0)
            return new ParseResult(new Type(String.valueOf(kind), 1, false, false, true, false), position + 1);
        if (kind == 'J' || kind == 'D')
            return new ParseResult(new Type(String.valueOf(kind), 2, false, false, true, false), position + 1);
        throw new TransformException("unknown descriptor type '" + kind + "' in " + descriptor);
    }

    static final class Type {
        final String descriptor;
        final int slots;
        final boolean reference;
        final boolean voidType;
        final boolean primitive;
        final boolean array;

        Type(String descriptor, int slots, boolean reference, boolean voidType,
             boolean primitive, boolean array) {
            this.descriptor = descriptor;
            this.slots = slots;
            this.reference = reference;
            this.voidType = voidType;
            this.primitive = primitive;
            this.array = array;
        }

        boolean oneSlot() { return slots == 1; }
        boolean twoSlot() { return slots == 2; }
        boolean isIntLike() { return "ZBCSIF".indexOf(descriptor.charAt(0)) >= 0 && descriptor.charAt(0) != 'F'; }
    }

    static final class MethodDesc {
        final List<Type> arguments;
        final Type returnType;

        MethodDesc(List<Type> arguments, Type returnType) {
            this.arguments = Collections.unmodifiableList(new ArrayList<>(arguments));
            this.returnType = returnType;
        }

        int argumentSlots() {
            int result = 0;
            for (Type type : arguments) result += type.slots;
            return result;
        }
    }

    private record ParseResult(Type type, int next) { }
}
