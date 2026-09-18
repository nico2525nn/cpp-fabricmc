# Real-client verification with mc-pilot and PrismLauncher

This document records the local black-box verification performed against the
Minecraft Java Edition **1.21.4** target (protocol **769**) on **2026-09-13
JST**. It is a client-compatibility record, not a claim of complete vanilla or
Fabric-mod parity.

## Result

The repaired server accepts a real Fabric 1.21.4 client and remains in the world
long enough to exercise chat, commands, block reads, block breaking, status
queries, and screenshot capture. The same result was reproduced with an
explicit `server.properties` configuration. A separate final run also started
the official Fabric client through the installed PrismLauncher CLI, refreshed an
existing authenticated account, and joined the same server through the CLI
`--server` option.

| probe | result | scope |
|---|---|---|
| mc-pilot CLI static checks | `PASS` — 224/224 CLI tests; protocol check and build pass | mc-pilot's own CLI/data checks |
| Fabric client login | `PASS` | Minecraft 1.21.4, Fabric Loader 0.16.14, Java 21, offline account through mc-pilot; authenticated account refresh through PrismLauncher |
| configuration → play → world | `PASS` | login success, configuration acknowledgement, chunks, commands, advancements, status |
| live operations | `PASS` | chat, `say`, position, block get, block break, post-break block get, screenshot |
| post-login stability | `PASS` | more than one minute of continuous play in the final normal-world run; no client protocol disconnect |
| default world type | `PASS` | no property produces `level=normal`; generated properties contain active `level-type=normal` |
| explicit properties | `PASS` | `port=25571`, `view=4`, `simulation=3`, `world=world`, `level=flat`, and MOTD were read from the server root |
| PrismLauncher CLI | `PASS` | Flatpak 11.1.0 responds to `--help`/`--version`; a temporary Fabric 1.21.4 instance launched under Xvfb with `--launch`, `--profile`, and `--server` |
| PrismLauncher game join | `PASS / LOCAL-ONLY` | the CLI-launched Fabric client connected to cppfm, loaded 89 advancements, and received the server welcome message; logs/screenshot were temporary |

The generated screenshot was a 427×240 real Minecraft client frame with the
HUD and advancement toasts visible. It was intentionally kept as temporary
local evidence rather than added as a repository asset.

## Reproduction

The following is the shape of the successful local run. Substitute the paths
for the checkout and the cloned mc-pilot directory on another machine.

```bash
export MCPILOT_DIR=/path/to/mc-pilot
export MCT_HOME=/tmp/mct-real
export DISPLAY=:99

timeout --foreground --kill-after=10 180 \
  "$MCPILOT_DIR/cli/bin/mct" client launch real-1.21.4 \
  --server 127.0.0.1:25570 --force

timeout --foreground --kill-after=10 150 \
  "$MCPILOT_DIR/cli/bin/mct" client wait-ready real-1.21.4 \
  --timeout 90

timeout --foreground --kill-after=10 30 \
  "$MCPILOT_DIR/cli/bin/mct" --client real-1.21.4 status all
timeout --foreground --kill-after=10 30 \
  "$MCPILOT_DIR/cli/bin/mct" --client real-1.21.4 chat send hello-from-real-test
timeout --foreground --kill-after=10 30 \
  "$MCPILOT_DIR/cli/bin/mct" --client real-1.21.4 chat command \
  'say real-command-probe'
timeout --foreground --kill-after=10 30 \
  "$MCPILOT_DIR/cli/bin/mct" --client real-1.21.4 block get 0 -60 0
timeout --foreground --kill-after=10 30 \
  "$MCPILOT_DIR/cli/bin/mct" --client real-1.21.4 block break 0 -60 0
timeout --foreground --kill-after=10 30 \
  "$MCPILOT_DIR/cli/bin/mct" --client real-1.21.4 block get 0 -60 0
timeout --foreground --kill-after=10 30 \
  "$MCPILOT_DIR/cli/bin/mct" --client real-1.21.4 screenshot \
  --output /tmp/cppfm-real-client.png

timeout --foreground --kill-after=10 45 \
  "$MCPILOT_DIR/cli/bin/mct" client stop real-1.21.4
```

The successful operation responses were `sent=true`, `inWorld=true`, a stone
block before breaking, and `minecraft:air` after breaking. The final status
reported 20 health, 20 food, no effects, no death, and an empty disconnect
reason.

For the PrismLauncher pass, the final invocation used the installed system
Flatpak and a temporary instance containing Minecraft 1.21.4 and Fabric Loader
0.16.14. The account selector was an existing authenticated profile; its name
and tokens are intentionally not recorded here:

```bash
timeout --foreground --kill-after=10 240 \
  env DISPLAY=:99 QT_QPA_PLATFORM=xcb QT_XCB_GL_INTEGRATION=none \
  flatpak run org.prismlauncher.PrismLauncher \
  --launch <temporary-instance-id> --profile <authenticated-profile> \
  --server 127.0.0.1:25572
```

The client log recorded `Connecting to 127.0.0.1, 25572`, loaded the world and
advancements, and received the server welcome message. A clean isolated
profile with no account was also checked: PrismLauncher does not manufacture
an offline account from `--offline`; it stops at the account-selection/demo
prompt. No account-bypass patch or fake account was used. The successful
PrismLauncher result therefore covers an existing authenticated account and
the client/server path, not first-time Microsoft sign-in.

