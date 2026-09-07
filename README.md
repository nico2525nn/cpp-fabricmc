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
| Optional Java integration | An embedded HotSpot/JNI bridge, a version-locked shadow API, selected callbacks, and a bounded structural transformer for server-side extensions |

This is a compatibility-oriented implementation, not a claim that every
Minecraft feature or every Fabric mod behaves identically. The Java integration
is deliberately bounded; it is not the official Mojang server runtime or a
general-purpose arbitrary-mod loader.

## Known limitations

- The supported target is Minecraft Java Edition 1.21.4 and protocol 769.
- Exact vanilla random-number and structure-NBT parity has not been proven for
  every world-generation path.
- The optional Java layer supports the repository's shadow API and tested
  extension surface. Arbitrary Fabric mods, the Mojang GameProvider, and the
  official client are outside the compatibility claim.
- The full gameplay harness reports `804 PASS / 0 FAIL / 804`; its output still
  names arbitrary Java-extension execution as a declared limitation.
- Short and medium synthetic load runs pass, but there is no accepted 2-hour or
  24-hour soak artifact and no current real-client/GUI capture.

## Verification evidence

The latest recorded runs include:

- Native server checks: all pass.
- Wire-format checks: `test_spec_wire` `395 PASS / 0 FAIL` and
  `test_wire_full` `399 PASS / 0 FAIL`.
- Integration checks: `test_server_full` `234 PASS / 0 FAIL` and
  `test_smoke_80` `223 PASS / 0 FAIL`.
- Focused gameplay and data checks: gameplay `804 PASS / 0 FAIL`, seed
  `201 PASS`, fuzz `25 PASS`, mining
  `59/59`, block hardness `1095 mismatch=0`, mob statistics `131 PASS`, and
  redstone `29 PASS`.
- Java compatibility checks: the bounded fixture corpus is `25/25`, and the
  offline pinned Loader/Knot probe passes.
- Load checks: 120 concurrent synthetic clients joined successfully; 300-,
  600-, and 1800-second diagnostic runs passed. Longer-run and real-client
  limitations remain as listed above.

These figures are test evidence for named scenarios. They are not an aggregate
score or a universal claim of vanilla parity. The complete commands, dates,
artifacts, and failure interpretation are maintained in
[Verification](docs/VERIFICATION.md) and [Current state](docs/CURRENT_STATE.md).

## Building and running

Requirements:

- C++20 compiler
- CMake and Ninja (or another supported CMake generator)
- JDK 21 only when building or running the optional Java integration

Configure and build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4
./build/cppfm --port 25565 --view-distance 6
```

The server expects the repository's `assets/registry/` and `assets/data/`
directories beside the selected world directory. Useful server-property and
CLI options are documented in [Verification](docs/VERIFICATION.md).

To enable the optional Java integration in a development checkout:

```bash
./build/cppfm \
  --jvm=true \
  --jvm-strict=true \
  --jvm-classes=build/jvm/classes \
  --jvm-mods=build/jvm/fixture-mods
```

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
python3 tests/test_server_full.py --binary ./build/cppfm
./build/test_seed_parity
./build/test_fuzz
ctest --test-dir build --output-on-failure
```

The gameplay harness prints its unsupported Java-extension boundary as an
informational declared limitation. A non-zero exit from any current gate is a
real failure; limitations are not represented as hidden passes.

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
src/jvm/        optional JNI/HotSpot bridge and handle routing
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
