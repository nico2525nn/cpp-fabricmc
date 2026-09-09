package cppfm.loader;

import cppfm.transform.ClassFileTransformer;
import cppfm.transform.TransformContext;
import cppfm.transform.TransformException;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Dependency-free class-file remapper for intermediary compiled mod code.
 *
 * <p>This runs before the structural Mixin transformer.  It rewrites class
 * constants, descriptors, field/method references, Mixin annotation strings,
 * and (for a registered mixin) shadow member declarations.  The remapper
 * deliberately edits constant-pool indexes instead of mutating shared
 * name/type entries in place, because a single JVM name/type entry can be
 * referenced by members with different owners.</p>
 */
public final class ClassFileNamespaceRemapper implements ClassFileTransformer {
    private final IntermediaryNamedMappings mappings;
    private final Set<String> mixinClasses = new HashSet<>();

    public ClassFileNamespaceRemapper(IntermediaryNamedMappings mappings) {
        this.mappings = mappings == null ? IntermediaryNamedMappings.identity() : mappings;
    }

    public IntermediaryNamedMappings mappings() {
        return mappings;
    }

    /** Mark a Mixin class so its @Shadow declaration names are remapped too. */
    public synchronized void registerMixinClass(String binaryName) {
        if (binaryName != null && !binaryName.isEmpty())
            mixinClasses.add(normalize(binaryName));
    }

    public synchronized boolean isMixinClass(String binaryName) {
        return mixinClasses.contains(normalize(binaryName));
    }

    /** Remap one class with the class name supplied by the loader. */
    @Override
    public byte[] transform(String binaryName, byte[] originalBytes, TransformContext context) {
        // The generated Minecraft shadow API is already in Yarn's named
        // namespace.  Remapping these classes would rewrite compatibility
        // aliases such as PistonBlock.field_10927 in their constant pool while
        // leaving the Java declaration untouched, which can turn a legal
        // <clinit> assignment into an IllegalAccessError against an inherited
        // final field.  Only mod bytecode (and explicitly registered mixins)
        // crosses the intermediary -> named edge.
        if (binaryName != null && binaryName.startsWith("net.minecraft.")
                && !isMixinClass(binaryName)) return originalBytes;
        return remap(binaryName, originalBytes, isMixinClass(binaryName));
    }

    /** Remap an annotation-bearing Mixin class before metadata inspection. */
    public byte[] remapMixin(String binaryName, byte[] originalBytes) {
        registerMixinClass(binaryName);
        return remap(binaryName, originalBytes, true);
    }

    /** Remap a class without applying the Mixin declaration-name policy. */
    public byte[] remap(String binaryName, byte[] originalBytes) {
        return remap(binaryName, originalBytes, false);
    }

    private byte[] remap(String binaryName, byte[] originalBytes, boolean mixin) {
        if (originalBytes == null) throw new NullPointerException("originalBytes");
        if (mappings.isIdentity()) return originalBytes;
        ClassFile file = ClassFile.read(originalBytes);
        boolean changed = file.remap(mappings, mixin);
        if (!changed) return originalBytes;
        return file.write();
    }

    private static String normalize(String name) {
        return name == null ? "" : name.replace('.', '/');
    }

    private static final class ClassFile {
        private final int minor;
        private final int major;
        private final ConstantPool pool;
        private final int access;
        private final int thisClass;
        private final int superClass;
        private final int[] interfaces;
        private final List<Member> fields;
        private final List<Member> methods;
        private final List<Attribute> attributes;

        private ClassFile(int minor, int major, ConstantPool pool, int access,
                          int thisClass, int superClass, int[] interfaces,
                          List<Member> fields, List<Member> methods,
                          List<Attribute> attributes) {
            this.minor = minor;
            this.major = major;
            this.pool = pool;
            this.access = access;
            this.thisClass = thisClass;
            this.superClass = superClass;
            this.interfaces = interfaces;
            this.fields = fields;
            this.methods = methods;
            this.attributes = attributes;
        }

        static ClassFile read(byte[] bytes) {
            try {
                DataInputStream input = new DataInputStream(new ByteArrayInputStream(bytes));
                if (input.readInt() != 0xCAFEBABE)
                    throw new IOException("class file magic is missing");
                int minor = input.readUnsignedShort();
                int major = input.readUnsignedShort();
                ConstantPool pool = ConstantPool.read(input);
                int access = input.readUnsignedShort();
                int thisClass = input.readUnsignedShort();
                int superClass = input.readUnsignedShort();
                int interfaceCount = input.readUnsignedShort();
                int[] interfaces = new int[interfaceCount];
                for (int i = 0; i < interfaceCount; ++i) interfaces[i] = input.readUnsignedShort();
                List<Member> fields = members(input, pool);
                List<Member> methods = members(input, pool);
                List<Attribute> attributes = attributes(input);
                if (input.available() != 0) throw new IOException("trailing class-file data");
                return new ClassFile(minor, major, pool, access, thisClass, superClass,
                    interfaces, fields, methods, attributes);
            } catch (IOException failure) {
                throw new TransformException("cannot parse class file for namespace remapping", failure);
            }
        }

