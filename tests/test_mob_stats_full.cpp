// test_mob_stats_full — plan44 G-05 (assessment-6 G-05 mob attributes 149 species).
// Reads docs/mob_stats_149.csv (single source of truth, plan44 §5) and verifies
// every one of the 149 kMobStats rows: name order, HP/speed/attack/followRange,
// XP, loot item/range, breeding item. mismatch must be 0.
// Plus hand-written vanilla spot checks (independent of the CSV/table):
// famous HP/attack/XP/loot values, follow_range overrides, slime size table,
// XP 30 species, loot 30+ species, hostile/daylight flags.
// Speed unit is repo-local AI scale (vanilla ordering); HP/attack/XP are vanilla-exact.
// Run: ./build/test_mob_stats_full  (no server needed)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/Entities.hpp"

using namespace cppfm;

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, msg) do { \
    bool c_ = static_cast<bool>(cond); \
    std::printf("  %s  %s\n", c_ ? " ok " : "FAIL", msg); \
    if (c_) ++g_pass; else ++g_fail; \
} while (0)
#define CHECK_EQ_INT(a, b, msg) do { \
    long long av_ = (long long)(a), bv_ = (long long)(b); \
    char buf_[256]; std::snprintf(buf_, sizeof buf_, "%s (got %lld want %lld)", msg, av_, bv_); \
    CHECK(av_ == bv_, buf_); \
} while (0)
#define CHECK_NEAR(a, b, eps, msg) do { \
    double av_ = (double)(a), bv_ = (double)(b); \
    char buf_[256]; std::snprintf(buf_, sizeof buf_, "%s (got %.4f want %.4f)", msg, av_, bv_); \
    CHECK(std::abs(av_ - bv_) <= (eps), buf_); \
} while (0)

struct CsvRow {
    std::string name;
    double hp = 0, speed = 0, attack = 0, fr = 0;
    long xp = 0;
    std::string drop, breed;
    long dmin = 0, dmax = 0;
    std::string prov;
};

static std::string csvPath() {
    if (const char* e = std::getenv("MOB_STATS_CSV")) return std::string(e);
    namespace fs = std::filesystem;
    fs::path base(__FILE__);
    base = base.parent_path() / ".." / "docs" / "mob_stats_149.csv";
    return base.lexically_normal().string();
}

static std::vector<CsvRow> loadCsv(const std::string& path) {
    std::vector<CsvRow> rows;
    std::ifstream f(path);
    if (!f) { std::printf("FAIL cannot open %s\n", path.c_str()); ++g_fail; return rows; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("name,", 0) == 0) continue;
        std::stringstream ss(line);
        std::vector<std::string> c;
        std::string cell;
        while (std::getline(ss, cell, ',')) c.push_back(cell);
        if (c.size() != 11) {
            char buf[128]; std::snprintf(buf, sizeof buf, "csv col count 11 (got %zu): %s", c.size(), line.c_str());
            CHECK(false, buf);
            continue;
        }
        CsvRow r;
        r.name = c[0];
        r.hp = std::stod(c[1]); r.speed = std::stod(c[2]);
        r.attack = std::stod(c[3]); r.fr = std::stod(c[4]);
        r.xp = std::stol(c[5]);
        r.drop = c[6]; r.dmin = std::stol(c[7]); r.dmax = std::stol(c[8]);
        r.breed = c[9]; r.prov = c[10];
        rows.push_back(r);
    }
    return rows;
}

