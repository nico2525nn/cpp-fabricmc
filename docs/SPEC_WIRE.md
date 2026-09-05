# SPEC_WIRE — Minecraft 1.21.4 / protocol 769

This is the byte-level source of truth for the current C++ implementation. Snapshot:
runtime snapshot `ddb15090190d1ff879cc140912579e56e94d44cb`, rechecked 2026-09-05. Scope
is Java Edition 1.21.4, protocol `769`, DataVersion `4189`, with an unmodified Fabric
1.21.4 server as the behavioral reference. `docs/MISSING_FEATURES_1_21_4.md` targets
are identified in every contract; this file does not change their status or the
publication status, which remains `BLOCKED`.

**Status:** current wire contract, with the implementation/omitted/deferred
classification below. **Limitations:** a protocol-compatible implementation is not
the official Fabric JVM runtime. Plan51's optional embedded layer is a bounded
server-side shadow-ABI bridge and does not alter packet IDs or provide arbitrary mod,
client GUI, or vanilla RNG parity; unverified vanilla RNG parity remains outside the
boundary. See [PLAN51_JVM.md](PLAN51_JVM.md).

The archived assessment-1 strict audit is a historical record labelled 78 gaps; it
is not a current packet count or a fresh parity result. The current numbered matrix
is separate: MISSING **#1–#90** reports the historical taxonomy count `DONE=90`.
Neither label overrides the current evidence table or the `BLOCKED` publication
status.

## 1. Feature overview

Targets are MISSING **#71–#79**, with related packet behavior in **#30, #54, #56,
#72–#76, #79–#80** and strict-audit history IDs S/D/H/W/E. The contract covers:

- Handshake, Status, Login, Configuration, Play, and disconnect state boundaries.
- framing, primitive encodings, compression, AES-CFB8 encryption, NBT/chat, slots,
  palettes, heightmaps, and light arrays;
- packet IDs, direction, field order, and the actual current sent/omitted/deferred
  alternatives; and
- byte-lock vectors and the tests which protect them.

Packet field bytes belong here. Gameplay causes belong in
[SPEC_GAMEPLAY.md](SPEC_GAMEPLAY.md); limits and incident response belong in
[SPEC_OPS.md](SPEC_OPS.md).

## 2. Vanilla/reference specification

### Version and provenance registry

| source | use | label |
|---|---|---|
| `https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.21.4/protocol.json` | IDs, fields, mapper/type definitions | `WIRE-ORACLE` |
| `https://minecraft.wiki/w/Java_Edition_protocol` | VarInt, Position, palette and light explanations | `VANILLA-CONCEPT` |
| `https://fabricmc.net/2024/12/02/1214.html` | Fabric 1.21.4 release boundary | `VANILLA-CONCEPT` |
| `https://maven.fabricmc.net/docs/fabric-loader-0.16.9/index.html` | loader/JVM boundary | `VANILLA-CONCEPT` |
| `https://maven.fabricmc.net/docs/yarn-1.21.4+build.1/` | names and concepts only | `VANILLA-CONCEPT` |
| reference-server captures and repository golden vectors | observed bytes | `CAPTURED` |

The raw protocol JSON is the first oracle for packet ID/type/field shape; captures
and tests are the authority for the bytes this repository actually emits. A Yarn
`1.21.4+build.9` URL returned 404 during research and is not used as provenance.

Configuration sends twelve ordered registry payloads and waits for the client's
`Select Known Packs` response. Join Game's dimension type is a registry index, not
an arbitrary dimension string. These are protocol facts, not optional conveniences.

## 3. Classes and data structures

