# Pinned official Fabric runtime

This directory records the exact official runtime inputs investigated for the
Fabric 1.21.4 / protocol 769 target. The jars are deliberately **not** stored
in git. `fabric-runtime.lock.json` is the source of truth for versions, URLs,
sizes, SHA-256 digests, required classes, services, and the embedded
MixinExtras payload. `LICENSE-METADATA.json` records the license/source
metadata separately so a provisioned cache can be audited without trusting
unversioned repository state.

## Explicit provisioning

The runtime never downloads anything. Provisioning is a separate, intentional
operation:

```text
python3 tools/fetch_fabric_runtime.py \
  --provision --cache-dir build/fabric-runtime
python3 tools/verify_fabric_runtime.py \
  --offline --cache-dir build/fabric-runtime \
  --shadow-classes build/jvm/classes
```

`--provision` is the only mode allowed to access the two fixed official hosts
in the lock file (`meta.fabricmc.net` and `maven.fabricmc.net`). Downloads are
written atomically and are accepted only after the locked size and SHA-256
match. No `.sha256` file or mutable metadata is trusted in place of the lock.

Every other invocation is offline. A missing cache item is a hard error that
names the item and tells the operator to run the explicit provisioning command.
A mismatched item is also a hard error; it is never silently replaced. Use
`--force` only together with `--provision` when intentionally refreshing a
cache, then review the resulting digest against a deliberately changed lock.

The verifier checks every top-level jar, required class/service, the mapping
resource, and the embedded MixinExtras jar's digest. It prints a colon-separated
classpath with `--print-classpath` for callers that need it; callers must pass
those local paths to Java. The C++ runtime must not receive the URLs above and
must not attempt a runtime fetch.

## Knot/GameProvider probe

The verifier can compile and run the small adapter and probe mod with the
provisioned jars:

```text
python3 tools/verify_fabric_runtime.py \
  --offline --cache-dir build/fabric-runtime \
  --shadow-classes build/jvm/classes --probe
```

The adapter is registered through the official Loader `GameProvider` service,
and is selected with `-Dfabric.skipMcProvider=true` plus
`-Dcppfm.game-provider=shadow`. It adds the existing C++ shadow class
directory to Knot's class path and launches `ShadowMain`, which invokes the
official Fabric Loader entrypoint API. Before launch, the verifier stages a
copy that excludes duplicate `net/fabricmc/**` and `org/spongepowered/**`
classes from the shadow build; it retains `net/minecraft/**`, `com/mojang/**`,
and `cppfm/**` as needed. This keeps `ModInitializer` and Mixin type identity
owned by the pinned official jars. The probe mod then exercises a real Mixin
injection against the shadow `MinecraftServer.getTicks()` method.

This is a bounded compatibility result, not a claim that the C++ server is a
Mojang server jar. The built-in official `MinecraftGameProvider` cannot be
used here because it requires a real 1.21.4 Minecraft server jar and its
official game metadata/mappings. The custom provider is an internal Loader
0.16.9 integration point, so the version is pinned and must be re-probed when
Loader changes. A passing probe proves that this local shadow class path can
be bootstrapped by Knot/Mixin; it does not prove arbitrary Fabric mods, client
GUI behavior, vanilla RNG parity, 24-hour operation, or automatic
integration with the existing C++ process. Those boundaries are recorded in
`probe-evidence.json`.

## Production embedded gate

The process probe above and the production embedded path are separate gates.
The former starts a new `java` process. The latter starts `cppfm`, lets its JNI
runtime create one HotSpot, and enables the opt-in official path in
`KnotLauncher`:

```text
timeout --foreground --kill-after=5 300 cmake --build build --target cppfm -j4
timeout --foreground --kill-after=5 180 python3 tools/verify_fabric_runtime.py \
  --offline --cache-dir build/fabric-runtime \
  --embedded-binary build/cppfm --embedded-classes build/jvm/classes \
  --evidence jvm/vendor/embedded-evidence.json --timeout 45
```

`embedded-evidence.json` is only marked passed when the same process observes
the official Loader 0.16.9/Knot target loader, `named` mappings, the fixture
Mixin return, the target `NativeBridge` handoff, the event bridge, and a clean
server stop. It also rejects callback and strict-startup failure markers. The
test builds a disposable provider/probe jar; no official jar or generated jar
is committed.

Normal launches remain on the dependency-free fallback. An explicit production
launch may opt in with `CPPFM_FABRIC_RUNTIME` and
`CPPFM_FABRIC_PROVIDER_JAR` (or the equivalent `-Dcppfm.fabric-runtime` and
`-Dcppfm.fabric-provider-jar` properties). The provider jar must be built from
the pinned `jvm/vendor/provider` sources; the runtime cache is verified
offline first. This path uses the C++ shadow `GameProvider`, not Mojang's
`MinecraftGameProvider`, so it is evidence of the in-process execution chain,
not official complete Minecraft/Fabric compatibility.

The target here remains Minecraft 1.21.4 and protocol 769.
