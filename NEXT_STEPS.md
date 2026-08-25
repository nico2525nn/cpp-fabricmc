# Next Steps — Development Continuation Guide

## Current State
All tests green (native/golden/integration/multi/anvil-interop). ~4,000 lines C++20.

## Priority Queue (from plan.md, ordered by impact)

### P0 — Inventory Click Handling (WindowClickC2S)
Packet 0x0F in play/toServer. Structure:
- windowId i8, stateId varint, slot i16, button i8, mode varint
- changedSlots[] {slot i16, stack Slot}
- cursorItem Slot

Slot format: [varint count][if count>0: varint itemId + varint addComp + varint remComp + components]

Implementation plan:
1. Parse packet fully in handlePlay case pl::cs::WindowClick (= 0x0F)
2. Apply changes to Player::inv[46] array
3. Resend ContainerSetContent(0x13) to sync client UI

### P0 — level.dat write/read
Use NBTValue tree (already implemented in src/core/NBTValue.hpp).
Write on shutdown + periodic flush. Read on boot.
Key fields: DataVersion(i32), spawn X/Y/Z, GameRules compound, Time long.

### P1 — playerdata/*.dat save/load
Save Player::inv/health/food/position/uuid as NBT.
File name = uuid hex string + ".dat".
Load on login if file exists.

### P1 — /gamerule command
GameRuleManager with map<string,string>. Persist inside level.dat GameRules compound.

### P2 — Brigadier command tree
CommandNode hierarchy with literal/argument types. Tab completion via Suggestions packet.

### P2 — Mob entity persistence to entities/*.mca
Serialize mob position/type/health to region files alongside chunk data.

### P3 — Full worldgen density functions
Requires JSON parsing VM for density_function definitions from misode/mcmeta.

## Architecture Notes
- Connection handles compression + encryption transparently
- World uses shared_mutex for concurrent access
- Persistence runs on background thread every 3s
- All entity data lives in Player/MobEntity/ItemEntity structs
- TestClient provides full protocol client for testing
