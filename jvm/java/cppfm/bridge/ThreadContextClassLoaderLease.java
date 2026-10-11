package cppfm.bridge;

import java.util.concurrent.atomic.AtomicBoolean;

/** Restores a thread context class loader on the thread whose value changed. */
public final class ThreadContextClassLoaderLease implements AutoCloseable {
    private final Thread owner;
    private final ClassLoader previous;
    private final AtomicBoolean closed = new AtomicBoolean();

    private ThreadContextClassLoaderLease(Thread owner, ClassLoader previous) {
        this.owner = owner;
        this.previous = previous;
    }

    public static ThreadContextClassLoaderLease install(ClassLoader loader) {
        if (loader == null) throw new NullPointerException("loader");
        Thread owner = Thread.currentThread();
        ClassLoader previous = owner.getContextClassLoader();
        owner.setContextClassLoader(loader);
        return new ThreadContextClassLoaderLease(owner, previous);
    }

    /** May be called from another thread; restoration targets the owner thread. */
    @Override
    public void close() {
        if (closed.compareAndSet(false, true))
            owner.setContextClassLoader(previous);
    }
}
