# Network goal-bug audit

Scope: `src/net/**`, `src/proto/**`, and `tests/test_goal_network_bugs.cpp` for Java 1.21.4 / protocol 769.

## Reproduced and fixed

| ID | Repro through shipped code | Impact | Treatment |
|---|---|---|---|
| NET-01 | `PacketDecoder::decodeFrame({0x00}, 256)` | Empty compressed frame returned an empty packet body; dispatch then attempted `in.u8()` on malformed input. | Reject `dataLength=0` with no body; regression in the owned harness. |
| NET-02 | Send only the outer length byte over a socket, then call `Connection::readFrameWithTimeout(40ms)`. | Old helper timed out only while waiting for the first byte; a peer could drip a partial frame forever. | Carry one deadline through VarInt and body reads; regression uses `socketpair`. |
| NET-03 | Queue positions `(0,1,0)` and `(0,0,2097152)`; the old XOR-packed `int64` dedup key collided. | Distinct block updates could overwrite one another before section grouping. | Ordered `PositionKey{x,y,z}` map; direct key regression. |
| NET-04 | Flush updates for multiple sections repeatedly. | `unordered_map` iteration made Bundle/MultiBlockChange section and record ordering nondeterministic. | Ordered `SectionKey`/`PositionKey` maps; ordering regression. |
| NET-05 | `RateLimiter::consume(NaN, now)` or `consume(-1, now)`. | NaN poisoned token state and negative input increased the budget. | Reject non-finite/negative byte counts; regression. |
| NET-06 | `AcceptGate::allow(1000)`, exhaust the window, then `allow(0)`. | Wall-clock rollback left the gate stuck until the old epoch elapsed. | Reset on rollback as well as forward expiry; regression. |
| NET-07 | `mojang_detail::base64Decode("AQ=garbage")`. | Invalid profile-key Base64 was silently ignored/partially decoded. | Strict alphabet/padding validation; regression. |
| NET-08 | `PacketDecoder::readVarintEncrypted(encryptedBytes, n, decryptor, consumed)`. | Shipped helper ignored its decryptor and parsed ciphertext as plaintext. | Decrypt one byte at a time, stop at the terminator, and report consumed bytes; regression. |
| NET-09 | `PacketEncoder::encode(body, -2)` or decoder threshold `-2`. | Invalid compression configuration silently selected the uncompressed path. | Accept only `-1` or non-negative thresholds; regression. |
| NET-10 | `mojang_detail::base64Decode("AB==")`. | Non-zero unused Base64 bits were accepted as a different key encoding. | Enforce canonical final sextet bits; regression. |
| NET-11 | Decode a long profile key such as `std::string(2048, 'z')`. | Signed left shifts overflowed the `int` accumulator, invoking undefined behavior on realistic key material. | Use a masked unsigned streaming accumulator; long-input vector regression. |
| NET-12 | Encode an uncompressed frame larger than the decoder's VarInt21 maximum. | The encoder could emit an outer length that the shipped decoder rejects. | Share the positive VarInt21 frame ceiling across encoder, connection, and decoder; largest-valid and first-invalid boundary regressions. |
| NET-13 | Abort while the output worker is retrying a partial send, or while the session thread is waiting/receiving. | Closing the descriptor before the operation ends can let a later retry use an unrelated socket after descriptor-number reuse. | Shutdown first, join the writer / drain active socket-operation leases, then release the descriptor; blocked-reader and blocked-writer lifecycle regressions. |
| NET-14 | Queue low-priority chunk output, then enqueue a Respawn state transition. | Priority selection could send Respawn before chunks encoded for the old dimension. | Sequence barrier drains earlier low-priority frames before Respawn while keeping later frames deferred; socketpair ordering regression. |
| NET-15 | Run concurrent logins while the profile-key endpoint refresh fails near cache expiry. | Every login could issue a duplicate HTTP request, and stale keys could be returned past the configured trust deadline. | Single-flight refresh, bounded stale fallback, and deadline enforcement; concurrent-loader, endpoint-failure, and fake-clock expiry regressions. |
| NET-16 | Leave a connection idle before a frame, then drip bytes through a started frame more slowly than the per-read timeout. | Starting the timeout before the frame incorrectly charges idle time; resetting it for every byte lets a slow peer hold a session forever. | `readFrame()` starts one 30-second absolute deadline on the first length byte; socketpair tests cover idle-before-frame and slow-drip body behavior. |
| NET-17 | Hold Status requests open while filling pending-session worker slots; repeatedly connect and disconnect sessions. | Status/ping waits could starve Login and completed `std::thread` handles accumulated in the registry. | Status has a separate 16-session gate and releases its pending-login slot after handshake; completed handles are reaped before new workers are registered. Live fairness/cap regression is in `test_flood_net`. |
| NET-18 | Race multiple successful logins against `max-players=1`, or launch with `max-players=0`. | Checking capacity before later Play registration permits oversubscription; treating zero as “uncapped” differs from vanilla. | Reserve a player slot under one admission mutex at Login and consume/release it on registration/teardown. `test_flood_net` verifies excess concurrent login rejection and zero-capacity rejection for ordinary profiles. The vanilla zero-capacity branch was checked in the SHA-1-pinned 1.21.4 server artifact below. Operator `bypassesPlayerLimit` is a separate declared partial-permission boundary. |

