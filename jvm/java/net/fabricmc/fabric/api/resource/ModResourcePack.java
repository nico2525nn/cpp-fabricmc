package net.fabricmc.fabric.api.resource;

import net.fabricmc.loader.api.metadata.ModMetadata;
import net.minecraft.resource.ResourcePack;

/** Resource-pack view owned by a Fabric mod. */
public interface ModResourcePack extends ResourcePack {
    ModMetadata getFabricModMetadata();
    ModResourcePack createOverlay(String overlay);
}