For the properties probe, start the server without a command-line override and
place the following values in the server root's `server.properties`:

```properties
server-port=25571
max-players=7
view-distance=4
simulation-distance=3
level-type=flat
motd=PropertyProbe
jvm=false
```

The server printed:

```text
[cppfm] CppFabricMC starting: port=25571 view=4 biome=minecraft:plains world=world level=flat
```

The client then completed the same login and world-entry path on port 25571.
This verifies that the properties file is read from the server root and that
`level-type=flat` is honored when it is explicitly configured. The production
default is now normal terrain; flat remains an explicit test/development
choice.

## Fixes made as a result of the real-client loop

- Advancement update flags are encoded as fixed big-endian `i32`, not VarInt.
- Declare Commands integer min/max values are fixed `i32`; the `time` minimum is
  also fixed `i32`, and `score_holder` writes its allow-multiple byte.
- Heightmaps use the 1.21.4 non-straddling packed layout: 37 longs for 256
  nine-bit values, rather than 36 contiguous/straddling longs.
- Entity spawn UUIDs are non-zero, valid, and distinct for each entity id.
- Zero-weight definitions for decorations, displays, projectiles, and vehicles
  are excluded from natural mob selection, so they cannot be sent through the
  generic living-mob spawn path.
- All server-side `EntityTeleport` producers share one 1.21.4 encoder. The
  packet is `entity id VarInt`, two `Vec3d` records (six `f64` values), yaw and
  pitch as `f32`, relative-position flags as fixed `i32`, and `onGround` as a
  boolean.
- The generated/default properties template now contains an active
  `level-type=normal`, while an explicit `flat` value remains honored.
- `--world-dir` is treated as the world save path; server properties remain in
  the server root, avoiding an accidental split configuration.

The last item is especially important for this investigation: the raw
Prismarine 1.21.4 protocol table currently describes the older short
`teleport_entity` shape. The official 1.21.4 Yarn model identifies the packet as
`EntityPositionS2CPacket` with a `PlayerPosition` record and a position-flags
set. The real client decoder exposed the mismatch during the first run; the
implementation now follows the official client-side shape documented in
[EntityPositionS2CPacket](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/network/packet/s2c/play/EntityPositionS2CPacket.html),
[PlayerPosition](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/entity/player/PlayerPosition.html),
and [PlayPackets](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.4/net/minecraft/network/packet/PlayPackets.html).

## mc-pilot assessment

For this repository's black-box client testing use case, the practical rating
is **6.5/10: useful beta tooling, not yet a reliability-grade test platform**.

What is good:

- It can launch a real Fabric client, inject a small control mod, expose useful
  JSON commands, and collect state/screenshots instead of relying only on a
  protocol bot.
- The repository has a meaningful CLI test surface: the local run passed
  224/224 CLI tests, the protocol consistency check, and the build.
- The command surface is convenient for repeatable smoke tests and can cover
  world actions that are difficult to express with a raw socket client.

What needs caution:

- The observed `wait-ready` failure after a rapid stop/start was an orphaned or
  still-closing WebSocket lifecycle problem: the Java client reported port 25580
  already in use, while the launcher process was still alive and no WebSocket
  listener was available. Waiting for the owned process to exit and relaunching
  made the test pass.
- A separate command sequence produced one
  `WebsocketNotConnectedException` in the client-mod log after the response
  socket closed. The Minecraft connection stayed in-world and all subsequent
  operations succeeded, so this is harness noise/race, not evidence of a server
  packet failure. A production test runner should make request ownership and
  WebSocket close ordering explicit.
- The client launcher script contains a platform-rule matcher hardcoded to
  `osx-arm64`. That is risky for Linux/x86-64 and should be corrected upstream;
  the successful run benefited from already available cached artifacts.
- The mc-pilot-controlled run here used an offline test identity. Its own
  launcher therefore did not validate Microsoft account login, session
  authentication, multiplayer security policy, or authenticated resource
  access; the separate PrismLauncher run did validate an existing account
  refresh and entitlement check.

The one-star count is a signal that the project is small and lightly adopted,
not a technical proof that it is unusable. The observed behavior supports
treating mc-pilot as a capable exploratory/e2e harness with explicit lifecycle
guards, rather than as the sole release gate.

## PrismLauncher assessment

The installed system Flatpak is PrismLauncher **11.1.0**. Its CLI help,
version command, GUI startup under Xvfb, account refresh, Fabric 1.21.4
instance launch, and `--server` join all succeeded in the final run. The
successful client log and server-side `login hello`/initial-chunk sequence
confirm that the CppFabricMC server was joined through PrismLauncher itself.
The clean no-account profile check also explains the earlier login error:
official PrismLauncher requires an account for normal play, and its `--offline`
argument does not create a standalone offline account in a fresh profile.

## Evidence boundary

This probe establishes real-client login, packet decoding on the exercised path,
world entry, and a small set of actions. It does not establish:

- arbitrary Fabric JVM-mod ABI or Fabric event-bus compatibility;
- first-time Microsoft interactive login (an existing account refresh and
  entitlement check passed);
- complete vanilla world generation or exact RNG L3 parity;
- every entity, menu, command, or dimension path;
- a two-hour or 24-hour soak; or
- compatibility with every PrismLauncher instance configuration.

Those boundaries remain documented in [Verification](VERIFICATION.md),
[Current state](CURRENT_STATE.md), and the feature specifications.
