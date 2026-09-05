package net.minecraft.util;

import java.util.Objects;

public final class Identifier implements Comparable<Identifier> {
    private final String namespace;
    private final String path;

    public Identifier(String namespace, String path) {
        if (!isValidNamespace(namespace) || !isValidPath(path))
            throw new InvalidIdentifierException("Invalid identifier: " + namespace + ":" + path);
        this.namespace = namespace;
        this.path = path;
    }
    public static Identifier of(String id) {
        Identifier result = tryParse(id);
        if (result == null) throw new IllegalArgumentException("invalid identifier: " + id);
        return result;
    }
    public static Identifier of(String namespace, String path) { return new Identifier(namespace, path); }
    public static Identifier ofVanilla(String path) { return new Identifier("minecraft", path); }
    public static Identifier tryParse(String id) {
        if (id == null || id.isEmpty()) return null;
        int separator = id.indexOf(':');
        if (separator < 0) return isValidPath(id) ? new Identifier("minecraft", id) : null;
        if (separator == 0 || separator == id.length() - 1 || id.indexOf(':', separator + 1) >= 0) return null;
        String namespace = id.substring(0, separator);
        String path = id.substring(separator + 1);
        return isValidNamespace(namespace) && isValidPath(path) ? new Identifier(namespace, path) : null;
    }
    public static boolean isValid(String id) { return tryParse(id) != null; }
    public static boolean isValidNamespace(String value) {
        if (value == null || value.isEmpty()) return false;
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '_' && c != '-' && c != '.') return false;
        }
        return true;
    }
    public static boolean isValidPath(String value) {
        if (value == null || value.isEmpty()) return false;
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            boolean ascii = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (!ascii && c != '_' && c != '-' && c != '.' && c != '/') return false;
        }
        return true;
    }
    public Identifier withPrefixedPath(String prefix) {
        return new Identifier(namespace, (prefix == null ? "" : prefix) + path);
    }
    public Identifier withSuffixedPath(String suffix) {
        return new Identifier(namespace, path + (suffix == null ? "" : suffix));
    }
    public String toTranslationKey() { return namespace + "." + path.replace('/', '.'); }
    public String toTranslationKey(String prefix) { return (prefix == null ? "" : prefix) + "." + toTranslationKey(); }
    public String toUnderscoreSeparatedString() { return namespace + "_" + path.replace('/', '_'); }
    public String toDebugString() { return "[" + namespace + ":" + path + "]"; }
    public String getNamespace() { return namespace; }
    public String getPath() { return path; }
    @Override public String toString() { return namespace + ":" + path; }
    @Override public int compareTo(Identifier other) { return toString().compareTo(other == null ? "" : other.toString()); }
    @Override public boolean equals(Object other) { return other instanceof Identifier i && toString().equals(i.toString()); }
    @Override public int hashCode() { return Objects.hash(namespace, path); }
}
