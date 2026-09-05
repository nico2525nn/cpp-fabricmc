package cppfm.vendor.fabric;

import net.fabricmc.loader.impl.launch.knot.KnotServer;

/** Explicit launcher boundary used by the offline probe and later JNI host. */
public final class CppfmKnotLauncher {
    private CppfmKnotLauncher() {
    }

    public static void main(String[] args) {
        if (!CppfmGameProvider.isSelected()) {
            throw new IllegalStateException(
                    "CppfmKnotLauncher requires -D" + CppfmGameProvider.SELECTOR_PROPERTY + "=shadow");
        }
        KnotServer.main(args);
    }
}
