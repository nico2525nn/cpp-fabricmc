package cppfm.vendor.fabric;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;

import net.fabricmc.loader.impl.game.GameProvider;
import net.fabricmc.loader.impl.game.patch.GameTransformer;
import net.fabricmc.loader.impl.launch.FabricLauncher;
import net.fabricmc.loader.impl.util.Arguments;

/**
 * Loader 0.16.9 adapter for the C++ shadow class directory.
 *
 * This intentionally implements the Loader-internal GameProvider contract,
 * rather than pretending that a C++ shadow is a Mojang server jar.  The
     * property gate keeps the official MinecraftGameProvider disabled for the
     * probe, and makes provider selection explicit to a later KnotLauncher.
 */
public final class CppfmGameProvider implements GameProvider {
    public static final String SELECTOR_PROPERTY = "cppfm.game-provider";
    public static final String SHADOW_MODE = "shadow";
    public static final String SHADOW_CLASSES_PROPERTY = "cppfm.shadow-classes";
    public static final String GAME_DIR_PROPERTY = "cppfm.game-dir";
    public static final String ENTRYPOINT_PROPERTY = "cppfm.provider.entrypoint";
    public static final String DEFAULT_ENTRYPOINT = "cppfm.vendor.fabric.ShadowMain";
    public static final String GAME_VERSION = "1.21.4";

    private final GameTransformer entrypointTransformer = new GameTransformer();
    private Arguments arguments = new Arguments();
    private Path shadowClasses;
    private String entrypoint = DEFAULT_ENTRYPOINT;

    public static boolean isSelected() {
        return SHADOW_MODE.equals(System.getProperty(SELECTOR_PROPERTY));
    }

    @Override
    public String getGameId() {
        return "minecraft";
    }

    @Override
    public String getGameName() {
        return "Minecraft (CppFabricMC shadow runtime)";
    }

    @Override
    public String getRawGameVersion() {
        return GAME_VERSION;
    }

    @Override
    public String getNormalizedGameVersion() {
        return GAME_VERSION;
    }

    @Override
    public Collection<BuiltinMod> getBuiltinMods() {
        return Collections.emptyList();
    }

    @Override
    public String getEntrypoint() {
        return entrypoint;
    }

    @Override
    public Path getLaunchDirectory() {
        String gameDir = arguments.getOrDefault("gameDir", System.getProperty(GAME_DIR_PROPERTY, "."));
        return Paths.get(gameDir).toAbsolutePath().normalize();
    }

    @Override
    public boolean isObfuscated() {
        return false;
    }

    @Override
    public boolean requiresUrlClassLoader() {
        return false;
    }

    @Override
    public boolean isEnabled() {
        return isSelected();
    }

    @Override
    public boolean locateGame(FabricLauncher launcher, String[] args) {
        if (!isSelected()) {
            return false;
        }
        arguments = new Arguments();
        arguments.parse(args);
        if (!arguments.containsKey("gameDir")) {
            arguments.put("gameDir", System.getProperty(GAME_DIR_PROPERTY, "."));
        }

        String configuredShadow = System.getProperty(SHADOW_CLASSES_PROPERTY);
        if (configuredShadow == null || configuredShadow.isBlank()) {
            System.err.println("CPPFM_PROVIDER_FAILURE missing -D" + SHADOW_CLASSES_PROPERTY);
            return false;
        }
        shadowClasses = Paths.get(configuredShadow).toAbsolutePath().normalize();
        if (!Files.isDirectory(shadowClasses)) {
            System.err.println("CPPFM_PROVIDER_FAILURE shadow directory does not exist: " + shadowClasses);
            return false;
        }
        entrypoint = System.getProperty(ENTRYPOINT_PROPERTY, DEFAULT_ENTRYPOINT);
        System.out.println("CPPFM_OFFICIAL_PROVIDER_SELECTED " + getClass().getName());
        return true;
    }

    @Override
    public void initialize(FabricLauncher launcher) {
        // locateGame runs before Knot constructs its classloader, so calling
        // addToClassPath there is an internal Loader 0.16.9 NPE.  At this
        // point the loader exists: retain the official parent code sources,
        // then add the shadow directory for Knot transformation.
        launcher.setValidParentClassPath(new ArrayList<>(launcher.getClassPath()));
        launcher.addToClassPath(shadowClasses);
        // The verifier (and production callers) must provide a shadow path
        // without net.fabricmc/org.spongepowered stubs.  These names must
        // come from pinned official jars, otherwise Java sees two unrelated
        // ModInitializer/Mixin type identities.  Restrict the remaining
        // shadow source to the namespaces used by this runtime.
        launcher.setAllowedPrefixes(shadowClasses, "net.minecraft.", "com.mojang.", "cppfm.");
        // KnotClassDelegate calls transform() for every transformable class,
        // including a provider with no game patches.  Loader's own
        // GameTransformer initializes its lookup map only through this
        // method; omitting it yields a null patchedClasses NPE.
        entrypointTransformer.locateEntrypoints(launcher, Collections.singletonList(shadowClasses));
    }

    @Override
    public GameTransformer getEntrypointTransformer() {
        return entrypointTransformer;
    }

    @Override
    public void unlockClassPath(FabricLauncher launcher) {
        // No late game jar exists.  initialize added the immutable shadow
        // directory after Knot constructed its classloader.
    }

    @Override
    public void launch(ClassLoader classLoader) {
        try {
            Class<?> mainClass = classLoader.loadClass(entrypoint);
            Method main = mainClass.getMethod("main", String[].class);
            main.invoke(null, (Object) getLaunchArguments(false));
        } catch (InvocationTargetException exception) {
            Throwable cause = exception.getCause();
            if (cause instanceof RuntimeException runtime) {
                throw runtime;
            }
            if (cause instanceof Error error) {
                throw error;
            }
            throw new RuntimeException("Cppfm provider entrypoint failed", cause);
        } catch (ReflectiveOperationException exception) {
            throw new RuntimeException("Cannot launch Cppfm provider entrypoint " + entrypoint, exception);
        }
    }

    @Override
    public Arguments getArguments() {
        return arguments;
    }

    @Override
    public String[] getLaunchArguments(boolean sanitize) {
        return arguments.toArray();
    }

    @Override
    public boolean canOpenErrorGui() {
        return false;
    }

    @Override
    public boolean hasAwtSupport() {
        return false;
    }
}
