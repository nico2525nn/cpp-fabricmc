# SOAK Manual — Vanilla 1.21.4 Client Long Play Verification (B-06)

> Target: `cpp-fabricmc` (protocol 769, DataVersion 4189) vs Vanilla 1.21.4 offline client.
> Server: `build/cppfm --port=25565 --view-distance=12 --world-dir=/tmp/soak-manual --online-mode=false`
> Client: Official Launcher 1.21.4, Installations → 1.21.4 → Play Offline

## Prerequisites
- Build green: `cmake -B build -G Ninja && cmake --build build -j4`
- Start server: `build/cppfm --port=25565 --view-distance=12 --world-dir=/tmp/soak-manual --online-mode=false`
- Vanilla client 1.21.4 Offline: Server Address `127.0.0.1:25565`
- Optional capture: `python3 tools/capture.py --port 25565 --duration 7200 --out captures/manual-soak-$(date +%Y%m%d).bin`

## Checklist (2h, 7 items + plan43 B1/B2 5 items = 12)

| # | Verification Item | Steps | Expected (PASS) |
|---|-------------------|-------|-----------------|
| 1 | Overworld movement/terrain | Move ±30000 (elytra/firework or creative fly), dig 5, place 5 blocks, check `LevelChunkWithLight 0x28` no desync | desync 0, `UpdateLight 0x2B` flood 0, TPS 20±2 |
| 2 | Combat | Sword 10 hits, bow 10, TNT 3, summon witch/ravager/warden (via `/summon`), check `HurtAnimation 0x25`/`EntitySoundEffect 0x6E` | Hurt 0x25 visible, no kick |
| 3 | Redstone | Lever→piston door 10 toggles, observer clock 300t (5× observer+piston), comparator chest test | `BlockUpdate 0x4E`/`MultiBlockChange 0x3C` axis `lx<<8|lz<<4|ly`, clock 300t no jam |
| 4 | Nether round-trip | Frame 4×5 obsidian + `flint_and_steel`, enter portal → dim -1, move ±1000, return ×2, then End ×1 | `Respawn 0x4A` + `SynchronizePlayerPosition 0x40` OK, keepAlive 0 timeout |
| 5 | Death / Respawn ×5 | `/kill` ×5, check respawnPos `30,-61,0`, `SetHealth 0x5A` 20, `DeathMessage 0x3D` | death 5 / respawn 5 OK, no ghost |
| 6 | Weather | `/weather thunder 1`, observe thunder sound `EntitySoundEffect 0x6E`/`SoundEffect 0x6F`, lightning `WorldParticles 0x2A` | UpdateLight flood 0, thunder sound heard |
| 7 | Chunk boundary / Inventory | Chunk -30k→+30k loop, `WorldBorder diameter 59999968` inside, inventory drag mode5 30 ops (creative drag 3×3, anvil 2, brewing 1) | chunkDesync 0, ContainerSetContent 0x12 correct |
| 8 | Signed command + Tab complete (plan43 W-03/W-04) | Type `/gam` + Tab → candidates show; run `/gamemode 1` and `/give @s minecraft:stone 1`; relog after each | `tabKick 0`, `signedKick 0`, suggestions cover typed token |
| 9 | Sign edit front/back (plan43 W-07) | Place oak sign, write 4 lines front + 4 lines back, relog | 8/8 lines persist (`signPersist 8/8`), no kick |
| 10 | Book & map (W-09 adjacent, visual only) | Book-and-quill: write 2 pages + sign; empty map: right-click to draw | No freeze/no-kick on edit; map renders (record only) |
| 11 | Biome color + light (O-03 early look) | Visit plains/forest/desert/ocean; toggle SmoothLighting ON/OFF; screenshot 3 scenes (leaves, water, day/night border) | Screenshots match vanilla look (record diffs, no numeric gate yet) |
| 12 | GUI ops (W-08/G-13 adjacent, visual only) | Anvil rename 1 item; beacon set effect (creative); crafting-table shift-click stack | Record unresponsive UIs (no PASS gate yet — plan45) |

> Items 10–12 are record-only (their FIXED gates live in plan45/plan46); items 8–9
> gate plan43 O-01 alongside `tools/replay_vanilla.py` (automated order/burst side).

## Capture & Diff
```bash
python3 tools/capture.py --port 25565 --duration 7200 --out captures/manual-soak-$(date +%Y%m%d).bin
# compare with vanilla capture (if available)
diff -u captures/vanilla-1.21.4-LevelChunkWithLight.bin captures/manual-soak-*.bin
# expected: non-air diff 0, Bundle axis visual一致 (lx<<8|lz<<4|ly)
```

## Pass Criteria (all must hold)
- `kickCount 0` (incl. `tabKick 0`, `signedKick 0`, `finishKick 0`)
- `signPersist 8/8` (item 9: front 4 + back 4 lines after relogin)
- `UpdateLight flood 0` (`lightEngine->drain` dirtyChunks ≤1024 bound, burst 16)
- `Bundle axis errors 0` (`lx<<8|lz<<4|ly` matches vanilla)
- `chunkDesync 0`
- `TPS 20±2` (`GameServer_tick.cpp:176 tickOnce 50ms`)
- `tools/replay_vanilla.py` all steps PASS (automated side of O-01)
- Record `chunkCache hitRate` and `keepAlive RTT` in `docs/SOAK_REPORT.md` manual section
