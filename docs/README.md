# CppFabricMC documentation

CppFabricMC is an independent clean-room implementation of the Minecraft: Java
Edition 1.21.4 server protocol in C++20. This directory collects the public
technical references for the supported protocol, gameplay, operations, Java
integration boundary, and verification evidence.

## Choose a topic

- [Wire specification](SPEC_WIRE.md) — packet formats, identifiers, state
  transitions, compression, encryption, and byte-level contracts.
- [Gameplay specification](SPEC_GAMEPLAY.md) — world storage, generation,
  blocks, entities, inventory, commands, simulation, and known limitations.
- [Operations specification](SPEC_OPS.md) — configuration, persistence,
  recovery, resource limits, load behavior, and administration.
- [Development guide](DEVELOPMENT.md) — source ownership, build conventions,
  clean-room methodology, and contribution workflow.
- [Verification](VERIFICATION.md) — reproducible commands, evidence, and the
  interpretation of passing and bounded results.
- [Current status](CURRENT_STATE.md) — the latest measured state and remaining
  limitations.

## Supported target

| Item | Value |
|---|---|
| Minecraft | Java Edition 1.21.4 |
| Protocol | 769 |
| World data | DataVersion 4189 |
| Runtime | C++20 server; the embedded HotSpot/JNI path is enabled by default in a binary built with JDK/JNI and when a compatible runtime JDK is available |

The Java integration is a deliberately bounded extension surface. JDK 17+ must
be available at configure/build time for the JNI bridge and embedded shadow
classes; a JDK installed only after a native-only build cannot add JVM support
to that existing binary. At runtime, a JNI-capable binary needs a compatible
JDK and fails open to the native server unless strict startup is requested. The
shadow-runtime build accepts JDK 17+, while the pinned real-mod probes use JDK
21; a mod's own Java requirement can be higher. It provides a version-locked
shadow API, selected Fabric-style callbacks, and a structural bytecode
transformer for the tested server-side use cases. It is not the official Mojang
server runtime and does not claim universal compatibility with arbitrary JVM
mods, client code, or GUI behavior.

With Python 3 available at configure/build time, repository-owned data is
embedded in the executable; when `javac` and JNI are also available, the Java
shadow classes are embedded as well. Without Python 3, the build uses external
assets and is not a self-contained one-file bundle. Copy
`cppfm`/`cppfm.exe` by itself to a fresh directory and launch it; the server
creates `world/`, `mods/`, `config/`, `libraries/`, logging folders,
`resourcepacks/`, and its hidden runtime cache, plus `server.properties` on
first start. `cmake --build build --target package` produces a one-file ZIP
containing that executable, but its CPack preflight refuses to create the ZIP
when the embedded resource pack or required registry assets are unavailable.
The ZIP and generated reports under `build/` are ignored local outputs, not
tracked/public release artifacts. The one-file
archive is verified on Linux x86-64; Windows and macOS need a native build on
those platforms, and the host still supplies native libraries and any JDK used
for Java integration at runtime; JDK/JNI are also needed at configure/build time
to produce the JNI-capable binary. When JNI and compiled classes are present,
`ctest -R '^package_jvm_smoke$'` separately extracts the exact ZIP and checks
default-on strict JVM startup without an external classes/assets override.

## Implementation map