| contract | implementation path/symbol | input/state | output/evidence | status/provenance |
|---|---|---|---|---|
| primitive writer/reader | `src/core/ByteBuffer.hpp::WriteBuffer`, `ReadBuffer` | signed/unsigned primitives, VarInt/VarLong, UUID, Position | big-endian fixed values and lock vectors | `IMPLEMENTATION` + `WIRE-ORACLE` |
| outer frame | `src/net/PacketEncoder.hpp::encodeRaw`, `src/net/Connection.hpp::Connection::sendFramed` | id + payload, threshold, cipher | length-prefixed frame | `IMPLEMENTATION` |
| frame decoder | `src/net/PacketDecoder.hpp::decodeFrame`, `decodeOuter` | outer frame, threshold, optional cipher | id + payload or explicit rejection | `IMPLEMENTATION` + `test_fuzz` |
| session dispatch | `src/game/GameServer_session.cpp::Session::run` | state and packet direction | state-specific handler | `IMPLEMENTATION` |
| registry data | `src/game/EmbeddedData.hpp::EmbeddedData::kRegistrySpec` | captured registry blobs | twelve registries in wire order, tags | `CAPTURED` + `IMPLEMENTATION` |
| chunk body | `src/game/ChunkCodec.hpp::serializeLevelChunkBody` | 24 sections, heightmaps, light | LevelChunkWithLight body | `CAPTURED` + `test_spec_wire` |
| batched updates | `src/net/PacketBatcher.cpp::PacketBatcher::flush` | queued block changes | Bundle or MultiBlockChange | `IMPLEMENTATION` + `test_wire_full` |
| slot/components | `src/game/Items.hpp::ItemStack::write` | item id, count, component list | Slot presence and component payloads | `WIRE-ORACLE` + `test_spec_wire` |

### Primitive invariants

- Fixed `i16/i32/i64` and floating-point bit patterns are big-endian.
- VarInt is the protocol's signed two's-complement encoding, with a maximum of five
  bytes; VarLong is at most ten bytes. This is not ZigZag encoding.
- Position is packed as `x:26 | z:26 | y:12`; the reader sign-extends all three
  signed fields. Negative coordinates and negative Y are therefore test cases.
- UUID byte fields are 16 raw bytes; a textual UUID is a different field contract.
- Anonymous NBT and strings are not interchangeable. A string has a VarInt byte
  length; anonymous NBT has its tag/root structure.

<a id="bundle-and-block-updates"></a>
<a id="packet-contract-table"></a>
## 4. Packet contract table

The table deliberately includes state and direction; an ID-only table is unsafe.
`C→S` means client to server and `S→C` means server to client.

