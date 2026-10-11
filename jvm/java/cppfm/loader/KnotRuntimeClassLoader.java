package cppfm.loader;

import java.net.URL;
import java.net.URLClassLoader;
import java.io.IOException;
import java.util.Enumeration;

/**
 * Parent loader for the version-locked official Fabric runtime.
 *
 * <p>The native JVM starts with dependency-free cppfm classes on the
 * application loader. Those classes contain API-shaped stubs, while the
 * official Loader, Mixin, and configured Minecraft libraries belong to this
 * loader. Prefer classes and resources in these explicit URLs, then fall back
 * to the application loader for cppfm and Minecraft shadows. The bridge itself
 * remains parent-owned and is handed off explicitly to Knot's target loader.</p>
 */
public final class KnotRuntimeClassLoader extends URLClassLoader {
    public KnotRuntimeClassLoader(URL[] urls, ClassLoader parent) {
        super(urls == null ? new URL[0] : urls.clone(), parent);
    }

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        synchronized (getClassLoadingLock(name)) {
            Class<?> loaded = findLoadedClass(name);
            if (loaded == null) {
                try {
                    loaded = findClass(name);
                } catch (ClassNotFoundException ignored) {
                    // The target Knot loader owns game and mod classes;
                    // cppfm shadows and bridge classes remain parent-owned.
                }
            }
            if (loaded == null) {
                loaded = getParent() == null
                    ? findClass(name)
                    : getParent().loadClass(name);
            }
            if (resolve) resolveClass(loaded);
            return loaded;
        }
    }

    @Override
    public URL getResource(String name) {
        URL resource = findResource(name);
        if (resource != null) return resource;
        return getParent() == null ? null : getParent().getResource(name);
    }

    @Override
    public Enumeration<URL> getResources(String name) throws IOException {
        // LoaderUtil.verifyClasspath() deliberately asks the Loader
        // classloader for API/ASM resources.  The application classpath also
        // contains dependency-free API-shaped stubs, so delegating those
        // resource names to the parent would make the official Loader reject
        // its own otherwise-correct class path as a duplicate.
        if (isOfficialResource(name)) return findResources(name);
        return super.getResources(name);
    }

    private static boolean isOfficialResource(String name) {
        return name.startsWith("net/fabricmc/")
            || name.startsWith("org/spongepowered/")
            || name.startsWith("org/objectweb/asm/")
            || name.startsWith("com/llamalad7/mixinextras/")
            || name.startsWith("cppfm/vendor/fabric/");
    }
}