## Protocol/compatibility limitations (not counted as code defects)

| ID | Exact shipped behavior | Impact/treatment |
|---|---|---|
| NET-L01 | `ChatMessageProcessor::verify(..., acknowledgedMask != 0, ...)` returns false because outbound signed-chat transcript entries are not emitted. | Signed chat with LastSeen acknowledgements fails closed; retain as a declared secure-chat limitation. |
| NET-L02 | `PlayAddResourcePack`/`PlayRemoveResourcePack` are mapped IDs, while the implemented offer is configuration `AddResourcePack (0x09)` and acknowledgement is play `ResourcePackReceive (0x2F)`. | Play-phase add/remove operations are not implemented; do not count constants as support. |
| NET-L03 | `Connection::sendRaw`/`writeFrameRaw` are explicit raw escape hatches and do not apply framing or AES state. | Callers must use `sendPacket*`/`sendFramed` after encryption; raw use is limited to legacy/handshake contexts. |
| NET-L04 | `PacketBatcher::queuePacketFor` normalizes dimensions other than `-1`, `0`, and `1` to overworld. | Unknown dimension identifiers cannot be represented by the current three-dimension model; this is fail-safe normalization, not general protocol support. |
| NET-L05 | KeepAlive IDs are direction/state-specific: play server `0x27`, play client `0x1A`, configuration `0x04`. | Wrong-state packets are not interchangeable; the owned test locks all mappings. |
| NET-L06 | Cookie and resource-pack result packets are stateful and UUID/key scoped; this network layer only frames/decodes them. | Persistence and policy remain in the game/session layer and are outside this network-owned patch. |

## Verification

### PR #1 adversarial-review follow-up (2026-09-23)

The exact Mojang 1.21.4 server artifact (`server.jar`, SHA-1
`4707d00eb834b446575d89a61a11b5d548d8c001`) was inspected with the official
server mappings. `PlayerList.canPlayerLogin` compares current player count
against `maxPlayers` and then checks the bypass flag; with zero players and a
limit of zero, an ordinary profile is rejected as server-full. See the
[official 1.21.4 artifact](https://piston-data.mojang.com/v1/objects/4707d00eb834b446575d89a61a11b5d548d8c001/server.jar)
and [Yarn 1.21.4 `PlayerManager` mapping](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.1/net/minecraft/server/PlayerManager.html).

The focused local CTest group passes **8/8**: `native`, `flood_net`, `fuzz`,
`goal_network_bugs`, `goal_cleanup_runtime`, `goal_cleanup_game`,
`goal_security_guards`, and `wire_b6`.
The live network fixture covers status-worker fairness, concurrent admission,
zero capacity, and oversize rejection. Exact-head GitHub Actions still must run
after the review changes are committed and pushed.

Standalone owned harness (also registered as the `goal_network_bugs` CTest gate):

`timeout --foreground --kill-after=5 120 c++ -std=c++20 -Isrc -Isrc/generated tests/test_goal_network_bugs.cpp -o /tmp/test_goal_network_bugs -lz -lcrypto -pthread && timeout --foreground --kill-after=5 30 /tmp/test_goal_network_bugs`

Initial-audit result: **36 passed, 0 failed**.

Additional checks:

- `PacketBatcher.cpp` syntax-checks with `c++ -std=c++20 -Isrc -Isrc/generated -fsyntax-only`.
- Existing `test_fuzz` target rebuilt successfully after framing changes.
- The final production rebuild and focused CTest rerun complete with no source error.

## PR #1 adversarial-review follow-up (2026-09-23)

An earlier PR-review rerun expanded the same owned harness to **92 passed, 0
failed**. The current post-fix run is **99 passed, 0 failed**; the associated
CTest group passes **8/8**, `test_flood_net` passes **108/108**,
`test_wire_b6` passes **137/137**, and `test_fuzz` passes **25/25**. These are
local working-tree results; they do not replace the required exact-head GitHub
Actions run after the review commit is pushed.
