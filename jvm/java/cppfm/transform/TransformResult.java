package cppfm.transform;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Immutable result of one class definition transformation. */
public final class TransformResult {
    private final String className;
    private final byte[] originalBytes;
    private final byte[] transformedBytes;
    private final boolean changed;
    private final Set<String> modifiedMethodDescriptors;
    private final List<String> diagnostics;
    private final Map<String, String> transformedMethodHashes;

    public TransformResult(String className, byte[] originalBytes, byte[] transformedBytes,
                           Set<String> modifiedMethodDescriptors, List<String> diagnostics,
                           Map<String, String> transformedMethodHashes) {
        this.className = className;
        this.originalBytes = originalBytes.clone();
        this.transformedBytes = transformedBytes.clone();
        this.changed = !java.util.Arrays.equals(this.originalBytes, this.transformedBytes);
        this.modifiedMethodDescriptors = Collections.unmodifiableSet(
            new LinkedHashSet<>(modifiedMethodDescriptors));
        this.diagnostics = Collections.unmodifiableList(new ArrayList<>(diagnostics));
        this.transformedMethodHashes = Collections.unmodifiableMap(
            new LinkedHashMap<>(transformedMethodHashes));
    }

    public String getClassName() {
        return className;
    }

    public byte[] getOriginalBytes() {
        return originalBytes.clone();
    }

    public byte[] getTransformedBytes() {
        return transformedBytes.clone();
    }

    public boolean isChanged() {
        return changed;
    }

    /** Descriptors are returned as {@code methodName(argumentTypes)returnType}. */
    public Set<String> getModifiedMethodDescriptors() {
        return modifiedMethodDescriptors;
    }

    public List<String> getDiagnostics() {
        return diagnostics;
    }

    /** Map keys are {@code binaryClassName#methodName(descriptor)}. */
    public Map<String, String> getTransformedMethodHashes() {
        return transformedMethodHashes;
    }

    public String getOriginalSha256() {
        return Hashes.sha256(originalBytes);
    }

    public String getTransformedSha256() {
        return Hashes.sha256(transformedBytes);
    }

    @Override
    public String toString() {
        return "TransformResult{" + className + ", changed=" + changed
            + ", methods=" + modifiedMethodDescriptors + ", diagnostics="
            + diagnostics.size() + "}";
    }
}
