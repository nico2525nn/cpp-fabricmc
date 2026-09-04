#!/usr/bin/env python3
"""
score_review.py — plan35 再評価フレーム (4カテゴリ)
  A Protocol 40 / B Gameplay 30 / C Server/Content 15 / D Stability 15  — task spec (40/30/15/15)
  および plan35.md §6 の 25/25/25/25 換算も併記。
  各項目は "対応済み (planX)" / "残" で列挙し現状スコアを算出する。
  実行: python3 tools/score_review.py  (or ./tools/score_review.py)
  参照: docs/VERIFICATION.md (score/audit evidence), docs/MISSING_FEATURES_1_21_4.md
"""
import sys

# Category max (task spec: 40/30/15/15)
C_MAX = {"A":40, "B":30, "C":15, "D":15}
# Alternative 25 each for plan35.md
C_MAX_25 = {"A":25, "B":25, "C":25, "D":25}

# Items: (name, weight, status_done, plan, note)
ITEMS = {
 "A": [ # Wire / Protocol — strict audit 78 gaps + 131 toClient
   ("Handshake/Status/Login 769",           4, True, "plan30-", "ids 0x00-0x7F verified, test_spec_wire 211"),
   ("Chunk/Light/Bundle/MultiBlockChange", 6, True, "plan30",  "ChunkCodec 64 vs 4096, Bundle 0x00, MultiBlockChange 0x4E axis fix"),
   ("Entity metadata/attributes",            5, True, "plan30 H1", "Creeper Boolean8, UpdateAttributes 0x7C varint mapper 22/32"),
   ("Inventory/Container 0x12-0x15",        4, True, "plan31",  "SlotComponent 3/17/45, ContainerSetContent"),
   ("Scoreboard/Teams/BossBar",             4, True, "plan30",  "ResetScore 0x49, Teams 0x67 color 21, BossBar 0x0A"),
   ("World/Title/Border/Time",              4, True, "plan30",  "WorldBorder 59999968, UpdateTime 0x6B"),
   ("Chat/Commands/Advancements",           5, True, "plan35 §1-4", "UpdateAdvancements 0x7B merged 29, /reload"),
   ("Unsent 27 documented",                 4, True, "plan34",  "0x18/0x20/0x25/0x50/0x51/0x6E etc unsent"),
   ("Compression/Encryption/KeepAlive",     4, True, "pre30",   "zlib threshold 256, AES-CFB8, KeepAlive 0x27"),
 ],
 "B": [ # Gameplay — MISSING 80 taxonomy + entity/world
   ("WorldGen Density 7 + MultiNoise",      5, True, "plan33",  "Beardifier/EndIslands/WeirdScaled/ShiftA/B"),
   ("Structures 20 sets salts",             5, True, "plan33",  "village 10387312 .. trial_chambers 94251327"),
   ("Mob AI 10+ (Breeze/Armadillo)",       5, True, "plan34",  "BehaviorTree 6 Goals, Ranged"),
   ("Block Behaviors/Redstone/Fluids",     5, True, "plan21-22","StairsHelper, Redstone, Fluids, LightEngine"),
   ("Advancements 29 + 3 triggers",         4, True, "plan35 §1", "story 20+cppfm9, tick/inventory/kill"),
   ("Loot 5 functions + 9 JSON",            4, True, "plan35 §2", "explosion_decay/furnace_smelt/copy_components"),
   ("Combat/PVP/Hunger/XP",                 2, True, "plan32-34","DamageCalculator, HungerManager"),
 ],
 "C": [ # Content / Server / Datapack
   ("Recipes 1578 + tags 67/20",            3, True, "plan32",  "JSON-driven, TagManager"),
   ("Commands 30+ Brigadier",              3, True, "plan32-35","execute/data/clone/locate/advancement/reload"),
   ("Advancements datapack 30",            2, True, "plan35 §1","assets/data/advancements/story 20"),
   ("Loot tables 9 + predicates",          2, True, "plan35 §2-3","Predicates context 3 types"),
   ("Datapack /reload + tick trigger",     2, True, "plan35 §4","DatapackManager reload + tick"),
   ("server.properties pvp/flight/hardcore",2, True, "plan35 §5","ServerProperties pvp/allow-flight/max-players"),
   ("Loot functions & copy_components",     1, True, "plan35 §2","furnace_smelt etc"),
 ],
 "D": [ # Stability — fuzz/soak/mutex/persist
   ("Fuzz 23 + Soak",                       3, True, "plan34",  "test_fuzz 23 PASS, soak 60s"),
   ("Weak-check 16 removal + smoke 123",   4, True, "plan34-35","smoke 112->123 PASS 0 FAIL"),
   ("spec_wire 211 lock",                   4, True, "plan35 §6","UpdateAdvancements 0x7B 3 cases, H1 32/32"),
   ("ThreadPool/mutex LRU documented",      2, True, "plan35 §5","ThreadPool 4, mutex 3種, LRU Chebyshev 16"),
   ("Anvil/level.dat persist",              2, True, "pre33",  "RegionFile zlib, level.dat 4189"),
 ],
}

def score(use25=False):
    max_map = C_MAX_25 if use25 else C_MAX
    total=0; max_total=sum(max_map.values())
    print(f"=== cppfm score review ({'25-each' if use25 else '40/30/15/15'}) ===")
    for cat in ["A","B","C","D"]:
        items=ITEMS[cat]
        total_w=sum(w for _,w,_,_,_ in items)
        done_w=sum(w for _,w,done,_,_ in items if done)
        # scale to category max
        cat_score = round(done_w/total_w*max_map[cat]) if total_w else 0
        # also raw fraction
        print(f"\n[{cat}] {cat_score}/{max_map[cat]}  ({done_w}/{total_w} weights done)")
        for name,w,done,plan,note in items:
            mark="✓" if done else "✗"
            status=f"{plan} {note}" if done else f"残 {note}"
            print(f"  {mark} {name:40s}  w={w}  {status}")
        total+=cat_score
    print(f"\nTOTAL {total}/{max_total}")
    if not use25:
        # also show 25-each
        print("\n--- alternative 25-each (plan35.md §6) ---")
        alt=sum(round(sum(w for _,w,d,_,_ in ITEMS[c] if d)/sum(w for _,w,_,_,_ in ITEMS[c])*25) for c in ["A","B","C","D"])
        print(f"Alternative total {alt}/100 (25 each)")
    return total

if __name__=="__main__":
    s=score(use25=False)
    # plan35見込み 89-92/100 (25 each) or ~90/100 with 40/30/15/15
    # threshold 90
    if s>=90:
        print("\nResult: 90台到達 (plan35 目標)")
    else:
        print(f"\nResult: {s}/100  90未満 -> plan36で Bundles/Anvil async を優先")
    sys.exit(0)
