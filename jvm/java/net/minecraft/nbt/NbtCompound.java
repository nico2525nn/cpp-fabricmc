package net.minecraft.nbt;

import com.mojang.serialization.Codec;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.Collections;

/** Small lossless compound useful for common item/entity data APIs. */
public class NbtCompound extends NbtElement {
    public static final Codec<NbtCompound> CODEC = new Codec<>() { };
    private final Map<String, NbtElement> values = new LinkedHashMap<>();
    /** Constructor shape used by the 1.21.4 Access Widener contract. */
    public NbtCompound(Map<?, ?> initialValues) {
        if (initialValues != null) {
            for (Map.Entry<?, ?> entry : initialValues.entrySet()) {
                if (entry.getKey() instanceof String key && entry.getValue() instanceof NbtElement value)
                    values.put(key, value);
            }
        }
    }
    public NbtCompound() { }
    @Override public byte getType() { return COMPOUND_TYPE; }
    @Override public NbtCompound copy() {
        NbtCompound copy = new NbtCompound();
        values.forEach((key, value) -> copy.values.put(key, value == null ? null : value.copy()));
        return copy;
    }
    public boolean contains(String key) { return key != null && values.containsKey(key); }
    public boolean contains(String key, int type) { return contains(key) && values.get(key) != null && values.get(key).getType() == type; }
    public Set<String> getKeys() { return Set.copyOf(values.keySet()); }
    public NbtElement get(String key) { return values.get(key); }
    /**
     * Returns the compound entries in their serialized order.
     *
     * <p>The real implementation exposes the map to the NBT writer through
     * package-private internals.  Keeping this view read-only gives the
     * packet buffer the same information without making the backing map
     * mutable from outside the NBT package.</p>
     */
    public Set<Map.Entry<String, NbtElement>> entrySet() {
        return Collections.unmodifiableSet(values.entrySet());
    }
    public NbtCompound put(String key, NbtElement value) { if (key != null && value != null) values.put(key, value); return this; }
    public NbtCompound putString(String key, String value) { return put(key, NbtString.of(value)); }
    public NbtCompound putInt(String key, int value) { return put(key, NbtInt.of(value)); }
    public NbtCompound putLong(String key, long value) { return put(key, NbtLong.of(value)); }
    public String getString(String key) { return getString(key, ""); }
    public String getString(String key, String fallback) { NbtElement value = get(key); return value instanceof NbtString text ? text.asString() : fallback; }
    public int getInt(String key) { return getInt(key, 0); }
    public int getInt(String key, int fallback) { NbtElement value = get(key); return value instanceof NbtInt number ? number.intValue() : fallback; }
    public long getLong(String key) { return getLong(key, 0L); }
    public long getLong(String key, long fallback) {
        NbtElement value = get(key);
        return value instanceof NbtLong number ? number.longValue()
            : value instanceof NbtInt number ? number.longValue() : fallback;
    }
    public NbtElement remove(String key) { return values.remove(key); }
    public boolean isEmpty() { return values.isEmpty(); }
    @Override public String toString() { return values.toString(); }
}
