# DensityFunction Coverage — Yarn 1.21.4 Correspondence (plan46 G-10)

> Code: `src/worldgen/DensityFunction.hpp` / `.cpp` (`DensityPipeline::parse`
> + `buildFromJson`). Tests: `test_gameplay_full` section `DENSITY`
> (`test_density_coverage`). Baseline HEAD `8ff124a`.

## 1. Correspondence table (Yarn ~25 types → status)

| Yarn type | status | notes |
|-----------|--------|-------|
| constant | done | exact |
| y_clamped_gradient | done | exact |
| noise | done | registry-seeded ImprovedNoise |
| shift / shift_a / shift_b | done | axis variants |
| shifted_noise | done | snake+camel child keys, string/object noise ref |
| abs / square / cube / half_negative / quarter_negative / squeeze | done | `half_`/`quarter_` verified in DENSITY asserts |
| add / mul / **min / max** | done | min/max were listed "missing" in assessment-6 but exist (`Nary`, verified) → FIXED-candidate |
| clamp | done | exact |
| range_choice | done | min/max + min_inclusive/max_exclusive aliases |
| beardifier | done | approx (±0.15 honest gap, pinned in gameplay_full) |
| old_blended_noise | done | xz/y factors + smear clamp 1..8 |
| blend_alpha / blend_offset / blend_density | done | offsets are no-op constants (documented) |
| end_islands | done | approx falloff (pinned ±0.5 honest gap) |
| weird_scaled_sampler (+interval_select alias) | done | type_1/type_2 mapper |
| cache_2d | done | per-column memo + beginPass reset |
| **flat_cache** | **done (plan46)** | was aliased to cache_2d; now its own single-position-memo node |
| **cache_once** | **done (plan46)** | was pass-through (silently wrong); now once-per-pass node |
| **interpolated** | **done (plan46)** | was pass-through; now explicit node (direct-eval approx, documented) |
| **spline** | **done (plan46)** | was absent; now cubic-Hermite (knot-exact, C1, clamped) |
| surface-rule chain (spline-driven surface builder) | unneeded | separate pipeline (surface rules ≠ density router); out of scope |
| NoiseHolder / ElementHolder indirection | unneeded | JSON layer only, no eval semantics |

Coverage after plan46: every Yarn eval-bearing type parses to a dedicated
node. Remaining numeric gaps are the two pre-existing pinned approximations
(beardifier, end_islands) — unchanged by this batch.

## 2. Impact quantification (1000-column bench, in-test)

Relief spline `{-3:-8, 0:62, 3:70}` (sea → plains → hills) sampled at
1000 columns x∈[-3,3]: **mean |Δheight| vs flat-62 baseline ≫ 1 block**
(asserted `> 1.0`, printed each run), knot error exactly 0 (< 2-block bar).
The number is the G-10 "influence" evidence: the new type moves terrain,
not just parses.

## 3. Semantics chosen (and documented as approximations)

- `interpolated`: vanilla resamples on the cell grid + trilinear; we eval
  directly (exact for smooth inputs, cheaper; JSON shape preserved so a
  future grid implementation drops in without format churn).
- `flat_cache`: vanilla caches per call-site; single-entry last-position
  cache is the honest minimal form (contract pinned: repeat=1 eval,
  move=recompute).
- `cache_once`: input evaluated at the origin once per pass (pinned:
  1 eval/pass, `beginPass` resets via pipeline `sample()`).
- `spline`: coordinate defaults to x when absent; value/derivative accept a
  float or a nested function (evaluated once at the knot); points sorted;
  clamped outside. Missing `points` → `buildFromJson` false (tested).