| state | direction | packet / ID | field order and encoding | implementation/evidence | status/provenance |
|---|---|---|---|---|---|
| Handshaking | C→S | Intention `0x00` | protocol VarInt, host String, port u16, next-state VarInt | `Session::handleHandshake`; native/status capture | implemented, `WIRE-ORACLE` |
| Status | C→S / S→C | Request `0x00` / Response `0x00` | empty request; JSON String response | `Session::handleStatus`; native | implemented, `CAPTURED` |
| Status | C→S / S→C | Ping `0x01` / Pong `0x01` | i64 payload echoed | `Session::handleStatus`; wire tests | implemented, `WIRE-ORACLE` |
| Login | C→S | Hello `0x00`, Key `0x01`, Login Acknowledged `0x03` | name/profile data; RSA-encrypted secret/token; empty acknowledgement | `Session::handleLogin`; native | implemented, `CAPTURED` |
| Login | S→C | Encryption Request `0x01` | server id String, public key bytes, verify token bytes, authenticate bool | `Session::sendEncryptionRequest`, `crypto::RsaKeyPair`; native | implemented, `WIRE-ORACLE` |
| Login | S→C | Game Profile/Login Success `0x02` | UUID, name, properties | `Session::sendLoginSuccess`; native | implemented, `CAPTURED` |
| Login | S→C | Set Compression `0x03` | threshold VarInt | `Session::handleLogin`; `PacketEncoder` | implemented, `WIRE-ORACLE` |
| Configuration | C→S | Select Known Packs `0x07` | pack list response | `Session::handleConfiguration`; native | implemented, `CAPTURED` |
| Configuration | S→C | Feature Flags `0x0C` | count + resource locations | `GameServer_session.cpp`; wire tests | implemented, `WIRE-ORACLE` |
| Configuration | S→C | Registry Data `0x07` × 12 | registry key, entry count, keyed optional NBT entries | `EmbeddedData::load`, `kRegistrySpec`; wire tests | implemented, `CAPTURED` |
| Configuration | S→C | Update Tags `0x0D` | registry/tag maps | `EmbeddedData::tags`; wire tests | implemented, `CAPTURED` |
| Configuration | S→C | Select Known Packs `0x0E` / Finish Configuration `0x03` | advertised packs, then empty finish | `Session::handleConfiguration`; native | implemented, `CAPTURED` |
| Play | S→C | Join Game/Login `0x2C` | entity id, hardcore, dimensions, registry-derived dimension, seed and distances | `Session::sendJoinGame`; `test_native` | implemented, `CAPTURED` |
| Play | S→C | BundleDelimiter `0x00` | empty body; start/end delimiter | `PacketBatcher::flush`; `test_spec_wire` | implemented, `WIRE-ORACLE` |
| Play | S→C | BlockUpdate `0x09` | packed Position, block-state VarInt | `PacketBatcher`; wire full | implemented, `WIRE-ORACLE` |
| Play | S→C | MultiBlockChange `0x4E` | section-position u64, count VarInt, packed-record VarInts | `PacketBatcher::tryFlushAsMultiBlockChange`; wire full | implemented, `WIRE-ORACLE` |
| Play | S→C | KeepAlive `0x27` | i64 id | `GameServer_core.cpp::tickCore`; `test_wire_full` | implemented, `WIRE-ORACLE` |
| Play | C→S | KeepAlive `0x1A` | i64 id | `Session::handlePlay`; native | accepted, `WIRE-ORACLE` |
| Play | S→C | LevelChunkWithLight `0x28` | i32 x, i32 z, heightmap NBT, data length/blob, block entities, masks and arrays | `ChunkCodec::serializeLevelChunkBody`; wire full | implemented, `CAPTURED` |
| Play | S→C | UpdateLight `0x2B` | chunk x/z VarInts, trust flag, mask arrays, light arrays | `ChunkCodec::serializeUpdateLightBody`; wire full | implemented, `WIRE-ORACLE` |
| Play | S→C | OpenScreen `0x35` | window id VarInt, menu type VarInt, anonymous-NBT title | `GameServer_session.cpp`; `test_wire_full` | implemented, `WIRE-ORACLE` |
| Play | S→C | ContainerSetContent `0x13` | window id VarInt, state id VarInt, Slot array, carried Slot | `GameServer::sendMenuContent`; wire full | implemented, `WIRE-ORACLE` |
| Play | S→C | ContainerSetSlot `0x15` | window id VarInt, state id VarInt, slot i16, Slot | `MenuInteraction`; wire full | implemented, `WIRE-ORACLE` |
| Play | S→C | TradeList `0x2E` | window id, offer count, buy/result Slots, booleans, uses, f32 multiplier | `GameServer` villager trade path; `test_spec_wire` | implemented, `WIRE-ORACLE` |
| Play | S→C | PlayerChat `0x3B` / SystemChat `0x73` | signed chat header or anonymous NBT + action-bar bool | `ChatMessageProcessor`, `GameServer_session.cpp`; wire full | PlayerChat conditional, SystemChat fallback |
| Play | S→C | UpdateAttributes `0x7C` | entity id, attribute mapper VarInt, f64 base, modifiers | `src/game/Attributes.hpp::attributeMapperId`; wire full | implemented, `WIRE-ORACLE` |
| Play | S→C | ResetScore `0x49`, Objective `0x64`, Score `0x68` | string holder/optional objective; objective and score fields | `src/game/Scoreboard.hpp`, commands; scoreboard reset | implemented, `WIRE-ORACLE` |
| Play | S→C | DeclareCommands `0x11` | flattened Brigadier nodes, flags, parser and redirect fields | `src/brigadier/Tree.hpp::writeDeclareCommands`; wire full | implemented, `WIRE-ORACLE` |

### Required corrected IDs

The current definitions and tests, not old comments, are authoritative:

```text
LevelChunkWithLight  = 0x28
UpdateLight          = 0x2B
KeepAlive (Play S→C) = 0x27
KeepAlive (Play C→S) = 0x1A
OpenScreen           = 0x35
TradeList            = 0x2E
ContainerSetContent  = 0x13
MultiBlockChange     = 0x4E
```

### From-client and omitted/deferred matrix

The matrix is state-aware: a packet that is valid in one state is not implicitly valid
in another. The exhaustive constants remain in `src/proto/Ids.hpp`; this table records
the contract boundaries and the important exception classes.

