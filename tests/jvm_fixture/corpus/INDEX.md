# plan51 compatibility fixture corpus

This directory is the source-of-truth inventory for plan51 §26.  The corpus
runner compiles every `src/**/*.java` with the existing `build/jvm/classes`
shadow API, stages each directory with its `fabric.mod.json` and resources,
then launches all 25 directory mods in one owned `cppfm` process.  It does not
depend on the current CMake fixture target, which intentionally compiles only
`FixtureMod.java` and `ServerMixin.java`.

`corpus.json` is the machine-readable hand-off list.  Every entry has a stable
case name, mod id, entrypoint, dependency edge, and execution mode.  A fixture
is not considered complete because its class loaded: the runner requires a
`CORPUS case=NN status=PASS` assertion and the report tool checks the generated
compatibility manifest as well.

| Case | Directory | Primary assertion | Mode |
| --- | --- | --- | --- |
| 01 | `01-loader-entrypoint` | server entrypoint invocation | runtime |
| 02 | `02-dependency` | dependency/topological init order | runtime |
| 03 | `03-fabric-api-event` | lifecycle, world, tick, packet-buffer API | runtime |
| 04 | `04-world-api` | native block state read/write | runtime |
| 05 | `05-entity-api` | safe empty entity/player boundary | runtime |
| 06 | `06-registry-api` | registry and reverse identity | runtime |
| 07 | `07-reflection` | reflective ABI discovery/invocation | runtime |
| 08 | `08-access-widener` | access-widener resource metadata | metadata-only |
| 09 | `09-accessor-mixin` | target implements executable Accessor | runtime; transformer required |
| 10 | `10-invoker-mixin` | target implements executable Invoker | runtime; transformer required |
| 11 | `11-inject-head` | executable HEAD injection | runtime |
| 12 | `12-inject-return` | executable RETURN injection | runtime |
| 13 | `13-inject-invoke` | executable INVOKE injection | runtime; transformer required |
| 14 | `14-inject-field` | executable FIELD injection | runtime; transformer required |
| 15 | `15-redirect` | executable call Redirect | runtime; transformer required |
| 16 | `16-overwrite` | executable Overwrite value | runtime |
| 17 | `17-modify-arg` | executable argument modifier | runtime; transformer required |
| 18 | `18-modify-constant` | executable constant modifier | runtime; transformer required |
| 19 | `19-modify-variable` | executable local/argument modifier | runtime; transformer required |
| 20 | `20-local-capture` | captured local value at TAIL | runtime; transformer required |
| 21 | `21-two-mod-transform-order` + `21-two-mod-transform-order-second` | two directory-mod handlers in declared dependency order (priority 900 then 1100) | runtime |
| 22 | `22-reentrant-callback` | Java → native → Java re-entry | runtime |
| 23 | `23-threading` | attached Java worker thread JNI call | runtime |
| 24 | `24-exception` | callback isolation and recovery | runtime |
| 25 | `25-object-identity` | stable server/world wrapper identity | runtime |

Cases 09–10, 13–15, and 17–20 intentionally fail when only the current
manual hook shell is present.  Their handlers are real assertions; the report
must remain FAIL until the structured transformer executes them.  Case 08 is a
metadata/resource check and does not claim that an official Fabric
Loader/Knot or access-widener implementation exists.
