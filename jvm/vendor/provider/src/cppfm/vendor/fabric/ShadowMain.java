package cppfm.vendor.fabric;

import net.fabricmc.api.ModInitializer;
import net.fabricmc.loader.api.FabricLoader;

/** Small main target proving the official Loader entrypoint path is live. */
public final class ShadowMain {
    private ShadowMain() {
    }

    public static void main(String[] args) {
        FabricLoader loader = FabricLoader.getInstance();
        System.out.println("CPPFM_OFFICIAL_LOADER_BOOTSTRAPPED");
        System.out.println("CPPFM_OFFICIAL_PROVIDER=" + CppfmGameProvider.class.getName());
        loader.invokeEntrypoints("main", ModInitializer.class, initializer -> initializer.onInitialize());
        System.out.println("CPPFM_OFFICIAL_ENTRYPOINTS_DONE");
    }
}
