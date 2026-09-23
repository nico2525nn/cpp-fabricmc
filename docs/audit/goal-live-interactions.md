# Goal live interaction-entry evidence

This record covers the owned TCP entry point in
`tests/test_goal_live_interactions.py`. It starts the built `cppfm`, logs in
with protocol `769`, and drives menu, inventory, player-action, metadata,
equipment, block, and portal packets against the real server. The fixture uses
an isolated flat world and closes both client and server in `finally`.

## Reproducible gates

| command | result | artifact |
|---|---|---|
| `ctest --test-dir build --output-on-failure -R 'goal_live_(features|interactions)'` | `2/2` passed in `76.45s` | `/tmp/grok-goal-e8af7c070433/implementer/ctest-live-interactions.log` |
| `timeout --foreground --kill-after=5 240 python3 tests/test_goal_live_interactions.py --binary ./build/cppfm --artifact-root /tmp/grok-goal-e8af7c070433/implementer/live-interactions-15` | `status=PASS` | `/tmp/grok-goal-e8af7c070433/implementer/live-interactions-15.log` |

The PASS transcript observed:

- chest `UseItemOn 0x3C` → `OpenScreen 0x35` and `ContainerSetContent 0x13`
  with a non-zero window id and at least the 27 container slots;
- valid and stale `WindowClick 0x10` requests, including authoritative stale
  resynchronization;
- `/effect` → `EntityEffect 0x7D` with entity id, effect id, amplifier `1`,
  positive duration, and flags;
- `EntityAction 0/1` → `SetEntityMetadata 0x5D` pose `5/0` and sneak flags
  `0x02/0`;
- helmet replacement → `SetEquipment 0x60` with the head slot marker;
- survival placement/dig → `AckBlockChange 0x05`, break animation, and a
  final block state `0` update;
- a bow `UseItem 0x3D` → `SpawnEntity 0x01` followed by movement packets for
  the same projectile id (the movement path is implemented in
  `GameServer_items.cpp`);
- a Nether portal block → `Respawn 0x4C` and `PlayerPosition 0x42`; portal
  `GameEvent 0x23` packets were recorded but are not a required assertion.

## Fix found during the run

The real portal move was initially rejected because the shared
`isMotionBlocking` fallback treated `nether_portal`, `end_portal`, `fire`, and
`soul_fire` as solid. `src/game/ChunkCodec.hpp` now classifies those states as
non-colliding, allowing the normal movement and portal-transfer path to run.

## Boundaries

This fixture does not claim full vanilla parity for all container click modes,
item components or trims, barrel/shulker-specific menus, portal frame creation
or round trips, projectile metadata/impact effects, water/lava/powder-snow
survival damage, hunger, fall mitigation, or PVP knockback. Those rows remain
`PARTIAL` or `UNVERIFIED` in the ledger unless a named assertion covers them.
