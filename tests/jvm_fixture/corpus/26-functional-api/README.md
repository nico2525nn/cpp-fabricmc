# Corpus 26: server-side functional API

This auxiliary directory mod exercises the largest observable server-side
surface currently available in the bounded shadow API:

- block and item registry registration, lookup, reverse lookup, and entries;
- `ServerWorld` block-state read/write;
- server/world lifecycle callbacks;
- a server tick callback with an observable counter;
- Fabric command registration followed by native command execution;
- a play C2S custom-payload codec, registry entry, receiver, and dispatch;
- an NBT state encode/decode round trip.

The fixture is intentionally not added to the strict `corpus.json` inventory.
That inventory and `jvm_compatibility_report.py` retain their historical 25
fixture contract. `tests/jvm_compatibility_smoke.py` stages this directory as
an auxiliary mod with dependencies on cases 24 and 25 and validates its dedicated
`FUNCTIONAL_FIXTURE` evidence in the same server process.

`PersistentStateManager` is not part of the current bounded shadow API and is
not faked here. The fixture registers the available `BEFORE_SAVE` callback and
uses the available NBT codec for an in-memory state round trip, then emits a
`persistent-state-boundary` diagnostic stating that native persistent-state
save/load cannot be claimed. A client/player connection is also not required;
the tested lifecycle path is the server/world path.
