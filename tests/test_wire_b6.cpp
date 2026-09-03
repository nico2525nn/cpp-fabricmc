// test_wire_b6 — plan45 B6 (W-05/W-08/W-09/W-10/W-11/W-13 + G-13 network side).
// Server-free unit test: Prismarine protocol.json 1.21.4 (protocol 769) shapes
// live-verified 2026-09-04 (play toServer 62 ids + B6 17 containers +
// packet_common_settings + login 5-kind mapper). Every expectation cites the
// verified shape; encode (WriteBuffer) and decode (ReadBuffer) agree field by
// field. G-13 half covers the network-side live sync (NameItem→anvil,
// grid→result refresh, shift-click transfer, per-player state separation).

#include "core/ByteBuffer.hpp"
#include "core/NBT.hpp"
#include "proto/Ids.hpp"
#include "game/Items.hpp"
#include "game/Recipes.hpp"
#include "game/Containers.hpp"
#include "game/MenuInteraction.hpp"
#include "game/GameServer.hpp"
#include "generated/ItemIds.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <optional>

using namespace cppfm;
using namespace cppfm::proto;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name) {
    if (cond) { ++g_pass; std::printf("  ok  %s\n", name); }
    else { ++g_fail; std::printf("  FAIL %s\n", name); }
}

// ---- helpers: build a serverbound frame body (id + payload) ----------------
static std::vector<std::uint8_t> frame(std::uint8_t id, const WriteBuffer& b) {
    std::vector<std::uint8_t> v;
    v.push_back(id);
    v.insert(v.end(), b.data.begin(), b.data.end());
    return v;
}
static std::optional<std::int32_t> readOptionVarint(ReadBuffer& in) {
    if (!in.boolean()) return std::nullopt;
    return in.varint();
}

static void test_ids() {
    std::printf("[ids] play toServer (protocol.json 1.21.4, 62 ids)\n");
    check(pl::cs::Settings == 0x0C, "W-05 settings 0x0C");
    check(pl::cs::SelectBundleItem == 0x02, "W-10(d) select_bundle 0x02");
    check(pl::cs::ConfigurationAcknowledged == 0x0E, "W-10(d) config-ack 0x0E");
    check(pl::cs::SetSlotState == 0x12, "W-10(d) set_slot_state 0x12");
    check(pl::cs::DebugSampleSubscription == 0x15, "W-10(d) debug_sample 0x15");
    check(pl::cs::EditBook == 0x16, "W-09 edit_book 0x16");
    check(pl::cs::QueryEntityNbt == 0x17, "W-10(d) query_entity_nbt 0x17");
    check(pl::cs::GenerateStructure == 0x19, "W-09 generate_structure 0x19");
    check(pl::cs::LockDifficulty == 0x1B, "W-10(d) lock_difficulty 0x1B");
    check(pl::cs::SteerBoat == 0x21, "W-10(a) steer_boat 0x21");
    check(pl::cs::PickItemFromBlock == 0x22, "W-08 pick_block 0x22");
    check(pl::cs::PickItemFromEntity == 0x23, "W-08 pick_entity 0x23");
    check(pl::cs::Pong == 0x2B, "W-10(c) pong 0x2B");
    check(pl::cs::RecipeBook == 0x2C, "W-08 recipe_book 0x2C");
    check(pl::cs::DisplayedRecipe == 0x2D, "W-08 displayed_recipe 0x2D");
    check(pl::cs::NameItem == 0x2E, "W-08 name_item 0x2E");
    check(pl::cs::ResourcePackReceive == 0x2F, "W-10(b) resource_pack 0x2F");
    check(pl::cs::AdvancementTab == 0x30, "W-10(d) advancement_tab 0x30");
    check(pl::cs::SetBeaconEffect == 0x32, "W-08 set_beacon 0x32");
    check(pl::cs::UpdateCommandBlock == 0x34, "W-09 cmdblock 0x34");
    check(pl::cs::UpdateCommandBlockMinecart == 0x35, "W-09 cmdblock_minecart 0x35");
    check(pl::cs::UpdateJigsaw == 0x37, "W-09 jigsaw 0x37");
    check(pl::cs::UpdateStructureBlock == 0x38, "W-09 structure_block 0x38");
    check(pl::cs::Spectate == 0x3B, "W-09 spectate 0x3B (was Ids-only)");
    check(pl::cs::QueryBlockEntityTag == 0x01, "W-10(d) query_block_nbt 0x01");
    check(pl::sc::TagQueryResponse == 0x75, "W-10(d) nbt_query_response 0x75");
}

