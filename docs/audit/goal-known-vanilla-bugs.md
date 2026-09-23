# Vanilla known-bug oracle (Minecraft Java 1.21.4)

Audit scope: external Mojang bugs used as **oracles**, not cppfm defects. The
target product is the C++ dedicated server (Fabric-compatible, protocol 769),
not the vanilla client or the vanilla integrated server. A Mojang issue is only
counted as a cppfm defect when a reproducible product-level failure exists. None
of the records below meets that bar.

## Method and bounded evidence

- Primary release source: [Minecraft Java Edition 1.21.4 — The Garden
  Awakens](https://feedback.minecraft.net/hc/en-us/articles/32385811139085-Minecraft-Java-Edition-1-21-4-The-Garden-Awakens),
  section **Fixed bugs in 1.21.4**. It labels MC-212 and MC-10025 as fixed and
  lists the 1.21.4 fixes for MC-277892, MC-277929, and the additional records
  below.
- Primary issue records: [MC-212](https://bugs-legacy.mojang.com/browse/MC-212),
  [MC-10025](https://bugs-legacy.mojang.com/browse/MC-10025),
  [MC-277892](https://bugs.mojang.com/browse/MC-277892), and
  [MC-277929](https://bugs.mojang.com/browse/MC-277929). The legacy tracker
  text is used only where the current tracker does not expose the description.
- Shipped-code checks run in this worktree (all commands timeout-bounded):
  `timeout --foreground --kill-after=5 60 ./build/test_menu_logic` → **41
  PASS / 0 FAIL**; `timeout --foreground --kill-after=5 90
  ./build/test_gameplay_full` → **806 PASS / 0 FAIL**. These are server/menu
  and gameplay checks, not vanilla-client rendering or crash tests.
- A PASS below means the bounded oracle did not reproduce the Mojang claim. It
  does not prove full vanilla parity. `N/A` means the claim requires a client or
  integrated-server surface cppfm does not ship; `UNVERIFIED` means a dedicated
  server path could be relevant but no product repro was run.

## Required records

| Mojang ID | Official claim | Applicability | Bounded local oracle and result | Classification |
|---|---|---|---|---|
| **MC-212** | “Fall damage is ignored for a couple of seconds when reloading into LAN or singleplayer worlds.” Mojang lists it as fixed in 1.21.4; the issue description explicitly says multiplayer server worlds are not affected. | Integrated server + client reload (LAN/singleplayer); not the dedicated multiplayer path. | `test_gameplay_full` covers ordinary fall/damage rules, but no integrated-world save/reload exists in cppfm and no reconnect immunity was reproduced. | **N/A — external vanilla client/integrated-server bug; not a cppfm defect.** |
| **MC-10025** | “Burn time indicator of a furnace not working correctly after reloading the world.” The issue describes changing the fuel slot, saving, and seeing the client progress indicator increase after reload; Mojang lists it fixed in 1.21.4. | Client GUI indicator over persisted furnace state; server persistence is only an input to the display. | cppfm persists furnace `BurnTime`, `CookTime`, and `CookTimeTotal` (`src/game/BlockEntities.hpp:305-309`) and reloads them (`src/game/BlockEntities.hpp:408-416`), but has no vanilla client GUI oracle for the indicator sequence. | **UNVERIFIED — no product repro; do not count.** |
| **MC-277892** | “Clicking on the selected recipe a second time in the stonecutter GUI turns the result item into a ghost item.” Mojang lists it among 1.21.4 fixes. | Vanilla client stonecutter GUI/state synchronization. | `test_menu_logic` passes 41/41; `test_gameplay_full` passes 806/806 including strict stonecutting (one input, exact recipe matching). cppfm has a stonecutter menu (`src/game/GameServer_session.cpp:2320-2323`) but no client renderer/ghost-stack oracle. | **N/A for the reported client ghost; not a cppfm defect.** |
| **MC-277929** | “The game crashes when attempting to use the void preset.” Mojang lists it among 1.21.4 fixes. | Client world-creation preset (and possibly integrated-server startup), not a dedicated-server protocol behavior. | cppfm exposes no vanilla client world-creation screen or void-preset implementation. No crash or product repro is available. | **N/A — outside product surface; not a cppfm defect.** |

## Additional relevant 1.21.4 records

These are also quoted in the same official 1.21.4 fixed-bug list. They are kept
separate from the required IDs so an external vanilla regression is not silently
turned into a cppfm finding.

| Mojang ID | Official claim | Server/client applicability | Bounded oracle and result | Classification |
|---|---|---|---|---|
| **MC-21650** | “Player is immune to damage for a few seconds after saving the world and returning.” | Integrated save/return lifecycle; no evidence in the release note that this is a dedicated-server protocol bug. | No integrated save/return harness is shipped. Ordinary damage/fall checks pass, but they do not exercise this lifecycle. | **N/A / unverified; not a cppfm defect.** |
| **MC-160051** | “Players can prevent fire damage by reloading world/re-joining server.” | The wording includes re-joining a server, so a dedicated lifecycle could matter. | No reconnect-while-burning product repro was run; the available focused tests do not claim this behavior. | **UNVERIFIED; do not count.** |
| **MC-277754** | “Chunks on the corner of the rendering distance are not synchronized between client and server for the terrain.” | Client rendering plus server chunk/update behavior; requires a real 1.21.4 client at a render-distance boundary. | cppfm has no real-client terrain-render oracle in this audit. Existing server gameplay PASS results cannot distinguish a client render desync. | **UNVERIFIED; do not count.** |
| **MC-277916** | “Containers are locked when upgrading a world from certain versions.” | World-version migration and container data; potentially server-side, but only for specific upgrade histories. | No vanilla upgrade fixture or legacy-world migration run was available; current menu tests use fresh cppfm state. | **UNVERIFIED; do not count.** |
| **MC-277955** | “Using a loom crashes the game.” | Vanilla client loom GUI/action path (the server only receives the resulting protocol actions). | cppfm models a loom menu (`src/game/GameServer_session.cpp:2336-2339`), and menu/gameplay tests pass, but there is no client crash oracle. | **N/A for the reported client crash; not a cppfm defect.** |
| **MC-277930** | “Eyeblossom subtitles are inverted.” | Client localization/subtitle presentation only. | No audio/subtitle renderer exists in the dedicated server and no client capture was run. | **N/A — client-only; not a cppfm defect.** |

## Outcome

The official source verifies that these were Mojang-tracked Java bugs and that
the required four were listed as fixed in or around 1.21.4. The bounded cppfm
checks found no corresponding product failure. The audit therefore records
**zero cppfm defects** from this external bug set; any future claim must add a
real cppfm reproduction (inputs, server log, and expected/observed behavior)
before changing that conclusion.
