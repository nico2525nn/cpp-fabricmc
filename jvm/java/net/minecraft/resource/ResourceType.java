package net.minecraft.resource;

/** The two vanilla resource-manager domains exposed by Fabric 1.21.4. */
public enum ResourceType {
    CLIENT_RESOURCES,
    SERVER_DATA;

    public String getDirectory() {
        return this == CLIENT_RESOURCES ? "assets" : "data";
    }
}
