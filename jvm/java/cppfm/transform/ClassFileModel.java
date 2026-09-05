package cppfm.transform;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Internal class-file model.  It deliberately keeps unknown attributes opaque. */
final class ClassFileModel {
    static final int ACC_STATIC = 0x0008;
    static final int ACC_NATIVE = 0x0100;
    static final int ACC_ABSTRACT = 0x0400;
    static final int ACC_SYNTHETIC = 0x1000;

    int minor;
    int major;
    int access;
    int thisClass;
    int superClass;
    ConstantPool pool;
    final List<Integer> interfaces = new ArrayList<>();
    final List<MemberModel> fields = new ArrayList<>();
    final List<MemberModel> methods = new ArrayList<>();
    final List<AttributeModel> attributes = new ArrayList<>();

    static ClassFileModel parse(byte[] bytes) {
        try {
            DataInputStream input = new DataInputStream(new ByteArrayInputStream(bytes));
            if (input.readInt() != 0xCAFEBABE) throw new TransformException("not a JVM class file");
            ClassFileModel result = new ClassFileModel();
            result.minor = input.readUnsignedShort();
            result.major = input.readUnsignedShort();
            result.pool = ConstantPool.read(input);
            result.access = input.readUnsignedShort();
            result.thisClass = input.readUnsignedShort();
            result.superClass = input.readUnsignedShort();
            for (int i = 0, count = input.readUnsignedShort(); i < count; ++i)
                result.interfaces.add(input.readUnsignedShort());
            readMembers(input, result.pool, result.fields);
            readMembers(input, result.pool, result.methods);
            result.attributes.addAll(readAttributes(input, result.pool));
            return result;
        } catch (IOException | IndexOutOfBoundsException failure) {
            throw new TransformException("invalid class file", failure);
        }
    }

    private static void readMembers(DataInputStream input, ConstantPool pool,
                                    List<MemberModel> output) throws IOException {
        for (int i = 0, count = input.readUnsignedShort(); i < count; ++i) {
            MemberModel member = new MemberModel();
            member.access = input.readUnsignedShort();
            member.nameIndex = input.readUnsignedShort();
            member.descriptorIndex = input.readUnsignedShort();
            member.attributes.addAll(readAttributes(input, pool));
            output.add(member);
        }
    }

    static List<AttributeModel> readAttributes(DataInputStream input, ConstantPool pool) throws IOException {
        ArrayList<AttributeModel> output = new ArrayList<>();
        for (int i = 0, count = input.readUnsignedShort(); i < count; ++i) {
            int nameIndex = input.readUnsignedShort();
            int length = input.readInt();
            if (length < 0 || length > input.available()) throw new TransformException("invalid attribute length");
            byte[] info = input.readNBytes(length);
            output.add(new AttributeModel(nameIndex, info));
        }
        return output;
    }

    String internalName() {
        return pool.className(thisClass);
    }

    String binaryName() {
        return internalName().replace('/', '.');
    }

    MemberModel method(String name, String descriptor) {
        for (MemberModel member : methods)
            if (name.equals(member.name(pool)) && descriptor.equals(member.descriptor(pool))) return member;
        return null;
    }

    List<MemberModel> methodsNamed(String name) {
        ArrayList<MemberModel> output = new ArrayList<>();
        for (MemberModel member : methods) if (name.equals(member.name(pool))) output.add(member);
        return output;
    }

    MemberModel field(String name, String descriptor) {
        for (MemberModel member : fields)
            if (name.equals(member.name(pool)) && (descriptor == null || descriptor.equals(member.descriptor(pool)))) return member;
        return null;
    }

    void addMethod(MemberModel method) {
        methods.add(method);
    }

    AttributeModel attribute(ConstantPool pool, String name) {
        for (AttributeModel attribute : attributes) if (name.equals(attribute.name(pool))) return attribute;
        return null;
    }

    byte[] write() {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            output.writeInt(0xCAFEBABE);
            output.writeShort(minor);
            output.writeShort(major);
            pool.write(output);
            output.writeShort(access);
            output.writeShort(thisClass);
            output.writeShort(superClass);
            output.writeShort(interfaces.size());
            for (int value : interfaces) output.writeShort(value);
            writeMembers(output, fields);
            writeMembers(output, methods);
            writeAttributes(output, attributes);
            output.flush();
            return bytes.toByteArray();
        } catch (IOException failure) {
            throw new TransformException("cannot write class file", failure);
        }
    }

    private void writeMembers(DataOutputStream output, List<MemberModel> members) throws IOException {
        output.writeShort(members.size());
        for (MemberModel member : members) {
            output.writeShort(member.access);
            output.writeShort(member.nameIndex);
            output.writeShort(member.descriptorIndex);
            writeAttributes(output, member.attributes);
        }
    }

    static void writeAttributes(DataOutputStream output, List<AttributeModel> attributes) throws IOException {
        output.writeShort(attributes.size());
        for (AttributeModel attribute : attributes) {
            output.writeShort(attribute.nameIndex);
            output.writeInt(attribute.info.length);
            output.write(attribute.info);
        }
    }
}

