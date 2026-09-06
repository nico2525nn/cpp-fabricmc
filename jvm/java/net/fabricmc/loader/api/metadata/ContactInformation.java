package net.fabricmc.loader.api.metadata;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;

/** Immutable contact links declared by a mod. */
public interface ContactInformation {
    ContactInformation EMPTY = new ContactInformation() {
        @Override public Optional<String> get(String key) { return Optional.empty(); }
        @Override public Map<String, String> asMap() { return Map.of(); }
    };

    Optional<String> get(String key);
    Map<String, String> asMap();

    static ContactInformation of(Map<String, String> values) {
        Map<String, String> copy = new LinkedHashMap<>();
        if (values != null) copy.putAll(values);
        return new ContactInformation() {
            private final Map<String, String> data = Map.copyOf(copy);
            @Override public Optional<String> get(String key) { return Optional.ofNullable(data.get(key)); }
            @Override public Map<String, String> asMap() { return data; }
        };
    }
}
