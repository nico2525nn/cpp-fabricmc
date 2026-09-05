package cppfm.transform;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Runtime annotation parser used without loading mixin classes. */
final class AnnotationModel {
    final String descriptor;
    final Map<String, ElementValue> values;

    AnnotationModel(String descriptor, Map<String, ElementValue> values) {
        this.descriptor = descriptor;
        this.values = Collections.unmodifiableMap(new LinkedHashMap<>(values));
    }

    String simpleName() {
        int slash = descriptor.lastIndexOf('/');
        int end = descriptor.endsWith(";") ? descriptor.length() - 1 : descriptor.length();
        return descriptor.substring(slash + 1, end);
    }

    ElementValue value(String name) { return values.get(name); }

    String string(String name, String fallback) {
        ElementValue value = value(name);
        return value != null && value.value instanceof String string ? string : fallback;
    }

    int integer(String name, int fallback) {
        ElementValue value = value(name);
        if (value == null) return fallback;
        if (value.value instanceof Number number) return number.intValue();
        return fallback;
    }

    boolean bool(String name, boolean fallback) {
        ElementValue value = value(name);
        return value != null && value.value instanceof Boolean booleanValue ? booleanValue : fallback;
    }

    AnnotationModel annotation(String name) {
        ElementValue value = value(name);
        return value != null && value.value instanceof AnnotationModel annotation ? annotation : null;
    }

    List<ElementValue> array(String name) {
        ElementValue value = value(name);
        return value != null && value.value instanceof List<?> list
            ? castElements(list) : List.of();
    }

    List<String> strings(String name) {
        ArrayList<String> output = new ArrayList<>();
        ElementValue value = value(name);
        if (value == null) return output;
        if (value.value instanceof String string) output.add(string);
        else if (value.value instanceof List<?> list)
            for (Object item : list) if (item instanceof ElementValue element && element.value instanceof String string) output.add(string);
        return output;
    }

    static List<AnnotationModel> fromAttributes(List<AttributeModel> attributes, ConstantPool pool) {
        ArrayList<AnnotationModel> output = new ArrayList<>();
        for (AttributeModel attribute : attributes) {
            String name = attribute.name(pool);
            if (name.equals("RuntimeVisibleAnnotations") || name.equals("RuntimeInvisibleAnnotations"))
                output.addAll(parseAttribute(attribute.info, pool));
        }
        return output;
    }

    static AnnotationModel first(List<AttributeModel> attributes, ConstantPool pool, String simpleName) {
        for (AnnotationModel annotation : fromAttributes(attributes, pool))
            if (annotation.simpleName().equals(simpleName)) return annotation;
        return null;
    }

    private static List<AnnotationModel> parseAttribute(byte[] bytes, ConstantPool pool) {
        try {
            DataInputStream input = new DataInputStream(new ByteArrayInputStream(bytes));
            int count = input.readUnsignedShort();
            ArrayList<AnnotationModel> output = new ArrayList<>();
            for (int i = 0; i < count; ++i) output.add(readAnnotation(input, pool));
            return output;
        } catch (IOException failure) {
            throw new TransformException("invalid annotation attribute", failure);
        }
    }

    private static AnnotationModel readAnnotation(DataInputStream input, ConstantPool pool) throws IOException {
        String descriptor = pool.utf8(input.readUnsignedShort());
        int count = input.readUnsignedShort();
        LinkedHashMap<String, ElementValue> values = new LinkedHashMap<>();
        for (int i = 0; i < count; ++i)
            values.put(pool.utf8(input.readUnsignedShort()), readValue(input, pool));
        return new AnnotationModel(descriptor, values);
    }

    private static ElementValue readValue(DataInputStream input, ConstantPool pool) throws IOException {
        char tag = (char) input.readUnsignedByte();
        return switch (tag) {
            case 'B' -> new ElementValue(tag, ((Number) pool.constantValue(input.readUnsignedShort())).byteValue());
            case 'C' -> new ElementValue(tag, (char) ((Number) pool.constantValue(input.readUnsignedShort())).intValue());
            case 'D', 'F', 'I', 'J', 'S' -> new ElementValue(tag, pool.constantValue(input.readUnsignedShort()));
            case 'Z' -> new ElementValue(tag, ((Number) pool.constantValue(input.readUnsignedShort())).intValue() != 0);
            case 's' -> new ElementValue(tag, pool.stringValue(input.readUnsignedShort()));
            case 'e' -> {
                String enumType = pool.utf8(input.readUnsignedShort());
                String enumName = pool.utf8(input.readUnsignedShort());
                yield new ElementValue(tag, new EnumValue(enumType, enumName));
            }
            case 'c' -> new ElementValue(tag, pool.utf8(input.readUnsignedShort()));
            case '@' -> new ElementValue(tag, readAnnotation(input, pool));
            case '[' -> {
                int count = input.readUnsignedShort();
                ArrayList<ElementValue> values = new ArrayList<>();
                for (int i = 0; i < count; ++i) values.add(readValue(input, pool));
                yield new ElementValue(tag, values);
            }
            default -> throw new TransformException("unsupported annotation element tag " + tag);
        };
    }

    private static List<ElementValue> castElements(List<?> values) {
        ArrayList<ElementValue> output = new ArrayList<>();
        for (Object value : values) if (value instanceof ElementValue element) output.add(element);
        return output;
    }

    static final class ElementValue {
        final char tag;
        final Object value;
        ElementValue(char tag, Object value) { this.tag = tag; this.value = value; }
    }

    record EnumValue(String descriptor, String name) { }
}
