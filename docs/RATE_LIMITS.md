# RATE_LIMITS.md — plan46 §1 (O-13/W-14/W-16) flood/defense policy

> Budgets, throttles and disconnect policy for malicious/degenerate input.
> Implementation: `src/net/RateLimiter.hpp`, `src/net/PacketDecoder.hpp`
> (`kMaxDeclared`, `OversizeError`), `src/net/Connection.hpp`
> (bandwidth budget, `SO_RCVTIMEO`), `src/game/GameServer_session.cpp`
> (per-packet try, `SpamTracker`, `kickPlay`), `src/game/GameServer_core.cpp`
> (accept gate). Tests: `tests/test_flood_net.cpp` (A1–A8).

## 1. Budgets (W-14)

| Layer | Value | On breach |
|---|---|---|
| Outer frame length | (0, 8MB] (`kMaxFrame`, kept) | kick + `Packet too large` Disconnect |
| Declared decompressed size (`dataLength`) | ≤ 2MB (`kMaxDeclared`, new) | kick + `Packet too large` Disconnect (`OversizeError`) |
| Per-connection bandwidth | 2MB burst / 1MB/s sustain (token bucket) | kick + `Packet too large` Disconnect |
| Global accept rate | 20 conn/s (`AcceptGate`) | refuse (close fd, no thread) |

Ordering guarantee: the budget decision precedes the allocation
(`Connection::readFrame` charges before `frame_.resize`; `decodeFrame`
checks `dataLen` before inflate), so a lying declaration is cheap to refuse.

Strictness (compression on, threshold T>0): `dataLength` MUST be 0
(uncompressed, body < T) or ≥ T. Negative, below-threshold non-zero, and
`dataLength=0` under `threshold=0` are rejected. Trailing bytes after the
deflate stream end are rejected (`decompressChecked`).

## 2. Chat throttle (O-13 A3) — vanilla 200式

Per player: `+20` per chat/command (chat, unsigned + signed commands share
one counter), `−1` per server tick (lazy decay at receipt, tick-driven so
tests are deterministic), kick when `> 200` with
`{"translate":"disconnect.spam"}` + abortive close. 11 messages inside one
tick window ⇒ kick (`10×20=200` not yet, `11×20=220>200`). Operators are
NOT exempt (vanilla parity). At 20 TPS the sustain rate is 1 msg/s.

## 3. Slow-loris guard (O-13 A6)

Every accepted socket gets `SO_RCVTIMEO=30s`; expiry surfaces as
`SocketClosedError(timedOut=true)` → kick-with-Disconnect path, freeing the
session thread. Safe: the server keepalives every 10s and conforming
clients answer. Configuration finish-ack additionally uses a 30s poll
deadline (pre-existing, plan43 W-12).

## 4. Disconnect policy (W-16) — unified

| Situation | Policy |
|---|---|
| Oversize (any stage) | kick + state-correct Disconnect (`Packet too large`) |
| Malformed packet in play | log (1/s rate-limited) + ignore + continue (session survives) |
| Unknown packet in play | log (1/s) + `skipRest` + continue |
| Unknown/malformed in login/config | kick + state-correct Disconnect |
| Chat/command spam | kick + `disconnect.spam` Disconnect |
| Read timeout / dead socket | quiet close (no Disconnect — peer is gone) |
| `Session::run` unexpected exception | log + best-effort Disconnect (`Internal server error`) |

Disconnect type follows the session state: play `0x1D`, configuration
`0x04`(cf Disconnect), login `0x00`(lo Disconnect), via `disconnectIn`.
Kicks use `kickPlay`: Disconnect → 50ms grace → `abort()` (RST, dead
socket observable by the peer's next read).

## 5. Attack matrix (plan46 §1 A1–A8) → coverage

| # | Attack | Defense | Test |
|---|---|---|---|
| A1 | 8MB frame burst | bandwidth bucket (2MB burst ⇒ first frame already over) | `U-A1`, `L-A1` |
| A2 | zlib bomb (small wire, 8MB declared) | `kMaxDeclared` 2MB pre-alloc check | `U-A2`, `L-A2`, `L-A2b` |
| A3 | chat 20msg/s | `SpamTracker` 200式 | `U-A3`, `L-A3` (+normal unaffected) |
| A4 | malformed ×1000 | per-packet try + ignore/continue | `L-A4` (200 unknown + 5 truncated) |
| A5 | connection burst | `AcceptGate` 20/s refuse | `U-A5`, `L-A5` |
| A6 | slow-loris (1B at a time) | `SO_RCVTIMEO` 30s | `U-A6`, `L-A6` |
| A7 | `dataLength` forgery | below-threshold / negative rejection | `U-A7` |
| A8 | `threshold=0` all-compressed | roundtrip test + `dataLength=0` rejection | `U-W14` boundary section |

Deliberate deviation from plan46 §1: budget breach kicks immediately
rather than “1s recv-stop then re-try then kick” — client→server traffic
is legitimately bytes/s, so any 2MB-burst breach is already hostile, and
immediate kick keeps the code path (and its tests) deterministic.
