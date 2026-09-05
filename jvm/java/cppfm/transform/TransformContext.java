package cppfm.transform;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * Per-class context supplied to every class-file transformer in a chain.
 * Transformers use it to publish method-level routing information and
 * actionable diagnostics without depending on a launcher implementation.
 */
public final class TransformContext {
    private final String className;
    private final byte[] originalBytes;
    private final boolean strict;
    private final LinkedHashSet<String> changedMethods = new LinkedHashSet<>();
    private final ArrayList<String> diagnostics = new ArrayList<>();

    public TransformContext(String className, byte[] originalBytes, boolean strict) {
        this.className = className;
        this.originalBytes = originalBytes.clone();
        this.strict = strict;
    }

    /** Binary class name (for example {@code net.minecraft.World}). */
    public String getClassName() {
        return className;
    }

    /** Original bytes supplied to the loader, defensively copied. */
    public byte[] getOriginalBytes() {
        return originalBytes.clone();
    }

    /** Whether this transformation chain is configured to fail on diagnostics. */
    public boolean isStrict() {
        return strict;
    }

    /** Record a JVM method name plus descriptor, such as {@code tick()V}. */
    public void recordMethod(String nameAndDescriptor) {
        if (nameAndDescriptor != null && !nameAndDescriptor.isEmpty()) {
            changedMethods.add(nameAndDescriptor);
        }
    }

    /** Add an actionable, non-fatal diagnostic for the current class. */
    public void diagnostic(String message) {
        if (message != null && !message.isEmpty()) diagnostics.add(message);
    }

    /** Read-only changed method descriptors in first-seen order. */
    public Set<String> getChangedMethods() {
        return Collections.unmodifiableSet(new LinkedHashSet<>(changedMethods));
    }

    /** Read-only diagnostics in first-seen order. */
    public List<String> getDiagnostics() {
        return Collections.unmodifiableList(new ArrayList<>(diagnostics));
    }

    void mergeFrom(TransformContext other) {
        changedMethods.addAll(other.changedMethods);
        diagnostics.addAll(other.diagnostics);
    }
}
