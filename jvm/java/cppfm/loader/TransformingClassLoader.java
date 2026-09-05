package cppfm.loader;

import cppfm.transform.ClassFileTransformer;
import cppfm.transform.MixinClassTransformer;
import cppfm.transform.MixinConfiguration;
import cppfm.transform.TransformContext;
import cppfm.transform.TransformListener;
import cppfm.transform.TransformResult;
import cppfm.transform.TransformException;
import cppfm.transform.ClassFileIntrospection;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

/**
 * URL-backed child-first loader with a deterministic pre-definition
 * class-file transformation chain.
 *
 * <p>This is the integration boundary for the later launcher.  The fixed
 * constructor is {@link #TransformingClassLoader(URL[], ClassLoader)}.  It is
 * deliberately independent of Fabric classes and can therefore be used while
 * Knot/Fabric is being bootstrapped.</p>
 */
public class TransformingClassLoader extends URLClassLoader {
    private final CopyOnWriteArrayList<ClassFileTransformer> transformers =
        new CopyOnWriteArrayList<>();
    private final CopyOnWriteArrayList<TransformListener> listeners =
        new CopyOnWriteArrayList<>();
    private final ConcurrentHashMap<String, TransformResult> results = new ConcurrentHashMap<>();
    private final MixinClassTransformer mixinTransformer;
    private volatile boolean strict;

    /** Fixed public integration constructor. */
    public TransformingClassLoader(URL[] urls, ClassLoader parent) {
        this(urls, parent, false);
    }

    /** Convenience constructor with explicit diagnostic policy. */
    public TransformingClassLoader(URL[] urls, ClassLoader parent, boolean strict) {
        super(urls == null ? new URL[0] : urls.clone(), parent);
        this.strict = strict;
        this.mixinTransformer = new MixinClassTransformer(strict);
        this.mixinTransformer.setMixinClassLoader(this);
        addTransformer(mixinTransformer);
    }

    /** Add a transformer; invocation order is registration order. */
    public void addTransformer(ClassFileTransformer transformer) {
        if (transformer == null) throw new NullPointerException("transformer");
        transformers.addIfAbsent(transformer);
    }

    public void removeTransformer(ClassFileTransformer transformer) {
        if (transformer != null) transformers.remove(transformer);
    }

    public List<ClassFileTransformer> getTransformers() {
        return Collections.unmodifiableList(new ArrayList<>(transformers));
    }

    public void addTransformListener(TransformListener listener) {
        if (listener != null) listeners.add(listener);
    }

    public void removeTransformListener(TransformListener listener) {
        if (listener != null) listeners.remove(listener);
    }

    public boolean isStrict() {
        return strict;
    }

    public void setStrict(boolean strict) {
        this.strict = strict;
        mixinTransformer.setStrict(strict);
    }

    /**
     * Register a mixin JSON resource visible from this loader.  The resource
     * may be written with or without a leading slash.  Its package/common,
     * server and mixins entries are resolved to class bytes before any target
     * class is defined.
     */
    public void registerMixinConfig(String resourceName) {
        if (resourceName == null || resourceName.isEmpty())
            throw new IllegalArgumentException("mixin resource name is empty");
        String normalized = resourceName.startsWith("/") ? resourceName.substring(1) : resourceName;
        try (InputStream stream = getResourceAsStream(normalized)) {
            if (stream == null) throw new TransformException("mixin config not found: " + resourceName);
            String json = new String(readAll(stream), java.nio.charset.StandardCharsets.UTF_8);
            mixinTransformer.registerConfiguration(MixinConfiguration.parse(json), this);
        } catch (IOException failure) {
            throw new TransformException("cannot read mixin config: " + resourceName, failure);
        }
    }

    /** Direct access for launchers that already parsed configuration. */
    public void registerMixinConfiguration(MixinConfiguration configuration) {
        mixinTransformer.registerConfiguration(configuration, this);
    }

    /**
     * Transform bytes explicitly, before a target class is defined.
     *
     * <p>The returned array is a defensive copy and the detailed result is
     * available through {@link #getTransformResult(String)}.</p>
     */
    public byte[] transform(String binaryName, byte[] originalBytes) {
        return transformResult(binaryName, originalBytes).getTransformedBytes();
    }

    /** Stable alias requested by the KnotLauncher integration contract. */
    public byte[] transformClassBytes(String binaryName, byte[] bytes) {
        return transform(binaryName, bytes);
    }

