package net.minecraft.resource;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Stream;

/**
 * Deterministic server-side resource-pack profile registry matching the
 * 1.21.4 named ABI.  Actual pack IO remains optional in cpp-fabricmc.
 */
public class ResourcePackManager {
    private final Set<ResourcePackProvider> providers = new LinkedHashSet<>();
    private final Map<String, ResourcePackProfile> profiles = new LinkedHashMap<>();
    private final List<ResourcePackProfile> enabled = new ArrayList<>();

    public ResourcePackManager(ResourcePackProvider[] providers) {
        if (providers != null) Collections.addAll(this.providers, providers);
        scanPacks();
    }

    public void scanPacks() {
        for (ResourcePackProvider provider : providers)
            if (provider != null) provider.register(profile -> {
                if (profile != null) profiles.put(profile.getId(), profile);
            });
    }

    public Map<String, ResourcePackProfile> providePackProfiles() {
        return Collections.unmodifiableMap(new LinkedHashMap<>(profiles));
    }

    public List<ResourcePackProfile> createResourcePacks() {
        return List.copyOf(enabled);
    }

    public ResourcePackProfile getProfile(String id) { return profiles.get(id); }
    public Collection<String> getIds() { return List.copyOf(profiles.keySet()); }
    public Collection<ResourcePackProfile> getProfiles() { return List.copyOf(profiles.values()); }
    public Collection<ResourcePackProfile> getEnabledProfiles() { return List.copyOf(enabled); }
    public Collection<String> getEnabledIds() {
        return enabled.stream().map(ResourcePackProfile::getId).toList();
    }
    public Stream<ResourcePackProfile> streamProfilesById(Collection<String> ids) {
        return ids == null ? Stream.empty() : ids.stream().map(profiles::get).filter(p -> p != null);
    }
    public boolean hasProfile(String id) { return profiles.containsKey(id); }
    public boolean enable(String id) {
        ResourcePackProfile profile = profiles.get(id);
        return profile != null && !enabled.contains(profile) && enabled.add(profile);
    }
    public boolean disable(String id) {
        ResourcePackProfile profile = profiles.get(id);
        return profile != null && enabled.remove(profile);
    }
    public void setEnabledProfiles(Collection<String> ids) {
        enabled.clear();
        if (ids != null) for (String id : ids) enable(id);
    }
    public boolean hasOptionalProfilesEnabled() { return false; }
    public void addProfile(ResourcePackProfile profile) {
        if (profile != null) profiles.put(profile.getId(), profile);
    }

    public static String listPacks(Collection<ResourcePackProfile> profiles) {
        return profiles == null ? "" : profiles.stream().map(ResourcePackProfile::getId).reduce((a, b) -> a + "," + b).orElse("");
    }
}
