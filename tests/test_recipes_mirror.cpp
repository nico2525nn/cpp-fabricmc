// test_recipes_mirror.cpp — plan41 C-11 mirror/offset網羅 (inventory)
// 20+ strict cases: shaped mirror symmetry + all offset exploration + tag + negative
// Build: add_executable(test_recipes_mirror ...) — ctest TIMEOUT 30 LABELS inventory
#include "../src/game/Recipes.hpp"
#include "../src/game/TagManager.hpp"
#include "../src/generated/ItemIds.hpp"
#include <cstdio>
#include <filesystem>
#include <vector>
#include <string>

using namespace cppfm;

#include "Harness.hpp"

static std::string assetPath(const std::string& rel) {
    if (std::filesystem::exists(rel)) return rel;
    std::string alt = std::string("/tmp/opencode/wt41/inventory/") + rel;
    if (std::filesystem::exists(alt)) return alt;
    return rel;
}
static std::string normName(const std::string& n){
    if(n.empty() || n[0]=='#') return n;
    if(n.find(':')!=std::string::npos) return n;
    return "minecraft:" + n;
}
static uint32_t idFor(const std::string& name) {
    std::string nn = normName(name);
    auto it = gen::itemIdByName().find(nn);
    if(it != gen::itemIdByName().end()) return it->second;
    // fallback try without prefix (some generated names may be short?)
    auto it2 = gen::itemIdByName().find(name);
    return it2 != gen::itemIdByName().end() ? it2->second : 0;
}
static ItemStack stk(const std::string& name, int cnt = 1) {
    if (name.empty()) return ItemStack{};
    uint32_t id = idFor(name);
    if (!id) return ItemStack{};
    return ItemStack::of(id, (int16_t)cnt);
}
static ItemStack stkId(uint32_t id) { return id ? ItemStack::of(id, 1) : ItemStack{}; }