int main() {
    std::printf("=== test_mob_stats_full — plan44 G-05 (149 species) ===\n");

    // ---- (0) CSV load: 149 rows ----
    std::string path = csvPath();
    std::printf("[csv] %s\n", path.c_str());
    std::vector<CsvRow> rows = loadCsv(path);
    CHECK_EQ_INT((long long)rows.size(), 149, "csv has 149 data rows");

    // ---- (1) full-table sweep: impl vs CSV, mismatch 0 ----
    int mismatch = 0, orderBad = 0;
    int frFilled = 0, frZero = 0;
    for (int i = 0; i < 149 && i < (int)rows.size(); ++i) {
        const MobStats& s = mobStats(static_cast<MobKind>(i));
        const CsvRow& r = rows[i];
        bool rowOk = true;
        if (!s.name || r.name != s.name) {
            if (mismatch < 5) std::printf("  row %d name: impl=%s csv=%s\n",
                i, s.name ? s.name : "(null)", r.name.c_str());
            rowOk = false; ++orderBad;
        }
        auto nz = [](const char* p) { return (p && *p) ? std::string(p) : std::string(""); };
        if (std::abs(s.maxHealth - r.hp) > 1e-6) rowOk = false;
        if (std::abs(s.moveSpeed - r.speed) > 1e-6) rowOk = false;
        if (std::abs(s.attackDamage - r.attack) > 1e-6) rowOk = false;
        if (std::abs(s.followRange - r.fr) > 1e-6) rowOk = false;
        if ((long long)s.xpDrop != r.xp) rowOk = false;
        if (nz(s.dropItem) != r.drop) rowOk = false;
        if (s.dropMin != r.dmin || s.dropMax != r.dmax) rowOk = false;
        if (nz(s.breedingItem) != r.breed) rowOk = false;
        if (!rowOk) {
            ++mismatch;
            if (mismatch <= 5) std::printf("  row %d values differ: impl(%s %.2f %.3f %.2f fr%.1f xp%u) csv(%.2f %.3f %.2f fr%.1f xp%ld)\n",
                i, s.name, s.maxHealth, s.moveSpeed, s.attackDamage, s.followRange, s.xpDrop,
                r.hp, r.speed, r.attack, r.fr, r.xp);
        }
        if (s.followRange > 0) ++frFilled; else ++frZero;
    }
    CHECK_EQ_INT(mismatch, 0, "G-05(1): 149 species HP/speed/attack/FR/xp/loot mismatch 0");
    CHECK_EQ_INT(orderBad, 0, "table order matches MobKind index (no misalignment)");
    std::printf("  .. followRange filled=%d zero(non-living)=%d\n", frFilled, frZero);
    CHECK(frFilled >= 80, "followRange filled for all living kinds (>=80)");
    CHECK(frZero >= 60, "followRange 0 only for non-living (vehicles/projectiles/displays)");

    // ---- (2) followRange vanilla overrides (hand-written, CSV-independent) ----
    auto fr = [](MobKind k) { return mobStats(k).followRange; };
    CHECK_NEAR(fr(MobKind::Zombie), 35, 1e-6, "zombie follow_range 35 (vanilla)");
    CHECK_NEAR(fr(MobKind::Husk), 35, 1e-6, "husk 35 (zombie family)");
    CHECK_NEAR(fr(MobKind::Drowned), 35, 1e-6, "drowned 35");
    CHECK_NEAR(fr(MobKind::ZombieVillager), 35, 1e-6, "zombie_villager 35");
    CHECK_NEAR(fr(MobKind::ZombifiedPiglin), 35, 1e-6, "zombified_piglin 35");
    CHECK_NEAR(fr(MobKind::Enderman), 64, 1e-6, "enderman 64");
    CHECK_NEAR(fr(MobKind::Ghast), 100, 1e-6, "ghast 100");
    CHECK_NEAR(fr(MobKind::Blaze), 48, 1e-6, "blaze 48");
    CHECK_NEAR(fr(MobKind::Wither), 40, 1e-6, "wither 40");
    CHECK_NEAR(fr(MobKind::EnderDragon), 64, 1e-6, "ender_dragon 64");
    CHECK_NEAR(fr(MobKind::Witch), 32, 1e-6, "witch 32");
    CHECK_NEAR(fr(MobKind::Evoker), 12, 1e-6, "evoker 12");
    CHECK_NEAR(fr(MobKind::Illusioner), 12, 1e-6, "illusioner 12");
    CHECK_NEAR(fr(MobKind::Vex), 64, 1e-6, "vex 64");
    CHECK_NEAR(fr(MobKind::Pillager), 32, 1e-6, "pillager 32");
    CHECK_NEAR(fr(MobKind::Ravager), 32, 1e-6, "ravager 32");
    CHECK_NEAR(fr(MobKind::Hoglin), 32, 1e-6, "hoglin 32");
    CHECK_NEAR(fr(MobKind::Zoglin), 32, 1e-6, "zoglin 32");
    CHECK_NEAR(fr(MobKind::Breeze), 24, 1e-6, "breeze 24");
    CHECK_NEAR(fr(MobKind::Creaking), 32, 1e-6, "creaking 32 (heart link)");
    CHECK_NEAR(fr(MobKind::Skeleton), 16, 1e-6, "skeleton 16");
    CHECK_NEAR(fr(MobKind::Spider), 16, 1e-6, "spider 16");
    CHECK_NEAR(fr(MobKind::Creeper), 16, 1e-6, "creeper 16");
    // non-living: raw 0 + perceive fallback 16
    CHECK_NEAR(fr(MobKind::Arrow), 0, 1e-6, "arrow followRange 0 (N/A)");
    CHECK_NEAR(fr(MobKind::Tnt), 0, 1e-6, "tnt followRange 0 (N/A)");
    CHECK_NEAR(fr(MobKind::Boat), 0, 1e-6, "boat followRange 0 (vehicle)");
    CHECK_NEAR(fr(MobKind::Item), 0, 1e-6, "item followRange 0 (N/A)");
    CHECK_NEAR(perceiveDist(MobKind::Arrow), 16, 1e-6, "perceiveDist fallback 16 for non-living");
    CHECK_NEAR(perceptionRange2(MobKind::Zombie), 35*35, 1e-6, "perceptionRange2 zombie 1225");
    CHECK_NEAR(perceptionRange2(MobKind::Ghast), 100*100, 1e-6, "perceptionRange2 ghast 10000");

    // irregular vanilla id: enum EnderCrystal <-> "minecraft:end_crystal" (NOT ender_;
    // crystal is the correct entity id per kEntities — do not "fix" the spelling)
    CHECK(mobStats(MobKind::EnderCrystal).name &&
          std::string(mobStats(MobKind::EnderCrystal).name) == "minecraft:end_crystal",
          "end_crystal id spelling (vanilla irregular, kEntities type 43)");

    // ---- famous HP / attack (vanilla) ----
    auto hp = [](MobKind k) { return mobStats(k).maxHealth; };
    auto atk = [](MobKind k) { return mobStats(k).attackDamage; };
    CHECK_NEAR(hp(MobKind::Warden), 500, 1e-6, "warden HP 500");
    CHECK_NEAR(hp(MobKind::Wither), 300, 1e-6, "wither HP 300");
    CHECK_NEAR(hp(MobKind::EnderDragon), 200, 1e-6, "dragon HP 200");
    CHECK_NEAR(hp(MobKind::IronGolem), 100, 1e-6, "iron_golem HP 100");
    CHECK_NEAR(hp(MobKind::Ravager), 100, 1e-6, "ravager HP 100");
    CHECK_NEAR(hp(MobKind::Enderman), 40, 1e-6, "enderman HP 40");
    CHECK_NEAR(hp(MobKind::Zombie), 20, 1e-6, "zombie HP 20");
    CHECK_NEAR(hp(MobKind::Creeper), 20, 1e-6, "creeper HP 20");
    CHECK_NEAR(hp(MobKind::Slime), 4, 1e-6, "slime base HP 4 (size 1)");
    CHECK_NEAR(atk(MobKind::Warden), 30, 1e-6, "warden attack 30");
    CHECK_NEAR(atk(MobKind::IronGolem), 15, 1e-6, "iron_golem attack 15");
    CHECK_NEAR(atk(MobKind::Enderman), 7, 1e-6, "enderman attack 7");
    CHECK_NEAR(atk(MobKind::Zombie), 3, 1e-6, "zombie attack 3");
    CHECK_NEAR(atk(MobKind::Creeper), 0, 1e-6, "creeper attack 0 (explodes)");

    // ---- slime / magma_cube size table ----
    CHECK_NEAR(mobStats(MobKind::Slime, 0).maxHealth, 1, 1e-6, "slime size0 HP 1");
    CHECK_NEAR(mobStats(MobKind::Slime, 1).maxHealth, 4, 1e-6, "slime size1 HP 4");
    CHECK_NEAR(mobStats(MobKind::Slime, 2).maxHealth, 16, 1e-6, "slime size2 HP 16");
    CHECK_NEAR(mobStats(MobKind::Slime, 0).attackDamage, 1, 1e-6, "slime size0 atk 1");
    CHECK_NEAR(mobStats(MobKind::Slime, 2).attackDamage, 4, 1e-6, "slime size2 atk 4");
    CHECK_NEAR(mobStats(MobKind::MagmaCube, 0).attackDamage, 3, 1e-6, "magma size0 atk 3 (s+2)");
    CHECK_NEAR(mobStats(MobKind::MagmaCube, 2).attackDamage, 6, 1e-6, "magma size2 atk 6 (s+2)");
    CHECK_NEAR(mobStats(MobKind::Slime, 2).followRange, 16, 1e-6, "slime size overload keeps followRange");

    // ---- (3) XP: 30 species ----
    auto xp = [](MobKind k) { return mobStats(k).xpDrop; };
    const std::pair<MobKind, std::uint32_t> xpExp[] = {
        {MobKind::Zombie,5},{MobKind::Skeleton,5},{MobKind::Spider,5},
        {MobKind::Creeper,5},{MobKind::Enderman,5},{MobKind::Witch,5},
        {MobKind::Blaze,10},{MobKind::Ghast,5},{MobKind::Wither,50},
        {MobKind::EnderDragon,12000},{MobKind::Guardian,10},{MobKind::ElderGuardian,10},
        {MobKind::Evoker,10},{MobKind::Vex,3},{MobKind::Endermite,3},
        {MobKind::Silverfish,5},{MobKind::Pig,1},{MobKind::Cow,1},
        {MobKind::Hoglin,5},{MobKind::Piglin,5},{MobKind::PiglinBrute,20},
        {MobKind::Pillager,5},{MobKind::Vindicator,5},{MobKind::Ravager,20},
        {MobKind::Shulker,5},{MobKind::Warden,5},{MobKind::Zoglin,5},
        {MobKind::Drowned,5},{MobKind::Husk,5},{MobKind::Stray,5},
    };
    for (auto& e : xpExp) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "xp %s == %u", mobStats(e.first).name, e.second);
        CHECK(xp(e.first) == e.second, buf);
    }

    // ---- (4) loot: 31 species contain expected item ----
    const std::pair<MobKind, const char*> lootExp[] = {
        {MobKind::Pig,"minecraft:porkchop"},{MobKind::Cow,"minecraft:beef"},
        {MobKind::Sheep,"minecraft:mutton"},{MobKind::Chicken,"minecraft:chicken"},
        {MobKind::Zombie,"minecraft:rotten_flesh"},{MobKind::Skeleton,"minecraft:bone"},
        {MobKind::Spider,"minecraft:string"},{MobKind::Enderman,"minecraft:ender_pearl"},
        {MobKind::Witch,"minecraft:glowstone_dust"},{MobKind::Blaze,"minecraft:blaze_rod"},
        {MobKind::Ghast,"minecraft:ghast_tear"},{MobKind::Guardian,"minecraft:prismarine_shard"},
        {MobKind::Evoker,"minecraft:totem_of_undying"},{MobKind::Wither,"minecraft:nether_star"},
        {MobKind::MagmaCube,"minecraft:magma_cream"},{MobKind::Slime,"minecraft:slime_ball"},
        {MobKind::Squid,"minecraft:ink_sac"},{MobKind::GlowSquid,"minecraft:glow_ink_sac"},
        {MobKind::Phantom,"minecraft:phantom_membrane"},{MobKind::IronGolem,"minecraft:iron_ingot"},
        {MobKind::Shulker,"minecraft:shulker_shell"},{MobKind::Warden,"minecraft:sculk_catalyst"},
        {MobKind::Breeze,"minecraft:breeze_rod"},{MobKind::Bogged,"minecraft:arrow"},
        {MobKind::Armadillo,"minecraft:armadillo_scute"},{MobKind::Creaking,"minecraft:resin_clump"},
        {MobKind::Hoglin,"minecraft:porkchop"},{MobKind::Piglin,"minecraft:gold_nugget"},
        {MobKind::Drowned,"minecraft:rotten_flesh"},{MobKind::Stray,"minecraft:bone"},
        {MobKind::CaveSpider,"minecraft:string"},
    };
    for (auto& e : lootExp) {
        const char* got = mobStats(e.first).dropItem;
        char buf[192];
        std::snprintf(buf, sizeof buf, "loot %s contains %s", mobStats(e.first).name, e.second);
        CHECK(got && std::string(got) == e.second, buf);
    }

    // ---- flags: hostile / daylight / breeding ----
    CHECK(mobStats(MobKind::Zombie).hostile, "zombie hostile");
    CHECK(mobStats(MobKind::Creeper).hostile, "creeper hostile");
    CHECK(mobStats(MobKind::Piglin).hostile, "piglin hostile");
    CHECK(!mobStats(MobKind::Cow).hostile, "cow not hostile");
    CHECK(!mobStats(MobKind::Wolf).hostile, "wolf not hostile (neutral)");
    CHECK(mobStats(MobKind::Zombie).burnsInDaylight, "zombie burns");
    CHECK(mobStats(MobKind::Skeleton).burnsInDaylight, "skeleton burns");
    CHECK(mobStats(MobKind::Stray).burnsInDaylight, "stray burns");
    CHECK(!mobStats(MobKind::Creeper).burnsInDaylight, "creeper no burn");
    CHECK(!mobStats(MobKind::Spider).burnsInDaylight, "spider no burn");
    CHECK(MobEntity::breedingItemFor(MobKind::Cow) != 0, "cow breeding item exists");
    CHECK(MobEntity::breedingItemFor(MobKind::Pig) != 0, "pig breeding item exists");

    std::printf("\nPASS %d FAIL %d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