| state/direction | packet IDs or family | handling/status | evidence/provenance |
|---|---|---|---|
| Handshaking C→S | Intention `0x00` | select Status or Login from protocol/version fields | `Session::handleHandshake`; `WIRE-ORACLE` |
| Status C→S | Request `0x00`, Ping `0x01` | respond with Response/Pong, then close or continue correctly | native/status capture; `CAPTURED` |
| Login C→S | Hello `0x00`, Key `0x01`, Login Acknowledged `0x03`, optional cookie/query responses | negotiate auth/encryption and enter Configuration | `Session::handleLogin`; native; `CAPTURED` |
| Configuration C→S | Client Information `0x00`, Select Known Packs `0x07`, Finish Acknowledgement `0x03`, keepalive/pong/resource responses | consume known-packs wait and state-correct acknowledgements; supported optional inputs are tolerated | `Session::handleConfiguration`; `test_wire_b6`; `IMPLEMENTATION` |
| Play C→S movement | Position `0x1C`, Position+Rotation `0x1D`, Rotation `0x1E`, Status-only `0x1F`, Vehicle `0x20`, Steer Boat `0x21` | parse movement flags/coordinates and update tracking; no extra trailing field is invented | `GameServer_session.cpp`; `test_wire_b6`; `CAPTURED` |
| Play C→S interaction | Use Entity `0x18`, Window Click `0x10`, Player Action `0x27`, Entity Action `0x28`, Use Item On `0x3C`, Use Item `0x3D` | dispatch through permission, inventory, physics, and event hooks | gameplay/wire tests; `IMPLEMENTATION` |
| Play C→S utility/UI | Settings `0x0C`, Tab Complete `0x0D`, Recipe Book `0x2C`, Name Item `0x2E`, Beacon `0x32`, Update Sign `0x39`, Spectate `0x3B` | parse or safely reject according to the handler and permission state | `test_wire_b6`; `CAPTURED`/`IMPLEMENTATION` |

Some protocol-defined serverbound or clientbound packets are deliberately not emitted
as standalone packets by this implementation. They are classified instead of being
silently called “complete”:

| packet | classification | observable alternative or boundary | status/provenance |
|---|---|---|---|
| ChunkBiomes `0x0E` | omitted | biomes are paletted inside LevelChunkWithLight `0x28` | `IMPLEMENTATION` + `CAPTURED` |
| DebugSample `0x1B`, HideMessage `0x1C` | omitted | debug/chat behavior has no standalone player-visible emission in this path | `DECLARED-LIMITATION` where not independently captured |
| ProfilelessChat `0x1E` | omitted | SystemChat `0x73` fallback when secure PlayerChat is unavailable | `IMPLEMENTATION` + `CAPTURED` |
| OpenBook `0x34`, OpenSignEntity `0x36` | omitted/deferred | item-use or BlockEntityData `0x07` path; automatic UI parity is not claimed | `DECLARED-LIMITATION` |
| End/Enter/Death Combat Event `0x3C–0x3E` | omitted | HurtAnimation `0x25`, DamageEvent `0x1A`, and SystemChat feedback | `IMPLEMENTATION`; historical E-series cross-check |
| StartConfiguration `0x70`, SetTickingState `0x78`, StepTick `0x79` | omitted | current server uses Transfer/configuration and fixed 20 TPS | `IMPLEMENTATION` + `DECLARED-LIMITATION` |
| SetProjectilePower `0x80`, CustomReportDetails `0x81`, ServerLinks `0x82` | future/deferred or omitted | bow pull/report/link extensions are outside the current sent set | `DECLARED-LIMITATION` |

The omitted/deferred list is not permission to accept malformed bytes: state, frame,
and field-shape validation still follows the decoder and the operational policy.

## 5. Wire events and observable ordering

Targets **#72, #74–#79**. These are emission/consumption events, not a claim that a
Fabric event bus exists:

1. `Session::handleHandshake` selects Status or Login.
2. Login may negotiate encryption and compression before normal framed traffic.
3. Configuration sends the feature/registry/tag sequence, sends `SelectKnownPacks`,
   and waits for the client response before finishing configuration.
4. Play actions can produce an acknowledgement (for example Ack Block Change), then
   a gameplay mutation may queue block packets. `PacketBatcher` swaps its queue
   under a mutex at flush time.