final class MemberModel {
    int access;
    int nameIndex;
    int descriptorIndex;
    final List<AttributeModel> attributes = new ArrayList<>();

    String name(ConstantPool pool) { return pool.utf8(nameIndex); }
    String descriptor(ConstantPool pool) { return pool.utf8(descriptorIndex); }

    AttributeModel attribute(ConstantPool pool, String name) {
        for (AttributeModel attribute : attributes) if (name.equals(attribute.name(pool))) return attribute;
        return null;
    }

    CodeModel code(ConstantPool pool) {
        AttributeModel attribute = attribute(pool, "Code");
        return attribute == null ? null : CodeModel.parse(attribute.info, pool);
    }

    void replaceCode(ConstantPool pool, CodeModel code) {
        AttributeModel attribute = attribute(pool, "Code");
        if (attribute == null) attributes.add(new AttributeModel(pool.addUtf8("Code"), code.write(pool)));
        else attribute.info = code.write(pool);
    }
}

final class AttributeModel {
    int nameIndex;
    byte[] info;

    AttributeModel(int nameIndex, byte[] info) {
        this.nameIndex = nameIndex;
        this.info = info;
    }

    String name(ConstantPool pool) { return pool.utf8(nameIndex); }
}

final class ConstantPool {
    static final class Entry {
        final int tag;
        final Object value;

        Entry(int tag, Object value) {
            this.tag = tag;
            this.value = value;
        }
    }

    private final ArrayList<Entry> entries = new ArrayList<>();
    private final HashMap<String, Integer> interned = new HashMap<>();

    private ConstantPool() {
        entries.add(null);
    }

    static ConstantPool read(DataInputStream input) throws IOException {
        ConstantPool pool = new ConstantPool();
        int count = input.readUnsignedShort();
        for (int index = 1; index < count; ++index) {
            int tag = input.readUnsignedByte();
            Object value;
            switch (tag) {
                case 1 -> value = input.readUTF();
                case 3 -> value = input.readInt();
                case 4 -> value = input.readFloat();
                case 5 -> {
                    value = input.readLong();
                    pool.entries.add(new Entry(tag, value));
                    pool.entries.add(null);
                    ++index;
                    continue;
                }
                case 6 -> {
                    value = input.readDouble();
                    pool.entries.add(new Entry(tag, value));
                    pool.entries.add(null);
                    ++index;
                    continue;
                }
                case 7, 8, 16, 19, 20 -> value = input.readUnsignedShort();
                case 9, 10, 11, 12, 17, 18 -> value = new int[] { input.readUnsignedShort(), input.readUnsignedShort() };
                case 15 -> value = new int[] { input.readUnsignedByte(), input.readUnsignedShort() };
                default -> throw new TransformException("unsupported constant-pool tag " + tag);
            }
            pool.entries.add(new Entry(tag, value));
        }
        pool.rebuildInterned();
        return pool;
    }

    private void rebuildInterned() {
        interned.clear();
        for (int i = 1; i < entries.size(); ++i) {
            Entry entry = entries.get(i);
            if (entry != null) interned.putIfAbsent(key(entry), i);
        }
    }

    private static String key(Entry entry) {
        if (entry == null) return "null";
        if (entry.value instanceof int[] array) return entry.tag + ":" + Arrays.toString(array);
        return entry.tag + ":" + String.valueOf(entry.value);
    }

    Entry entry(int index) {
        if (index <= 0 || index >= entries.size() || entries.get(index) == null)
            throw new TransformException("invalid constant-pool index " + index);
        return entries.get(index);
    }

    String utf8(int index) {
        Entry entry = entry(index);
        if (entry.tag != 1) throw new TransformException("constant-pool entry " + index + " is not UTF8");
        return (String) entry.value;
    }

    String className(int index) {
        Entry entry = entry(index);
        if (entry.tag != 7) throw new TransformException("constant-pool entry " + index + " is not Class");
        return utf8((Integer) entry.value);
    }

