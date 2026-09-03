# BACKUP.md — Backup / Restore / Integrity (plan46 §2, O-08)

> Target: `docs/assessment-6.md` **O-08** (backup + integrity checker) and **O-07**
> (crash recovery). Server: Fabric 1.21.4-compat, protocol 769, DataVersion 4189.

## 1. Layout

```
<worldDir>/
  level.dat / level.dat_old   # atomic write: tmp -> _old backup -> rename
  level.dat.corrupt*          # quarantined corrupt inputs (never deleted by server)
  session.lock                # "<pid> <startMs>" — startup guard (O-08)
  region/r.X.Z.mca            # chunk storage (per-chunk zlib)
  playerdata/<uuid>.dat       # per-player NBT (+ *.dat.corrupt quarantine)
ops.json  banned-players.json  banned-ips.json  whitelist.json   # cwd (server working dir)
```

## 2. Online backup (server running)

`save-off`-like autosave pause does not exist as a separate gate; the safe
sequence is:

1. `save-all` (flush chunks + `level.dat`) via console / RCON / in-game op.
2. Copy the whole `<worldDir>` **plus** `ops.json`, `banned-players.json`,
   `banned-ips.json`, `whitelist.json` from the server working dir.
   - The periodic `level.dat` save runs every 1200 ticks (~60 s), so a copy
     taken right after `save-all` is self-consistent.
3. Verify the copy offline: `check_world <copyDir>` → exit `0` expected
   (see §4).

Do **not** copy a world while `save-all` is still flushing (watch the
`[cppfm] periodic level.dat save` / `saved r.X.Z mca` stderr lines), and never
back up *into* the live `<worldDir>`.

## 3. Restore

1. Stop the server (`SIGTERM` → clean stop; `session.lock` is removed).
2. Move the live dir aside (`world_YYYYMMDD/`), put the backup in place.
3. `check_world <worldDir>` → `0` (OK) or `1` (repairable: bad chunks will be
   regenerated, bad playerdata quarantined — see O-07 below).
4. Start the server; confirm the startup log shows
   `[cppfm][recovery] level source=level.dat ok=1`
   (or `level.dat_old` with a quarantine note after a crash).

Dry-run evidence (O-08 completion): copy → restore → boot was exercised by
`suite_restart_persist` in `tests/test_server_full.py` (restart with same
worldDir, ban/ops JSON persistence) and by `check_world` exit-code asserts.

## 4. `tools/check_world` (offline checker)

```
check_world <worldDir>   # exit 0=OK 1=REPAIRABLE 2=FATAL
```

| Check | OK | Repairable (1) | Fatal (2) |
|---|---|---|---|
| `level.dat` NBT + `DataVersion==4189` | parse + version match | version drift (auto-upgraded on load) | both `level.dat` and `level.dat_old` unreadable |
| `region/*.mca` per-chunk header/zlib/NBT | all chunks decode | bad chunk listed; regenerated on next load (O-07b) | — |
| `playerdata/*.dat` | all parse | bad file listed; quarantined to `.dat.corrupt` on load (O-07b) | — |
| `session.lock` | informational only (live pid vs stale) | — | — |

## 5. Crash recovery (O-07 — 3 stages, automatic at startup)

`WorldDataManager::loadWithRecovery` (`src/game/WorldDataManager.*`):

1. `level.dat` parses → boot normally (`RecoveryResult.src = Dat`).
2. else `level.dat_old` parses → boot + warning; corrupt `level.dat` is
   renamed to `level.dat.corrupt` (forensics, never silently overwritten).
3. else both unreadable → both preserved as `*.corrupt`, fresh world generated
   + warning (`src = Fresh`, `ok = false`).

Chunk granularity: `Persistence::loadChunk` catches per-chunk failures, so a
truncated/corrupt `.mca` entry regenerates **that chunk only** (neighbours
untouched). Player granularity: `GameServer::loadPlayerData` routes through
`loadPlayerDataIsolated` (`src/game/PlayerDataRecovery.hpp`) — a corrupt
`.dat` becomes `.dat.corrupt` and that player spawns fresh.

Every stage is logged (`[cppfm][recovery] …`, `Persistence::loadLevelData`
prints `WorldDataManager::lastRecovery()`), satisfying O-07(c).

Corrupt admin files (`ops.json`, `banned-players.json`, `banned-ips.json`)
likewise boot with an **empty set + stderr warning** instead of crashing
(`GameServer::loadOps/loadBans/loadBannedIps`).

## 6. `session.lock` (O-08 exclusion)

- Written at startup (`GameServer::init`), removed on clean stop
  (`GameServer::stop`). Content: `<pid> <startMs>`.
- Another **live** pid holding the lock → loud stderr warning, startup
  continues (availability-first; Docker PID-reuse false positives must never
  refuse boot).
- **Stale** lock (dead pid, e.g. crash / `kill -9`) → one-line notice,
  lock overwritten.
- Only the owning pid removes the file on shutdown (re-reads pid first).

## 7. RCON / moderation notes (O-09 / O-10)

- Source RCON (`src/net/Rcon.*`, loopback bind, `listen(...,8)`, one thread per
  connection on a dedicated worker thread — game tick is never blocked).
  Wrong password → `id=-1` reject, connection stays open (vanilla semantics,
  no IP ban). Evidence: `test_rcon_multi` (5 concurrent + 10× wrong-pass) and
  `suite_rcon` in `tests/test_server_full.py`.
- `ban` = insert + `saveBans` + immediate `kickPlayer` (play `Disconnect`
  `Banned by an operator.`); `whitelist on` takes effect at next login
  (ops bypass, vanilla 2-stage semantics); `op`/`deop` apply live and persist
  to `ops.json`. Evidence: `suite_permissions` + `suite_restart_persist`
  (5 s disconnect, whitelist reject, restart persistence, live deop/op).
