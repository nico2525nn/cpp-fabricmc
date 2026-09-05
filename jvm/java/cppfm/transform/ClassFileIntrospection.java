package cppfm.transform;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** Read-only class-file evidence helpers exposed to launchers and tests. */
public final class ClassFileIntrospection {
    private ClassFileIntrospection() { }

    /**
     * Return SHA-256 hashes for every concrete method's Code attribute.
     * Keys are {@code binaryClassName#name(descriptor)}.
     */
    public static Map<String, String> methodHashes(String binaryName, byte[] bytes) {
        if (binaryName == null || bytes == null) return Map.of();
        ClassFileModel model = ClassFileModel.parse(bytes);
        LinkedHashMap<String, String> output = new LinkedHashMap<>();
        for (MemberModel method : model.methods) {
            CodeModel code = method.code(model.pool);
            if (code != null) output.put(binaryName.replace('/', '.') + "#"
                + method.name(model.pool) + method.descriptor(model.pool), Hashes.sha256(code.code));
        }
        return Collections.unmodifiableMap(output);
    }

    public static String binaryName(byte[] bytes) {
        return ClassFileModel.parse(bytes).binaryName();
    }

    /** Validate class structure before a caller defines transformed bytes. */
    public static void validate(byte[] bytes) {
        ClassFileSafety.validateBytes(bytes);
    }
}
