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

Standalone owned harness (also registered as the `goal_network_bugs` CTest gate):

`timeout --foreground --kill-after=5 120 c++ -std=c++20 -Isrc -Isrc/generated tests/test_goal_network_bugs.cpp -o /tmp/test_goal_network_bugs -lz -lcrypto -pthread && timeout --foreground --kill-after=5 30 /tmp/test_goal_network_bugs`

Result: **36 passed, 0 failed**.

Additional checks:

- `PacketBatcher.cpp` syntax-checks with `c++ -std=c++20 -Isrc -Isrc/generated -fsyntax-only`.
- Existing `test_fuzz` target rebuilt successfully after framing changes.
- The final production rebuild and focused CTest rerun complete with no source error.