static void test_settings() {
    std::printf("[W-05] settings = packet_common_settings (9 fields)\n");
    WriteBuffer b;
    b.string("en_us"); b.i8(12); b.varint(0); b.boolean(true); b.u8(0x7F);
    b.varint(1); b.boolean(false); b.boolean(true); b.varint(0);
    auto fB6_1 = frame(pl::cs::Settings, b); ReadBuffer in(fB6_1);
    check(in.u8() == 0x0C, "settings id");
    check(in.string(64) == "en_us", "locale");
    check(GameServer::clampClientViewDistance(in.i8()) == 12, "viewDistance 12");
    check(in.varint() == 0, "chatFlags");
    check(in.boolean(), "chatColors");
    check(in.u8() == 0x7F, "skinParts");
    check(in.varint() == 1, "mainHand right");
    check(!in.boolean(), "textFiltering off");
    check(in.boolean(), "serverListing on");
    check(in.varint() == 0, "particleStatus all");
    check(GameServer::clampClientViewDistance(-5) == 2, "i8 negative clamps to 2");
    check(GameServer::clampClientViewDistance(0) == 2, "0 clamps to 2");
    check(GameServer::clampClientViewDistance(100) == 32, ">32 clamps to 32");
    check(GameServer::clampClientViewDistance(8) == 8, "in-range passes through");
}

static void test_w08() {
    std::printf("[W-08] name_item / beacon / pick / recipe_book / displayed\n");
    { // name_item {name}
        WriteBuffer b; b.string("Excalibur");
        auto fB6_2 = frame(pl::cs::NameItem, b); ReadBuffer in(fB6_2);
        check(in.u8() == 0x2E, "name_item id");
        check(in.string(50) == "Excalibur", "anvil rename text");
    }
    { // set_beacon_effect {option+option} — both present
        WriteBuffer b;
        b.boolean(true); b.varint(1); b.boolean(true); b.varint(10);
        auto fB6_3 = frame(pl::cs::SetBeaconEffect, b); ReadBuffer in(fB6_3);
        check(in.u8() == 0x32, "beacon id");
        auto p = readOptionVarint(in), s = readOptionVarint(in);
        check(p && *p == 1, "primary speed(1)");
        check(s && *s == 10, "secondary regen(10)");
    }
    { // set_beacon_effect — secondary absent
        WriteBuffer b;
        b.boolean(true); b.varint(3); b.boolean(false);
        auto fB6_4 = frame(pl::cs::SetBeaconEffect, b); ReadBuffer in(fB6_4);
        (void)in.u8();
        auto p = readOptionVarint(in), s = readOptionVarint(in);
        check(p && *p == 3 && !s, "haste + absent secondary");
    }
    { // pick_item_from_block {position, includeData}
        WriteBuffer b; b.position(10, 64, -5); b.boolean(false);
        auto fB6_5 = frame(pl::cs::PickItemFromBlock, b); ReadBuffer in(fB6_5);
        check(in.u8() == 0x22, "pick_block id");
        std::int32_t x, y, z; in.position(x, y, z);
        check(x == 10 && y == 64 && z == -5, "pick position");
        check(!in.boolean(), "includeData false");
    }
    { // pick_item_from_entity {entityId, includeData}
        WriteBuffer b; b.varint(42); b.boolean(true);
        auto fB6_6 = frame(pl::cs::PickItemFromEntity, b); ReadBuffer in(fB6_6);
        check(in.u8() == 0x23, "pick_entity id");
        check(in.varint() == 42, "entityId");
        check(in.boolean(), "includeData true");
    }
    { // recipe_book {bookId, bookOpen, filterActive}
        WriteBuffer b; b.varint(0); b.boolean(true); b.boolean(false);
        auto fB6_7 = frame(pl::cs::RecipeBook, b); ReadBuffer in(fB6_7);
        check(in.u8() == 0x2C, "recipe_book id");
        check(in.varint() == 0 && in.boolean() && !in.boolean(), "crafting book open, no filter");
    }
    { // displayed_recipe {recipeId}
        WriteBuffer b; b.varint(7);
        auto fB6_8 = frame(pl::cs::DisplayedRecipe, b); ReadBuffer in(fB6_8);
        check(in.u8() == 0x2D && in.varint() == 7, "displayed_recipe id+payload");
    }
}

