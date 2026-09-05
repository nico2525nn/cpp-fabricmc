package net.fabricmc.loader.api;

import java.util.List;
import java.util.Map;

public final class ModMetadata {
    private final String id, name, version;
    public ModMetadata(String id, String name, String version) { this.id = id == null ? "" : id; this.name = name == null ? id : name; this.version = version == null ? "" : version; }
    public String getId() { return id; }
    public String getName() { return name; }
    public String getVersion() { return version; }
    public List<String> getProvides() { return List.of(); }
    public Map<String, String> getCustomValues() { return Map.of(); }
    public boolean containsCustomValue(String key) { return false; }
}
