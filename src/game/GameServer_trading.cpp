#include "GameServer.hpp"
#include "BlockEntityDrops.hpp"
#include "BlockEntityKinds.hpp"
#include "EntitySupport.hpp"
#include "MetadataTypes.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "StairsHelper.hpp"
#include "EquipmentComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "CombatManager.hpp"
#include "HungerManager.hpp"
#include "MobSpawner.hpp"
#include "MenuLogic.hpp"
#include "PotionBrewing.hpp"
#include "GameServerHelpers.hpp"

namespace cppfm {
using namespace proto;

const std::vector<TradeOffer>& GameServer::tradeTable() {
    using TO = TradeOffer;
    static const std::vector<TradeOffer> table = [] {
        auto id = [](const char* n) {
            auto it = gen::itemIdByName().find(n);
            return it!=gen::itemIdByName().end()? it->second : 0u;
        };
        return std::vector<TO>{
            {id("minecraft:wheat"), 20, 0, 0, id("minecraft:emerald"), 1, 16, 2, 0.05f},
            {id("minecraft:coal"), 15, 0, 0, id("minecraft:emerald"), 1, 16, 2, 0.05f},
            {id("minecraft:emerald"), 1, 0, 0, id("minecraft:bread"), 4, 16, 2, 0.05f},
            {id("minecraft:emerald"), 3, 0, 0, id("minecraft:iron_pickaxe"), 1, 12, 10, 0.05f},
            {id("minecraft:porkchop"), 7, 0, 0, id("minecraft:emerald"), 1, 16, 5, 0.05f},
        };
    }();
    return table;
}
static std::string professionToString(VillagerData::Profession p){
    switch(p){
        case VillagerData::ARMORER: return "minecraft:armorer";
        case VillagerData::BUTCHER: return "minecraft:butcher";
        case VillagerData::CARTOGRAPHER: return "minecraft:cartographer";
        case VillagerData::CLERIC: return "minecraft:cleric";
        case VillagerData::FARMER: return "minecraft:farmer";
        case VillagerData::FISHERMAN: return "minecraft:fisherman";
        case VillagerData::FLETCHER: return "minecraft:fletcher";
        case VillagerData::LEATHERWORKER: return "minecraft:leatherworker";
        case VillagerData::LIBRARIAN: return "minecraft:librarian";
        case VillagerData::MASON: return "minecraft:mason";
        case VillagerData::SHEPHERD: return "minecraft:shepherd";
        case VillagerData::TOOLSMITH: return "minecraft:toolsmith";
        case VillagerData::WEAPONSMITH: return "minecraft:weaponsmith";
        default: return "minecraft:farmer";
    }
}
static const std::unordered_map<std::string, std::array<std::vector<TradeOffer>,5>>& professionTrades(){
    using TO = TradeOffer;
    static std::unordered_map<std::string, std::array<std::vector<TO>,5>> m;
    static bool init=false;
    if(init) return m;
    init=true;
    auto id = [](const char* n)->uint32_t{
        auto it = gen::itemIdByName().find(n);
        return it!=gen::itemIdByName().end()? it->second : 0u;
    };
    auto emerald=id("minecraft:emerald");
    m["minecraft:farmer"] = {{
        std::vector<TO>{{emerald,1,0,0,id("minecraft:bread"),6,16,2,0.05f},{id("minecraft:wheat"),20,0,0,emerald,1,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:pumpkin"),6,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:pumpkin_pie"),4,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:melon_slice"),4,0,0,emerald,1,16,7,0.05f},{emerald,1,0,0,id("minecraft:cookie"),18,12,10,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cake"),1,12,15,0.05f},{emerald,1,0,0,id("minecraft:suspicious_stew"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:golden_carrot"),3,12,30,0.05f},{emerald,1,0,0,id("minecraft:glistering_melon_slice"),3,12,30,0.05f}}
    }};
    m["minecraft:librarian"] = {{
        std::vector<TO>{{emerald,1,0,0,id("minecraft:bookshelf"),1,12,2,0.05f},{id("minecraft:paper"),24,0,0,emerald,1,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:book"),4,0,0,emerald,1,12,5,0.05f},{emerald,5,0,0,id("minecraft:clock"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:written_book"),1,0,0,emerald,1,12,10,0.05f},{emerald,5,0,0,id("minecraft:name_tag"),1,12,10,0.05f}},
        std::vector<TO>{{emerald,8,0,0,id("minecraft:enchanted_book"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,5,0,0,id("minecraft:enchanted_book"),1,12,30,0.20f}}
    }};
    m["minecraft:cleric"] = {{
        std::vector<TO>{{id("minecraft:rotten_flesh"),32,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:redstone"),2,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:gold_ingot"),3,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:glowstone"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit_foot"),1,0,0,emerald,1,12,10,0.05f},{emerald,1,0,0,id("minecraft:ender_pearl"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:scute"),4,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:bottle_o_enchanting"),1,12,15,0.05f}},
        std::vector<TO>{{id("minecraft:nether_wart"),1,0,0,emerald,1,12,30,0.05f},{emerald,1,0,0,id("minecraft:ender_pearl"),1,12,30,0.05f}}
    }};
    m["minecraft:armorer"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:iron_leggings"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_boots"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:lava_bucket"),1,0,0,emerald,1,12,10,0.05f},{emerald,4,0,0,id("minecraft:iron_helmet"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,6,0,0,id("minecraft:diamond_chestplate"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_chestplate"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_boots"),1,12,30,0.05f}}
    }};
    m["minecraft:weaponsmith"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:iron_axe"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_sword"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:iron_sword"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,8,0,0,id("minecraft:diamond_axe"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_sword"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_axe"),1,12,30,0.05f}}
    }};
    m["minecraft:toolsmith"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:stone_axe"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_pickaxe"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:iron_pickaxe"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,6,0,0,id("minecraft:diamond_pickaxe"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_pickaxe"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_shovel"),1,12,30,0.05f}}
    }};
    m["minecraft:butcher"] = {{
        std::vector<TO>{{id("minecraft:chicken"),14,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:cooked_chicken"),8,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:porkchop"),7,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:cooked_porkchop"),5,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:mutton"),7,0,0,emerald,1,16,7,0.05f},{emerald,1,0,0,id("minecraft:cooked_mutton"),7,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit"),4,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:rabbit_stew"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cooked_rabbit"),5,12,30,0.05f},{emerald,1,0,0,id("minecraft:cooked_mutton"),7,12,30,0.05f}}
    }};
    m["minecraft:fisherman"] = {{
        std::vector<TO>{{id("minecraft:string"),20,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:cooked_cod"),6,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:coal"),10,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:cooked_salmon"),6,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:cod"),15,0,0,emerald,1,16,10,0.05f},{emerald,1,0,0,id("minecraft:fishing_rod"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:salmon"),13,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:bucket"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cooked_cod"),6,12,30,0.05f},{emerald,1,0,0,id("minecraft:nautilus_shell"),1,12,30,0.05f}}
    }};
    m["minecraft:fletcher"] = {{
        std::vector<TO>{{id("minecraft:stick"),32,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:arrow"),16,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,16,5,0.05f},{emerald,2,0,0,id("minecraft:bow"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:string"),14,0,0,emerald,1,16,10,0.05f},{emerald,3,0,0,id("minecraft:crossbow"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:feather"),24,0,0,emerald,1,12,15,0.05f},{emerald,3,0,0,id("minecraft:bow"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,5,0,0,id("minecraft:arrow"),16,12,30,0.05f},{emerald,5,0,0,id("minecraft:tipped_arrow"),5,12,30,0.05f}}
    }};
    m["minecraft:leatherworker"] = {{
        std::vector<TO>{{id("minecraft:leather"),6,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:leather_leggings"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),26,0,0,emerald,1,12,5,0.05f},{emerald,2,0,0,id("minecraft:leather_chestplate"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit_hide"),9,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:leather_helmet"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:scute"),6,0,0,emerald,1,12,15,0.05f},{emerald,5,0,0,id("minecraft:leather_chestplate"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,6,0,0,id("minecraft:saddle"),1,12,30,0.05f},{emerald,4,0,0,id("minecraft:leather_horse_armor"),1,12,30,0.05f}}
    }};
    m["minecraft:mason"] = {{
        std::vector<TO>{{id("minecraft:clay_ball"),10,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:brick"),10,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:stone"),20,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:chiseled_stone_bricks"),4,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:granite"),16,0,0,emerald,1,16,10,0.05f},{emerald,1,0,0,id("minecraft:diorite"),1,16,10,0.05f}},
        std::vector<TO>{{id("minecraft:quartz"),12,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:quartz_pillar"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:quartz_block"),1,12,30,0.05f},{emerald,1,0,0,id("minecraft:clay"),10,12,30,0.05f}}
    }};
    m["minecraft:shepherd"] = {{
        std::vector<TO>{{id("minecraft:wool"),18,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:shears"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:dye"),12,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:bed"),1,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:wool"),18,0,0,emerald,1,16,10,0.05f},{emerald,2,0,0,id("minecraft:banner"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:string"),14,0,0,emerald,1,12,15,0.05f},{emerald,3,0,0,id("minecraft:painting"),3,12,15,0.05f}},
        std::vector<TO>{{emerald,3,0,0,id("minecraft:bed"),1,12,30,0.05f},{emerald,2,0,0,id("minecraft:shears"),1,12,30,0.05f}}
    }};
    m["minecraft:cartographer"] = {{
        std::vector<TO>{{id("minecraft:paper"),24,0,0,emerald,1,16,2,0.05f},{emerald,7,0,0,id("minecraft:empty_map"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:glass_pane"),11,0,0,emerald,1,16,5,0.05f},{emerald,7,0,0,id("minecraft:explorer_map"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:compass"),1,0,0,emerald,1,12,10,0.05f},{emerald,7,0,0,id("minecraft:explorer_map"),1,12,10,0.05f}},
        std::vector<TO>{{emerald,12,0,0,id("minecraft:explorer_map"),1,12,15,0.05f},{emerald,7,0,0,id("minecraft:item_frame"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,20,0,0,id("minecraft:explorer_map"),1,12,30,0.05f},{emerald,7,0,0,id("minecraft:banner"),1,12,30,0.05f}}
    }};
    return m;
}
bool GameServer::openTrading(Player& p, MobEntity& v) {
    if (!p.conn) return false;
    const int windowId = ++villagerWindowSeq_;
    WriteBuffer b;
    b.varint(windowId);
    b.varint(menus::kMerchant);
    nbt::writeTextComponent(b, "Villager");
    p.conn->trySendPacket(proto::pl::sc::OpenScreen, b);
    WriteBuffer tl;
    tl.varint(windowId);
    // NITWIT: no trades
    if (v.villagerData.profession == VillagerData::NITWIT) {
        tl.varint(0);
        tl.varint(0); tl.varint(0);
        int lvl = std::clamp(v.villagerData.level,1,5);
        tl.varint(lvl); tl.i32(v.villagerXp); tl.boolean(false);
        p.conn->trySendPacket(proto::pl::sc::TradeList, tl);
        return true;
    }
    int lvl = std::clamp(v.villagerData.level,1,5);
    if (v.villagerLevel != lvl) lvl = std::clamp(v.villagerLevel,1,5);
    std::string profStr = professionToString(v.villagerData.profession);
    const auto& pt = professionTrades();
    const std::vector<TradeOffer>* offersPtr = nullptr;
    std::vector<TradeOffer> fallback;
    auto it = pt.find(profStr);
    if (it != pt.end()) {
        offersPtr = &it->second[lvl-1];
    } else {
        const auto& t = tradeTable();
        int num = std::min<int>((int)t.size(), lvl*2);
        if (num==0) num = std::min<int>((int)t.size(), 2);
        fallback.assign(t.begin(), t.begin()+num);
        offersPtr = &fallback;
    }
    const auto& offers = *offersPtr;
    int num = (int)offers.size();
    // lvl*2 slicing is already per-level vector size <=2*lvl, but enforce min 2 for lvl1 fallback display For farmer lvl1 size 2 already OK
    tl.varint(static_cast<std::int32_t>(num));
    int gossipRep = v.gossip.get(p.uuid);
    for (int i=0;i<num;++i) {
        const auto& t = offers[i];
        float baseMult = t.priceMultiplier;
        const float gossipDiscount = std::min(
            0.5F, static_cast<float>(gossipRep) * 0.02F);
        // villager Workaround: priceMult stays baseMult, specialPrice carries discount
        float priceMult = baseMult;
        int specialPrice = - int(std::floor(gossipDiscount * t.inCount));
        // firstBuy
        tl.varint(static_cast<std::int32_t>(t.inItem));
        tl.varint(t.inCount);
        tl.varint(0);
        ItemStack::of(t.outItem, t.outCount).write(tl);
        if (t.inItem2 != 0) {
            tl.boolean(true);
            tl.varint(static_cast<std::int32_t>(t.inItem2));
            tl.varint(t.inCount2);
            tl.varint(0);
        } else {
            tl.boolean(false);
        }
        tl.boolean(false);
        tl.i32(0);
        tl.i32(t.maxUses);
        tl.i32(t.xp);
        tl.i32(specialPrice);
        tl.f32(priceMult);
        tl.i32(t.demand);
    }
    tl.varint(0);
    tl.varint(0);
    tl.varint(lvl);
    tl.i32(v.villagerXp);
    tl.boolean(true);
    p.conn->trySendPacket(proto::pl::sc::TradeList, tl);
    return true;
}
bool GameServer::selectTrade(Player& p, std::int32_t index,
                             std::int32_t villagerEntityId) {
    // The session and the tick thread can both touch a player's inventory.
    // Resolve the villager first, then perform the complete input/output
    // transaction while holding the player state lock.  No packet, event, or
    // world callback is made under either lock.
    std::unique_lock playerLock(p.stateMtx);
    if (!p.inPlay || p.dead || !p.conn) return false;

    TradeOffer t{};
    std::shared_ptr<MobEntity> villager;
    const auto playerDimension = canonicalDimension(p.dimension);
    const double playerX = p.x;
    const double playerY = p.y;
    const double playerZ = p.z;
    const auto playerUuid = p.uuid;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard mobLock(*candidate->stateMtx);
        if (candidate->kind != MobKind::Villager ||
            canonicalDimension(candidate->dimension) != playerDimension)
            continue;
        if (villagerEntityId >= 0 && candidate->entityId != villagerEntityId)
            continue;
        const double dx = candidate->x - playerX;
        const double dz = candidate->z - playerZ;
        if (dx * dx + dz * dz >= 128.0) {
            if (villagerEntityId >= 0) return false;
            continue;
        }
        villager = candidate;
        if (candidate->villagerData.profession == VillagerData::NITWIT)
            return false;
        const std::string profession =
            professionToString(candidate->villagerData.profession);
        const auto it = professionTrades().find(profession);
        if (it != professionTrades().end()) {
            int level = std::clamp(candidate->villagerData.level, 1, 5);
            if (candidate->villagerLevel != level)
                level = std::clamp(candidate->villagerLevel, 1, 5);
            const auto& offers = it->second[static_cast<std::size_t>(level - 1)];
            if (index < 0 || index >= static_cast<std::int32_t>(offers.size()))
                return false;
            t = offers[static_cast<std::size_t>(index)];
        }
        break;
    }
    if (!villager) {
        // The fallback is kept for the legacy direct GameServer API.  A live
        // merchant session always supplies its entity id and therefore cannot
        // trade against an unrelated nearby/default offer.
        if (villagerEntityId >= 0) return false;
        const auto& trades = tradeTable();
        if (index < 0 || static_cast<std::size_t>(index) >= trades.size())
            return false;
        t = trades[static_cast<std::size_t>(index)];
    }
    auto countItem = [&](std::uint32_t itemId) {
        int total = 0;
        for (const auto& stack : p.inv)
            if (!stack.empty() && stack.itemId == itemId) total += stack.count;
        return total;
    };
    if (countItem(t.inItem) < t.inCount ||
        (t.inItem2 != 0 && countItem(t.inItem2) < t.inCount2))
        return false;

    // Keep a rollback point for the complete transaction.  The preflight
    // count checks above should make input consumption infallible, but the
    // snapshot also protects this boundary if the inventory layout or a
    // future trade rule changes.
    const auto originalInventory = p.inv;
    int need = t.inCount;
    int need2 = 0;
    for (auto& s : p.inv) {
        if (need <= 0) break;
        if (!s.empty() && s.itemId == t.inItem) {
            const int take = std::min<int>(s.count, need);
            s.count = static_cast<std::int16_t>(s.count - take);
            need -= take;
            if (s.count <= 0) s = ItemStack::air();
        }
    }
    if (t.inItem2 != 0) {
        need2 = t.inCount2;
        for (auto& s: p.inv) {
            if (need2<=0) break;
            if (!s.empty() && s.itemId == t.inItem2) {
                const int take = std::min<int>(s.count, need2);
                s.count = static_cast<std::int16_t>(s.count - take);
                need2 -= take;
                if (s.count <= 0) s = ItemStack::air();
            }
        }
    }
    if (need != 0 || (t.inItem2 != 0 && need2 != 0)) {
        p.inv = originalInventory;
        return false;
    }
    // addToInventory works on a trial copy and commits only on success.  If
    // the result cannot fit, restore the inputs so a failed trade is atomic.
    if (!addToInventory(p, t.outItem, t.outCount)) {
        p.inv = originalInventory;
        return false;
    }
    const auto tradeDimension = playerDimension;
    playerLock.unlock();
    resendInventory(p);
    spawnXpOrbsFor(tradeDimension, playerX, playerY + 1, playerZ, 2, &p);
    {
        std::string soldName = "minecraft:emerald";
        for (const auto& [name, id] : gen::itemIdByName())
            if (id == t.inItem) { soldName = name; break; }
        onVillagerTraded(p, soldName, t.inCount);
    }
    broadcastSoundFor(tradeDimension, "minecraft:entity.villager.yes",
                      playerX, playerY, playerZ, .8f, 1.f, "neutral");
    bool villagerLevelledUp = false;
    std::int8_t levelUpDimension = 0;
    double levelUpX = 0.0;
    double levelUpY = 0.0;
    double levelUpZ = 0.0;
    bool stillPresent = false;
    {
        const auto currentMobs = mobsSnapshot();
        stillPresent = villager &&
                       std::find(currentMobs.begin(), currentMobs.end(), villager) !=
                           currentMobs.end();
    }
    if (stillPresent) {
        std::lock_guard mobLock(*villager->stateMtx);
        if (canonicalDimension(villager->dimension) == tradeDimension) {
            const double dx = villager->x - playerX;
            const double dz = villager->z - playerZ;
            if (dx * dx + dz * dz < 64.0) {
                auto& m = *villager;
                m.villagerXp += 3 + (nextRandom()%4);
                m.gossip.add(playerUuid, 2);
                // Level up check: every 10 xp -> level++ (vanilla xp thresholds 10,70 etc simplified)
                if (m.villagerXp >= m.villagerLevel * 10 && m.villagerLevel < 5) {
                    m.setVillagerLevel(m.villagerLevel+1);
                    villagerLevelledUp = true;
                    levelUpDimension = canonicalDimension(m.dimension);
                    levelUpX = m.x;
                    levelUpY = m.y;
                    levelUpZ = m.z;
                } else {
                    m.syncVillagerLevel();
                }
                // Restock: 2/day (vanilla: work POI, 6000-12000 ticks, max 2 per day)
                std::int64_t curDay = tickNo_ / 24000;
                if (curDay != m.villagerLastRestockDay) {
                    m.villagerRestocksToday = 0;
                    m.villagerLastRestockDay = curDay;
                }
                if (m.villagerRestocksToday >= 2) {
                    // already restocked twice today, schedule next day morning
                    m.restockUntil = (curDay+1)*24000 + 2000;
                } else {
                    if (m.restockUntil < tickNo_) {
                        m.restockUntil = tickNo_ + MobEntity::kRestockSecondWindowTicks + (nextRandom()%2000);
                    }
                }
            }
        }
    }
    if (villagerLevelledUp) {
        broadcastSoundFor(levelUpDimension,
                          "minecraft:entity.villager.levelup",
                          levelUpX, levelUpY, levelUpZ, 1.f, 1.f, "neutral");
    }
    return true;
}

} // namespace cppfm
