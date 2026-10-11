package cppfm.api_fixture;

import cppfm.bridge.ThreadContextClassLoaderLease;

/** Contract test for cross-thread cleanup of a TCCL change. */
public final class ThreadContextClassLoaderLeaseContractTest {
    public static void main(String[] args) throws InterruptedException {
        Thread owner = Thread.currentThread();
        ClassLoader previous = owner.getContextClassLoader();
        ClassLoader installed = new ClassLoader(previous) { };
        ThreadContextClassLoaderLease lease =
            ThreadContextClassLoaderLease.install(installed);
        if (owner.getContextClassLoader() != installed)
            throw new AssertionError("lease did not install its context loader");

        Thread closer = new Thread(lease::close, "tccl-lease-closer");
        closer.start();
        closer.join();
        if (owner.getContextClassLoader() != previous)
            throw new AssertionError("close did not restore the owner thread");
        lease.close();
        if (owner.getContextClassLoader() != previous)
            throw new AssertionError("lease close is not idempotent");
        System.out.println("THREAD CONTEXT CLASS LOADER LEASE PASS");
    }
}
