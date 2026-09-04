# GUI Checklist — Real-Client Visual Verification (plan46 O-03)

> Scope: assessment-6 **O-03** (light / biome-color recompute, Medium).
> Depends on: B1 **O-01** (replay harness `tools/replay_vanilla.py`,
> `docs/SOAK_MANUAL.md` items 1–9) + B2 sign wire + B6 book wire.
> Code change in this batch: `UpdateViewDistance 0x59` is now **sent**
> (was "not sent", `Ids.hpp`) — on login (`sendJoinGame`) and on every
> client Settings 0x0C change (`applyClientSettings`, server-clamped value).
> UpdateLight 0x2B was already drained per-tick
> (`GameServer_tick.cpp` light drain + `LightEngine::drain` batch).

## 0. Controlled conditions (all screenshots)

- Same seed, same coordinates, same dayTime, same client settings.
- Client: vanilla 1.21.4, no resource packs, render-distance 8 then 12,
  SmoothLighting ON then OFF (4 combinations for scene A).
- Vanilla reference: official server jar, identical conditions, side-by-side.

## 1. UpdateLight re-send trigger table (documented behavior)

| cause | scope | server path | client observable |
|-------|-------|-------------|-------------------|
| block change in sim distance | 3×3 sections around the chunk if present (`LightEngine.cpp` expansion) | `onBlockChanged` → `drain()` → `UpdateLight 0x2B` | light updates within 1 tick, no flood (burst cap 16/tick) |
| day/night transition | sky-light columns of loaded chunks | time sync 20t + light drain | smooth dimming ≤ 5s after transition (vanilla also lags briefly) |
| chunk (re)send | full 18×18 section mask in `LevelChunkWithLight 0x28` | `sendChunk` path | F3 server-light == client-light after settle |
| outside sim distance | sky storage allocated only, BFS skipped (sim cull) | `onBlockChanged` early-out | chunk edge may stay stale until re-entry (by design, same as vanilla sim edge) |

## 2. Manual checklist

### B1 — carried over from O-01/SOAK_MANUAL (must stay green)

- [ ] Login → spawn render → move → break/place → inventory → disconnect, 0 kicks.

### B2 — signs (wire from B2, visual here)

- [ ] Oak sign: 4 front lines + 4 back lines render after relog (8/8 persist).
- [ ] Glowing-text sign renders glow at night.
- [ ] Waxed sign ignores edit attempts (no freeze).

### B6 — book (wire from B6, visual here)

- [ ] Book-and-quill: 2 pages written + signed, readable after relog.
- [ ] Empty map → right-click → drawn map renders (`MapItemData` path; if the
  map-data packet is unimplemented, mark **out-of-scope with reason**, never
  water-filled PASS).

### O-03 core — 3 screenshot scenes (vanilla side-by-side)

- [ ] **Scene A — light border**: day/night boundary over plains at
  SmoothLighting ON/OFF × render-distance 8/12. PASS = no banding/desync
  vs vanilla within 5s of transition.
- [ ] **Scene B — foliage**: forest + swamp leaves. PASS = color match.
  Mismatch triage first: check `LevelChunkWithLight` biome palette (G-11)
  before blaming the client (biome-blend setting / GPU gamma are
  client-side confounders — see §3).
- [ ] **Scene C — water**: river + ocean surface + underwater. PASS = color
  match under identical conditions.

### View distance (new in this batch)

- [ ] `/settings` change render-distance mid-session → `UpdateViewDistance`
  0x59 arrives (capture with `tools/capture.py`) and client draw distance
  follows without relog.
- [ ] Automated side: `tools/replay_vanilla.py` + GUI sequence below.

## 3. Biome-color triage (server bug vs client artifact)

1. Capture the chunk bytes; decode the biome palette of `0x28`.
2. If the palette id is wrong → server bug (G-11 correspondence table).
3. If the palette is right but the color is off → check client biome-blend
   setting, then GPU gamma, then SmoothLighting — only then file a server gap.

## 4. Replay integration (automated part → CI, screenshots stay manual)

Append to the O-01 replay (`tools/replay_vanilla.py`) the GUI sequence:

```
sign_edit(front 4 + back 4) → book_sign(2 pages) → map_create →
  /time set night → /time set day → settings(view-distance 8→12) →
  assert UpdateViewDistance 0x59 seen + session alive
```

Packet asserts (order + ids + byte-shape) run in CI; the 3 screenshots are
recorded here with date + vanilla对照 hash. Human-gated items stay PARTIAL
until the screenshots land — no口头 FIXED.
