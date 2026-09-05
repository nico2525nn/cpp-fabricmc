package net.minecraft.nbt;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/** Small lossless compound useful for common item/entity data APIs. */
public class NbtCompound extends NbtElement {
    private final Map<String, NbtElement> values = new LinkedHashMap<>();
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
    public NbtCompound put(String key, NbtElement value) { if (key != null && value != null) values.put(key, value); return this; }
    public NbtCompound putString(String key, String value) { return put(key, NbtString.of(value)); }
    public NbtCompound putInt(String key, int value) { return put(key, NbtInt.of(value)); }
    public String getString(String key) { return getString(key, ""); }
    public String getString(String key, String fallback) { NbtElement value = get(key); return value instanceof NbtString text ? text.asString() : fallback; }
    public int getInt(String key) { return getInt(key, 0); }
    public int getInt(String key, int fallback) { NbtElement value = get(key); return value instanceof NbtInt number ? number.intValue() : fallback; }
    public NbtElement remove(String key) { return values.remove(key); }
    public boolean isEmpty() { return values.isEmpty(); }
    @Override public String toString() { return values.toString(); }
}
