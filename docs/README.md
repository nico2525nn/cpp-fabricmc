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
| Runtime | C++20 server; optional embedded HotSpot/JNI integration |

The optional Java integration is a deliberately bounded extension surface. It
provides a version-locked shadow API, selected Fabric-style callbacks, and a
structural bytecode transformer for the tested server-side use cases. It is not
the official Mojang server runtime and does not claim universal compatibility
with arbitrary JVM mods, client code, or GUI behavior.

## Implementation map

| Area | Main paths | Reference |
|---|---|---|
| Encoding and protocol IDs | `src/core/`, `src/proto/` | [Wire specification](SPEC_WIRE.md) |
| Connections and packets | `src/net/` | [Wire specification](SPEC_WIRE.md) |
| World and persistence | `src/game/World*`, `src/game/RegionFile.*`, `src/game/WorldDataManager.*` | [Gameplay](SPEC_GAMEPLAY.md), [Operations](SPEC_OPS.md) |
| Simulation and world generation | `src/physics/`, `src/worldgen/`, `src/game/WorldGen.cpp` | [Gameplay](SPEC_GAMEPLAY.md) |
| Entities, items, and menus | `src/game/Entities.*`, `Items.*`, `Containers.*`, `Recipes.*` | [Gameplay](SPEC_GAMEPLAY.md) |
| Commands and administration | `src/game/Commands.*`, `src/game/commands_*.cpp` | [Gameplay](SPEC_GAMEPLAY.md), [Operations](SPEC_OPS.md) |
| Optional Java integration | `src/jvm/`, `jvm/java/`, `jvm/shadow_api.json` | [Verification](VERIFICATION.md#jvm-boundary-gate) |
| Tests and tooling | `tests/`, `tools/` | [Verification](VERIFICATION.md) |

## Verification at a glance

The latest recorded evidence includes:

- Native server checks and the ordinary integration suite pass.
- Wire checks report `395 PASS / 0 FAIL` for the specification vectors and
  `399 PASS / 0 FAIL` for the full wire suite.
- The full server harness reports `234 PASS / 0 FAIL`; the 80-scenario smoke
  integration test reports `223 PASS / 0 FAIL`.
- The full CTest regression set reports `33/33` tests passed, including the
  native, JVM, wire, gameplay, recovery, and ordinary multi-client gates.
- The bounded Java fixture corpus reports `25/25`, and the pinned offline
  Loader/Knot probe passes.
- A 120-client synthetic load run and 300-, 600-, and 1800-second diagnostics
  pass.

These are named-scenario results, not a universal compatibility percentage.
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