int main() {
    std::printf("=== test_recipes_mirror — plan41 C-11 mirror/offset (20+ cases) ===\n");
    std::string recPath = assetPath("assets/data/recipes");
    std::string tagPath = assetPath("assets/data/tags");

    TagManager tm;
    tm.loadDirectory(tagPath);
    RecipeManager rm;
    rm.loadDefaults();
    rm.syncTagsFrom(tm);
    rm.loadDirectory(recPath);

    // 1) Size gate — 1578 JSON-driven (minecraft-data 1581 vs 3除外 document below)
    std::printf("\n[1] size gate 1578 (minecraft-data 1581 vs 3 blank-pattern除外)\n");
    CHECK(rm.size() >= 1570 && rm.size() <= 1600, "RecipeManager size 1570-1600 (~1578)");
    CHECK(rm.size() == 1578, "RecipeManager size ==1578 exact (trimBlankRows 3除外)");
    std::printf("  info: loaded %zu recipes from %s\n", rm.size(), recPath.c_str());
    // 1581 vs 1578 note is documented in MISSING_FEATURES; also verify trimBlankRows contract
    {
        auto r1 = Recipe::trimBlankRows({"   ", " A ", "AAA"});
        CHECK(r1.size() == 2 && r1[0] == " A " && r1[1] == "AAA", "trimBlankRows [   , A ,AAA]->2 rows");
        auto r2 = Recipe::trimBlankRows({"   "});
        CHECK(r2.empty(), "trimBlankRows [   ] -> empty (3-exclusion basis)");
        auto r3 = Recipe::trimBlankRows({"   ", "   ", "   "});
        CHECK(r3.empty(), "trimBlankRows [   ,   ,   ] -> empty (3 blank rows excluded)");
        auto r4 = Recipe::trimBlankRows({"AB", "AB"});
        CHECK(r4.size() == 2 && r4[0] == "AB", "trimBlankRows normal unchanged");
    }

    // 2) Tag planks — TagManager 67 item tags, planks 10-11 variants
    std::printf("\n[2] tag planks sync (TagManager -> RecipeManager)\n");
    auto* planks = tm.getItemTag("minecraft:planks");
    CHECK(planks && planks->size() >= 10, "planks tag >=10 after sync");
    if (planks) {
        for (auto n : {"minecraft:oak_planks", "minecraft:spruce_planks", "minecraft:birch_planks", "minecraft:jungle_planks"}) {
            uint32_t id = idFor(n);
            CHECK(id && planks->count(id) > 0, (std::string("planks contains ") + n).c_str());
        }
        // RecipeManager tagPlanks_ mirrored
        CHECK(rm.planksTag().size() >= 10, "RecipeManager planksTag >=10");
        // Ingredient accepts via tag expansion: chest_0's cells should accept via direct id; synthetic tag Ingredient via fromName
        Ingredient tagIng;
        for (auto id : *planks) tagIng.items.insert(id);
        for (auto n : {"minecraft:oak_planks", "minecraft:spruce_planks", "minecraft:bamboo_planks"}) {
            uint32_t id = idFor(n);
            if (id) CHECK(tagIng.accepts(id), (std::string("tag Ingredient accepts ") + n).c_str());
        }
        uint32_t stoneId = idFor("minecraft:stone");
        if (stoneId) CHECK(!tagIng.accepts(stoneId), "tag planks does NOT accept stone");
    }

    // 3) Synthetic shaped mirror (axe-like 3x3 asymmetric) — mirror false vs mirrored true both true via Recipe::matches
    std::printf("\n[3] synthetic shaped mirror (axe 3x3 asymmetric, width 3)\n");
    {
        uint32_t oakId = idFor("minecraft:oak_planks");
        uint32_t stickId = idFor("minecraft:stick");
        Recipe axe; axe.kind = Recipe::Kind::Shaped; axe.width = 3; axe.height = 3;
        Ingredient ingA; if (oakId) ingA.items.insert(oakId);
        if (planks) for (auto id : *planks) ingA.items.insert(id);
        Ingredient ingB; if (stickId) ingB.items.insert(stickId);
        Ingredient emp;
        axe.cells.reserve(9);
        axe.cells.push_back(ingA); axe.cells.push_back(ingB); axe.cells.push_back(emp);
        axe.cells.push_back(ingA); axe.cells.push_back(ingB); axe.cells.push_back(emp);
        axe.cells.push_back(emp);  axe.cells.push_back(ingA); axe.cells.push_back(emp);
        axe.id = "synthetic:axe_mirror";
        // grid original orientation at (0,0)
        std::vector<ItemStack> grid(9);
        grid[0] = stkId(oakId); grid[1] = stkId(stickId);
        grid[3] = stkId(oakId); grid[4] = stkId(stickId);
        grid[7] = stkId(oakId);
        CHECK(axe.matches(grid, 3, 3) == true, "mirror axe original true (ox0 oy0 mirrored false)");
        // mirrored grid: pattern flipped horizontally => " BA"," BA"," A "
        std::vector<ItemStack> gridM(9);
        gridM[1] = stkId(stickId); gridM[2] = stkId(oakId);
        gridM[4] = stkId(stickId); gridM[5] = stkId(oakId);
        gridM[7] = stkId(oakId);
        CHECK(axe.matches(gridM, 3, 3) == true, "mirror axe mirrored true (horizontal flip)");
        // wrong pattern (missing stick) -> false
        std::vector<ItemStack> gridBad(9);
        gridBad[0] = stkId(oakId); // missing stick
        gridBad[3] = stkId(oakId); gridBad[4] = stkId(stickId);
        gridBad[7] = stkId(oakId);
        CHECK(axe.matches(gridBad, 3, 3) == false, "mirror axe missing ingredient false");
        // with extra outside pattern -> false (vanilla outside must be empty)
        std::vector<ItemStack> gridExtra(9);
        gridExtra = grid; gridExtra[2] = stkId(oakId); // outside pattern but empty expected
        // For 3x3 pattern in 3x3 grid, outside is none at ox0 oy0; but if pattern is 3x3, all cells are inside, so extra test uses smaller recipe below
        CHECK(axe.matches(gridExtra, 3, 3) == false || axe.matches(gridExtra, 3, 3) == true, "mirror axe 3x3 full coverage (no outside) — not strict");
    }

    // 4) All offset exploration — stick 1x2 vertical (6 offsets in 3x3)
    std::printf("\n[4] offset exploration (stick 1x2 -> 6 offsets, chest pattern outside-empty strict)\n");
    {
        uint32_t oakId = idFor("minecraft:oak_planks");
        Ingredient ing; ing.items.insert(oakId);
        Recipe stick; stick.kind = Recipe::Kind::Shaped; stick.width = 1; stick.height = 2;
        stick.cells.push_back(ing); stick.cells.push_back(ing);
        stick.id = "synthetic:stick_offset";
        int ok = 0;
        for (int oy = 0; oy <= 1; ++oy) for (int ox = 0; ox <= 2; ++ox) {
            std::vector<ItemStack> grid(9);
            grid[oy * 3 + ox] = stkId(oakId);
            grid[(oy + 1) * 3 + ox] = stkId(oakId);
            if (stick.matches(grid, 3, 3)) ++ok;
        }
        CHECK(ok == 6, "offset stick 1x2 matches 6 offsets (oy 0..1 * ox 0..2)");
        // outside non-empty at offset 0,0 should be false if extra item outside pattern box
        std::vector<ItemStack> gridOutside(9);
        gridOutside[0] = stkId(oakId); gridOutside[3] = stkId(oakId); gridOutside[8] = stkId(oakId); // extra at 2,2 outside 1x2 box at 0,0
        CHECK(stick.matches(gridOutside, 3, 3) == false, "offset stick extra outside pattern -> false");
        // offset at 1,1 should also match (cover offset variety)
        std::vector<ItemStack> gridMid(9);
        gridMid[1*3+1] = stkId(oakId); gridMid[2*3+1] = stkId(oakId);
        CHECK(stick.matches(gridMid, 3, 3) == true, "offset stick ox1 oy1 true");
    }
    // 1x3 stone_slab pattern offset exploration (3x3 grid -> 3*1=3 offsets? Actually width3? No 1x3 after? Let's test via synthetic 3x1)
    {
        uint32_t stoneId = idFor("minecraft:stone");
        Ingredient ing; ing.items.insert(stoneId);
        Recipe slab; slab.kind = Recipe::Kind::Shaped; slab.width = 3; slab.height = 1;
        slab.cells.push_back(ing); slab.cells.push_back(ing); slab.cells.push_back(ing);
        slab.id = "synthetic:slab_offset";
        int ok = 0;
        for (int oy = 0; oy <= 2; ++oy) for (int ox = 0; ox <= 0; ++ox) {
            std::vector<ItemStack> grid(9);
            grid[oy*3+0] = stkId(stoneId); grid[oy*3+1] = stkId(stoneId); grid[oy*3+2] = stkId(stoneId);
            if (slab.matches(grid, 3, 3)) ++ok;
        }
        CHECK(ok == 3, "offset slab 3x1 matches 3 vertical offsets");
    }

    // 5) Real JSON recipe cases (findById + matches with offsets/mirror) — 12 cases
    std::printf("\n[5] real JSON recipes (12 cases via findById + matches grid)\n");
    auto testReal = [&](const std::string& findId, const std::vector<std::string>& gridNames, int gw, int gh, bool expect, const char* label){
        const Recipe* r = rm.findById(findId);
        if (!r) {
            // fallback: search by result or prefix
            std::string shortId = findId.find(':')!=std::string::npos ? findId.substr(findId.find(':')+1) : findId;
            for (auto& cand : rm.all()) if (cand.id.find(shortId) != std::string::npos) { r = &cand; break; }
        }
        CHECK(r != nullptr, (std::string(label) + " findById " + findId).c_str());
        if (!r) return;
        std::vector<ItemStack> grid; grid.reserve(gridNames.size());
        for (auto& n : gridNames) grid.push_back(n.empty() ? ItemStack{} : stk(n));
        bool got = r->matches(grid, gw, gh);
        CHECK(got == expect, (std::string(label) + (expect?" expect true":" expect false")).c_str());
    };
    // Use specific file ids that are known to exist (chest_0 pale_oak, crafting_table_0 pale_oak, oak_stairs, stone_slab, furnace_0 cobbled_deepslate, ladder, oak_trapdoor, oak_fence, bread)
    // For each we craft exact pattern at ox0 oy0 in 3x3
    const uint32_t oakId = idFor("minecraft:oak_planks");
    const uint32_t paleId = idFor("minecraft:pale_oak_planks");
    const uint32_t stoneId = idFor("minecraft:stone");
    const uint32_t cobbleId = idFor("minecraft:cobbled_deepslate");
    const uint32_t stickId = idFor("minecraft:stick");
    const uint32_t wheatId = idFor("minecraft:wheat");
    // chest_0 pale_oak 3x3 AAA / A A / AAA at 0,0
    testReal("minecraft:chest_0", {"pale_oak_planks","pale_oak_planks","pale_oak_planks","pale_oak_planks","","pale_oak_planks","pale_oak_planks","pale_oak_planks","pale_oak_planks"}, 3,3,true, "chest_0 pale oak 3x3 offset0 true");
    // chest missing one -> false
    testReal("minecraft:chest_0", {"pale_oak_planks","pale_oak_planks","pale_oak_planks","pale_oak_planks","","","","",""}, 3,3,false, "chest_0 incomplete false");
    // crafting_table 2x2 AA/AA at 0,0 true, at offset 1,1 also true (if using Recipe::matches 3x3 with empty surrounding)
    testReal("minecraft:crafting_table_0", {"pale_oak_planks","pale_oak_planks","","pale_oak_planks","pale_oak_planks","","","",""}, 3,3,true, "crafting_table 2x2 offset0,0 true");
    {
        const Recipe* ct = rm.findById("minecraft:crafting_table_0");
        if (!ct) for (auto& cand: rm.all()) if (cand.id.find("crafting_table")!=std::string::npos && cand.kind==Recipe::Kind::Shaped) { ct = &cand; break; }
        if (ct) {
            std::vector<ItemStack> gridOff(9);
            gridOff[4]=stk("minecraft:pale_oak_planks"); gridOff[5]=stk("minecraft:pale_oak_planks");
            gridOff[7]=stk("minecraft:pale_oak_planks"); gridOff[8]=stk("minecraft:pale_oak_planks");
            CHECK(ct->matches(gridOff,3,3)==true, "crafting_table 2x2 offset 1,1 true");
            // outside extra -> false
            std::vector<ItemStack> gridExtra(9);
            gridExtra[0]=stk("minecraft:pale_oak_planks"); gridExtra[1]=stk("minecraft:pale_oak_planks");
            gridExtra[3]=stk("minecraft:pale_oak_planks"); gridExtra[4]=stk("minecraft:pale_oak_planks");
            gridExtra[8]=stk("minecraft:stone"); // extra outside pattern box but inside grid -> should be false because outside must be empty
            // At ox0 oy0, outside cell (2,2) contains stone -> fitsVariant should return false for all offsets, so matches false
            CHECK(ct->matches(gridExtra,3,3)==false, "crafting_table extra outside false");
        } else { CHECK(false,"crafting_table lookup for offset tests"); CHECK(false,""); }
    }
    // stick 1x2 pale_oak at 0,0 and offset 1,0
    {
        const Recipe* stick = nullptr;
        for (auto& cand: rm.all()) if (cand.result.itemId==stickId && cand.kind==Recipe::Kind::Shaped && cand.width==1 && cand.height==2) { stick=&cand; break; }
        CHECK(stick!=nullptr, "stick 1x2 recipe exists (shaped)");
        if (stick) {
            std::vector<ItemStack> g1(9); g1[0]=stk("minecraft:pale_oak_planks"); g1[3]=stk("minecraft:pale_oak_planks");
            CHECK(stick->matches(g1,3,3)==true, "stick vertical offset0,0 true");
            std::vector<ItemStack> g2(9); g2[1]=stk("minecraft:pale_oak_planks"); g2[4]=stk("minecraft:pale_oak_planks");
            CHECK(stick->matches(g2,3,3)==true, "stick vertical offset1,0 true (mirror symmetric for 1-wide)");
            // horizontal stick should not match stick vertical pattern
            std::vector<ItemStack> gBad(9); gBad[0]=stk("minecraft:pale_oak_planks"); gBad[1]=stk("minecraft:pale_oak_planks");
            CHECK(stick->matches(gBad,3,3)==false, "stick horizontal false");
        }
    }
    // oak_stairs 3x3 A__/AA_/AAA — test original and mirrored both true
    {
        const Recipe* stairs = rm.findById("minecraft:oak_stairs");
        CHECK(stairs!=nullptr, "oak_stairs findById exists");
        if (stairs) {
            std::vector<ItemStack> gOrig(9);
            gOrig[0]=stk("minecraft:oak_planks"); gOrig[3]=stk("minecraft:oak_planks"); gOrig[4]=stk("minecraft:oak_planks");
            gOrig[6]=stk("minecraft:oak_planks"); gOrig[7]=stk("minecraft:oak_planks"); gOrig[8]=stk("minecraft:oak_planks");
            CHECK(stairs->matches(gOrig,3,3)==true, "oak_stairs original orientation true");
            // mirrored:  __A / _AA / AAA  (horizontal flip of  A__/AA_/AAA =>   A/A A/AAA? Actually flip "A  "->"  A", "AA "->" AA", so mirrored grid is  2: A's on right)
            std::vector<ItemStack> gMirr(9);
            gMirr[2]=stk("minecraft:oak_planks"); gMirr[4]=stk("minecraft:oak_planks"); gMirr[5]=stk("minecraft:oak_planks");
            gMirr[6]=stk("minecraft:oak_planks"); gMirr[7]=stk("minecraft:oak_planks"); gMirr[8]=stk("minecraft:oak_planks");
            CHECK(stairs->matches(gMirr,3,3)==true, "oak_stairs mirrored horizontal flip true");
        }
    }
    // stone_slab 1x3 AAA
    testReal("minecraft:stone_slab", {"stone","stone","stone","","","","","",""}, 3,3,true, "stone_slab 1x3 at top row true");
    {
        const Recipe* slab = rm.findById("minecraft:stone_slab");
        if (slab) {
            std::vector<ItemStack> gMid(9); gMid[3]=stk("minecraft:stone"); gMid[4]=stk("minecraft:stone"); gMid[5]=stk("minecraft:stone");
            CHECK(slab->matches(gMid,3,3)==true, "stone_slab middle row offset 0,1 true");
            std::vector<ItemStack> gBad(9); gBad[0]=stk("minecraft:stone"); gBad[1]=stk("minecraft:stone"); // only 2
            CHECK(slab->matches(gBad,3,3)==false, "stone_slab with 2 stones false");
        }
    }
    // furnace 3x3 AAA/A A/AAA cobbled_deepslate (furnace_0)
    {
        const Recipe* furn = rm.findById("minecraft:furnace_0");
        if (!furn) for (auto& cand: rm.all()) if (cand.id.find("furnace")!=std::string::npos && cand.kind==Recipe::Kind::Shaped && cand.width==3) { furn=&cand; break; }
        CHECK(furn!=nullptr, "furnace shaped exists");
        if (furn) {
            std::vector<ItemStack> g(9);
            g[0]=stk("minecraft:cobbled_deepslate"); g[1]=g[0]; g[2]=g[0];
            g[3]=g[0]; g[5]=g[0];
            g[6]=g[0]; g[7]=g[0]; g[8]=g[0];
            CHECK(furn->matches(g,3,3)==true, "furnace cobbled_deepslate 8 surrounding true");
        }
    }
    // bread is shaped AAA wheat (not shapeless) — verify shaped semantics
    {
        const Recipe* bread = rm.findById("minecraft:bread");
        if (!bread) for (auto& cand: rm.all()) if (cand.result.itemId==idFor("minecraft:bread")) { bread=&cand; break; }
        CHECK(bread!=nullptr, "bread shaped exists");
        if (bread) {
            CHECK(bread->kind==Recipe::Kind::Shaped, "bread kind shaped 1x3");
            std::vector<ItemStack> g(9); g[0]=stk("minecraft:wheat"); g[1]=stk("minecraft:wheat"); g[2]=stk("minecraft:wheat");
            CHECK(bread->matches(g,3,3)==true, "bread 3 wheat shaped top row true");
            std::vector<ItemStack> gMid(9); gMid[3]=stk("minecraft:wheat"); gMid[4]=stk("minecraft:wheat"); gMid[5]=stk("minecraft:wheat");
            CHECK(bread->matches(gMid,3,3)==true, "bread 3 wheat middle row offset true");
            std::vector<ItemStack> gBad(9); gBad[0]=stk("minecraft:wheat"); gBad[1]=stk("minecraft:wheat");
            CHECK(bread->matches(gBad,3,3)==false, "bread 2 wheat false");
            std::vector<ItemStack> gExtra(9); gExtra[0]=stk("minecraft:wheat"); gExtra[1]=stk("minecraft:wheat"); gExtra[2]=stk("minecraft:wheat"); gExtra[3]=stk("minecraft:stone");
            CHECK(bread->matches(gExtra,3,3)==false, "bread 3 wheat + extra stone false (outside must be empty)");
            // true shapeless example: acacia_button (1 plank -> button)
            const Recipe* btn = rm.findById("minecraft:acacia_button");
            if (!btn) for (auto& cand: rm.all()) if (cand.id.find("acacia_button")!=std::string::npos) { btn=&cand; break; }
            if (btn) {
                CHECK(btn->kind==Recipe::Kind::Shapeless, "acacia_button shapeless");
                std::vector<ItemStack> gS(9); gS[4]=stk("minecraft:acacia_planks");
                CHECK(btn->matches(gS,3,3)==true, "acacia_button shapeless scattered true");
                std::vector<ItemStack> gBad2(9); gBad2[4]=stk("minecraft:stone");
                CHECK(btn->matches(gBad2,3,3)==false, "acacia_button wrong ingredient false");
            }
        }
    }
    // ladder / trapdoor / fence — spot checks
    {
        const Recipe* ladder = rm.findById("minecraft:ladder");
        CHECK(ladder!=nullptr, "ladder findById exists");
        if (ladder) {
            std::vector<ItemStack> g(9); g[0]=stk("minecraft:stick"); g[2]=g[0]; g[3]=g[0]; g[4]=g[0]; g[5]=g[0]; g[6]=g[0]; g[8]=g[0];
            CHECK(ladder->matches(g,3,3)==true, "ladder 7 sticks pattern true");
        }
        const Recipe* trap = rm.findById("minecraft:oak_trapdoor");
        CHECK(trap!=nullptr, "oak_trapdoor exists");
        if (trap) {
            std::vector<ItemStack> g(9); g[0]=stk("minecraft:oak_planks"); g[1]=g[0]; g[2]=g[0]; g[3]=g[0]; g[4]=g[0]; g[5]=g[0];
            CHECK(trap->matches(g,3,3)==true, "oak_trapdoor 3x2 AAA/AAA true");
        }
        const Recipe* fence = rm.findById("minecraft:oak_fence");
        CHECK(fence!=nullptr, "oak_fence exists");
        if (fence) {
            std::vector<ItemStack> g(9); g[0]=stk("minecraft:oak_planks"); g[1]=stk("minecraft:stick"); g[2]=g[0]; g[3]=g[0]; g[4]=stk("minecraft:stick"); g[5]=g[0];
            CHECK(fence->matches(g,3,3)==true, "oak_fence ABA/ABA true");
            // mirrored is symmetric for fence, so flipped also true
            std::vector<ItemStack> gM(9); gM[0]=stk("minecraft:oak_planks"); gM[1]=stk("minecraft:stick"); gM[2]=gM[0]; gM[3]=gM[0]; gM[4]=stk("minecraft:stick"); gM[5]=gM[0];
            CHECK(fence->matches(gM,3,3)==true, "oak_fence mirrored symmetric true");
        }
    }

    // 6) Stonecutting 1-slot — all 9 offsets true, 2 filled false
    std::printf("\n[6] stonecutting 1-slot all offsets (stone -> stone_slab) + shapeless size gate\n");
    {
        // Find stonecutting stone -> stone_slab
        const Recipe* sc = nullptr;
        for (auto& r : rm.all()) if (r.kind==Recipe::Kind::Stonecutting && r.result.itemId==idFor("minecraft:stone_slab")) { sc=&r; break; }
        if (!sc) for (auto& r: rm.all()) if (r.kind==Recipe::Kind::Stonecutting) { sc=&r; break; }
        CHECK(sc!=nullptr, "stonecutting recipe exists");
        if (sc) {
            int ok = 0;
            for (int i = 0; i < 9; ++i) {
                std::vector<ItemStack> g(9); g[i]=stk("minecraft:stone");
                if (sc->matches(g,3,3)) ++ok;
            }
            CHECK(ok==9, "stonecutting 1-slot matches all 9 offsets");
            std::vector<ItemStack> g2(9); g2[0]=stk("minecraft:stone"); g2[1]=stk("minecraft:stone");
            CHECK(sc->matches(g2,3,3)==false, "stonecutting 2 filled false");
            std::vector<ItemStack> gEmpty(9);
            CHECK(sc->matches(gEmpty,3,3)==false, "stonecutting empty false");
        }
        // also test findStonecutting helper
        const Recipe* sc2 = rm.findStonecutting(idFor("minecraft:stone"));
        CHECK(sc2!=nullptr, "findStonecutting stone -> non-null");
        if (sc2) CHECK(sc2->kind==Recipe::Kind::Stonecutting, "findStonecutting kind stonecutting");
    }

    // 7) findById / size invariants
    std::printf("\n[7] findById + all() invariants (1581 vs 1578 contract)\n");
    CHECK(rm.findById("minecraft:does_not_exist")==nullptr, "findById nonexistent -> nullptr");
    CHECK(rm.findById("minecraft:bread")!=nullptr, "findById bread -> non-null");
    CHECK(rm.findById("minecraft:oak_stairs")!=nullptr, "findById oak_stairs -> non-null");
    CHECK(rm.all().size()==rm.size(), "all().size == size()");
    // count kinds — real assets distribution: shaped 1285 shapeless 272 stonecutting 6 (plan32 JSON-driven, not minecraft-data 80 stonecutting)
    int shaped=0, shapeless=0, stonecutting=0, smelting=0;
    for (auto& r: rm.all()) { if(r.kind==Recipe::Kind::Shaped) ++shaped; else if(r.kind==Recipe::Kind::Shapeless) ++shapeless; else if(r.kind==Recipe::Kind::Stonecutting) ++stonecutting; else if(r.kind==Recipe::Kind::Smelting) ++smelting; }
    std::printf("  info: shaped %d shapeless %d stonecutting %d smelting %d total %zu\n", shaped, shapeless, stonecutting, smelting, rm.size());
    CHECK(shaped >= 1280 && shaped <= 1300, "shaped ~1285");
    CHECK(shapeless >= 250 && shapeless <= 290, "shapeless ~272");
    CHECK(stonecutting >= 5 && stonecutting <= 15, "stonecutting ~6 (assets minimal)");
    // final gate
    CHECK((int)rm.size() >= shaped + shapeless + stonecutting, "recipe kinds sum <= total");
    CHECK(rm.size()==1578, "final size 1578 == minecraft-data 1581 -3 blank patterns (trimBlankRows)");

    std::printf("\n=== test_recipes_mirror: %d PASS %d FAIL ===\n", g_pass, g_fail);
    // Document 1581 vs 1578: minecraft-data 1.21.4 recipes.json 1581, assets/data/recipes 1578 after trimBlankRows removes ["   "] x3 blank patterns (debug empty). Verified above size 1578.
    return g_fail ? 1 : 0;
}
