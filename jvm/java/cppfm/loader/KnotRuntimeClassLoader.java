package cppfm.loader;

import java.net.URL;
import java.net.URLClassLoader;
import java.io.IOException;
import java.util.Enumeration;

/**
 * Parent loader for the version-locked official Fabric runtime.
 *
 * <p>The native JVM starts with the dependency-free cppfm classes on the
 * application loader.  Those classes contain small API-shaped stubs under
 * {@code net.fabricmc} and {@code org.spongepowered}; allowing the ordinary
 * parent-first URLClassLoader policy here would make the official Loader see
 * the stubs instead of its own API and Mixin classes.  This loader therefore
 * gives the pinned official jars priority only for their owned namespaces.
 * The cppfm bridge itself remains parent-owned and is handed off explicitly
 * to Knot's target loader after it has been created.</p>
 */
public final class KnotRuntimeClassLoader extends URLClassLoader {
    private static final String[] OFFICIAL_PREFIXES = {
        "net.fabricmc.",
        "org.spongepowered.",
        "org.objectweb.asm.",
        "com.llamalad7.mixinextras.",
        "cppfm.vendor.fabric."
    };

    public KnotRuntimeClassLoader(URL[] urls, ClassLoader parent) {
        super(urls == null ? new URL[0] : urls.clone(), parent);
    }

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        synchronized (getClassLoadingLock(name)) {
            Class<?> loaded = findLoadedClass(name);
            if (loaded == null && isOfficialNamespace(name)) {
                try {
                    loaded = findClass(name);
                } catch (ClassNotFoundException ignored) {
                    // The target Knot loader owns game and adapter-only
                    // classes.  Let the parent resolve optional official
                    // classes when this host does not contain them.
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
        if (isOfficialResource(name)) return findResource(name);
        return super.getResource(name);
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

    private static boolean isOfficialNamespace(String name) {
        for (String prefix : OFFICIAL_PREFIXES) {
            if (name.startsWith(prefix)) return true;
        }
        return false;
    }

    private static boolean isOfficialResource(String name) {
        return name.startsWith("net/fabricmc/")
            || name.startsWith("org/spongepowered/")
            || name.startsWith("org/objectweb/asm/")
            || name.startsWith("com/llamalad7/mixinextras/")
            || name.startsWith("cppfm/vendor/fabric/");
    }
}
