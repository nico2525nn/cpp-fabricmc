# Goal live feature-entry evidence

This record covers the owned executable entry point, not native test doubles.
`tests/test_goal_live_features.py` starts `build/cppfm`, performs the real
status/login/configuration/play flow with `tests/mcproto.py`, and sends command
and interaction packets over TCP. The fixture uses an isolated world and server
root under the goal scratch directory.

## Reproducible gates

| command | result | artifact |
|---|---|---|
| `ctest --test-dir build --output-on-failure -R '^goal_live_features$'` | `1/1` passed in `42.56s` | `/tmp/grok-goal-e8af7c070433/implementer/ctest-live-features-final.log` |
| `python3 tests/test_goal_live_features.py --binary ./build/cppfm --artifact-root /tmp/grok-goal-e8af7c070433/implementer/live-entry` | `status=PASS` | `/tmp/grok-goal-e8af7c070433/implementer/live-features-13.log` |
| `timeout --foreground --kill-after=5 600 python3 tests/test_server_full.py --binary ./build/cppfm --suites=conn,commands,permissions,chat,datapack,persistence,restart` | `198 PASS, 0 FAIL` | `/tmp/grok-goal-e8af7c070433/implementer/server-full-final.log` |

The live fixture requires protocol `769`, an initial `DeclareCommands` and
`UpdateTime`, a non-empty tab-completion response for `/gi`, and command
feedback for time, gamerule, weather, difficulty, gamemode, `setblock`,
`fill`, `execute as`, `forceload`, `summon`, teleport, `give`, tags, teams,
scoreboard, world border, spawn points, bossbar, effects, XP, damage, data,
datapack/function/schedule, reload, and time query. It also requires the
resulting `level.dat` to be present and at least 64 bytes.

The same session observes `SpawnEntity` for the summoned pig (entity type 94),
`SetSlot`, `Teams`, block-update packets, default-spawn, difficulty, bossbar,
health, experience, and entity-effect packets. A real `UseEntity` packet keeps
the connection alive, but this build emitted no `SetPassengers` packet; riding
row #31 therefore remains a limitation rather than a pass.

## Row routing

The transcript is strong evidence for command rows #60, #61, and #64 and the
handshake/configuration/play row #72. The `DeclareCommands` packet supports row
#77. Rows #57, #59, #68, #71, #73, #89, and #90 gain partial live evidence,
but remain incomplete because the fixture does not parse every Brigadier
argument, independently validate every VarInt/Position field, exercise AES
encryption, spawn XP orbs, or validate effect modifiers. Existing partial rows
#62, #63, #70, and #76 remain partial for their documented missing branches or
cookie/resource-pack timeout assertions.

No row is upgraded from a broad packet ID alone when the row requires a deeper
state or vanilla transcript oracle. Nether/End generation, barrel/shulker and
full menu semantics, water, powder-snow, lava, hunger, fall mitigation, PVP
knockback, and full entity AI still require controlled fixtures or a real
vanilla client; focused menu/block evidence is recorded separately in
`goal-live-interactions.md`.

## Cleanup

The owned server and client are closed in `finally`; the CTest target returns
only after the server process has exited. The JSON state file and server log
are retained below the supplied scratch root for review.
