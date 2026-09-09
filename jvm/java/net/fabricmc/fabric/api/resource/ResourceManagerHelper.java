package net.fabricmc.fabric.api.resource;

import cppfm.bridge.CppModRuntime;
import java.util.function.Function;
import net.fabricmc.loader.api.ModContainer;
import net.minecraft.registry.RegistryWrapper;
import net.minecraft.resource.ResourceType;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;

/**
 * Minimal server-side resource loader registry used by Fabric's convention
 * tag bootstrap.  Reload execution belongs to the native server lifecycle;
 * registration remains deterministic and safe during mod initialization.
 */
public interface ResourceManagerHelper {
    ResourceManagerHelper SERVER_DATA = new ServerDataResourceManagerHelper();
    ResourceManagerHelper CLIENT_RESOURCES = new NoopResourceManagerHelper();

    static ResourceManagerHelper get(ResourceType type) {
        return type == ResourceType.SERVER_DATA ? SERVER_DATA : CLIENT_RESOURCES;
    }

    default void addReloadListener(IdentifiableResourceReloadListener listener) {
        registerReloadListener(listener);
    }

    void registerReloadListener(IdentifiableResourceReloadListener listener);

    void registerReloadListener(Identifier identifier,
                                Function<RegistryWrapper.WrapperLookup,
                                    IdentifiableResourceReloadListener> factory);

    static boolean registerBuiltinResourcePack(Identifier id, ModContainer container,
                                               net.fabricmc.fabric.api.resource.ResourcePackActivationType activationType) {
        return false;
    }

    static boolean registerBuiltinResourcePack(Identifier id, ModContainer container,
                                               Text displayName,
                                               net.fabricmc.fabric.api.resource.ResourcePackActivationType activationType) {
        return false;
    }

    static boolean registerBuiltinResourcePack(Identifier id, ModContainer container,
                                               String resourcePath,
                                               net.fabricmc.fabric.api.resource.ResourcePackActivationType activationType) {
        return id != null && container != null && resourcePath != null;
    }

    /** Compatibility overload for the old local nested spelling. */
    @Deprecated
    static boolean registerBuiltinResourcePack(Identifier id, ModContainer container,
                                               ResourcePackActivationType activationType) {
        return registerBuiltinResourcePack(id, container,
            activationType == null ? null : net.fabricmc.fabric.api.resource.ResourcePackActivationType.valueOf(activationType.name()));
    }

    /** Compatibility overload for the old local nested spelling. */
    @Deprecated
    static boolean registerBuiltinResourcePack(Identifier id, ModContainer container,
                                               Text displayName,
                                               ResourcePackActivationType activationType) {
        return registerBuiltinResourcePack(id, container, displayName,
            activationType == null ? null : net.fabricmc.fabric.api.resource.ResourcePackActivationType.valueOf(activationType.name()));
    }

    static boolean registerBuiltinResourcePack(Identifier id, String resourcePath,
                                               ModContainer container, boolean alwaysEnabled) {
        return id != null && resourcePath != null && container != null;
    }

    enum ResourcePackActivationType { DEFAULT_ENABLED, NORMAL, ALWAYS_ENABLED }

    final class ServerDataResourceManagerHelper implements ResourceManagerHelper {
        @Override
        public void registerReloadListener(IdentifiableResourceReloadListener listener) {
            CppModRuntime.registerServerReloadListener(listener);
        }

        @Override
        public void registerReloadListener(Identifier identifier,
                                           Function<RegistryWrapper.WrapperLookup,
                                               IdentifiableResourceReloadListener> factory) {
            CppModRuntime.registerServerReloadListener(identifier, factory);
        }
    }

    final class NoopResourceManagerHelper implements ResourceManagerHelper {
        @Override public void registerReloadListener(IdentifiableResourceReloadListener listener) { }
        @Override public void registerReloadListener(Identifier identifier,
                                                      Function<RegistryWrapper.WrapperLookup,
                                                          IdentifiableResourceReloadListener> factory) { }
    }
}
