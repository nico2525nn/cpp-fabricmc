package cppfm.api_fixture;

/** Implementation compiled through Fabric and mod-owned interface layers. */
public final class TransitiveFabricOverrideFixture
        implements ModOwnedReloadLayer {
    @Override public String fabricId() { return "fixture"; }
    @Override public void method_2() { }
    @Override public void method_4() { }

    public void invokeInheritedReload() { method_4(); }
}
