package net.fabricmc.loader.api.metadata;

import net.fabricmc.api.EnvType;

/** Environment selector used by Fabric mod metadata. */
public enum ModEnvironment {
    CLIENT,
    SERVER,
    UNIVERSAL;

    public boolean matches(EnvType environment) {
        return this == UNIVERSAL || (this == CLIENT && environment == EnvType.CLIENT)
            || (this == SERVER && environment == EnvType.SERVER);
    }
}
