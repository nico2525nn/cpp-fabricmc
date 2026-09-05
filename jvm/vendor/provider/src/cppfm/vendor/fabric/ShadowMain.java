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
        ClassLoader targetLoader = Thread.currentThread().getContextClassLoader();
        System.out.println("CPPFM_OFFICIAL_TARGET_CONTEXT classLoader="
                + targetLoader.getClass().getName());
        System.out.println("CPPFM_OFFICIAL_MAPPING namespace="
                + loader.getMappingResolver().getCurrentRuntimeNamespace());
        if (Boolean.getBoolean("cppfm.official.embedded")) {
            invokeKnotLauncher("officialHandoff", targetLoader);
        }
        loader.invokeEntrypoints("main", ModInitializer.class, initializer -> initializer.onInitialize());
        if (Boolean.getBoolean("cppfm.official.embedded")) {
            invokeKnotLauncher("officialFinalize");
        }
        System.out.println("CPPFM_OFFICIAL_ENTRYPOINTS_DONE");
    }

    private static void invokeKnotLauncher(String methodName, Object... arguments) {
        try {
            ClassLoader target = Thread.currentThread().getContextClassLoader();
            // ShadowMain is defined by Knot's target loader, but the native
            // bootstrap entrypoint is intentionally owned by the JVM
            // application loader.  Asking Knot to resolve it would apply the
            // game-code exposure gate and reject the bridge class before the
            // explicit target-loader handoff can happen.
            ClassLoader application = ClassLoader.getSystemClassLoader();
            Class<?> launcher = Class.forName("cppfm.bridge.KnotLauncher", true, application);
            if ("officialHandoff".equals(methodName)) {
                launcher.getMethod(methodName, ClassLoader.class).invoke(null, arguments[0]);
            } else {
                launcher.getMethod(methodName).invoke(null);
            }
        } catch (ReflectiveOperationException exception) {
            throw new IllegalStateException("official cppfm handoff failed: " + methodName, exception);
        }
    }
}
