package net.fabricmc.fabric.impl.attachment;

import java.util.HashMap;
import java.util.Map;
import java.util.WeakHashMap;
import net.fabricmc.fabric.api.attachment.v1.AttachmentType;

/**
 * Storage for the server-side shadow implementation of Fabric attachments.
 *
 * <p>The real API installs this storage with mixins on Entity, Chunk,
 * BlockEntity, and World.  Those classes are ordinary Java shadows here, so
 * the same contract is provided by a weakly keyed side table.  Weak keys keep
 * an unloaded wrapper from retaining its attachment map indefinitely.</p>
 */
public final class AttachmentStorage {
    private static final Map<Object, Map<AttachmentType<?>, Object>> VALUES = new WeakHashMap<>();

    private AttachmentStorage() { }

    public static synchronized <A> A get(Object target, AttachmentType<A> type) {
        Map<AttachmentType<?>, Object> values = VALUES.get(target);
        if (values == null) return null;
        @SuppressWarnings("unchecked")
        A value = (A) values.get(type);
        return value;
    }

    public static synchronized <A> A put(Object target, AttachmentType<A> type, A value) {
        Map<AttachmentType<?>, Object> values = VALUES.get(target);
        @SuppressWarnings("unchecked")
        A previous = values == null ? null : (A) values.get(type);
        if (value == null) {
            if (values != null) {
                values.remove(type);
                if (values.isEmpty()) VALUES.remove(target);
            }
            return previous;
        }
        if (values == null) {
            values = new HashMap<>();
            VALUES.put(target, values);
        }
        values.put(type, value);
        return previous;
    }

    public static synchronized void clear() {
        VALUES.clear();
    }
}
