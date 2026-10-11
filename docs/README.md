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
- [Real-client verification](MC_PILOT_REAL_TEST.md) — bounded checks with a
  mc-pilot-managed Fabric client and PrismLauncher, including their scope and
  limitations.
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
mods, client code, or GUI behavior. Selected JNI callbacks may overlap across
session and tick threads; native mutations are queued to the server thread, but
Fabric's exact callback thread affinity is not reproduced.

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

The project uses CMake and CTest for focused unit, wire-format, gameplay,
integration, persistence-recovery, and operational checks. The optional
Mineflayer probe connects independent Minecraft 1.21.4 clients to a fresh local
server and exercises login, chat, movement, block interaction, and selected
inventory/command paths, including filled-shulker dispenser placement into cave
air. A separate bounded probe covers the selected Java
extension and mod-loading surface.

These checks establish only the behavior and scenarios they exercise; they are
not a universal compatibility percentage. In particular, arbitrary Fabric
mods, the complete vanilla world-generation random-call sequence, configured
feature execution, full GUI behavior, and accepted multi-hour soak evidence
remain outside the current compatibility claim. The optional `/place feature`
probe tests safe block mutation for a small fixed-pattern subset, not vanilla
configured-feature parity.

Use [Verification](VERIFICATION.md) for reproducible commands and evidence
boundaries, [Current status](CURRENT_STATE.md) for dated results, and [Real-client
verification](MC_PILOT_REAL_TEST.md) for the bounded desktop-client record.

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