        boolean remap(IntermediaryNamedMappings mappings, boolean mixin) {
            boolean changed = false;
            String selfOwner = pool.utf8((Integer) pool.entries.get(thisClass).a);
            String[] originalUtf8 = pool.snapshotUtf8();
            String[] originalClassNames = new String[pool.entries.size()];
            String[] originalNameTypeNames = new String[pool.entries.size()];
            String[] originalNameTypeDescriptors = new String[pool.entries.size()];
            for (int index = 1; index < pool.entries.size(); ++index) {
                CpEntry entry = pool.entries.get(index);
                if (entry == null) continue;
                if (entry.tag == 7 && entry.a > 0 && entry.a < originalUtf8.length)
                    originalClassNames[index] = originalUtf8[entry.a];
                if (entry.tag == 12) {
                    if (entry.a > 0 && entry.a < originalUtf8.length)
                        originalNameTypeNames[index] = originalUtf8[entry.a];
                    if (entry.b > 0 && entry.b < originalUtf8.length)
                        originalNameTypeDescriptors[index] = originalUtf8[entry.b];
                }
            }

            // Descriptors also occur in LocalVariableTable, Record and
            // annotation attributes, which all point at UTF-8 constants.
            // Replacing only descriptor-shaped strings keeps ordinary text
            // constants untouched.
            for (int index = 1; index < originalUtf8.length; ++index) {
                String value = originalUtf8[index];
                if (value == null) continue;
                String mapped = value;
                // Method/field descriptors start with `(`, `L`, or `[`.  A
                // class Signature attribute can instead start with formal
                // type parameters (`<T:...>`), while generic method/field
                // signatures can contain intermediary owners inside nested
                // type arguments.  Both use the same `Lowner;` grammar after
                // erasure, so send either shape through the generic-aware
                // descriptor mapper.
                if (looksLikeDescriptor(value) || (!value.isEmpty() && value.charAt(0) == '<'))
                    mapped = mappings.mapDescriptor(mapped);
                if (mixin) mapped = mappings.mapSymbol(mapped);
                if (!mapped.equals(value)) {
                    pool.setUtf8(index, mapped);
                    changed = true;
                }
            }

            // CONSTANT_Class names are independent of NameAndType entries.
            // Arrays use descriptor syntax even in a CONSTANT_Class.
            for (int index = 1; index < pool.entries.size(); ++index) {
                CpEntry entry = pool.entries.get(index);
                if (entry == null || entry.tag != 7) continue;
                String original = originalClassNames[index];
                if (original == null) continue;
                String mapped = original.charAt(0) == '['
                    ? mappings.mapDescriptor(original) : mappings.mapClassName(original);
                if (!mapped.equals(original)) {
                    entry.a = pool.addUtf8(mapped);
                    changed = true;
                }
            }

            // Descriptors on name/type entries are owner-independent and can
            // be replaced safely.  Member names are handled per reference
            // below because the same name/type can be shared by owners.
            for (int index = 1; index < pool.entries.size(); ++index) {
                CpEntry entry = pool.entries.get(index);
                if (entry == null || entry.tag != 12) continue;
                String original = originalNameTypeDescriptors[index];
                if (original == null) continue;
                String mapped = mappings.mapDescriptor(original);
                if (!mapped.equals(original)) {
                    entry.b = pool.addUtf8(mapped);
                    changed = true;
                }
            }

            for (int index = 1; index < pool.entries.size(); ++index) {
                CpEntry entry = pool.entries.get(index);
                if (entry == null || (entry.tag != 9 && entry.tag != 10 && entry.tag != 11)) continue;
                CpEntry ownerEntry = pool.entries.get(entry.a);
                if (ownerEntry == null || ownerEntry.tag != 7) continue;
                String owner = entry.a > 0 && entry.a < originalClassNames.length
                    ? originalClassNames[entry.a] : null;
                CpEntry nameType = pool.entries.get(entry.b);
                if (owner == null || nameType == null || nameType.tag != 12) continue;
                String name = originalNameTypeNames[entry.b];
                String descriptor = originalNameTypeDescriptors[entry.b];
                if (name == null || descriptor == null) continue;
                boolean field = entry.tag == 9;
                // A Mixin class is remapped as a real class too.  Its own
                // declarations are passed through mapSymbol below, so its
                // self-references must use the same symbol mapping.  Using
                // only the Minecraft owner/member table here can otherwise
                // leave PUTSTATIC/INVOKESTATIC pointing at the old
                // intermediary name after the declaration was renamed.
                boolean selfReference = mixin && owner.equals(selfOwner);
                String mappedName = selfReference ? mappings.mapSymbol(name) : field
                    ? mappings.mapFieldName(owner, name, descriptor)
                    : mappings.mapMethodName(owner, name, descriptor);
                String mappedDescriptor = mappings.mapDescriptor(descriptor);
                int nameIndex = pool.addUtf8(mappedName);
                int descriptorIndex = pool.addUtf8(mappedDescriptor);
                int nameTypeIndex = pool.addNameAndType(nameIndex, descriptorIndex);
                if (nameTypeIndex != entry.b) {
                    entry.b = nameTypeIndex;
                    changed = true;
                }
            }

            if (mixin) {
                for (Member member : fields) {
                    String name = pool.utf8(member.nameIndex);
                    String mapped = mappings.mapSymbol(name);
                    if (!mapped.equals(name)) {
                        member.nameIndex = pool.addUtf8(mapped);
                        changed = true;
                    }
                    String descriptor = pool.utf8(member.descriptorIndex);
                    String mappedDescriptor = mappings.mapDescriptor(descriptor);
                    if (!mappedDescriptor.equals(descriptor)) {
                        member.descriptorIndex = pool.addUtf8(mappedDescriptor);
                        changed = true;
                    }
                }
                for (Member member : methods) {
                    String name = pool.utf8(member.nameIndex);
                    String mapped = mappings.mapSymbol(name);
                    if (!mapped.equals(name)) {
                        member.nameIndex = pool.addUtf8(mapped);
                        changed = true;
                    }
                    String descriptor = pool.utf8(member.descriptorIndex);
                    String mappedDescriptor = mappings.mapDescriptor(descriptor);
                    if (!mappedDescriptor.equals(descriptor)) {
                        member.descriptorIndex = pool.addUtf8(mappedDescriptor);
                        changed = true;
                    }
                }
            }
            return changed;
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
                output.writeShort(interfaces.length);
                for (int value : interfaces) output.writeShort(value);
                writeMembers(output, fields);
                writeMembers(output, methods);
                writeAttributes(output, attributes);
                output.flush();
                return bytes.toByteArray();
            } catch (IOException failure) {
                throw new TransformException("cannot write remapped class file", failure);
            }
        }

