# Next Steps — Development Continuation Guide

## Current State
All tests green (native/golden/integration/multi/anvil). ~15k lines C++20.

plan2.md (network/modding-API) and plan3.md (gameplay systems) are fully
implemented. plan4.md holds the researched queue for the remaining vanilla
gaps, with exact wire formats already extracted from the community datasets.

## Priority Queue (from plan4.md)

### P1-B Villagers & trading
Trade List packet format documented in plan4; needs MobKind::Villager,
merchant menu (type 19), select_trade handling and a small trade table.

### P2-E Hoppers/Dispensers
HopperData(5 slots) in BlockEntityStore + hoppersTick() every 8 ticks moving
stacks between adjacent inventories. Dispenser fires projectiles via the
existing spawnProjectile().

### P2-G Nether dimension
Second World instance (world_nether), Respawn-based transition, nether density
function (ceiling/floor via y_clamped_gradient).

### P2-H Enchanting/anvil/brewing menus
Menu types 13/8/11 already known; EnchantmentHelper exists for damage — extend
to mining/durability attribute modifiers.

### P3-M Placement context for stairs/doors/logs
stateWithProps() supports arbitrary property maps already; wire facing/half
from UseItemOn context and add door two-block placement + right-click toggle.

## Architecture Notes
- Connection handles compression + encryption transparently
- World uses shared_mutex; LightEngine/FluidSim/Redstone hook onBlockChanged
- Persistence runs on background thread every 3s (+ chunk extras callbacks)
- All entity data lives in Player/MobEntity/ItemEntity/XpOrb/Projectile structs
- AI: per-mob Brain+AiContext in GameServer::mobAi_ (see src/game/AiBrain.*)
- TestClient provides a full protocol client for native tests
