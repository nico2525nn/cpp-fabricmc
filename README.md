# CppFabricMC — a clean-room Minecraft 1.21.4 server in C++

CppFabricMC is an independent C++20 implementation of the Minecraft: Java
Edition 1.21.4 server protocol (protocol 769). It is designed for behavioral
interoperability with an unmodded Fabric/vanilla server, with the wire format,
world data, and gameplay behavior implemented from scratch.

The project does not contain Mojang or Microsoft source code or assets. Protocol
and behavior claims are supported by black-box reference captures, focused
tests, and explicit limitations.

## Documentation

The [documentation index](docs/README.md) is the best place to continue. The
main references are:

- [Wire specification](docs/SPEC_WIRE.md)
- [Gameplay specification](docs/SPEC_GAMEPLAY.md)
- [Operations specification](docs/SPEC_OPS.md)
- [Development guide](docs/DEVELOPMENT.md)
- [Verification and evidence](docs/VERIFICATION.md)
- [Current state](docs/CURRENT_STATE.md)
- [Java integration overview](docs/VERIFICATION.md#jvm-boundary-gate)
- [Supported feature details](docs/SPEC_GAMEPLAY.md)

## What is implemented

| Area | Coverage |
|---|---|
| Network protocol | Status ping, login, online-mode encryption, configuration, play state, chunk streaming, batching, cookies, resource packs, transfer, plugin channels, and signed chat verification |
| World and persistence | Anvil region files, `level.dat`, block entities, per-cell biomes, chunk loading/unloading, and interoperability with vanilla world data |
| World generation | Multi-noise biomes, density functions, Nether and End terrain, ores, configured/placed features, and common structures |
| Simulation | Block and fluid ticks, lighting, weather, redstone, pistons, rails, explosions, fire, portals, and world borders |
| Entities and survival | Mobs, AI behavior trees, attributes, effects, hunger, damage, projectiles, breeding, aging, and dimension respawn |
| Items and containers | Data components, durability, enchantments, crafting, recipes, furnace, storage, villager and workstation menus, drag actions, and authoritative slot updates |
| Commands and administration | Brigadier parsing, selectors, common vanilla commands, functions, scoreboards, teams, boss bars, permissions, whitelist, bans, RCON, and server properties |
| Fabric-compatible Java integration | Default-on in binaries configured and built with the required JDK/JNI bridge: embedded HotSpot, a version-locked shadow API, selected callbacks, a tick-thread executor boundary, and a bounded structural transformer for server-side extensions; compatible runtime JDK/classes are still required, and a no-JNI build remains native-only |

This is a compatibility-oriented implementation, not a claim that every
Minecraft feature or every Fabric mod behaves identically. The Java integration
is deliberately bounded; it is not the official Mojang server runtime or a
general-purpose arbitrary-mod loader.

## Known limitations

- The supported target is Minecraft Java Edition 1.21.4 and protocol 769.
- Exact vanilla random-number and structure-NBT parity has not been proven for
  every world-generation path.
- The Java layer supports the repository's shadow API and tested extension
  surface. It is default-on only when configure/build finds the required JDK/JNI
  inputs; a JNI-capable binary still needs a compatible runtime JDK/classes and
  fails open to the native server when they are unavailable. A binary built
  without JNI remains native-only until rebuilt; arbitrary Fabric mods, the
  Mojang GameProvider, and the official client are outside the compatibility
  claim.
- The full gameplay harness reports `807 PASS / 0 FAIL / 807`; its output still
  names arbitrary Java-extension execution as a declared limitation.
- Enchanting and crafter behavior have focused coverage, but their bounded
  implementations are not presented as complete vanilla menu parity.
- Short and medium synthetic load runs pass, but there is no accepted 2-hour or
  24-hour soak artifact and no current real-client/GUI capture.

## Verification evidence

The latest recorded runs include:

- Native server checks: the named `test_native` checks pass; this target has no
  stable aggregate count.
- Wire-format checks: `test_spec_wire` `395 PASS / 0 FAIL` and
  `test_wire_full` `399 PASS / 0 FAIL`.
- Integration checks: the clean extracted Linux package's `test_server_full`
  reports `234 PASS / 0 FAIL`; the source-tree `test_smoke_80` reports
  `223 PASS / 0 FAIL`.
- Focused gameplay and data checks confirmed for the current working tree:
  gameplay `807 PASS / 0 FAIL`, seed
  `201 PASS`, fuzz `25 PASS`, mining
  `59/59`, block hardness `1095 mismatch=0`, mob statistics `131 PASS`,
  redstone `42 PASS / 0 FAIL`, fluids `23 PASS / 0 FAIL`, and menu logic
  `41 PASS / 0 FAIL`.
- Java compatibility checks: the bounded historical fixture corpus is `25/25`; its
  harness also passes the auxiliary functional API fixture, the standalone Shadow
  ABI gate passes, and the offline pinned Loader/Knot probe passes.
- The latest recorded 2026-09-10 operational baseline is non-nightly CTest `42/42 PASS`
  in `397.54s`, multi-client `ALL PASS` in `17.84s`, and bot smoke `ALL PASS`
  in `20.87s`. The release-specific `package_jvm_smoke` gate is separate: it
  passed against the exact CPack ZIP after clean extraction, with default-on
  strict JVM startup and embedded classes/assets verified. The locally generated
  ignored Linux CPack output contains exactly one executable; its clean
  extracted-directory `test_server_full` run is `234 PASS / 0 FAIL`.
- The class-file linkage tooling and fail-closed runtime-diagnostic contract pass;
  raw official-provider scans are retained as conservative diagnostics for
  Mixin-added members.
- Public server-side Modrinth probes for Lithium, FerriteCore, and Carpet pass
  individually and in combination under Java 21 with zero classified runtime
  linkage/bootstrap diagnostics. This is a bounded bootstrap/clean-shutdown
  result, not gameplay, registry, rendering, client, or arbitrary-mod parity.
  A wider 12-entry screening records 8 target-compatible runtime passes and 4
  explicit non-target/invalid classifications. The latest local generated
  report is
  `build/real-mod-candidates/compatibility-candidates-report-final-20260908.json`;
  this is an ignored local build output, not a tracked or public evidence
  artifact. The available Create archives have no matching Fabric 1.21.4 server
  artifact and are not counted as runtime passes.
- Load checks: 120 concurrent synthetic clients joined successfully; the latest
  60-second post-review soak had 0 disconnects and 1.0% post-fill RSS growth;
  300-, 600-, and 1800-second diagnostic runs also passed. Longer-run and real-client
  limitations remain as listed above.

The latest recorded local package evidence is
`build/packages/cppfabricmc-1.21.4-Linux-x86_64.zip` (SHA-256
`c6ae183d4e527f1b75f4cae35a0bcbfb3dba1ab4552e7039ae0f129b378e5440`, archive size
`54395700` bytes). The ZIP contains only `cppfm`; the package was tested from a
clean extraction directory. `build/` is an ignored local output directory, not a
release asset embedded in this repository.

The package target and separate JVM gate were both rerun successfully against
the current executable. The CPack preflight ran during package generation, and
the JVM gate then consumed that exact ZIP from a clean extraction directory.

These figures are test evidence for named scenarios. They are not an aggregate
score or a universal claim of vanilla parity. The complete commands, dates,
artifacts, and failure interpretation are maintained in
[Verification](docs/VERIFICATION.md) and [Current state](docs/CURRENT_STATE.md).

## Building and running

Requirements:

- C++20 compiler
- CMake 3.20 or newer and Ninja (or another supported CMake generator)
- Python 3 for the self-contained resource pack; the `package` target refuses
  to create a release-looking archive when the pack is unavailable
- JDK 17 or newer, available at configure/build time, for the embedded
  shadow-runtime classes and JNI bridge. The deployed JVM-capable binary also
  needs a compatible runtime JDK; a binary configured without JNI remains
  native-only until it is rebuilt. JDK 21 is used for the pinned real-mod
  probes (native-only fallback is available when no JDK is installed). Some
  upstream mods, such as the tested C2ME candidate, require a newer Java
  release and are classified explicitly rather than silently passed.

Configure and build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
./build/cppfm
```

When Python 3 is available at configure/build time, the build embeds the
repository-owned registry/data inputs, the Java shadow classes when `javac` and
JNI are available, and the default properties template in the executable. A
build configured without Python 3 uses external assets and is not a
self-contained one-file bundle. The resulting
`build/cppfm` (or `cppfm.exe` on Windows) can be copied by itself into an empty
server directory, just like `server.jar`. On its first launch it creates and
uses this layout:

| Path | Purpose |
|---|---|
| `world/` | Overworld save, region files, entities, POI, player data, and dimensions |
| `mods/` | User-provided server-side mod directories or JARs |
| `config/` | User and mod configuration |
| `libraries/` | Optional JVM/mod libraries |
| `logs/`, `crash-reports/` | Runtime diagnostics |
| `resourcepacks/` | Server resource-pack workspace |
| `.cppfm/jvm/classes/` | Extracted, executable-owned Java compatibility classes |
| `server.properties` | Created from the embedded example and then preserved |

Existing user files are preserved. Embedded assets are only installed when
missing; the hidden Java compatibility directory is refreshed from the
executable on each launch so an upgraded binary cannot use stale shadow
classes. Set `CPPFM_SERVER_DIR` to choose another server directory; relative
paths are resolved from that directory.

To create a one-file ZIP containing the executable, run:

```bash
timeout --foreground --kill-after=5 180 cmake --build build --target package
```

The package target performs a fail-closed preflight inside CPack. It refuses to
generate the ZIP when Python 3, the required registry assets, or the generated
embedded resource header is unavailable; the native executable can still be
built for fallback use. The package is written to the ignored local
`build/packages/` directory and contains exactly one file. The
one-file ZIP is currently verified on Linux x86-64; Windows and macOS require
their own native CMake build and are not implied by the Linux artifact. As with
most native applications, the host still supplies its platform C/C++ runtime,
OpenSSL/zlib libraries, and (for Java integration) a compatible JDK. “One file”
describes the distribution archive, not a statically linked guarantee for every
host or a bundled Java runtime. Useful server-property and CLI options are documented in
[Verification](docs/VERIFICATION.md).

When the build has both JNI and compiled shadow classes, run the separate
release JVM gate after building the package:

```bash
timeout --foreground --kill-after=5 120 ctest --test-dir build \
  -R '^package_jvm_smoke$' --output-on-failure --timeout 60
```

This gate consumes the exact ZIP, verifies that it contains only `cppfm` (or
`cppfm.exe` on Windows), extracts it into a clean temporary directory, and
starts the extracted executable without a `--jvm-classes` override. It requires
strict JVM startup evidence and checks the extracted classes and registry assets.
It is registered only when JNI/classes are available; the ordinary non-strict
native fallback remains valid when they are not.

Java integration is on by default only in a binary configured and built after
the required JDK/JNI inputs were found and the JNI bridge was compiled. The
runtime host must also provide a compatible JDK and the embedded/generated
classes. A binary built without JNI remains native-only even if a JDK is
installed later; rebuild it after installing the JDK. The relevant
controls are:

```bash
./build/cppfm --jvm-strict=true
./build/cppfm --jvm=false
```

`--jvm-strict=true` makes a missing/incompatible runtime JDK, JVM library, or
class tree a fatal startup error; without strict mode the server logs the reason
and continues with its native implementation. Put user Java mods in `mods/` and
their configuration in `config/`. The embedded Java layer is a compatibility
extension surface, not a copy of the official Minecraft server or a promise
that every Fabric mod can run unchanged. It does not ship the official Mojang
GameProvider, client, or GUI runtime.

Connect with a Minecraft 1.21.4 client in offline mode, for example by using a
launcher profile pointed at `127.0.0.1`. The default development world is a
creative superflat world.

## Running tests

Build first, then run focused checks as needed:

```bash
./build/test_native ./build/cppfm
./build/test_smoke_80 ./build/cppfm
./build/test_spec_wire
./build/test_wire_full
timeout --foreground --kill-after=5 450 python3 tests/test_server_full.py --binary ./build/cppfm
./build/test_seed_parity
./build/test_fuzz
timeout --foreground --kill-after=5 1200 ctest --test-dir build -LE 'nightly|package' --output-on-failure --timeout 450
```

The gameplay harness prints its unsupported Java-extension boundary as an
informational declared limitation. A non-zero exit from any current gate is a
real failure; limitations are not represented as hidden passes. The regular CTest
command excludes the explicitly labeled nightly soak tests and the release-only
package gate; run the package command and `package_jvm_smoke` separately when
validating a release artifact.

## Clean-room methodology

Reference behavior is observed through a local server and a client written for
this project. Captured registry, tag, login, join, chunk, and keepalive data is
used as reproducible evidence and golden vectors. Community protocol
documentation is used for format definitions. No decompiled Mojang code,
Mojang assets, or obfuscation maps are used.

## Architecture

```text
src/core/       byte buffers, NBT, JSON
src/proto/      protocol identifiers and packet schemas
src/net/        connections, encryption, compression, packet batching
src/game/       worlds, entities, items, containers, commands, simulation
src/worldgen/   density, biomes, structures, portals
src/brigadier/  command tree and argument parsing
src/jvm/        JNI/HotSpot bridge and handle routing
src/generated/  generated block, item, and entity tables
jvm/java/       bounded Java shadow API and transformer support
tests/          unit, wire, integration, compatibility, and load tests
tools/          capture, verification, benchmarking, and diagnostics
```

For ownership, implementation details, and evidence maintenance, see the
[development guide](docs/DEVELOPMENT.md).

---

CppFabricMC is an independent interoperability research project and is not
affiliated with Mojang or Microsoft. “Minecraft” is a trademark of Mojang AB.