        private static List<Member> members(DataInputStream input, ConstantPool pool) throws IOException {
            int count = input.readUnsignedShort();
            ArrayList<Member> output = new ArrayList<>(count);
            for (int i = 0; i < count; ++i)
                output.add(new Member(input.readUnsignedShort(), input.readUnsignedShort(),
                    input.readUnsignedShort(), attributes(input)));
            return output;
        }

        private static List<Attribute> attributes(DataInputStream input) throws IOException {
            int count = input.readUnsignedShort();
            ArrayList<Attribute> output = new ArrayList<>(count);
            for (int i = 0; i < count; ++i) {
                int nameIndex = input.readUnsignedShort();
                int length = input.readInt();
                if (length < 0) throw new IOException("negative attribute length");
                byte[] info = input.readNBytes(length);
                if (info.length != length) throw new IOException("truncated attribute");
                output.add(new Attribute(nameIndex, info));
            }
            return output;
        }

        private static void writeMembers(DataOutputStream output, List<Member> members) throws IOException {
            output.writeShort(members.size());
            for (Member member : members) {
                output.writeShort(member.access);
                output.writeShort(member.nameIndex);
                output.writeShort(member.descriptorIndex);
                writeAttributes(output, member.attributes);
            }
        }

        private static void writeAttributes(DataOutputStream output, List<Attribute> attributes) throws IOException {
            output.writeShort(attributes.size());
            for (Attribute attribute : attributes) {
                output.writeShort(attribute.nameIndex);
                output.writeInt(attribute.info.length);
                output.write(attribute.info);
            }
        }

