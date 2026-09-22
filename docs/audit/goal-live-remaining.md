# Goal live remaining-entry evidence

This record covers the owned TCP fixture in
`tests/test_goal_live_remaining.py`. It starts the real `cppfm` binary,
logs in with protocol `769`, and drives serverbound combat, item, menu,
creative-inventory, movement, and world-border packets. The server and client
are isolated under the artifact directory and both are stopped in `finally`.

## Reproducible gate

| command | result | artifact |
|---|---|---|
| `timeout --foreground --kill-after=5 500 python3 tests/test_goal_live_remaining.py --binary ./build/cppfm --artifact-root /tmp/grok-goal-e8af7c070433/implementer/live-remaining-strict > /tmp/grok-goal-e8af7c070433/implementer/live-remaining-strict.log 2>&1` | `status=PASS`, `GOAL-LIVE-REMAINING` | `/tmp/grok-goal-e8af7c070433/implementer/live-remaining-strict.log` and `remaining-state.json` |
| `timeout --foreground --kill-after=5 180 ctest --test-dir build -R '^goal_live_remaining$' --output-on-failure --timeout 120` | `1/1` passed in `63.93s` | `/tmp/grok-goal-e8af7c070433/implementer/ctest-live-remaining-strict.log` |

The transcript observed:

- a real pig attack → `HurtAnimation 0x25` and `EntityVelocity 0x5F` for the
  same entity;
- `UseItemOn 0x3C` with a pig spawn egg → `SpawnEntity 0x01`;
- ender-pearl `UseItem 0x3D` → projectile spawn and `SetCooldown 0x17` with
  item id `1042` and `20` ticks;
- chest, barrel, shulker-box, hopper, anvil, and brewing-stand screens →
  `OpenScreen 0x35` plus authoritative `ContainerSetContent 0x13` counts of
  `63`, `63`, `63`, `41`, `39`, and `41` respectively, with the expected menu
  type ids `2`, `2`, `20`, `16`, `8`, `11`, and one anvil/two brewing
  `ContainerSetData 0x14` packets;
- serverbound `SetCreativeModeSlot 0x36` for a helmet → `SetEquipment 0x60`
  with the head marker;
- survival entry into fire → `SetHealth 0x62`/`DamageEvent 0x1A`;
- world-border center/size updates → border packets `0x26`, `0x52`, `0x54`
  and controlled outside-border health/damage observations.

## Boundaries

This fixture does not claim pearl teleport collision semantics, projectile
impact metadata, powder-snow freeze duration, drowning, hunger exhaustion,
fall mitigation, complete creative cursor/drag modes, or complete container
item mutation semantics. Those remain `PARTIAL` or `UNVERIFIED` in the
ledger unless another fixture names the exact assertion.
