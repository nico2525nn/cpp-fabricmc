package cppfm.transform;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Reference-map reader used for mixins compiled from Mojang-mapped sources.
 *
 * <p>Fabric's refmap is a named-to-intermediary table.  The runtime classes in
 * this project are Yarn named classes, so a reference is first read from the
 * refmap and then passed through the normal intermediary-to-named resolver.
 * Keeping this operation in the transformer avoids treating Mojmap symbols as
 * if they were intermediary symbols.</p>
 */
public final class MixinReferenceMap {
    public static final MixinReferenceMap EMPTY = new MixinReferenceMap(Map.of());

    private final Map<String, Map<String, String>> entries;

    private MixinReferenceMap(Map<String, Map<String, String>> entries) {
        LinkedHashMap<String, Map<String, String>> copy = new LinkedHashMap<>();
        for (Map.Entry<String, Map<String, String>> entry : entries.entrySet())
            copy.put(normalizeMixin(entry.getKey()), Collections.unmodifiableMap(new LinkedHashMap<>(entry.getValue())));
        this.entries = Collections.unmodifiableMap(copy);
    }

    public static MixinReferenceMap parse(String json) {
        if (json == null || json.isBlank()) return EMPTY;
        Map<String, Object> root = MixinConfiguration.parse(json).values();
        Object mappings = root.get("mappings");
        if (!(mappings instanceof Map<?, ?>)) {
            Object data = root.get("data");
            if (data instanceof Map<?, ?> dataMap) {
                // Modern refmaps nest the actual table below the namespace
                // edge, normally named:intermediary.
                Object namespace = dataMap.get("named:intermediary");
                mappings = namespace instanceof Map<?, ?> ? namespace : dataMap;
            }
        }
        if (!(mappings instanceof Map<?, ?> rawMappings)) return EMPTY;
        LinkedHashMap<String, Map<String, String>> result = new LinkedHashMap<>();
        for (Map.Entry<?, ?> mixin : rawMappings.entrySet()) {
            if (!(mixin.getKey() instanceof String mixinName)
                || !(mixin.getValue() instanceof Map<?, ?> rawMembers)) continue;
            LinkedHashMap<String, String> members = new LinkedHashMap<>();
            for (Map.Entry<?, ?> member : rawMembers.entrySet())
                if (member.getKey() instanceof String key && member.getValue() instanceof String value)
                    members.put(key, value);
            result.put(mixinName, members);
        }
        return result.isEmpty() ? EMPTY : new MixinReferenceMap(result);
    }

    /** Return a mapped method token such as {@code place(…)}. */
    String methodToken(String mixinName, String source, String defaultOwner,
                       DescriptorResolver resolver) {
        if (source == null || source.isEmpty()) return source;
        String mapped = lookup(mixinName, source);
        if (mapped == null && source.indexOf('(') < 0)
            mapped = lookupByPrefix(mixinName, source, true);
        if (mapped == null) return source;
        Reference reference = Reference.parse(mapped);
        if (reference.name.isEmpty()) return source;
        if (!reference.method()) return reference.name;
        Reference resolved = resolve(reference, defaultOwner, resolver);
        return resolved.name + resolved.descriptor;
    }

    /** Return a mapped field name for an {@code @Accessor}/{@code @Shadow}. */
    String fieldName(String mixinName, String source, String defaultOwner, String descriptor,
                    DescriptorResolver resolver) {
        if (source == null || source.isEmpty()) return source;
        String mapped = lookup(mixinName, source);
        if (mapped == null) return source;
        Reference reference = Reference.parse(mapped);
        if (reference.name.isEmpty()) return source;
        Reference resolved = resolve(reference.withDescriptor(
            reference.descriptor.isEmpty() ? descriptor : reference.descriptor), defaultOwner, resolver);
        return resolved.name;
    }

    /** Return a mapped {@code @At} member target in JVM target syntax. */
    String atTarget(String mixinName, String source, String defaultOwner,
                    DescriptorResolver resolver) {
        if (source == null || source.isEmpty()) return source;
        String mapped = lookup(mixinName, source);
        if (mapped == null) return source;
        Reference reference = Reference.parse(mapped);
        if (reference.name.isEmpty()) return source;
        Reference resolved = resolve(reference, defaultOwner, resolver);
        String owner = resolved.owner;
        if (owner.isEmpty()) return source;
        if (resolved.method()) return "L" + owner + ";" + resolved.name + resolved.descriptor;
        return "L" + owner + ";" + resolved.name
            + (resolved.descriptor.isEmpty() ? "" : ":" + resolved.descriptor);
    }