    String stringValue(int index) {
        Entry entry = entry(index);
        if (entry.tag == 8) return utf8((Integer) entry.value);
        if (entry.tag == 1) return (String) entry.value;
        return null;
    }

    Object constantValue(int index) {
        Entry entry = entry(index);
        return switch (entry.tag) {
            case 3, 4, 5, 6 -> entry.value;
            case 8 -> utf8((Integer) entry.value);
            case 7 -> className(index);
            default -> null;
        };
    }

    String memberOwner(int index) {
        Entry entry = entry(index);
        if (entry.tag != 9 && entry.tag != 10 && entry.tag != 11)
            throw new TransformException("constant-pool entry " + index + " is not a member reference");
        return className((Integer) ((int[]) entry.value)[0]);
    }

    String memberName(int index) {
        int nameType = memberNameTypeIndex(index);
        return utf8(((int[]) entry(nameType).value)[0]);
    }

    String memberDescriptor(int index) {
        int nameType = memberNameTypeIndex(index);
        return utf8(((int[]) entry(nameType).value)[1]);
    }

    int memberNameTypeIndex(int index) {
        Entry entry = entry(index);
        if (entry.tag != 9 && entry.tag != 10 && entry.tag != 11)
            throw new TransformException("constant-pool entry is not a member reference");
        return ((int[]) entry.value)[1];
    }

    int tag(int index) { return entry(index).tag; }

    int addUtf8(String value) { return add(new Entry(1, value)); }
    int addInteger(int value) { return add(new Entry(3, value)); }
    int addFloat(float value) { return add(new Entry(4, value)); }
    int addLong(long value) { return add(new Entry(5, value)); }
    int addDouble(double value) { return add(new Entry(6, value)); }
    int addClass(String internalName) { return add(new Entry(7, addUtf8(internalName))); }
    int addString(String value) { return add(new Entry(8, addUtf8(value))); }
    int addNameType(String name, String descriptor) {
        return add(new Entry(12, new int[] { addUtf8(name), addUtf8(descriptor) }));
    }
    int addFieldRef(String owner, String name, String descriptor) {
        return add(new Entry(9, new int[] { addClass(owner), addNameType(name, descriptor) }));
    }
    int addMethodRef(String owner, String name, String descriptor, boolean interfaceMethod) {
        return add(new Entry(interfaceMethod ? 11 : 10,
            new int[] { addClass(owner), addNameType(name, descriptor) }));
    }
    int addMethodHandle(int kind, int reference) {
        return add(new Entry(15, new int[] { kind, reference }));
    }
    int addMethodType(String descriptor) { return add(new Entry(16, addUtf8(descriptor))); }

    boolean containsTag(int tag) {
        for (Entry entry : entries) if (entry != null && entry.tag == tag) return true;
        return false;
    }

    private int add(Entry entry) {
        String key = key(entry);
        Integer existing = interned.get(key);
        if (existing != null) return existing;
        int index = entries.size();
        entries.add(entry);
        if (entry.tag == 5 || entry.tag == 6) entries.add(null);
        interned.put(key, index);
        return index;
    }

    /** Import a reference recursively while remapping mixin-owned members. */
    int importEntry(ConstantPool source, int sourceIndex, String sourceOwner,
                    String targetOwner, Map<String, String> methodRenames,
                    Map<String, String> fieldRenames) {
        return importEntry(source, sourceIndex, sourceOwner, targetOwner, methodRenames,
            fieldRenames, 0);
    }

