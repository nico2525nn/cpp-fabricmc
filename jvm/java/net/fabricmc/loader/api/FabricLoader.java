package net.fabricmc.loader.api;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Optional;

/** Stable subset of FabricLoader used by server-side mods during bootstrap. */
public final class FabricLoader {
    private static final FabricLoader INSTANCE = new FabricLoader();
    private final Path gameDir = Paths.get(System.getProperty("user.dir", ".")).toAbsolutePath().normalize();
    private final ModContainer minecraft;
    private FabricLoader() {
        minecraft = new ModContainer(
            new net.fabricmc.loader.api.metadata.ModMetadata("minecraft", "Minecraft", "1.21.4"),
            gameDir);
    }
    public static FabricLoader getInstance() { return INSTANCE; }
    public boolean isModLoaded(String id) { return id != null && "minecraft".equals(id); }
    public boolean isDevelopmentEnvironment() { return Boolean.getBoolean("fabric.development"); }
    public net.fabricmc.api.EnvType getEnvironmentType() { return net.fabricmc.api.EnvType.SERVER; }
    public Path getGameDir() { return gameDir; }
    public Path getConfigDir() { return gameDir.resolve("config"); }
    public List<ModContainer> getAllMods() { return List.of(minecraft); }
    public Optional<ModContainer> getModContainer(String id) {
        return isModLoaded(id) ? Optional.of(minecraft) : Optional.empty();
    }
    public MappingResolver getMappingResolver() { return MappingResolver.IDENTITY; }
}
