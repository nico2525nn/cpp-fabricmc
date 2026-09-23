# Goal live entry/settings matrix

`tests/test_goal_live_matrix.py` is the owned live gate for the shipped
`cppfm` entry point. It starts the executable itself (never an in-process
fixture), uses `tests/mcproto.py` for protocol framing, and gives every launch
an isolated root, log, and POSIX process group.

## Coverage

| Area | Evidence collected |
| --- | --- |
| Two launches | Launch A and Launch B use distinct, dynamically selected non-default ports. |
| Status | Complete status handshake and ping; protocol `769`, version `1.21.4`, non-empty description, and player fields are checked. |
| Login | Offline login start, compression, login success UUID/name, and exact response digests are recorded. |
| Configuration | Registry/resource/feature packets are consumed with the shipped helper; packet IDs, lengths, and SHA-256 digests are recorded. |
| Play | Join Game is required; non-empty play packets are recorded and server keep-alives are answered with the real client packet. |
| Properties/CLI | `server.properties` supplies a different `server-port`, view distance, MOTD, seed, compression, and JVM mode; CLI port/view/MOTD overrides are proven by the startup log and live listener. |
| Restart/recovery | Launch B reuses Launch A's world directory after clean process-group shutdown; a pre-existing marker digest must remain unchanged and `level.dat` presence/digest is reported. |
| Cleanup | SIGTERM is sent only to the owned process group, with bounded SIGKILL escalation and no accepted PASS after an owned process remains. |

Run it with a bounded outer timeout:

```text
timeout --foreground --kill-after=5 180 python3 tests/test_goal_live_matrix.py --binary ./build/cppfm
```

The single-line JSON result includes the artifact directory, startup
observations, non-empty response digests, cleanup records, and deterministic
world-recovery evidence. Artifacts are retained under `/tmp` (or the supplied
`--artifact-root`) for review; only that directory is created by this gate.

Latest bounded verification (`build/cppfm`): `status=PASS`, two distinct
non-default listeners, Launch A `flat/view=3`, Launch B `flat/view=2`, both
login compression `256`, configuration `16` non-empty packets, Join Game
observed, and both owned processes exited `0` without escalation. The shared
marker SHA-256 remained
`b60e82eb80183ab176a11d7c4b98f4f5d8f634135d0f844d157ae561583e95ad` across
restart. This build did not create `level.dat`; the report records that as
`level_dat_present=false` rather than treating absence as persistence proof.

## Unavailable paths

This environment has no real Minecraft client or GUI. Client rendering,
keyboard/mouse input, screenshots, resource-pack presentation, and visual
parity are therefore **UNAVAILABLE**, not simulated and not counted as PASS.
The protocol flow is server-boundary evidence only; it is not a claim of full
vanilla client compatibility.

## Recorded run

The command above is the reproducible source of the current evidence. A run
must finish with `status=PASS`; a missing binary, protocol failure, non-empty
response failure, startup-setting mismatch, recovery mismatch, or cleanup
ambiguity is a failure/blocker rather than a downgraded liveness result.
