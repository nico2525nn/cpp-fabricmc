package cppfm.loader;

import java.net.URL;

/**
 * Public integration alias discovered by the native-side KnotLauncher.
 *
 * <p>All behavior lives in {@link KnotLikeClassLoader}; this class exists so
 * the launcher can use the conventional {@code KnotClassLoader} name without
 * requiring or colliding with a {@code KnotLauncher} implementation.</p>
 */
public class KnotClassLoader extends KnotLikeClassLoader {
    /** Fixed launcher integration constructor. */
    public KnotClassLoader(URL[] urls, ClassLoader parent) {
        super(urls, parent);
    }

    public KnotClassLoader(URL[] urls, ClassLoader parent, boolean strict) {
        super(urls, parent, strict);
    }
}
