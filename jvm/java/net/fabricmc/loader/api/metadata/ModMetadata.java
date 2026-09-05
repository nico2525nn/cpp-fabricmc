package net.fabricmc.loader.api.metadata;

import java.util.List;
import java.util.Map;

/** Minimal metadata view matching the package exposed by Fabric Loader. */
public class ModMetadata {
    private final String id;
    private final String name;
    private final String version;

    public ModMetadata(String id, String name, String version) {
        this.id = id == null ? "" : id;
        this.name = name == null ? this.id : name;
        this.version = version == null ? "" : version;
    }

    public String getId() { return id; }
    public String getName() { return name; }
    public String getVersion() { return version; }
    public List<String> getProvides() { return List.of(); }
    public Map<String, String> getCustomValues() { return Map.of(); }
    public boolean containsCustomValue(String key) { return false; }
}
