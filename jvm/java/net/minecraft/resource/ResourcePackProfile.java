package net.minecraft.resource;

/** Minimal persistent profile object for server-side resource-pack consumers. */
public class ResourcePackProfile {
    private final String id;

    public ResourcePackProfile() { this(""); }
    public ResourcePackProfile(String id) { this.id = id == null ? "" : id; }
    public String getId() { return id; }
    public ResourcePack createResourcePack() { return null; }
}
