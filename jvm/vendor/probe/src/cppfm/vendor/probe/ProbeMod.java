package cppfm.vendor.probe;

import java.lang.reflect.Method;

import net.fabricmc.api.ModInitializer;

/** Fabric mod entrypoint used only by the offline official-runtime probe. */
public final class ProbeMod implements ModInitializer {
    public static void reportMixinReturn(Object value) {
        // Keep java/lang/System out of the transformed callback bytecode. The
        // pinned Mixin 0.8.7/ASM stack cannot parse Java 25 platform classes;
        // this helper is loaded normally after the callback has been applied.
        System.out.println("CPPFM_OFFICIAL_MIXIN_RETURN value=" + value);
    }

    @Override
    public void onInitialize() {
        System.out.println("CPPFM_OFFICIAL_MOD_ENTRYPOINT");
        try {
            ClassLoader loader = Thread.currentThread().getContextClassLoader();
            Class<?> serverClass = Class.forName("net.minecraft.server.MinecraftServer", true, loader);
            Method factory = serverClass.getMethod("of", long.class);
            Object server = factory.invoke(null, 1L);
            Object ticks = serverClass.getMethod("getTicks").invoke(server);
            System.out.println("CPPFM_OFFICIAL_SHADOW_TARGET classLoader="
                    + serverClass.getClassLoader().getClass().getName() + " ticks=" + ticks);
            if (!(ticks instanceof Integer)) {
                throw new IllegalStateException("MinecraftServer.getTicks did not return int");
            }
        } catch (ReflectiveOperationException exception) {
            throw new IllegalStateException("official probe could not load/invoke shadow MinecraftServer", exception);
        }
    }
}