    /** Transform and return the complete immutable result. */
    public TransformResult transformResult(String binaryName, byte[] originalBytes) {
        if (binaryName == null || binaryName.isEmpty()) throw new IllegalArgumentException("empty class name");
        if (originalBytes == null) throw new NullPointerException("originalBytes");
        String normalized = binaryName.replace('/', '.');
        byte[] current = originalBytes.clone();
        TransformContext context = new TransformContext(normalized, current, strict);
        for (ClassFileTransformer transformer : transformers) {
            byte[] next;
            try {
                next = transformer.transform(normalized, current.clone(), context);
            } catch (TransformException failure) {
                throw failure;
            } catch (Throwable failure) {
                throw new TransformException("transformer failed for " + normalized
                    + " (" + transformer.getClass().getName() + ")", failure);
            }
            if (next != null && next != current) {
                byte[] candidate = next.clone();
                try {
                    ClassFileIntrospection.validate(candidate);
                    current = candidate;
                } catch (RuntimeException failure) {
                    String message = "rejected invalid transformed bytes for " + normalized
                        + " from " + transformer.getClass().getName() + ": " + failure.getMessage();
                    context.diagnostic(message);
                    if (strict) throw failure instanceof TransformException exception
                        ? exception : new TransformException(message, failure);
                }
            }
        }
        Map<String, String> hashes = ClassFileIntrospection.methodHashes(normalized, current);
        TransformResult result = new TransformResult(normalized, originalBytes, current,
            context.getChangedMethods(), context.getDiagnostics(), hashes);
        results.put(normalized, result);
        for (TransformListener listener : listeners) {
            try {
                listener.onTransformed(result);
            } catch (Throwable failure) {
                if (strict) throw new TransformException("transform listener failed for " + normalized, failure);
            }
        }
        return result;
    }

    public TransformResult getTransformResult(String binaryName) {
        return results.get(normalize(binaryName));
    }

    public TransformResult getLastTransformResult() {
        TransformResult last = null;
        for (TransformResult result : results.values()) last = result;
        return last;
    }

    /** All changed method descriptors observed so far, qualified by class. */
    public Set<String> getTransformedMethodDescriptors() {
        LinkedHashSet<String> output = new LinkedHashSet<>();
        for (Map.Entry<String, TransformResult> entry : results.entrySet()) {
            for (String descriptor : entry.getValue().getModifiedMethodDescriptors())
                output.add(entry.getKey() + "#" + descriptor);
        }
        return Collections.unmodifiableSet(output);
    }

    /** Changed method descriptors for one binary class name. */
    public Set<String> getTransformedMethodDescriptors(String binaryName) {
        TransformResult result = getTransformResult(binaryName);
        return result == null ? Set.of() : result.getModifiedMethodDescriptors();
    }

    /** Method hashes keyed by {@code binaryClassName#name(descriptor)}. */
    public Map<String, String> getTransformedMethodHashes() {
        LinkedHashMap<String, String> output = new LinkedHashMap<>();
        for (TransformResult result : results.values()) output.putAll(result.getTransformedMethodHashes());
        return Collections.unmodifiableMap(output);
    }

    public List<String> getDiagnostics() {
        ArrayList<String> output = new ArrayList<>();
        output.addAll(mixinTransformer.getDiagnostics());
        for (TransformResult result : results.values()) output.addAll(result.getDiagnostics());
        return Collections.unmodifiableList(output);
    }

    public List<String> getDiagnostics(String binaryName) {
        TransformResult result = getTransformResult(binaryName);
        return result == null ? List.of() : result.getDiagnostics();
    }

    /** Exposes the built-in Mixin transformer for explicit byte registration. */
    public MixinClassTransformer getMixinTransformer() {
        return mixinTransformer;
    }

    /**
     * Resolve a mod class through this one URL loader.  Keeping this method on
     * the loader makes class identity explicit: launcher, mixin metadata and
     * transformed target all use the same defining loader.
     */
    public Class<?> resolveModClass(String binaryName) throws ClassNotFoundException {
        return loadClass(binaryName.replace('/', '.'));
    }

    /** Stable identity handle for launcher code that passes the mod loader on. */
    public ClassLoader getModClassLoader() {
        return this;
    }

    @Override
    protected Class<?> findClass(String name) throws ClassNotFoundException {
        String resourceName = name.replace('.', '/') + ".class";
        URL resource = findResource(resourceName);
        if (resource == null) throw new ClassNotFoundException(name);
        try (InputStream stream = resource.openStream()) {
            byte[] original = readAll(stream);
            byte[] transformed = transformClassBytes(name, original);
            return defineClass(name, transformed, 0, transformed.length);
        } catch (IOException failure) {
            throw new ClassNotFoundException("cannot read " + resourceName, failure);
        }
    }

    /**
     * Child-first policy with a conservative parent boundary for platform and
     * service-loader classes.  The boundary can be overridden by subclasses.
     */
    protected boolean parentFirst(String name) {
        return name.startsWith("java.") || name.startsWith("javax.")
            || name.startsWith("jdk.") || name.startsWith("sun.")
            || name.startsWith("com.sun.") || name.startsWith("org.w3c.")
            || name.startsWith("org.xml.") || name.startsWith("org.slf4j.");
    }

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        synchronized (getClassLoadingLock(name)) {
            Class<?> loaded = findLoadedClass(name);
            if (loaded == null) {
                if (parentFirst(name)) {
                    try {
                        loaded = getParent() == null ? null : getParent().loadClass(name);
                    } catch (ClassNotFoundException ignored) { }
                }
                if (loaded == null) {
                    try {
                        loaded = findClass(name);
                    } catch (ClassNotFoundException failure) {
                        loaded = getParent() == null ? null : getParent().loadClass(name);
                    }
                }
            }
            if (resolve) resolveClass(loaded);
            return loaded;
        }
    }

    private static String normalize(String name) {
        return name == null ? null : name.replace('/', '.');
    }

    private static byte[] readAll(InputStream stream) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        for (int count; (count = stream.read(buffer)) >= 0; ) {
            if (count > 0) output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }
}
