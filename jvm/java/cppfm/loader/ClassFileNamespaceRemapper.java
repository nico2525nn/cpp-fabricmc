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
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
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
    private static final String MIXIN_DESCRIPTOR =
        "Lorg/spongepowered/asm/mixin/Mixin;";
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

    static boolean hasMixinClassAnnotation(byte[] originalBytes) {
        if (originalBytes == null) throw new NullPointerException("originalBytes");
        return ClassFile.read(originalBytes).hasClassAnnotation(MIXIN_DESCRIPTOR);
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

        boolean hasClassAnnotation(String descriptor) {
            return AnnotationStringRemapper.hasAnnotationDescriptor(
                attributes, pool, descriptor);
        }

        boolean remap(IntermediaryNamedMappings mappings, boolean mixin) {
            boolean changed = false;
            String selfOwner = pool.utf8((Integer) pool.entries.get(thisClass).a);
            Map<DeclaredMethodKey, String> mappedOverrideMethods = new HashMap<>();
            Set<Integer> stringConstants = pool.literalStringConstants();
            for (int index : stringConstants) {
                String value = pool.utf8(index);
                if (!mapGeneralUtf8(value, mappings).equals(value))
                    pool.detachStringConstants(index);
            }
            Set<Integer> annotationStringConstants = new HashSet<>();
            AnnotationStringRemapper annotationRemapper =
                new AnnotationStringRemapper(pool, mappings, mixin, annotationStringConstants);
            changed |= annotationRemapper.rewrite(attributes);
            for (Member member : fields)
                changed |= annotationRemapper.rewrite(
                    member.attributes, true, pool.utf8(member.descriptorIndex));
            for (Member member : methods)
                changed |= annotationRemapper.rewrite(
                    member.attributes, false, pool.utf8(member.descriptorIndex));

            if (!mixin) {
                for (Member method : methods) {
                    String name = pool.utf8(method.nameIndex);
                    String descriptor = pool.utf8(method.descriptorIndex);
                    String mapped = mapImplementedMethodName(
                        mappings, name, descriptor);
                    if (!mapped.equals(name)) {
                        mappedOverrideMethods.put(
                            new DeclaredMethodKey(name, descriptor), mapped);
                        method.nameIndex = pool.addUtf8(mapped);
                        changed = true;
                    }
                }
            }

            String[] originalUtf8 = pool.snapshotUtf8();
            Set<Integer> literalStringConstants = pool.literalStringConstants();
            literalStringConstants.addAll(annotationStringConstants);
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
                if (value == null || literalStringConstants.contains(index)) continue;
                String mapped = mapGeneralUtf8(value, mappings);
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
                boolean selfReference = owner.equals(selfOwner);
                String mappedName;
                if (selfReference && mixin) {
                    mappedName = mappings.mapSymbol(name);
                } else if (selfReference && !field) {
                    mappedName = mappedOverrideMethods.getOrDefault(
                        new DeclaredMethodKey(name, descriptor), name);
                } else {
                    mappedName = field
                        ? mappings.mapFieldName(owner, name, descriptor)
                        : mappings.mapMethodName(owner, name, descriptor);
                }
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

        private String mapImplementedMethodName(
            IntermediaryNamedMappings mappings, String name,
            String descriptor) {
            if (name.startsWith("<")) return name;
            String mapped = name;
            if (superClass != 0) {
                String parent = className(superClass);
                String candidate = mergeImplementedMethodName(
                    mappings, parent, name, descriptor, mapped);
                mapped = candidate;
            }
            for (int interfaceIndex : interfaces) {
                String parent = className(interfaceIndex);
                String candidate = mergeImplementedMethodName(
                    mappings, parent, name, descriptor, mapped);
                mapped = candidate;
            }
            // Intermediary method IDs are globally assigned. A mod-owned
            // base class/interface can sit between this declaration and the
            // Fabric/Minecraft owner, so immediate-parent inspection alone
            // misses real overrides. Only use the fallback for an actual
            // intermediary ID and when name+descriptor has one global target.
            if (mapped.equals(name) && isIntermediaryMethodName(name))
                mapped = mappings.mapUniqueMethodName(name, descriptor);
            return mapped;
        }

        private static boolean isIntermediaryMethodName(String name) {
            if (!name.startsWith("method_") || name.length() == 7) return false;
            for (int index = 7; index < name.length(); ++index)
                if (name.charAt(index) < '0' || name.charAt(index) > '9')
                    return false;
            return true;
        }

        private String mergeImplementedMethodName(
            IntermediaryNamedMappings mappings, String owner, String name,
            String descriptor, String current) {
            String candidate = mappings.mapMethodName(owner, name, descriptor);
            if (candidate.equals(name)) return current;
            if (!current.equals(name) && !current.equals(candidate))
                throw new TransformException(
                    "ambiguous intermediary override " + owner + "." + name +
                    descriptor + " maps to both " + current + " and " + candidate);
            return candidate;
        }

        private String className(int classIndex) {
            CpEntry entry = pool.entries.get(classIndex);
            if (entry == null || entry.tag != 7)
                throw new TransformException(
                    "class-file inheritance entry is not a class: " + classIndex);
            return pool.utf8(entry.a);
        }

        private record DeclaredMethodKey(String name, String descriptor) { }

        private static String mapGeneralUtf8(String value,
                                             IntermediaryNamedMappings mappings) {
            String mapped = value;
            // Method/field descriptors start with `(`, `L`, or `[`. A class
            // Signature can instead start with formal type parameters, while
            // generic member signatures contain the same `Lowner;` grammar.
            if (looksLikeDescriptor(value) ||
                (!value.isEmpty() && value.charAt(0) == '<'))
                mapped = mappings.mapDescriptor(mapped);
            return mapped;
        }

        private static final class AnnotationStringRemapper {
            private final ConstantPool pool;
            private final IntermediaryNamedMappings mappings;
            private final boolean mixin;
            private final Set<Integer> protectedStringIndexes;
            private final Set<String> mixinTargetOwners = new HashSet<>();
            private final Map<AnnotationStringKey, Integer> detachedStrings = new HashMap<>();
            private boolean annotatedMemberIsField;
            private boolean defaultRemapEnabled = true;
            private String annotatedMemberDescriptor;
            private boolean changed;

            static boolean hasAnnotationDescriptor(
                List<Attribute> attributes, ConstantPool pool, String expected) {
                boolean found = false;
                for (Attribute attribute : attributes) {
                    String name = pool.utf8(attribute.nameIndex);
                    if (!name.equals("RuntimeVisibleAnnotations") &&
                        !name.equals("RuntimeInvisibleAnnotations")) continue;
                    Cursor cursor = new Cursor(
                        attribute.info, 0, attribute.info.length);
                    int count = cursor.u2();
                    for (int index = 0; index < count; ++index) {
                        String descriptor = pool.utf8(cursor.u2());
                        if (descriptor.equals(expected)) found = true;
                        skipAnnotationPairs(cursor, pool);
                    }
                    requireConsumed(cursor, true, "class annotation attribute");
                }
                return found;
            }

            private static void skipAnnotationPairs(Cursor cursor,
                                                   ConstantPool pool) {
                for (int index = 0, count = cursor.u2(); index < count; ++index) {
                    pool.utf8(cursor.u2());
                    skipElementValue(cursor, pool);
                }
            }

            private static void skipAnnotation(Cursor cursor, ConstantPool pool) {
                pool.utf8(cursor.u2());
                skipAnnotationPairs(cursor, pool);
            }

            private static void skipElementValue(Cursor cursor,
                                                 ConstantPool pool) {
                switch (cursor.u1()) {
                    case 'B', 'C', 'I', 'S', 'Z' -> {
                        if (pool.tag(cursor.u2()) != 3)
                            throw malformed("invalid integer annotation value");
                    }
                    case 'D' -> {
                        if (pool.tag(cursor.u2()) != 6)
                            throw malformed("invalid double annotation value");
                    }
                    case 'F' -> {
                        if (pool.tag(cursor.u2()) != 4)
                            throw malformed("invalid float annotation value");
                    }
                    case 'J' -> {
                        if (pool.tag(cursor.u2()) != 5)
                            throw malformed("invalid long annotation value");
                    }
                    case 's' -> pool.utf8(cursor.u2());
                    case 'c' -> pool.utf8(cursor.u2());
                    case 'e' -> {
                        pool.utf8(cursor.u2());
                        pool.utf8(cursor.u2());
                    }
                    case '@' -> skipAnnotation(cursor, pool);
                    case '[' -> {
                        for (int index = 0, count = cursor.u2(); index < count;
                             ++index)
                            skipElementValue(cursor, pool);
                    }
                    default -> throw malformed(
                        "unknown class annotation element tag");
                }
            }

            private AnnotationStringRemapper(ConstantPool pool,
                                             IntermediaryNamedMappings mappings,
                                             boolean mixin,
                                             Set<Integer> protectedStringIndexes) {
                this.pool = pool;
                this.mappings = mappings;
                this.mixin = mixin;
                this.protectedStringIndexes = protectedStringIndexes;
            }

            boolean rewrite(List<Attribute> attributes) {
                if (mixin) defaultRemapEnabled = readMixinRemap(attributes);
                return rewrite(attributes, false, null);
            }

            private boolean readMixinRemap(List<Attribute> attributes) {
                for (Attribute attribute : attributes) {
                    String name = pool.utf8(attribute.nameIndex);
                    if (!name.equals("RuntimeVisibleAnnotations") &&
                        !name.equals("RuntimeInvisibleAnnotations")) continue;
                    Cursor cursor = new Cursor(attribute.info, 0, attribute.info.length);
                    for (int index = 0, count = cursor.u2(); index < count; ++index) {
                        String descriptor = pool.utf8(cursor.u2());
                        if (descriptor.equals("Lorg/spongepowered/asm/mixin/Mixin;"))
                            return annotationRemap(cursor, true);
                        skipAnnotationPairs(cursor, pool);
                    }
                    requireConsumed(cursor, true, "class annotation attribute");
                }
                return true;
            }

            /** Read an annotation's explicit remap element without consuming its visitor cursor. */
            private boolean annotationRemap(Cursor cursor, boolean fallback) {
                Cursor scan = new Cursor(cursor.bytes, cursor.position, cursor.limit);
                for (int index = 0, count = scan.u2(); index < count; ++index) {
                    String name = pool.utf8(scan.u2());
                    if (name.equals("remap")) {
                        if (scan.u1() != 'Z')
                            throw malformed("remap annotation value is not boolean");
                        int constantIndex = scan.u2();
                        requireConstantTag(constantIndex, 3, "remap annotation value");
                        int value = pool.entries.get(constantIndex).a;
                        if (value != 0 && value != 1)
                            throw malformed("invalid boolean remap annotation value");
                        return value != 0;
                    }
                    skipElementValue(scan, pool);
                }
                return fallback;
            }

            boolean rewrite(List<Attribute> attributes, boolean memberIsField,
                            String memberDescriptor) {
                annotatedMemberIsField = memberIsField;
                annotatedMemberDescriptor = memberDescriptor;
                for (Attribute attribute : attributes) {
                    Cursor cursor = new Cursor(attribute.info, 0, attribute.info.length);
                    boolean recognized = visitAttribute(attribute.nameIndex, cursor);
                    requireConsumed(cursor, recognized, "class-file attribute");
                }
                return changed;
            }

            private boolean visitAttribute(int nameIndex, Cursor cursor) {
                String name = pool.utf8(nameIndex);
                switch (name) {
                    case "RuntimeVisibleAnnotations", "RuntimeInvisibleAnnotations" -> {
                        for (int i = 0, count = cursor.u2(); i < count; ++i)
                            visitAnnotation(cursor);
                    }
                    case "RuntimeVisibleParameterAnnotations", "RuntimeInvisibleParameterAnnotations" -> {
                        for (int parameter = 0, count = cursor.u1(); parameter < count; ++parameter)
                            for (int i = 0, annotations = cursor.u2(); i < annotations; ++i)
                                visitAnnotation(cursor);
                    }
                    case "RuntimeVisibleTypeAnnotations", "RuntimeInvisibleTypeAnnotations" -> {
                        for (int i = 0, count = cursor.u2(); i < count; ++i) {
                            skipTypeAnnotationTarget(cursor);
                            int pathLength = cursor.u1();
                            cursor.skip(2 * pathLength);
                            visitAnnotation(cursor);
                        }
                    }
                    case "AnnotationDefault" -> visitElementValue(
                        cursor, "", "", defaultRemapEnabled);
                    case "Code" -> visitCode(cursor);
                    case "Record" -> visitRecord(cursor);
                    default -> { return false; }
                }
                return true;
            }

            private void visitCode(Cursor cursor) {
                cursor.skip(4); // max_stack, max_locals
                cursor.skipLength(cursor.u4());
                int exceptionCount = cursor.u2();
                cursor.skip(8 * exceptionCount);
                visitNestedAttributes(cursor, cursor.u2());
            }

            private void visitRecord(Cursor cursor) {
                for (int i = 0, components = cursor.u2(); i < components; ++i) {
                    cursor.skip(4); // name_index, descriptor_index
                    visitNestedAttributes(cursor, cursor.u2());
                }
            }

            private void visitNestedAttributes(Cursor cursor, int count) {
                for (int i = 0; i < count; ++i) {
                    int nameIndex = cursor.u2();
                    long length = cursor.u4();
                    int end = cursor.checkedEnd(length);
                    int oldLimit = cursor.limit;
                    cursor.limit = end;
                    boolean recognized = visitAttribute(nameIndex, cursor);
                    requireConsumed(cursor, recognized, "nested class-file attribute");
                    cursor.position = end;
                    cursor.limit = oldLimit;
                }
            }

            private void visitAnnotation(Cursor cursor) {
                String descriptor = pool.utf8(cursor.u2());
                boolean remapEnabled = annotationRemap(cursor, defaultRemapEnabled);
                for (int i = 0, values = cursor.u2(); i < values; ++i) {
                    String name = pool.utf8(cursor.u2());
                    visitElementValue(cursor, descriptor, name, remapEnabled);
                }
            }

            private void visitElementValue(Cursor cursor, String annotationDescriptor,
                                           String elementName,
                                           boolean remapEnabled) {
                int tag = cursor.u1();
                switch (tag) {
                    case 'B', 'C', 'I', 'S', 'Z' ->
                        requireConstantTag(cursor.u2(), 3, "integer annotation value");
                    case 'D' -> requireConstantTag(cursor.u2(), 6, "double annotation value");
                    case 'F' -> requireConstantTag(cursor.u2(), 4, "float annotation value");
                    case 'J' -> requireConstantTag(cursor.u2(), 5, "long annotation value");
                    case 's' -> remapStringValue(
                        cursor, annotationDescriptor, elementName, remapEnabled);
                    case 'e' -> visitEnumValue(cursor);
                    case 'c' -> visitClassValue(
                        cursor, annotationDescriptor, elementName);
                    case '@' -> visitAnnotation(cursor);
                    case '[' -> {
                        for (int i = 0, count = cursor.u2(); i < count; ++i)
                            visitElementValue(cursor, annotationDescriptor,
                                elementName, remapEnabled);
                    }
                    default -> throw malformed("unknown annotation element tag " + tag);
                }
            }

            private void remapStringValue(Cursor cursor, String annotationDescriptor,
                                          String elementName,
                                          boolean remapEnabled) {
                int indexOffset = cursor.position;
                int oldIndex = cursor.u2();
                String original = pool.utf8(oldIndex);
                String remapped = mapAnnotationString(
                    annotationDescriptor, elementName, original, remapEnabled);
                rewriteStringIndex(cursor, indexOffset, oldIndex, original, remapped);
            }

            private void visitEnumValue(Cursor cursor) {
                int typeIndex = cursor.u2();
                String enumDescriptor = pool.utf8(typeIndex);
                if (enumDescriptor.length() < 3 || enumDescriptor.charAt(0) != 'L' ||
                    enumDescriptor.charAt(enumDescriptor.length() - 1) != ';')
                    throw malformed("invalid enum annotation descriptor");
                String owner = enumDescriptor.substring(1, enumDescriptor.length() - 1);
                int indexOffset = cursor.position;
                int oldIndex = cursor.u2();
                String original = pool.utf8(oldIndex);
                String remapped = mappings.mapFieldName(
                    owner, original, enumDescriptor);
                rewriteStringIndex(cursor, indexOffset, oldIndex, original, remapped);
            }

            private void visitClassValue(Cursor cursor, String annotationDescriptor,
                                         String elementName) {
                int index = cursor.u2();
                String descriptor = pool.utf8(index);
                if (!isClassLiteralDescriptor(descriptor))
                    throw malformed("invalid class-literal annotation descriptor");
                if (mixin && annotationDescriptor.equals(
                        "Lorg/spongepowered/asm/mixin/Mixin;") &&
                    elementName.equals("value"))
                    rememberMixinTargetDescriptor(descriptor);
            }

            private void rewriteStringIndex(Cursor cursor, int indexOffset,
                                            int oldIndex, String original,
                                            String remapped) {
                boolean mustDetach = !remapped.equals(original)
                    || !mapGeneralUtf8(remapped, mappings).equals(remapped);
                if (!mustDetach) return;

                AnnotationStringKey key = new AnnotationStringKey(oldIndex, remapped);
                Integer detachedIndex = detachedStrings.get(key);
                if (detachedIndex == null) {
                    detachedIndex = pool.appendUtf8(remapped);
                    detachedStrings.put(key, detachedIndex);
                }
                cursor.putU2(indexOffset, detachedIndex);
                protectedStringIndexes.add(detachedIndex);
                changed = true;
            }

            private String mapAnnotationString(String descriptor, String element,
                                               String value, boolean remapEnabled) {
                if (!mixin) return value;
                if (descriptor.equals("Lorg/spongepowered/asm/mixin/Mixin;")
                        && element.equals("targets")) {
                    rememberMixinTarget(value);
                    return mappings.mapMixinTarget(value);
                }
                if (!remapEnabled) return value;
                if (descriptor.equals("Lorg/spongepowered/asm/mixin/injection/At;")
                        && element.equals("target"))
                    return mappings.mapMixinSelector(
                        value, mixinTargetOwners,
                        IntermediaryNamedMappings.MixinSelectorKind.ANY);
                if (descriptor.equals("Lorg/spongepowered/asm/mixin/gen/Accessor;")
                        && element.equals("value"))
                    return mappings.mapMixinSelector(
                        value, mixinTargetOwners,
                        IntermediaryNamedMappings.MixinSelectorKind.FIELD);
                if (descriptor.equals("Lorg/spongepowered/asm/mixin/gen/Invoker;")
                        && element.equals("value"))
                    return mappings.mapMixinSelector(
                        value, mixinTargetOwners,
                        IntermediaryNamedMappings.MixinSelectorKind.METHOD);
                if (descriptor.equals("Lorg/spongepowered/asm/mixin/Shadow;")
                        && element.equals("aliases"))
                    return mappings.mapMixinSelector(
                        value, mixinTargetOwners,
                        annotatedMemberIsField
                            ? IntermediaryNamedMappings.MixinSelectorKind.FIELD
                            : IntermediaryNamedMappings.MixinSelectorKind.METHOD,
                        annotatedMemberDescriptor);
                if (isInjectorAnnotation(descriptor) && element.equals("method"))
                    return mappings.mapMixinSelector(
                        value, mixinTargetOwners,
                        IntermediaryNamedMappings.MixinSelectorKind.METHOD);
                return value;
            }

            private void rememberMixinTarget(String target) {
                String internalName = target.indexOf('/') < 0 && target.indexOf('.') >= 0
                    ? target.replace('.', '/') : target;
                mixinTargetOwners.add(mappings.unmapClassName(internalName));
            }

            private void rememberMixinTargetDescriptor(String descriptor) {
                if (descriptor.length() < 3 || descriptor.charAt(0) != 'L' ||
                    descriptor.charAt(descriptor.length() - 1) != ';') return;
                String owner = descriptor.substring(1, descriptor.length() - 1);
                mixinTargetOwners.add(mappings.unmapClassName(owner));
            }

            private void requireConstantTag(int index, int expectedTag,
                                            String description) {
                if (pool.tag(index) != expectedTag)
                    throw malformed("invalid constant-pool entry for " + description);
            }

            private static boolean isClassLiteralDescriptor(String descriptor) {
                if (descriptor.isEmpty()) return false;
                if (descriptor.equals("V") || descriptor.length() == 1)
                    return descriptor.equals("V") || "ZBCSIJFD".indexOf(
                        descriptor.charAt(0)) >= 0;
                return (descriptor.charAt(0) == '[' && descriptor.length() > 1) ||
                    (descriptor.charAt(0) == 'L' &&
                     descriptor.charAt(descriptor.length() - 1) == ';');
            }

            private static boolean isInjectorAnnotation(String descriptor) {
                return switch (descriptor) {
                    case "Lorg/spongepowered/asm/mixin/injection/Inject;",
                         "Lorg/spongepowered/asm/mixin/injection/Redirect;",
                         "Lorg/spongepowered/asm/mixin/injection/ModifyArg;",
                         "Lorg/spongepowered/asm/mixin/injection/ModifyConstant;",
                         "Lorg/spongepowered/asm/mixin/injection/ModifyVariable;",
                         "Lcom/llamalad7/mixinextras/injector/ModifyReturnValue;",
                         "Lcom/llamalad7/mixinextras/injector/ModifyExpressionValue;",
                         "Lcom/llamalad7/mixinextras/injector/v2/WrapWithCondition;",
                         "Lcom/llamalad7/mixinextras/injector/wrapoperation/WrapOperation;",
                         "Lcom/llamalad7/mixinextras/injector/wrapmethod/WrapMethod;" -> true;
                    default -> false;
                };
            }

            private static void skipTypeAnnotationTarget(Cursor cursor) {
                int targetType = cursor.u1();
                switch (targetType) {
                    case 0x00, 0x01, 0x16 -> cursor.skip(1);
                    case 0x10, 0x17, 0x42 -> cursor.skip(2);
                    case 0x11, 0x12 -> cursor.skip(2);
                    case 0x13, 0x14, 0x15 -> { }
                    case 0x40, 0x41 -> {
                        for (int i = 0, count = cursor.u2(); i < count; ++i)
                            cursor.skip(6);
                    }
                    case 0x43, 0x44, 0x45, 0x46 -> cursor.skip(2);
                    case 0x47, 0x48, 0x49, 0x4a, 0x4b -> cursor.skip(3);
                    default -> throw malformed("unknown type-annotation target " + targetType);
                }
            }

            private static void requireConsumed(Cursor cursor, boolean recognized,
                                                String description) {
                if (recognized && cursor.position != cursor.limit)
                    throw malformed("malformed " + description);
            }

            private static TransformException malformed(String message) {
                return new TransformException("cannot remap " + message);
            }

            private record AnnotationStringKey(int originalIndex, String remappedValue) { }
        }

        private static final class Cursor {
            private final byte[] bytes;
            private int position;
            private int limit;

            private Cursor(byte[] bytes, int position, int limit) {
                this.bytes = bytes;
                this.position = position;
                this.limit = limit;
            }

            private int u1() {
                require(1);
                return bytes[position++] & 0xff;
            }

            private int u2() {
                return (u1() << 8) | u1();
            }

            private long u4() {
                return ((long) u1() << 24) | ((long) u1() << 16)
                    | ((long) u1() << 8) | u1();
            }

            private void skip(int count) {
                if (count < 0) throw new TransformException("negative class-file skip length");
                require(count);
                position += count;
            }

            private void skipLength(long count) {
                if (count > Integer.MAX_VALUE)
                    throw new TransformException("oversized class-file attribute length");
                skip((int) count);
            }

            private int checkedEnd(long length) {
                if (length > Integer.MAX_VALUE || length < 0)
                    throw new TransformException("invalid nested class-file attribute length");
                int end = position + (int) length;
                if (end < position || end > limit)
                    throw new TransformException("truncated nested class-file attribute");
                return end;
            }

            private void putU2(int offset, int value) {
                if (offset < 0 || offset + 2 > limit || value < 0 || value > 0xffff)
                    throw new TransformException("invalid annotation constant-pool index");
                bytes[offset] = (byte) (value >>> 8);
                bytes[offset + 1] = (byte) value;
            }

            private void require(int count) {
                if (count < 0 || position > limit - count)
                    throw new TransformException("truncated class-file annotation data");
            }
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

        Set<Integer> literalStringConstants() {
            Set<Integer> output = new HashSet<>();
            for (CpEntry entry : entries) {
                if (entry != null && entry.tag == 8) output.add(entry.a);
            }
            return output;
        }

        void detachStringConstants(int utf8Index) {
            Map<Integer, Integer> duplicates = new HashMap<>();
            final int originalSize = entries.size();
            for (int index = 1; index < originalSize; ++index) {
                CpEntry entry = entries.get(index);
                if (entry == null || entry.tag != 8 || entry.a != utf8Index)
                    continue;
                Integer duplicate = duplicates.get(utf8Index);
                if (duplicate == null) {
                    duplicate = appendUtf8(utf8(utf8Index));
                    duplicates.put(utf8Index, duplicate);
                }
                entry.a = duplicate;
            }
        }

        String utf8(int index) {
            CpEntry entry = index > 0 && index < entries.size() ? entries.get(index) : null;
            if (entry == null || entry.tag != 1) throw new TransformException("constant is not UTF-8: " + index);
            return entry.text;
        }

        int tag(int index) {
            CpEntry entry = index > 0 && index < entries.size() ? entries.get(index) : null;
            if (entry == null)
                throw new TransformException("constant-pool index is invalid: " + index);
            return entry.tag;
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
            return appendUtf8(value);
        }

        private int appendUtf8(String value) {
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