5. Malformed/oversize frames are handed to the state-specific disconnect policy in
   [SPEC_OPS.md#limits-and-security](SPEC_OPS.md#limits-and-security).

<a id="state-transitions"></a>
## 6. State transitions

```text
HANDSHAKING
  ├─ status request/ping → STATUS → response/pong
  └─ login intention → LOGIN → encryption/compression/login success
                         → CONFIGURATION → known-packs acknowledgement
                         → registries/tags/finish configuration
                         → PLAY → keepalive/chunk/entity/menu/chat
                         → DISCONNECT
```

| state | accepted client IDs | server IDs | unknown/malformed behavior |
|---|---|---|---|
| Handshaking | Intention `0x00` | none before next state | reject invalid protocol/next state |
| Status | Request/Ping `0x00/0x01` | Response/Pong `0x00/0x01` | close or state-correct failure |
| Login | Hello/Key/Login Acknowledged and supported login responses | Disconnect, Encryption Request, Game Profile, Set Compression | kick; invalid names/protocol are not silently accepted |
| Configuration | client information, known-packs response, finish acknowledgement and supported responses | registries, tags, known packs, finish | kick or reject unknown configuration packet |
| Play | IDs in `proto::pl::cs` and supported optional inputs | IDs in `proto::pl::sc` | apply play policy: disconnect for unsafe framing, otherwise handler-specific rejection/continue |

The Login/Configuration strictness versus Play tolerance is intentional and is
cross-referenced by the operational disconnect policy.

## 7. Reproduction and implementation flow

The documentation workflow for a wire claim is:

1. compare `src/proto/Ids.hpp` to the pinned 1.21.4 protocol JSON;
2. identify the field writer/reader symbol;
3. assign a golden vector or capture and a named test;
4. record direction, state, ID, field order, and compression/encryption stage;
5. classify the packet as sent, omitted with an observable alternative, or future/
   deferred; and
6. run the static and wire gates in [VERIFICATION.md#wire-gate](VERIFICATION.md#wire-gate).

This canonical-doc change performs no runtime packet implementation.

## 8. C++ design example

The following is a documentation model only. It is **not** a requested second
registry or a new runtime class:

```cpp
struct PacketContract {
    State state;
    Direction direction;
    std::uint8_t id;
    std::vector<FieldContract> fields;
    std::string implementation;
    std::string evidence;
};

// MultiBlockChange record, documentation invariant:
packed = (stateId << 12) | (localX << 8) | (localZ << 4) | localY;
```

`src/net/PacketBatcher.cpp` and `tests/test_spec_wire.cpp` implement and lock this
formula. The old axis note in source comments is not evidence against the executable
writer and test vector.

## 9. Source/class composition

| responsibility | source symbols |
|---|---|
| primitive bytes | `src/core/ByteBuffer.hpp::WriteBuffer`, `ReadBuffer` |
| NBT/chat | `src/core/NBT.hpp::Writer`, `writeTextComponent` |
| IDs | `src/proto/Ids.hpp::cppfm::proto` |
| framing/compression | `src/net/Connection.hpp`, `PacketEncoder.hpp`, `PacketDecoder.hpp`, `src/core/Zlib.hpp` |
| encryption/auth | `src/net/Crypto.hpp::AesCfb8`, `mcSha1Hex`, `RsaKeyPair` |
| batch | `src/net/PacketBatcher.hpp/.cpp` |
| session | `src/game/GameServer_session.cpp::Session` |
| chunk/light | `src/game/ChunkCodec.hpp`, `src/physics/LightEngine.*` |
| gameplay packet producers | `src/game/Items.hpp`, `Containers.hpp`, `Attributes.hpp`, `Scoreboard.hpp`, `Tree.hpp` |

## 10. Module split and ownership

The ownership boundary is one-way:

```text
README → WIRE / GAMEPLAY / OPS / DEVELOPMENT / VERIFICATION
WIRE → source symbols + verification evidence
GAMEPLAY → WIRE trigger links + behavior evidence
OPS → WIRE framing link + incident/runbook evidence
```

Packet field tables are not duplicated in Gameplay or Operations. Gameplay names a
packet as an observable consequence; Operations names a packet only as a response or
limit.

## 11. Cautions

- `BundleDelimiter 0x00` is a packet; the `bundle_contents` item component is a
  different concept and is not part of this 1.21.4 wire contract.
- A single-valued paletted container writes `value` **and then `longCount=0`**.
- `UpdateLight` chunk coordinates are VarInts, while LevelChunk coordinates are i32.
- `ContainerSetContent` and `OpenScreen` IDs in old notes (`0x12` and `0x34`) are
  stale; use the table above. The same applies to old `TradeList 0x2D` and Play
  KeepAlive `0x26` notes. Historical values may remain only in archive provenance,
  not in a current packet contract.
- `UpdateAttributes` mapper IDs and slot component IDs must not be inferred from an
  older release.
- The current header comment in `src/proto/Ids.hpp` contains an old axis sentence;
  the implementation and byte tests win.

## 12. Performance

Serialization performance is not a parity oracle. The observable budgets are:

| budget | current implementation |
|---|---|
| outer frame | `Connection::kMaxFrame = 8 MiB` |
| declared decompressed size | `PacketDecoder::kMaxDeclared = 2 MiB` |
| configured compression | `PacketEncoder` uses `dataLength=0` below threshold and uncompressed size + zlib above it; the session negotiates the configured threshold |
| batch | `PacketBatcher` uses the 64-count/50 ms policy documented by the current header and tick integration |

MSPT, RSS, chunk-generation, and soak results are owned by
[SPEC_OPS.md#performance-and-load](SPEC_OPS.md#performance-and-load). The runtime
follow-up is identified by the snapshot above; this wire-contract refresh does not
alter packet encoding or runtime performance paths.

## 13. Thread safety

`Connection::tx_` serializes writes. Each session owns its connection; encryption
contexts are direction-specific. `PacketBatcher::mtx_` protects the queue and
`lastFlushMs` is atomic. The tick thread swaps the queue, so a session can enqueue
without moving the queue to another owner. Documentation does not introduce a new
thread, mutex, or packet worker.

## 14. Edge cases

- VarInt/VarLong overflow, truncated frames, negative lengths, and empty bodies;
- compression threshold `0`, `dataLength=0`, forged declarations below threshold,
  trailing zlib bytes, and decompressed-size bombs;
- signed Position at negative X/Z/Y and section coordinates;
- absent versus present optional NBT and UUID byte versus string forms;
- single palette `longCount=0`, 4,096 block entries versus 64 biome entries;
- MultiBlockChange deduplication (last write wins), local x/z/y ordering, and Bundle
  start/end; and
- omitted alternatives such as solo ChunkBiomes/ProfilelessChat/OpenBook must not be
  mistaken for missing field implementations.

## 15. Test method and evidence

Fresh byte-lock evidence at the snapshot:

| target | result |
|---|---|
| `test_spec_wire` | `392 PASS 0 FAIL 0 SKIP` |
| `test_wire_full` | `405 PASS 0 FAIL 0 SKIP` |
| `test_wire_b6` | `133 PASS 0 FAIL` |
| `test_scoreboard_reset` | `22 PASS 0 FAIL` |
| `test_fuzz` | `23 PASS 0 FAIL` |

These are named current-snapshot results, not inherited values from the handover or
historical audits. In particular, the old `test_spec_wire` value `328` is stale;
the current value is `392`. `test_native` is intentionally recorded as `ALL PASS`
without an invented aggregate count. A passing wire lock also does not clear the
intentional E-14 failure or missing real-client/long-run evidence; the former
`soak_bot` blocker is resolved by three integrated 300-second passes.

Named vectors include:

- uniform biome palette `00 28 00` (value 40 followed by zero longs);
- `UpdateLight` `(cx=0, cz=-1)` prefix `00 FF FF FF FF 0F`;
- empty BundleDelimiter body (zero bytes);
- ContainerSetContent header `window=0, state=1, item-count=2`;
- Slot components `damage=3`, `enchantments=10`, `repair_cost=17`, `trim=45`; and
- ResetScore wildcard and MultiBlockChange 13-byte body checks.

The full command matrix and timeout wrappers are in
[VERIFICATION.md#wire-gate](VERIFICATION.md#wire-gate).

## 16. Priority and status

**Priority: highest.** Every gameplay and operations claim depends on this contract.
The current sent paths above are `IMPLEMENTATION`/`WIRE-ORACLE` or `CAPTURED` and are
locked by tests. Omitted alternatives are not silently promoted to sent packets;
future/deferred entries remain `DECLARED-LIMITATION`.