    /** Map an overwrite/accessor/invoker member reference if one is present. */
    String memberToken(String mixinName, String source, String defaultOwner,
                       DescriptorResolver resolver) {
        if (source == null || source.isEmpty()) return source;
        String mapped = lookup(mixinName, source);
        if (mapped == null) {
            String name = source;
            int open = source.indexOf('(');
            if (open >= 0) name = source.substring(0, open);
            mapped = lookup(mixinName, name);
            if (mapped == null) mapped = lookupByPrefix(mixinName, name, false);
        }
        if (mapped == null) return source;
        Reference reference = Reference.parse(mapped);
        Reference resolved = resolve(reference, defaultOwner, resolver);
        if (resolved.name.isEmpty()) return source;
        return resolved.name + resolved.descriptor;
    }

    private String lookup(String mixinName, String key) {
        Map<String, String> members = entries.get(normalizeMixin(mixinName));
        return members == null ? null : members.get(key);
    }

    private String lookupByPrefix(String mixinName, String source, boolean methodOnly) {
        Map<String, String> members = entries.get(normalizeMixin(mixinName));
        if (members == null) return null;
        String prefix = source + "(";
        String result = null;
        for (Map.Entry<String, String> entry : members.entrySet()) {
            Reference reference = Reference.parse(entry.getValue());
            if (methodOnly && !reference.method()) continue;
            if (!entry.getKey().equals(source) && !entry.getKey().startsWith(prefix)) continue;
            if (result != null && !result.equals(entry.getValue())) return null;
            result = entry.getValue();
        }
        return result;
    }

    private static Reference resolve(Reference reference, String defaultOwner,
                                     DescriptorResolver resolver) {
        String sourceOwner = reference.owner.isEmpty() ? normalizeOwner(defaultOwner) : reference.owner;
        String owner = sourceOwner == null ? "" : resolver.resolveOwner(sourceOwner);
        String descriptor = reference.descriptor == null ? "" : resolver.resolveDescriptor(reference.descriptor);
        String name = reference.name;
        if (!name.isEmpty()) {
            name = reference.method()
                ? resolver.resolveMethod(sourceOwner, reference.name, reference.descriptor)
                : resolver.resolveField(sourceOwner, reference.name, reference.descriptor);
        }
        return new Reference(owner, name, descriptor);
    }

    private static String normalizeMixin(String name) {
        return name == null ? "" : name.replace('.', '/');
    }

    private static String normalizeOwner(String owner) {
        if (owner == null) return "";
        String value = owner.replace('.', '/');
        if (value.startsWith("L") && value.endsWith(";")) return value.substring(1, value.length() - 1);
        return value;
    }

    private record Reference(String owner, String name, String descriptor) {
        boolean method() { return descriptor != null && descriptor.startsWith("("); }

        Reference withDescriptor(String value) { return new Reference(owner, name, value == null ? "" : value); }

        static Reference parse(String value) {
            if (value == null || value.isEmpty()) return new Reference("", "", "");
            String text = value;
            String owner = "";
            if (text.startsWith("L")) {
                int semicolon = text.indexOf(';');
                if (semicolon >= 0) {
                    owner = text.substring(1, semicolon);
                    text = text.substring(semicolon + 1);
                }
            }
            int open = text.indexOf('(');
            if (open >= 0) return new Reference(owner, text.substring(0, open), text.substring(open));
            int colon = text.indexOf(':');
            if (colon >= 0) return new Reference(owner, text.substring(0, colon), text.substring(colon + 1));
            // A class-only mapping is not a member reference.  Preserve it as
            // an owner so callers can safely leave an unsupported annotation
            // target unchanged rather than inventing a member name.
            if (owner.isEmpty() && text.indexOf('/') >= 0)
                return new Reference(text, "", "");
            return new Reference(owner, text, "");
        }
    }
}