        private static boolean looksLikeDescriptor(String value) {
            if (value.isEmpty()) return false;
            char first = value.charAt(0);
            return first == 'L' || first == '[' || first == '(';
        }
    }

    private static final class Member {
        private final int access;
        private int nameIndex;
        private int descriptorIndex;
        private final List<Attribute> attributes;

        private Member(int access, int nameIndex, int descriptorIndex, List<Attribute> attributes) {
            this.access = access;
            this.nameIndex = nameIndex;
            this.descriptorIndex = descriptorIndex;
            this.attributes = attributes;
        }
    }

    private record Attribute(int nameIndex, byte[] info) { }

    private static final class CpEntry {
        private final int tag;
        private int a;
        private int b;
        private long wide;
        private String text;

        private CpEntry(int tag) { this.tag = tag; }
    }

    private static final class ConstantPool {
        private final List<CpEntry> entries;

        private ConstantPool(List<CpEntry> entries) {
            this.entries = entries;
        }

        static ConstantPool read(DataInputStream input) throws IOException {
            int count = input.readUnsignedShort();
            ArrayList<CpEntry> entries = new ArrayList<>(count);
            entries.add(null);
            for (int index = 1; index < count; ++index) {
                int tag = input.readUnsignedByte();
                CpEntry entry = new CpEntry(tag);
                switch (tag) {
                    case 1 -> entry.text = input.readUTF();
                    case 3, 4 -> entry.a = input.readInt();
                    case 5, 6 -> {
                        entry.wide = input.readLong();
                        entries.add(entry);
                        entries.add(null);
                        ++index;
                        continue;
                    }
                    case 7, 8, 16, 19, 20 -> entry.a = input.readUnsignedShort();
                    case 9, 10, 11, 12, 17, 18 -> {
                        entry.a = input.readUnsignedShort();
                        entry.b = input.readUnsignedShort();
                    }
                    case 15 -> {
                        entry.a = input.readUnsignedByte();
                        entry.b = input.readUnsignedShort();
                    }
                    default -> throw new IOException("unsupported constant-pool tag " + tag);
                }
                entries.add(entry);
            }
            return new ConstantPool(entries);
        }

        String[] snapshotUtf8() {
            String[] output = new String[entries.size()];
            for (int index = 1; index < entries.size(); ++index) {
                CpEntry entry = entries.get(index);
                if (entry != null && entry.tag == 1) output[index] = entry.text;
            }
            return output;
        }

        String utf8(int index) {
            CpEntry entry = index > 0 && index < entries.size() ? entries.get(index) : null;
            if (entry == null || entry.tag != 1) throw new TransformException("constant is not UTF-8: " + index);
            return entry.text;
        }

        void setUtf8(int index, String value) {
            CpEntry entry = entries.get(index);
            if (entry == null || entry.tag != 1) throw new TransformException("constant is not UTF-8: " + index);
            entry.text = value;
        }

        int addUtf8(String value) {
            for (int index = 1; index < entries.size(); ++index) {
                CpEntry entry = entries.get(index);
                if (entry != null && entry.tag == 1 && value.equals(entry.text)) return index;
            }
            CpEntry entry = new CpEntry(1);
            entry.text = value;
            if (entries.size() >= 65535) throw new TransformException("constant pool overflow while remapping");
            entries.add(entry);
            return entries.size() - 1;
        }

        int addNameAndType(int nameIndex, int descriptorIndex) {
            for (int index = 1; index < entries.size(); ++index) {
                CpEntry entry = entries.get(index);
                if (entry != null && entry.tag == 12 && entry.a == nameIndex && entry.b == descriptorIndex)
                    return index;
            }
            CpEntry entry = new CpEntry(12);
            entry.a = nameIndex;
            entry.b = descriptorIndex;
            if (entries.size() >= 65535) throw new TransformException("constant pool overflow while remapping");
            entries.add(entry);
            return entries.size() - 1;
        }

        void write(DataOutputStream output) throws IOException {
            output.writeShort(entries.size());
            for (int index = 1; index < entries.size(); ++index) {
                CpEntry entry = entries.get(index);
                if (entry == null) continue;
                output.writeByte(entry.tag);
                switch (entry.tag) {
                    case 1 -> output.writeUTF(entry.text);
                    case 3, 4 -> output.writeInt(entry.a);
                    case 5, 6 -> output.writeLong(entry.wide);
                    case 7, 8, 16, 19, 20 -> output.writeShort(entry.a);
                    case 9, 10, 11, 12, 17, 18 -> {
                        output.writeShort(entry.a);
                        output.writeShort(entry.b);
                    }
                    case 15 -> {
                        output.writeByte(entry.a);
                        output.writeShort(entry.b);
                    }
                    default -> throw new IOException("unsupported constant-pool tag " + entry.tag);
                }
            }
        }
    }
}
