# Gameplay physics adversarial audit

Scope: `src/physics/**` only. Each repro below calls a shipped public behavior
or engine API and is covered by `tests/test_goal_gameplay_bugs.cpp` unless
marked limitation. The standalone harness result is **15 PASS / 0 FAIL**.

## Fixed root causes

| ID | Repro and observed defect | Root cause and treatment |
|---|---|---|
| GB-G01 | Schedule `(0,1,0)` and `(0,0,1048576)`; one entry disappeared. | `BlockTickScheduler::posKey3` XORed overlapping y/z bits. It now uses the vanilla 26/12/26 packed layout. |
| GB-G02 | Schedule two distinct positions; `pendingCount()` reported four. | The count added both the queue and dedup set. It now reports unique pending positions. |
| GB-G03 | Tick `RandomTickScheduler` at 100, schedule delay 5, inspect deadline. | The relative overload treated delay as an absolute tick. A scheduler clock and saturating addition now produce 105. |
| GB-G04 | Put a wheat crop under an opaque roof, give only the crop cell block light 15, and tick it. | Crop growth sampled `y+1`, rejecting a lit crop under a roof. It now samples the crop position. |
| GB-G05 | Block a chorus flower at age 0 and tick until its first state change. | A failed growth attempt jumped directly to age 5. It now increments one age per failed attempt. |
| GB-G06 | Tick a lit campfire. | Generic fire logic added an invalid `age` property to campfire states. Campfires now remain stable and do not use age-based fire spreading. |
| GB-G07 | Query `FlammableRegistry` for bamboo, a wood fence, carpet, and scaffolding. | Fallback coverage omitted these vanilla flammable families. Explicit fallback entries now preserve ignition odds. |
| GB-G08 | Feed an east-facing repeater directly from a redstone block at its input. | `isPoweredHere` only inspected neighbors, so direct sources were invisible. The query now includes power at the requested cell. |
| GB-G09 | Call `FluidSim::touch(INT_MAX,-60,0)` and `touch(0,kMaxY,0)`. | Out-of-world notifications entered scheduling/chunk paths. Fluid touch now rejects non-26-bit x/z and build-height coordinates. |
| GB-G10 | Call `LightEngine::onBlockChanged(INT_MAX,...)`. | Boundary arithmetic could queue work from an invalid coordinate. Light entry now rejects the same coordinate bounds. |
| GB-G11 | Call redstone change/query at `INT_MAX`. | Redstone accepted invalid positions before neighbor arithmetic. Both change and power-query entry paths now fail closed. |
| GB-G12 | Place glowstone, then a water source one block away and drain light. | Water attenuation was inferred from generated numeric state ranges. Classification now uses the block name and yields level 14. |
| GB-G13 | Run the blocked-chorus growth repro under ASan; `stateWithProps` read a dead `std::to_string` buffer. | The failed-growth branch passed a temporary string through `std::string_view`. It now retains the age string for the complete state lookup. The sanitized repro is clean. |

## Explicit limitations (safe treatment)

| ID | Exact shipped repro | Safe treatment |
|---|---|---|
| LIM-G01 | `SoulFireBehavior::tick` delegates to `FireBehavior::tick` after checking its base block. | Soul fire may use generic fire spread/age behavior; callers should treat soul-fire spread as unsupported rather than claim vanilla parity. Keep the base-block validation and do not apply this path to damage calculations. |
| LIM-G02 | Place a detector rail and call `RedstoneEngine::onBlockChanged`; no minecart/entity input API exists in `src/physics`. | Detector rails remain inert without an entity signal. Do not synthesize power from neighboring redstone; integration must provide a minecart event before claiming support. |
| LIM-G03 | Search the shipped physics API for fall, collision, fire-damage, freeze-damage, hunger, armor, or damage calculators: none are present under `src/physics`. | These calculations live outside this owned module (or are absent). Treat them as unverified and fail closed; this audit does not claim damage/armor/collision compatibility. |
| LIM-G04 | `LightEngine::ensureSkyLight` rebuilds one chunk and does not cross into an absent neighbor. | Cross-chunk sky propagation waits for a neighbor ticket/rebuild. Keep the chunk unloaded rather than recursively generating it during lighting. |

## Verification

- `timeout --foreground --kill-after=5 60 ./build/test_fluids` → 23 PASS / 0 FAIL.
- `timeout --foreground --kill-after=5 60 ./build/test_redstone_engine_full` → 42 PASS / 0 FAIL.
- Standalone owned harness `build/test_goal_gameplay_bugs` → 15 PASS / 0 FAIL.
- ASan/UBSan focused rerun of `goal_gameplay_bugs` → 2/2 sanitized network/gameplay gates PASS after GB-G13.
- This report's gameplay defect fixes are scoped to `src/physics`; network,
  session/security, CMake, and cleanup changes are tracked in their separate
  goal reports.
