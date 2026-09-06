package net.fabricmc.loader.api.metadata;

import java.nio.file.Path;
import java.util.List;

/** Origin information for a resolved mod container. */
public interface ModOrigin {
    Kind getKind();
    List<Path> getPaths();
    String getParentModId();
    String getParentSubLocation();

    enum Kind { PATH, NESTED, DIRECTORY, UNKNOWN }
}