| Area | Main paths | Reference |
|---|---|---|
| Encoding and protocol IDs | `src/core/`, `src/proto/` | [Wire specification](SPEC_WIRE.md) |
| Connections and packets | `src/net/` | [Wire specification](SPEC_WIRE.md) |
| World and persistence | `src/game/World*`, `src/game/RegionFile.*`, `src/game/WorldDataManager.*` | [Gameplay](SPEC_GAMEPLAY.md), [Operations](SPEC_OPS.md) |
| Simulation and world generation | `src/physics/`, `src/worldgen/`, `src/game/WorldGen.cpp` | [Gameplay](SPEC_GAMEPLAY.md) |
| Entities, items, and menus | `src/game/Entities.*`, `Items.*`, `Containers.*`, `Recipes.*` | [Gameplay](SPEC_GAMEPLAY.md) |
| Commands and administration | `src/game/Commands.*`, `src/game/commands_*.cpp` | [Gameplay](SPEC_GAMEPLAY.md), [Operations](SPEC_OPS.md) |
| Java integration (default-on when configured/built with JDK/JNI; runtime JDK still required) | `src/jvm/`, `jvm/java/`, `jvm/shadow_api.json` | [Verification](VERIFICATION.md#jvm-boundary-gate) |
| Mod linkage and runtime diagnostics | `tools/scan_mod_linkage.py`, `tools/compare_real_mod_corpus.py`, `tests/real_mod_corpus/` | [Verification](VERIFICATION.md#static-gate), [JVM boundary](VERIFICATION.md#jvm-boundary-gate) |
| Tests and tooling | `tests/`, `tools/` | [Verification](VERIFICATION.md) |

## Verification at a glance

The latest recorded working-tree evidence includes:

- The 2026-09-10 operational baseline is non-nightly CTest `42/42 PASS` in
  `397.54s`, multi-client `ALL PASS` in `17.84s`, and bot smoke `ALL PASS` in
  `20.87s`. The named `test_native` checks also pass, but that target has no
  stable aggregate count. The separate `package_jvm_smoke` release gate passes
  on the exact CPack ZIP with clean extraction, embedded assets/classes, and
  default-on strict JVM startup.
- Wire checks report `395 PASS / 0 FAIL` for the specification vectors and
  `399 PASS / 0 FAIL` for the full wire suite.
- The clean extracted Linux package's `test_server_full` harness reports
  `234 PASS / 0 FAIL`; the source-tree 80-scenario smoke integration test
  reports `223 PASS / 0 FAIL`.
- Focused gameplay checks confirmed for the current working tree include
  gameplay `807 PASS / 0 FAIL`, redstone `42 PASS / 0 FAIL`, fluids
  `23 PASS / 0 FAIL`, and menu logic `41 PASS / 0 FAIL`; enchanting and crafter
  coverage is bounded and does not
  establish complete vanilla menu parity.
- The locally generated ignored Linux one-file package contains exactly one
  executable; its clean extracted-directory `test_server_full` run is
  `234 PASS / 0 FAIL`, and the separate `package_jvm_smoke` gate also passes.
  This package is a local ignored `build/` output, not a tracked/public artifact.
- The class-file linkage tooling and fail-closed runtime-diagnostic contract both
  pass. Static official-provider scans remain conservative diagnostics because raw
  Mixin classes can reference members added only after transformation.
- The bounded historical Java fixture corpus reports `25/25`; its harness also
  passes the auxiliary functional API fixture. The standalone Shadow ABI gate
  reports `906` source classes, `763` class files, and `8,297` audited members; and
  the pinned offline Loader/Knot probe passes.
- The locked server-side Modrinth corpus (Lithium, FerriteCore, and Carpet)
  passes individually and in combination under Java 21, with zero classified
  runtime linkage/bootstrap diagnostics. This proves only bounded JVM/mod
  bootstrap and owned clean shutdown for those artifacts, not gameplay,
  registry, rendering, client, GUI, or arbitrary-mod parity. A wider 12-entry
  screening has 8 target-compatible runtime passes and 4 explicit non-target/
  invalid classifications. The latest bounded report is the ignored local
  build output
  `build/real-mod-candidates/compatibility-candidates-report-final-20260908.json`;
  the available Create archives have no matching Fabric 1.21.4 server artifact
  in the lock and therefore were not counted as runtime passes.
- A 120-client synthetic load run and 300-, 600-, and 1800-second diagnostics
  pass.

These are named-scenario results, not a universal compatibility percentage. The
latest package-target rebuild, clean extracted-package checks, and full
non-nightly CTest gates pass. The declared E-14, L3, long-run, and real-client
boundaries remain open.
Exact vanilla random-number parity for every generation path, arbitrary Fabric
JVM mods, accepted two-hour or 24-hour soak evidence, and a real-client/GUI
artifact remain outside the current claim. See [Verification](VERIFICATION.md)
for dates, commands, and failure interpretation.

## Clean-room boundary

The implementation is developed from protocol documentation, public API
documentation, and black-box observations of reference behavior. The
repository does not contain Mojang or Microsoft source code, assets, decompiled
code, or obfuscation maps. Captures and golden vectors are used as reproducible
behavioral evidence.

The machine-readable entity fixture is available at
[mob_stats_149.csv](mob_stats_149.csv). Build and test prerequisites, generated
data policy, and contribution rules are described in the [Development
guide](DEVELOPMENT.md).

CppFabricMC is not affiliated with Mojang or Microsoft. “Minecraft” is a
trademark of Mojang AB.