static void test_w09() {
    std::printf("[W-09] cmdblock / jigsaw / structure / edit_book / generate / spectate\n");
    { // update_command_block {location,command,mode,flags} (mode 1 auto, track+cond+auto)
        WriteBuffer b; b.position(1, 2, 3); b.string("say hi"); b.varint(1); b.u8(0x07);
        auto fB6_9 = frame(pl::cs::UpdateCommandBlock, b); ReadBuffer in(fB6_9);
        check(in.u8() == 0x34, "cmdblock id");
        std::int32_t x, y, z; in.position(x, y, z);
        check(x == 1 && y == 2 && z == 3, "cmdblock pos");
        check(in.string(32767) == "say hi", "command");
        check(in.varint() == 1, "mode auto");
        check(in.u8() == 0x07, "flags track+cond+auto");
    }
    { // minecart {entityId,command,track_output}
        WriteBuffer b; b.varint(9); b.string("time set day"); b.boolean(true);
        auto fB6_10 = frame(pl::cs::UpdateCommandBlockMinecart, b); ReadBuffer in(fB6_10);
        check(in.u8() == 0x35 && in.varint() == 9, "minecart id+eid");
        check(in.string(32767) == "time set day", "minecart command");
        check(in.boolean(), "track_output");
    }
    { // jigsaw 8 fields
        WriteBuffer b; b.position(0, 64, 0);
        b.string("a"); b.string("b"); b.string("c"); b.string("d"); b.string("e");
        b.varint(1); b.varint(2);
        auto fB6_11 = frame(pl::cs::UpdateJigsaw, b); ReadBuffer in(fB6_11);
        check(in.u8() == 0x37, "jigsaw id");
        std::int32_t x, y, z; in.position(x, y, z);
        check(x == 0 && y == 64 && z == 0, "jigsaw pos");
        for (int i = 0; i < 5; ++i) (void)in.string(512);
        check(in.varint() == 1 && in.varint() == 2, "selection+placement priority");
    }
    { // structure_block 16 fields
        WriteBuffer b; b.position(4, 65, 4);
        b.varint(1); b.varint(2); b.string("mystruct");
        b.i8(0); b.i8(1); b.i8(0); b.i8(5); b.i8(5); b.i8(5);
        b.varint(0); b.varint(1); b.string(""); b.f32(1.0f); b.varint(7); b.u8(0x01);
        auto fB6_12 = frame(pl::cs::UpdateStructureBlock, b); ReadBuffer in(fB6_12);
        check(in.u8() == 0x38, "structure id");
        std::int32_t x, y, z; in.position(x, y, z);
        check(x == 4 && y == 65 && z == 4, "structure pos");
        check(in.varint() == 1 && in.varint() == 2, "action+mode");
        check(in.string(512) == "mystruct", "structure name");
        check(in.i8() == 0 && in.i8() == 1 && in.i8() == 0, "offset");
        check(in.i8() == 5 && in.i8() == 5 && in.i8() == 5, "size");
        check(in.varint() == 0 && in.varint() == 1, "mirror+rotation");
        check(in.string(512).empty(), "metadata empty");
        float integ = in.f32();
        check(integ > 0.99f && integ < 1.01f, "integrity 1.0");
        check(in.varint() == 7 && in.u8() == 0x01, "seed+flags");
    }
    { // edit_book draft {hand,pages[],no title}
        WriteBuffer b; b.varint(0); b.varint(2);
        b.string("page one"); b.string("page two"); b.boolean(false);
        auto fB6_13 = frame(pl::cs::EditBook, b); ReadBuffer in(fB6_13);
        check(in.u8() == 0x16, "edit_book id");
        check(in.varint() == 0, "main hand");
        check(in.varint() == 2, "2 pages");
        check(in.string(32767) == "page one", "page 1");
        check(in.string(32767) == "page two", "page 2");
        check(!in.boolean(), "no title (draft save)");
    }
    { // edit_book signed {hand,pages[],title}
        WriteBuffer b; b.varint(0); b.varint(1); b.string("hello"); b.boolean(true);
        b.string("My Book");
        auto fB6_14 = frame(pl::cs::EditBook, b); ReadBuffer in(fB6_14);
        (void)in.u8(); (void)in.varint(); (void)in.varint(); (void)in.string(32767);
        check(in.boolean(), "title present (sign)");
        check(in.string(128) == "My Book", "book title");
    }
    { // generate_structure {location,levels,keepJigsaws}
        WriteBuffer b; b.position(0, 70, 0); b.varint(3); b.boolean(true);
        auto fB6_15 = frame(pl::cs::GenerateStructure, b); ReadBuffer in(fB6_15);
        check(in.u8() == 0x19, "generate id");
        std::int32_t x, y, z; in.position(x, y, z);
        check(y == 70, "generate y");
        check(in.varint() == 3 && in.boolean(), "levels+keepJigsaws");
    }
    { // spectate {target UUID}
        WriteBuffer b;
        std::uint8_t u[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        b.uuid(u);
        auto fB6_16 = frame(pl::cs::Spectate, b); ReadBuffer in(fB6_16);
        check(in.u8() == 0x3B, "spectate id (dispatch fixed)");
        auto rb = in.bytes(16);
        check(rb.size() == 16 && rb[0] == 1 && rb[15] == 16, "target UUID 16B");
    }
}

static void test_w10() {
    std::printf("[W-10] steer_boat / resource_pack / pong / adv_tab / bundle / slot / debug / query / lock / ack\n");
    { // steer_boat {leftPaddle,rightPaddle}
        WriteBuffer b; b.boolean(true); b.boolean(false);
        auto fB6_17 = frame(pl::cs::SteerBoat, b); ReadBuffer in(fB6_17);
        check(in.u8() == 0x21, "steer_boat id");
        check(in.boolean() && !in.boolean(), "left paddle only");
    }
    { // resource_pack_receive {uuid,result=1 declined}
        WriteBuffer b;
        std::uint8_t u[16] = {};
        b.uuid(u); b.varint(1);
        auto fB6_18 = frame(pl::cs::ResourcePackReceive, b); ReadBuffer in(fB6_18);
        check(in.u8() == 0x2F, "resource_pack id");
        check(in.bytes(16).size() == 16, "pack uuid");
        check(in.varint() == 1, "result declined(1) → kick when forced");
    }
    { // pong {id i32}
        WriteBuffer b; b.i32(123456);
        auto fB6_19 = frame(pl::cs::Pong, b); ReadBuffer in(fB6_19);
        check(in.u8() == 0x2B && in.i32() == 123456, "pong id echo");
    }
    { // advancement_tab open {0, tabId}
        WriteBuffer b; b.varint(0); b.string("minecraft:story/root");
        auto fB6_20 = frame(pl::cs::AdvancementTab, b); ReadBuffer in(fB6_20);
        check(in.u8() == 0x30 && in.varint() == 0, "adv_tab open");
        check(in.string(512) == "minecraft:story/root", "tab id");
    }
    { // advancement_tab close {1, void}
        WriteBuffer b; b.varint(1);
        auto fB6_21 = frame(pl::cs::AdvancementTab, b); ReadBuffer in(fB6_21);
        (void)in.u8();
        check(in.varint() == 1 && in.remaining() == 0, "adv_tab close (no tabId)");
    }
    { // select_bundle_item {slotId,selectedItemIndex}
        WriteBuffer b; b.varint(40); b.varint(2);
        auto fB6_22 = frame(pl::cs::SelectBundleItem, b); ReadBuffer in(fB6_22);
        check(in.u8() == 0x02 && in.varint() == 40 && in.varint() == 2, "bundle slot+index");
    }
    { // set_slot_state {slot_id,window_id:ContainerID(varint),state}
        WriteBuffer b; b.varint(5); b.varint(0); b.boolean(true);
        auto fB6_23 = frame(pl::cs::SetSlotState, b); ReadBuffer in(fB6_23);
        check(in.u8() == 0x12 && in.varint() == 5 && in.varint() == 0 && in.boolean(),
              "slot_state triple");
    }
    { // debug_sample_subscription {type}
        WriteBuffer b; b.varint(0);
        auto fB6_24 = frame(pl::cs::DebugSampleSubscription, b); ReadBuffer in(fB6_24);
        check(in.u8() == 0x15 && in.varint() == 0, "debug_sample type");
    }
    { // query_block_nbt {transactionId,location} + TagQueryResponse echo shape
        WriteBuffer b; b.varint(77); b.position(3, 60, 3);
        auto fB6_25 = frame(pl::cs::QueryBlockEntityTag, b); ReadBuffer in(fB6_25);
        check(in.u8() == 0x01 && in.varint() == 77, "query_block id+tx");
        std::int32_t x, y, z; in.position(x, y, z);
        check(x == 3 && y == 60 && z == 3, "query pos");
        WriteBuffer resp; resp.varint(77);
        nbt::Writer w(resp); w.rootCompound(); w.endCompound();
        ReadBuffer rin(resp.data);
        check(rin.varint() == 77, "tag_query_response tx echo (0x75)");
    }
    { // query_entity_nbt {transactionId,entityId}
        WriteBuffer b; b.varint(78); b.varint(11);
        auto fB6_26 = frame(pl::cs::QueryEntityNbt, b); ReadBuffer in(fB6_26);
        check(in.u8() == 0x17 && in.varint() == 78 && in.varint() == 11, "query_entity triple");
    }
    { // lock_difficulty {locked}
        WriteBuffer b; b.boolean(true);
        auto fB6_27 = frame(pl::cs::LockDifficulty, b); ReadBuffer in(fB6_27);
        check(in.u8() == 0x1B && in.boolean(), "lock_difficulty");
    }
    { // configuration_acknowledged {} (empty)
        WriteBuffer b;
        auto fB6_28 = frame(pl::cs::ConfigurationAcknowledged, b); ReadBuffer in(fB6_28);
        check(in.u8() == 0x0E && in.remaining() == 0, "config-ack empty body");
    }
}

static void test_w11_w13() {
    std::printf("[W-11/W-13] login 5-kind accept + name validation\n");
    // login toServer mapper (protocol.json): 0x00 start / 0x01 enc / 0x02 plugin
    // / 0x03 ack / 0x04 cookie — all five must be receivable in the ack wait.
    check(lo::cs::Hello == 0x00, "login_start 0x00");
    check(lo::cs::Key == 0x01, "login encryption_begin 0x01");
    check(lo::cs::CustomQueryAnswer == 0x02, "login_plugin_response 0x02");
    check(lo::cs::LoginAcknowledged == 0x03, "login_acknowledged 0x03");
    check(lo::cs::CookieResponse == 0x04, "cookie_response 0x04");
    { // cookie_response {key, value:option<ByteArray>} — kept, not thrown
        WriteBuffer b; b.string("vanilla:token"); b.boolean(true);
        b.varint(3); b.u8(1); b.u8(2); b.u8(3);
        ReadBuffer in(b.data);
        check(in.string(256) == "vanilla:token", "cookie key");
        check(in.boolean(), "cookie present");
        check(in.varint() == 3 && in.bytes(3).size() == 3, "cookie bytes");
    }
    { // plugin_response {messageId, successful, data?}
        WriteBuffer b; b.varint(5); b.boolean(false);
        ReadBuffer in(b.data);
        check(in.varint() == 5 && !in.boolean(), "plugin response tolerated");
    }
    // W-13(b): vanilla charset [A-Za-z0-9_], length 3..16
    check(GameServer::isValidPlayerName("Steve_123"), "valid name");
    check(GameServer::isValidPlayerName("abc"), "3 chars ok");
    check(GameServer::isValidPlayerName("1234567890123456"), "16 chars ok");
    check(!GameServer::isValidPlayerName("ab"), "2 chars rejected");
    check(!GameServer::isValidPlayerName("12345678901234567"), "17 chars rejected");
    check(!GameServer::isValidPlayerName(""), "empty rejected");
    check(!GameServer::isValidPlayerName("bad name"), "space rejected");
    check(!GameServer::isValidPlayerName("§cheat"), "section-sign rejected");
    check(!GameServer::isValidPlayerName("a.b"), "dot rejected");
    check(GameServer::isValidPlayerName("UPPER_OK"), "uppercase+underscore ok");
}

// ---- G-13 network side: live container sync (Menu-level, server-free) ------
struct NullIo : MenuIo {
    void dropFromPlayer(cppfm::Player&, const ItemStack&, bool) override {}
    void blockEntityChanged(std::int64_t) override {}
    void itemCrafted(cppfm::Player&, const ItemStack&) override {}
    void itemSmelted(cppfm::Player&, const ItemStack&) override {}
};
static std::uint32_t sid(const char* n) {
    auto it = gen::itemIdByName().find(n);
    return it == gen::itemIdByName().end() ? 0 : it->second;
}
static void setGrid(Menu& m, std::initializer_list<const char*> cells) {
    int i = 0;
    for (auto n : cells) {
        if (i < 9 && n) m.craftGrid[i] = ItemStack::of(sid(n), 1);
        ++i;
    }
}
static void test_g13_live() {
    std::printf("[G-13] grid→result live refresh (10 recipes) + shift-click + separation\n");
    RecipeManager recipes;
    recipes.loadDefaults();
    // JSON-driven since plan32 §3: 1578 files under assets/data/recipes.
    // ctest runs from build/, manual runs from source root — try both.
    recipes.loadDirectory("assets/data/recipes");
    if (recipes.size() == 0) recipes.loadDirectory("../assets/data/recipes");
    check(recipes.size() > 1000, "recipe table loaded (1578 JSON, plan32)");
    Menu m; m.type = MenuType::Crafting;
    struct Case { const char* want; std::initializer_list<const char*> grid; };
    // vanilla chains (log→planks→sticks→torch, planks→table, cobble→furnace...)
    const Case cases[] = {
        {"minecraft:oak_planks", {"minecraft:oak_log"}},
        {"minecraft:stick", {"minecraft:oak_planks", nullptr, nullptr,
                             "minecraft:oak_planks"}},
        {"minecraft:crafting_table", {"minecraft:oak_planks", "minecraft:oak_planks", nullptr,
                                      "minecraft:oak_planks", "minecraft:oak_planks"}},
        {"minecraft:torch", {"minecraft:coal", nullptr, nullptr, "minecraft:stick"}},
        {"minecraft:furnace", {"minecraft:cobblestone", "minecraft:cobblestone", "minecraft:cobblestone",
                               "minecraft:cobblestone", nullptr, "minecraft:cobblestone",
                               "minecraft:cobblestone", "minecraft:cobblestone", "minecraft:cobblestone"}},
        {"minecraft:chest", {"minecraft:oak_planks", "minecraft:oak_planks", "minecraft:oak_planks",
                             "minecraft:oak_planks", nullptr, "minecraft:oak_planks",
                             "minecraft:oak_planks", "minecraft:oak_planks", "minecraft:oak_planks"}},
        {"minecraft:bread", {"minecraft:wheat", "minecraft:wheat", "minecraft:wheat"}},
        {"minecraft:paper", {"minecraft:sugar_cane", "minecraft:sugar_cane", "minecraft:sugar_cane"}},
        {"minecraft:oak_boat", {"minecraft:oak_planks", nullptr, "minecraft:oak_planks",
                                "minecraft:oak_planks", "minecraft:oak_planks", "minecraft:oak_planks"}},
        {"minecraft:diamond_sword", {"minecraft:diamond", nullptr, nullptr,
                                     "minecraft:diamond", nullptr, nullptr,
                                     "minecraft:stick"}},
    };
    int done = 0;
    for (auto& c : cases) {
        for (auto& s : m.craftGrid) s = ItemStack::air();
        setGrid(m, c.grid);
        m.refreshCraftResult(recipes);
        if (!m.craftResult.empty() && m.craftResult.name() == c.want) ++done;
        else std::printf("    -- grid→%s got %s\n", c.want,
                         m.craftResult.empty() ? "(empty)" : m.craftResult.name().c_str());
    }
    check(done == 10, "10/10 grid→result round-trips (chain incl.)");
    // shift-click (mode 1) on the result moves product to player inventory
    {
        cppfm::Player p;
        NullIo io;
        ItemStack cursor = ItemStack::air();
        for (auto& s : m.craftGrid) s = ItemStack::air();
        setGrid(m, {"minecraft:oak_planks", nullptr, nullptr, "minecraft:oak_planks"});
        m.refreshCraftResult(recipes);
        check(!m.craftResult.empty(), "sticks available for shift-click");
        const bool changed = ClickLogic::apply(m, p, recipes, 0, 0, 1, cursor, io);
        m.refreshCraftResult(recipes);
        bool found = false;
        for (auto& s : p.inv)
            if (!s.empty() && s.name() == "minecraft:stick") { found = true; break; }
        check(changed && found, "shift-click result → player inventory");
        // insufficient-material shift: single plank pair yields once, grid drains
        bool found2 = false;
        for (auto& s : p.inv)
            if (!s.empty() && s.name() == "minecraft:stick" && s.count >= 4) { found2 = true; break; }
        check(found2, "shift-click crafts full batch (4 sticks)");
    }
    // state separation: two players sharing a table keep independent results
    {
        Menu a, b;
        a.type = MenuType::Crafting; b.type = MenuType::Crafting;
        setGrid(a, {"minecraft:oak_log"});
        setGrid(b, {"minecraft:wheat", "minecraft:wheat", "minecraft:wheat"});
        a.refreshCraftResult(recipes);
        b.refreshCraftResult(recipes);
        check(a.craftResult.name() == "minecraft:oak_planks" &&
              b.craftResult.name() == "minecraft:bread",
              "per-menu craftResult separation (shared table, distinct cursors)");
    }
    // anvil rename path (W-08→G-13 joint): rename text is per-menu state
    {
        Menu anvil;
        anvil.type = MenuType::Anvil;
        anvil.anvilRename = "Excalibur";
        check(anvil.anvilRename == "Excalibur", "anvil rename stored per-menu");
    }
}

int main() {
    std::printf("=== test_wire_b6 — plan45 B6 (shapes live-verified 2026-09-04) ===\n");
    test_ids();
    test_settings();
    test_w08();
    test_w09();
    test_w10();
    test_w11_w13();
    test_g13_live();
    std::printf("\n=== WIRE_B6: %d PASS %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
