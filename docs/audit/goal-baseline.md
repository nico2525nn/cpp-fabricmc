# Goal baseline ledger

## Freeze

This is a read-only baseline of the intentionally dirty working tree. It does not
claim a clean checkout or a release gate. No build/test command was run for this
ledger; recorded results remain historical evidence in `Handoff.md`,
`docs/CURRENT_STATE.md`, and `docs/VERIFICATION.md`.

| field | value |
|---|---|
| captured | 2026-09-20 (local session; content hashes are authoritative) |
| repository | `/run/media/nico/d/学校/app/cpp-fabricmc` |
| branch | `main` |
| HEAD | `b786093f84791e6381665f9e6aa86d4823f7a131` |
| status entries | `71`; status-text SHA-256 `89811f0a2f9749f34b482b7f46b1fc8c88691361db6441a6a28912892fb71e26` |
| unstaged diff | `ca3a5fd35b677f0c06dd318c1e459b01589c7dfee28db72a59f795bef17d73b8` (239176 bytes) |
| staged diff | `9e8e63ac5586beae7421a256e69be880a0befddebb182648f20475aa425ceb54` (31427 bytes) |
| diff-stat text | SHA-256 `28726c2ed4904bddd34f3401b29eac40b63365ce9a76544ffd8f64c19bcce80e`; 62 lines |
| non-ignored tree entries | `3394`; path/content fingerprint `b65de6b6d75600bdefafda88b8c9d58563456e7f73ae42b1997272e92eef1a76` |
| standard-untracked paths | `9`; see status output at capture time |

The tree fingerprint sorts tracked plus standard-untracked files and hashes each
`path\0sha256(file)\0` record. Ignored build output is excluded. Recreate with
`git ls-files -z`, `git ls-files --others --exclude-standard -z`, and the same
record hash. Status and diff hashes preserve the dirty-tree shape without reset,
clean, stash, checkout, or overwrite operations.

## Eligible LOC

Primary scope follows Plan54 exactly: files below `src/`, `tests/`, and `tools/`
with suffix `.cpp`, `.hpp`, `.py`, or `.java`; line count is Python
`len(Path.read_text(errors="replace").splitlines())`. Current standard-untracked
source-like files are included. CMake, docs, assets, JVM vendor inputs, generated
build output, and ignored files are excluded.

| root | files | physical lines |
|---|---:|---:|
| `src/` | 156 | 67075 |
| `tests/` | 118 | 24315 |
| `tools/` | 28 | 9601 |
| **total** | **302** | **100991** |

Eligible path/hash fingerprint: `9636b03403e700b1bd716965b1e22378c4ee204bfc6336c2430d2f7caa0c0700`.

The historical Plan54/current-state value `98,648 / 298` is not substituted: this dirty-tree freeze is `100,991 / 302`. The companion protected manifest records an explicit current 80-path set (`5,994` lines); it is not silently equated with the historical `80 / 8,939` claim.

## Reproduction

```text
python3 - <<'PY'
from pathlib import Path
roots=(Path("src"),Path("tests"),Path("tools")); suffixes=('.cpp', '.hpp', '.py', '.java')
files=sorted(p for r in roots for p in r.rglob("*") if p.is_file() and p.suffix in suffixes)
print(len(files), sum(len(p.read_text(errors="replace").splitlines()) for p in files))
PY
```

Protected paths/hashes: `docs/audit/goal-protected-manifest.txt`. Coverage ledger: `docs/audit/goal-feature-ledger.md`.