    /**
     * Import a reference and relocate CONSTANT_Dynamic/InvokeDynamic bootstrap
     * indexes by the number of bootstrap methods already present in the target.
     */
    int importEntry(ConstantPool source, int sourceIndex, String sourceOwner,
                    String targetOwner, Map<String, String> methodRenames,
                    Map<String, String> fieldRenames, int bootstrapOffset) {
        Entry sourceEntry = source.entry(sourceIndex);
        return switch (sourceEntry.tag) {
            case 1 -> addUtf8((String) sourceEntry.value);
            case 3 -> addInteger((Integer) sourceEntry.value);
            case 4 -> addFloat((Float) sourceEntry.value);
            case 5 -> addLong((Long) sourceEntry.value);
            case 6 -> addDouble((Double) sourceEntry.value);
            case 7 -> addClass(remapClassName(source.className(sourceIndex), sourceOwner, targetOwner));
            case 8 -> addString(source.utf8((Integer) sourceEntry.value));
            case 12 -> {
                int[] pair = (int[]) sourceEntry.value;
                yield addNameType(source.utf8(pair[0]), remapDescriptor(source.utf8(pair[1]), sourceOwner, targetOwner));
            }
            case 9, 10, 11 -> {
                String owner = source.memberOwner(sourceIndex);
                String name = source.memberName(sourceIndex);
                String descriptor = source.memberDescriptor(sourceIndex);
                boolean own = owner.equals(sourceOwner);
                String memberKey = name + descriptor;
                // An ordinary static helper field belongs to the mixin class
                // and is initialized by that class' <clinit>.  An empty
                // field-rename value is the explicit preserve-owner marker;
                // shadow/instance fields still move onto the target class.
                boolean preserveHelperField = own && sourceEntry.tag == 9
                    && fieldRenames.containsKey(memberKey)
                    && fieldRenames.get(memberKey).isEmpty();
                String remappedOwner = own && !preserveHelperField ? targetOwner : owner;
                if (own && !preserveHelperField) {
                    if (sourceEntry.tag == 9) name = fieldRenames.getOrDefault(memberKey, name);
                    else name = methodRenames.getOrDefault(memberKey, name);
                }
                descriptor = remapDescriptor(descriptor, sourceOwner, targetOwner);
                yield sourceEntry.tag == 9
                    ? addFieldRef(remappedOwner, name, descriptor)
                    : addMethodRef(remappedOwner, name, descriptor, sourceEntry.tag == 11);
            }
            case 15 -> {
                int[] pair = (int[]) sourceEntry.value;
                yield addMethodHandle(pair[0], importEntry(source, pair[1], sourceOwner, targetOwner,
                    methodRenames, fieldRenames, bootstrapOffset));
            }
            case 16 -> addMethodType(remapDescriptor(source.utf8((Integer) sourceEntry.value), sourceOwner, targetOwner));
            case 17, 18 -> {
                int[] pair = (int[]) sourceEntry.value;
                if (bootstrapOffset < 0)
                    throw new TransformException("dynamic constant is missing BootstrapMethods");
                int bootstrapIndex = pair[0] + bootstrapOffset;
                if (bootstrapIndex < 0 || bootstrapIndex > 65535)
                    throw new TransformException("bootstrap method index out of range: " + bootstrapIndex);
                yield add(new Entry(sourceEntry.tag, new int[] {
                    bootstrapIndex, importEntry(source, pair[1], sourceOwner, targetOwner,
                        methodRenames, fieldRenames, bootstrapOffset)
                }));
            }
            case 19, 20 -> add(new Entry(sourceEntry.tag, addUtf8(source.utf8((Integer) sourceEntry.value))));
            default -> throw new TransformException("cannot import constant-pool tag " + sourceEntry.tag);
        };
    }

    static String remapClassName(String name, String sourceOwner, String targetOwner) {
        if (name.equals(sourceOwner)) return targetOwner;
        return remapDescriptor(name, sourceOwner, targetOwner);
    }

    static String remapDescriptor(String descriptor, String sourceOwner, String targetOwner) {
        return descriptor.replace("L" + sourceOwner + ";", "L" + targetOwner + ";");
    }

    void write(DataOutputStream output) throws IOException {
        output.writeShort(entries.size());
        for (int i = 1; i < entries.size(); ++i) {
            Entry entry = entries.get(i);
            if (entry == null) continue;
            output.writeByte(entry.tag);
            switch (entry.tag) {
                case 1 -> output.writeUTF((String) entry.value);
                case 3 -> output.writeInt((Integer) entry.value);
                case 4 -> output.writeFloat((Float) entry.value);
                case 5 -> output.writeLong((Long) entry.value);
                case 6 -> output.writeDouble((Double) entry.value);
                case 7, 8, 16, 19, 20 -> output.writeShort((Integer) entry.value);
                case 9, 10, 11, 12, 17, 18 -> {
                    int[] pair = (int[]) entry.value;
                    output.writeShort(pair[0]);
                    output.writeShort(pair[1]);
                }
                case 15 -> {
                    int[] pair = (int[]) entry.value;
                    output.writeByte(pair[0]);
                    output.writeShort(pair[1]);
                }
                default -> throw new TransformException("cannot write constant-pool tag " + entry.tag);
            }
        }
    }
}

final class Hashes {
    private Hashes() { }

    static String sha256(byte[] bytes) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(bytes);
            StringBuilder output = new StringBuilder(digest.length * 2);
            for (byte value : digest) output.append(String.format("%02x", value & 0xff));
            return output.toString();
        } catch (NoSuchAlgorithmException failure) {
            throw new AssertionError(failure);
        }
    }
}
