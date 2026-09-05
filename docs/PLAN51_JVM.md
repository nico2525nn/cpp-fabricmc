# Plan51 — embedded JVM compatibility boundary

This document records the implemented subset of plan51 for Minecraft Java Edition
1.21.4, protocol 769, and DataVersion 4189. It is a compatibility boundary for
server-side extensions, not a claim that the C++ server is the official Minecraft
runtime or that arbitrary Fabric mods are supported.

## Outcome

The native server now has an opt-in, process-local HotSpot/JNI boundary. The normal
build and runtime remain C++-only unless `--jvm=true` or `jvm=true` is configured.
When a JDK with JNI headers is available, the build produces the dependency-free
Java compatibility classes and an executable fixture. At runtime the bridge:

- starts one embedded HotSpot VM through `JNI_CreateJavaVM`;
- exposes generation-safe opaque handles instead of C++ pointers;
- loads directory or JAR candidates with a small `fabric.mod.json` reader,
  dependency ordering, server entrypoints, and server mixin metadata;
- exposes native-backed server, world, player, entity, inventory, message, command,
  registry, and lifecycle surfaces;
- dispatches selected Fabric-style lifecycle/player/block/damage/spawn events on the
  game thread; and
- runs a bounded Mixin hook shell for manually exposed `HEAD`, `TAIL`, `RETURN`, and
  simple `@Overwrite` sites.

The loader message intentionally says `compatibility loader fallback`: Fabric
Loader/Knot is not embedded by this change.

## Implemented status

| plan51 area | status | evidence / boundary |
|---|---|---|
| Optional JVM bootstrap and clean shutdown | `IMPLEMENTED` | `src/jvm/JvmRuntime.*`; `jvm_runtime` starts HotSpot and exits through the owned process path |
| Opaque object handles and cache | `IMPLEMENTED-PARTIAL` | `NativeHandleTable`, `JavaObjectCache`, `test_jvm_handles`; entity/world lifetime coverage remains bounded to wired server paths |
| Native server/world/player/entity bridge | `IMPLEMENTED-PARTIAL` | `jvm/java/net/minecraft/**` and JNI methods in `JvmRuntime.cpp`; shadow objects expose only the documented subset |
| Inventory and registry/settings surface | `IMPLEMENTED-PARTIAL` | logical PlayerInventory slots, native-backed ItemStack mutation, custom Block registration; data components and every registry are not mirrored |
| Lifecycle and Fabric-style events | `IMPLEMENTED-PARTIAL` | server/tick/world/player/block/damage/spawn callbacks; networking send is an explicit no-op transport boundary |
| Commands | `IMPLEMENTED-PARTIAL` | minimal Brigadier tree/literal/string/integer execution and registration; redirects, suggestions, and full parser parity are absent |
| Mixin `HEAD`/`TAIL`/`RETURN`/simple `Overwrite` | `IMPLEMENTED-PARTIAL` | `MixinHooks` plus manually wired `World`/`MinecraftServer` call sites; fixture proves all four paths |
| Accessor/Invoker/Shadow/Redirect/Modify* | `ABI-ONLY` | annotations exist for compilation; no transformer or execution implementation |
| Structured class-file bytecode transformation | `TODO` | manifest reports `structuredBytecode=false` and `manual-hook-shell` |
| Official Fabric Loader/Knot and arbitrary mod corpus | `DECLARED-LIMITATION` | intentionally not embedded or claimed |

## Source ownership

| concern | owner |
|---|---|
| JNI VM, native calls, handle lifetime, routing | `src/jvm/` |
| Java compatibility API and fallback loader | `jvm/java/` |
| declarative ABI and build manifest | `jvm/shadow_api.json`, `tools/generate_shadow.py` |
| native integration points | `src/game/GameServer.hpp`, `GameServer_{core,session,tick,combat}.cpp`, `BehaviorTree.cpp` |
| deterministic fixture | `tests/jvm_fixture/` |
| process, handle, and manifest evidence | `tests/jvm_runtime_smoke.py`, `tests/test_jvm_handles.cpp`, CTest |

The generated manifest is a build artifact at
`build/jvm/compatibility-manifest.json`; it is generated from the declarative ABI
specification and does not generate Mojang classes or bytecode.

## Configuration

```text
--jvm=true
--jvm-strict=true|false
--jvm-classes=<compiled compatibility classes>
--jvm-mods=<directory containing mod directories or .jar files>
--jvm-config=<config directory>
--jvm-java-home=<JDK home, optional>
--jvm-library=<absolute libjvm.so path, optional>
```

The same keys can be placed in `server.properties` (`jvm`, `jvm-strict`,
`jvm-classes`, `jvm-mods`, `jvm-config`, `jvm-java-home`, and `jvm-library`). In
non-strict mode an unavailable or malformed optional JVM layer is logged and the
native server continues. Strict mode makes startup failure visible and fatal.

## Evidence

The focused plan51 gate was run on 2026-09-05 from the 1.21.4/protocol-769 source:

```text
cppfm_jvm_classes       PASS
cppfm_jvm_fixture       PASS
test_jvm_handles        PASS
jvm_runtime             PASS
jvm_manifest            PASS
```

The runtime fixture observed `embedded HotSpot started`, entrypoint initialization,
command registration and integer-argument execution, World API access,
`SERVER_STARTED`, `MIXIN_HEAD`, `MIXIN_TAIL`, `MIXIN_RETURN`, `MIXIN_OVERWRITE`, and
`END_SERVER_TICK`, then terminated its owned server cleanly. It also routed a
Java-registered command from the native console ingress. These are boundary tests,
not evidence of arbitrary mod or client compatibility.

## Explicit non-goals

Plan51 does not close the project's other declared boundaries: official-client/GUI
evidence, accepted 2-hour or 24-hour evidence, exact vanilla Xoroshiro L3 byte
parity, or universal Fabric JVM-mod compatibility. The target remains protocol 769;
no other protocol version is part of this implementation.

Further Mixin coverage requires a versioned class-file transformer, transformed
bytecode tests, and a real mod corpus. Further API coverage requires per-method
ABI/evidence entries; adding names to the shadow package alone is not sufficient.
