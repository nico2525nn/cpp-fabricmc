package cppfm.loader;

import java.net.URL;

/**
 * Knot-shaped child-first loader facade used by the native launcher.
 *
 * <p>The name intentionally does not collide with the launcher's
 * {@code KnotLauncher}.  It owns no process lifecycle; it only resolves URLs,
 * applies registered class-file transformers before definition, and exposes
 * the transformation evidence APIs inherited from
 * {@link TransformingClassLoader}.</p>
 */
public class KnotLikeClassLoader extends TransformingClassLoader {
    /** Fixed launcher integration constructor. */
    public KnotLikeClassLoader(URL[] urls, ClassLoader parent) {
        super(urls, parent);
    }

    public KnotLikeClassLoader(URL[] urls, ClassLoader parent, boolean strict) {
        super(urls, parent, strict);
    }
}
