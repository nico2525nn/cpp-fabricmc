package net.fabricmc.fabric.api.attachment.v1;

import java.util.Objects;
import java.util.function.Supplier;
import java.util.function.UnaryOperator;
import net.fabricmc.fabric.impl.attachment.AttachmentStorage;

/** Implemented by entities, chunks, block entities, and worlds. */
public interface AttachmentTarget {
    String NBT_ATTACHMENT_KEY = "fabric:attachments";

    default <A> A getAttached(AttachmentType<A> type) {
        return AttachmentStorage.get(this, Objects.requireNonNull(type, "type"));
    }

    default <A> A getAttachedOrThrow(AttachmentType<A> type) {
        return Objects.requireNonNull(getAttached(type), "No value was attached");
    }

    default <A> A getAttachedOrSet(AttachmentType<A> type, A defaultValue) {
        Objects.requireNonNull(defaultValue, "default value cannot be null");
        A value = getAttached(type);
        if (value != null) return value;
        setAttached(type, defaultValue);
        return defaultValue;
    }

    default <A> A getAttachedOrCreate(AttachmentType<A> type, Supplier<A> initializer) {
        A value = getAttached(type);
        if (value != null) return value;
        A created = Objects.requireNonNull(initializer, "initializer").get();
        Objects.requireNonNull(created, "initializer result cannot be null");
        setAttached(type, created);
        return created;
    }

    default <A> A getAttachedOrCreate(AttachmentType<A> type) {
        Supplier<A> initializer = Objects.requireNonNull(type, "type").initializer();
        if (initializer == null) {
            throw new IllegalArgumentException(
                "Single-argument getAttachedOrCreate is reserved for attachment types with default initializers");
        }
        return getAttachedOrCreate(type, initializer);
    }

    default <A> A getAttachedOrElse(AttachmentType<A> type, A defaultValue) {
        A value = getAttached(type);
        return value == null ? defaultValue : value;
    }

    default <A> A getAttachedOrGet(AttachmentType<A> type, Supplier<A> defaultValue) {
        Objects.requireNonNull(defaultValue, "default value supplier cannot be null");
        A value = getAttached(type);
        return value == null ? defaultValue.get() : value;
    }

    default <A> A setAttached(AttachmentType<A> type, A value) {
        return AttachmentStorage.put(this, Objects.requireNonNull(type, "type"), value);
    }

    default boolean hasAttached(AttachmentType<?> type) {
        return getAttached(Objects.requireNonNull(type, "type")) != null;
    }

    default <A> A removeAttached(AttachmentType<A> type) {
        return setAttached(type, null);
    }

    default <A> A modifyAttached(AttachmentType<A> type, UnaryOperator<A> modifier) {
        Objects.requireNonNull(modifier, "modifier");
        return setAttached(type, modifier.apply(getAttached(type)));
    }
}
