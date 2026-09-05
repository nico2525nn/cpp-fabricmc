package net.minecraft.util;

import java.util.Objects;

public final class Identifier implements Comparable<Identifier> {
    private final String namespace;
    private final String path;

    public Identifier(String namespace, String path) {
        if (namespace == null || path == null || namespace.isEmpty() || path.isEmpty())
            throw new IllegalArgumentException("identifier parts must not be empty");
        this.namespace = namespace;
        this.path = path;
    }
    public static Identifier of(String id) {
        Identifier result = tryParse(id);
        if (result == null) throw new IllegalArgumentException("invalid identifier: " + id);
        return result;
    }
    public static Identifier of(String namespace, String path) { return new Identifier(namespace, path); }
    public static Identifier tryParse(String id) {
        if (id == null || id.isEmpty()) return null;
        int separator = id.indexOf(':');
        if (separator < 0) return new Identifier("minecraft", id);
        if (separator == 0 || separator == id.length() - 1 || id.indexOf(':', separator + 1) >= 0) return null;
        return new Identifier(id.substring(0, separator), id.substring(separator + 1));
    }
    public String getNamespace() { return namespace; }
    public String getPath() { return path; }
    @Override public String toString() { return namespace + ":" + path; }
    @Override public int compareTo(Identifier other) { return toString().compareTo(other.toString()); }
    @Override public boolean equals(Object other) { return other instanceof Identifier i && toString().equals(i.toString()); }
    @Override public int hashCode() { return Objects.hash(namespace, path); }
}
