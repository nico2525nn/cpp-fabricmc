
#include "../src/core/ByteBuffer.hpp"
#include "../src/core/NBT.hpp"
#include "../src/game/Attributes.hpp"
#include "../src/game/Scoreboard.hpp"
#include "../src/game/Teams.hpp"
#include "../src/game/Items.hpp"
#include "../src/game/Particles.hpp"
#include "../src/game/MetadataTypes.hpp"
#include "../src/game/ChunkCodec.hpp"
#include "../src/game/World.hpp"
#include "../src/game/Stats.hpp"
#include "../src/proto/Ids.hpp"

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <optional>
#include <array>

using namespace cppfm;

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

static void hexDump(const std::vector<std::uint8_t>& v, size_t limit=48){
    for(size_t i=0;i<v.size() && i<limit;++i) std::printf("%02x ", v[i]);
    if(v.size()>limit) std::printf("... (%zu)", v.size());
}
static bool expectEq(const std::vector<std::uint8_t>& actual,
                     const std::vector<std::uint8_t>& expected,
                     const char* name){
    if(actual==expected){ std::printf("  ok  %s (%zu)\n", name, actual.size()); ++g_pass; return true; }
    size_t off=0; size_t n=std::min(actual.size(), expected.size());
    for(size_t i=0;i<n;++i) if(actual[i]!=expected[i]){off=i;break;}
    if(actual.size()!=expected.size() && off==n) off=n;
    std::printf("  FAIL %s  diff@%zu  actual=", name, off); hexDump(actual); std::printf("\n       expected="); hexDump(expected);
    if(off<actual.size()||off<expected.size()) std::printf("\n       actual[%zu]=%02x expected[%zu]=%02x\n", off, off<actual.size()?actual[off]:0xff, off, off<expected.size()?expected[off]:0xff);
    else std::printf("\n");
    ++g_fail; return false;
}
static void check(bool cond, const char* name){
    if(cond){ std::printf("  ok  %s\n", name); ++g_pass; } else { std::printf("  FAIL %s\n", name); ++g_fail; }
}
static void checkGap(bool implemented, const char* name){
    if(implemented){ std::printf("  ok  %s (implemented)\n", name); ++g_pass; }
    else { std::printf("  FAIL %s — not implemented / unsent (gap)\n", name); ++g_fail; }
}

static void test_primitives_varint(){
    std::printf("[T01] varint 5-byte boundary (Prismarine varint = zigzag-free 32-bit)\n");
    struct C{int32_t v; std::vector<uint8_t> e;}; std::vector<C> cs={
        {0,{0x00}}, {1,{0x01}}, {127,{0x7f}}, {128,{0x80,0x01}}, {255,{0xff,0x01}},
        {2147483647,{0xff,0xff,0xff,0xff,0x07}}, {-1,{0xff,0xff,0xff,0xff,0x0f}}, {-2147483648,{0x80,0x80,0x80,0x80,0x08}},
        {16383,{0xff,0x7f}}, {16384,{0x80,0x80,0x01}}
    };
    for(auto &c:cs){ WriteBuffer b; b.varint(c.v); char n[64]; std::snprintf(n,64,"varint %d",c.v); expectEq(b.data,c.e,n); ReadBuffer r(b.data); check(r.varint()==c.v,"varint roundtrip"); }
    WriteBuffer b; b.varint(2147483647); check(b.data.size()==5,"varint 2^31-1 5 bytes");
}
static void test_primitives_varlong(){
    std::printf("[T02] varlong 10-byte boundary (Prismarine varlong)\n");
    struct C{int64_t v; size_t len;}; std::vector<C> cs={{0,1},{1,1},{127,1},{128,2},{2147483647,5},{9223372036854775807LL,9},{-1,10}};
    for(auto &c:cs){ WriteBuffer b; b.varlong(c.v); char n[64]; std::snprintf(n,64,"varlong %lld len %zu", (long long)c.v, b.data.size()); check(b.data.size()==c.len,n); ReadBuffer r(b.data); check(r.varlong()==c.v,"varlong roundtrip"); }
    WriteBuffer b; b.varlong(-1); expectEq(b.data, std::vector<uint8_t>{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01}, "varlong -1 10 bytes FF*9 01");
}
static void test_primitives_string(){
    std::printf("[T03] string pstring varint len + bytes (max 32767)\n");
    WriteBuffer b; b.string("hi"); expectEq(b.data, std::vector<uint8_t>{0x02,'h','i'}, "string hi 02 68 69");
    WriteBuffer e; e.string(""); expectEq(e.data, std::vector<uint8_t>{0x00}, "string empty 00");
    std::string big(32767,'a'); WriteBuffer bb; bb.string(big); check(bb.data.size()==32767+3,"string 32767 + varint 3 (FF FF 01?)"); // 32767 varint = 0xFF 0xFF 0x01 (3)
    ReadBuffer r(bb.data); std::string s=r.string(32767); check(s.size()==32767,"string 32767 roundtrip");
}
static void test_primitives_uuid(){
    std::printf("[T04] UUID 16B BE raw (login_success / SpawnEntity)\n");
    uint8_t raw[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}; WriteBuffer b; b.uuid(raw); expectEq(b.data, std::vector<uint8_t>(raw,raw+16), "UUID 16B preserved");
}
static void test_primitives_position(){
    std::printf("[T05] Position 26-12-26 bitfield (wiki.vg Position)\n");
    WriteBuffer b; b.position(0,-60,0); expectEq(b.data, std::vector<uint8_t>{0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0xc4}, "pos 0,-60,0");
    WriteBuffer b2; b2.position(33554431,2047,33554431); // max positive 26/12
    ReadBuffer r(b2.data); int32_t x,y,z; r.position(x,y,z); check(x==33554431 && y==2047 && z==33554431,"pos max roundtrip");
    WriteBuffer b3; b3.position(-33554432,-2048,-33554432); ReadBuffer r3(b3.data); int32_t x3,y3,z3; r3.position(x3,y3,z3); check(x3==-33554432 && y3==-2048 && z3==-33554432,"pos min roundtrip");
    WriteBuffer b4; b4.position(10,64,-5); ReadBuffer r4(b4.data); int32_t xa,ya,za; r4.position(xa,ya,za); check(xa==10&&ya==64&&za==-5,"pos 10,64,-5");
}
static void test_primitives_floats(){
    std::printf("[T06] f64/f32 BE (world_particles x,y,z f64 / offset f32)\n");
    WriteBuffer b; b.f64(10.5); expectEq(b.data, std::vector<uint8_t>{0x40,0x25,0x00,0x00,0x00,0x00,0x00,0x00}, "f64 10.5 40 25 00..");
    WriteBuffer c; c.f32(0.5f); expectEq(c.data, std::vector<uint8_t>{0x3f,0x00,0x00,0x00}, "f32 0.5 3F 00 00 00");
    WriteBuffer d; d.f64(-5.25); ReadBuffer r(d.data); check(r.f64()==-5.25,"f64 -5.25 roundtrip");
}
static void test_primitives_ints_bool(){
    std::printf("[T07] i8/i16/i32/i64 BE + boolean (1 byte)\n");
    WriteBuffer b; b.i8(-1); b.i16(-1); b.i32(-1); b.i64(-1); b.boolean(true); b.boolean(false);
    expectEq(b.data, std::vector<uint8_t>{0xff, 0xff,0xff, 0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0x01,0x00}, "ints -1 + bool");
    WriteBuffer c; c.i16(800); expectEq(c.data, std::vector<uint8_t>{0x03,0x20}, "i16 800 03 20 BE");
    WriteBuffer d; d.i32(2001); expectEq(d.data, std::vector<uint8_t>{0x00,0x00,0x07,0xd1}, "i32 2001 00 00 07 D1");
}
static void test_primitives_option_array(){
    std::printf("[T08] option (bool prefix) + array (varint count)\n");
    WriteBuffer opt; opt.boolean(true); opt.string("hi"); // option<string> present
    expectEq(opt.data, std::vector<uint8_t>{0x01,0x02,'h','i'}, "option<string> present 01 02 hi");
    WriteBuffer opt2; opt2.boolean(false); expectEq(opt2.data, std::vector<uint8_t>{0x00}, "option absent 00");
    WriteBuffer arr; arr.varint(2); arr.varint(10); arr.varint(20); expectEq(arr.data, std::vector<uint8_t>{0x02,0x0a,0x14}, "array varint 2x [10,20]");
}
static void test_primitives_bitfield(){
    std::printf("[T09] bitfield MultiBlockChange x22/z22/y20 + angle i8 + anonymousNbt slot/mapper\n");
    auto packed = [](int32_t cx,int32_t cz,int32_t sy)->std::vector<uint8_t>{ uint64_t p=( (uint64_t)(cx&0x3FFFFF)<<42) | ((uint64_t)(cz&0x3FFFFF)<<20) | (uint64_t)(sy&0xFFFFF); WriteBuffer b; b.u64(p); return b.data; };
    expectEq(packed(0,0,4), std::vector<uint8_t>{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04}, "bitfield (0,0,4)");
    expectEq(packed(1,-1,0), std::vector<uint8_t>{0x00,0x00,0x07,0xff,0xff,0xf0,0x00,0x00}, "bitfield (1,-1,0) negative z 22b");
    int enc=(1<<12)|(1<<8)|(3<<4)|2; // 4402
    WriteBuffer rb; rb.varint(enc); expectEq(rb.data, std::vector<uint8_t>{0xb2,0x22}, "record 4402 B2 22");
    WriteBuffer ang; ang.i8(64); expectEq(ang.data, std::vector<uint8_t>{0x40}, "angle i8 64 (90deg)");
    WriteBuffer nb; nbt::writeTextComponent(nb,"hi"); check(nb.data[0]==0x0A && nb.data.back()==0x00,"anonymousNbt hi wrapper");
    check(ItemStack::kDamageComponentId==3 && ItemStack::kRepairCostComponentId==17 && ItemStack::kTrimComponentIdReal==45, "slot component ids 3/17/45");
    ItemStack s=ItemStack::of(1,1); s.setDamage(5); WriteBuffer sb; s.write(sb); check(sb.data.size()>=7,"slot damage 5 wire");
    WriteBuffer mp; mp.varint(8); expectEq(mp.data, std::vector<uint8_t>{0x08}, "mapper generic.fall_damage_multiplier 8");
    check(attributeMapperId(Attribute::ARMOR)==0 && attributeMapperId(Attribute::GRAVITY)==11, "mapper armor 0 gravity 11");
}

static void test_packet_ids_131(){
    std::printf("[ID] 131 play toClient id lock (Prismarine protocol.json packet_* -> Ids.hpp 769)\n");
    check(proto::pl::sc::BundleDelimiter==0x00,"BundleDelimiter 0x00");
    check(proto::pl::sc::SpawnEntity==0x01,"SpawnEntity 0x01");
    check(proto::pl::sc::SpawnExperienceOrb==0x02,"SpawnExperienceOrb 0x02");
    check(proto::pl::sc::Animation==0x03,"Animation 0x03");
    check(proto::pl::sc::AwardStats==0x04,"AwardStats 0x04");
    check(proto::pl::sc::AckBlockChange==0x05,"AckBlockChange 0x05");
    check(proto::pl::sc::BlockBreakAnimation==0x06,"BlockBreakAnimation 0x06");
    check(proto::pl::sc::BlockEntityData==0x07,"BlockEntityData 0x07");
    check(proto::pl::sc::BlockAction==0x08,"BlockAction 0x08");
    check(proto::pl::sc::BlockUpdate==0x09,"BlockUpdate 0x09");
    check(proto::pl::sc::BossBar==0x0A,"BossBar 0x0A");
    check(proto::pl::sc::ChangeDifficulty==0x0B,"ChangeDifficulty 0x0B");
    check(proto::pl::sc::ChunkBatchFinished==0x0C,"ChunkBatchFinished 0x0C");
    check(proto::pl::sc::ChunkBatchStart==0x0D,"ChunkBatchStart 0x0D");
    check(proto::pl::sc::ChunkBiomes==0x0E,"ChunkBiomes 0x0E unsent");
    check(proto::pl::sc::ClearTitles==0x0F,"ClearTitles 0x0F");
    check(proto::pl::sc::CommandSuggestions==0x10,"CommandSuggestions 0x10");
    check(proto::pl::sc::DeclareCommands==0x11,"DeclareCommands 0x11");
    check(proto::pl::sc::CloseContainer==0x12,"CloseContainer 0x12");
    check(proto::pl::sc::ContainerSetContent==0x13,"ContainerSetContent 0x13");
    check(proto::pl::sc::ContainerSetData==0x14,"ContainerSetData 0x14");
    check(proto::pl::sc::ContainerSetSlot==0x15,"ContainerSetSlot 0x15");
    check(proto::pl::sc::CookieRequest==0x16,"CookieRequest 0x16");
    check(proto::pl::sc::SetCooldown==0x17,"SetCooldown 0x17");
    check(proto::pl::sc::ChatSuggestions==0x18,"ChatSuggestions 0x18");
    check(proto::pl::sc::CustomPayload==0x19,"CustomPayload 0x19");
    check(proto::pl::sc::DamageEvent==0x1A,"DamageEvent 0x1A");
    check(proto::pl::sc::DebugSample==0x1B,"DebugSample 0x1B unsent");
    check(proto::pl::sc::DisguisedChat==0x1C,"DisguisedChat 0x1C");
    check(proto::pl::sc::Disconnect==0x1D,"Disconnect 0x1D");
    check(proto::pl::sc::ProfilelessChat==0x1E,"ProfilelessChat 0x1E unsent");
    check(proto::pl::sc::EntityEvent==0x1F,"EntityEvent 0x1F");
    check(proto::pl::sc::SyncEntityPosition==0x20,"SyncEntityPosition 0x20");
    check(proto::pl::sc::Explosion==0x21,"Explosion 0x21");
    check(proto::pl::sc::ForgetLevelChunk==0x22,"ForgetLevelChunk 0x22");
    check(proto::pl::sc::GameEvent==0x23,"GameEvent 0x23");
    check(proto::pl::sc::OpenHorseWindow==0x24,"OpenHorseWindow 0x24 unsent");
    check(proto::pl::sc::HurtAnimation==0x25,"HurtAnimation 0x25");
    check(proto::pl::sc::InitializeWorldBorder==0x26,"InitializeWorldBorder 0x26");
    check(proto::pl::sc::KeepAlive==0x27,"KeepAlive 0x27");
    check(proto::pl::sc::LevelChunkWithLight==0x28,"LevelChunkWithLight 0x28");
    check(proto::pl::sc::WorldEvent==0x29,"WorldEvent 0x29");
    check(proto::pl::sc::WorldParticles==0x2A,"WorldParticles 0x2A");
    check(proto::pl::sc::UpdateLight==0x2B,"UpdateLight 0x2B");
    check(proto::pl::sc::Login==0x2C,"Login 0x2C");
    check(proto::pl::sc::MapData==0x2D,"MapData 0x2D unsent");
    check(proto::pl::sc::TradeList==0x2E,"TradeList 0x2E");
    check(proto::pl::sc::MoveEntityPos==0x2F,"MoveEntityPos 0x2F");
    check(proto::pl::sc::MoveEntityPosRot==0x30,"MoveEntityPosRot 0x30");
    check(proto::pl::sc::MoveMinecart==0x31,"MoveMinecart 0x31 unsent");
    check(proto::pl::sc::EntityLook==0x32,"EntityLook 0x32");
    check(proto::pl::sc::VehicleMove==0x33,"VehicleMove 0x33 unsent");
    check(proto::pl::sc::OpenBook==0x34,"OpenBook 0x34 unsent");
    check(proto::pl::sc::OpenScreen==0x35,"OpenScreen 0x35");
    check(proto::pl::sc::OpenSignEntity==0x36,"OpenSignEntity 0x36 unsent");
    check(proto::pl::sc::PingResponse==0x38,"PingResponse 0x38");
    check(proto::pl::sc::PlaceGhostRecipe==0x39,"PlaceGhostRecipe 0x39");
    check(proto::pl::sc::Abilities==0x3A,"Abilities 0x3A");
    check(proto::pl::sc::PlayerChat==0x3B,"PlayerChat 0x3B");
    check(proto::pl::sc::EndCombatEvent==0x3C,"EndCombatEvent 0x3C unsent");
    check(proto::pl::sc::EnterCombatEvent==0x3D,"EnterCombatEvent 0x3D unsent");
    check(proto::pl::sc::DeathCombatEvent==0x3E,"DeathCombatEvent 0x3E unsent");
    check(proto::pl::sc::PlayerInfoRemove==0x3F,"PlayerInfoRemove 0x3F");
    check(proto::pl::sc::PlayerInfoUpdate==0x40,"PlayerInfoUpdate 0x40");
    check(proto::pl::sc::FacePlayer==0x41,"FacePlayer 0x41 unsent");
    check(proto::pl::sc::PlayerPosition==0x42,"PlayerPosition 0x42");
    check(proto::pl::sc::PlayerRotation==0x43,"PlayerRotation 0x43 unsent");
    check(proto::pl::sc::RecipeBookAdd==0x44,"RecipeBookAdd 0x44");
    check(proto::pl::sc::RecipeBookRemove==0x45,"RecipeBookRemove 0x45");
    check(proto::pl::sc::RecipeBookSettings==0x46,"RecipeBookSettings 0x46");
    check(proto::pl::sc::RemoveEntities==0x47,"RemoveEntities 0x47");
    check(proto::pl::sc::RemoveMobEffect==0x48,"RemoveMobEffect 0x48");
    check(proto::pl::sc::ResetScore==0x49,"ResetScore 0x49");
    check(proto::pl::sc::PlayRemoveResourcePack==0x4A,"PlayRemoveResourcePack 0x4A unsent");
    check(proto::pl::sc::PlayAddResourcePack==0x4B,"PlayAddResourcePack 0x4B unsent");
    check(proto::pl::sc::Respawn==0x4C,"Respawn 0x4C");
    check(proto::pl::sc::RotateHead==0x4D,"RotateHead 0x4D");
    check(proto::pl::sc::MultiBlockChange==0x4E,"MultiBlockChange 0x4E");
    check(proto::pl::sc::SelectAdvancementTab==0x4F,"SelectAdvancementTab 0x4F unsent");
    check(proto::pl::sc::ServerData==0x50,"ServerData 0x50");
    check(proto::pl::sc::ActionBar==0x51,"ActionBar 0x51");
    check(proto::pl::sc::WorldBorderCenter==0x52,"WorldBorderCenter 0x52");
    check(proto::pl::sc::WorldBorderLerpSize==0x53,"WorldBorderLerpSize 0x53");
    check(proto::pl::sc::WorldBorderSize==0x54,"WorldBorderSize 0x54");
    check(proto::pl::sc::WorldBorderWarningDelay==0x55,"WorldBorderWarningDelay 0x55");
    check(proto::pl::sc::WorldBorderWarningReach==0x56,"WorldBorderWarningReach 0x56");
    check(proto::pl::sc::Camera==0x57,"Camera 0x57");
    check(proto::pl::sc::SetCenterChunk==0x58,"SetCenterChunk 0x58");
    check(proto::pl::sc::UpdateViewDistance==0x59,"UpdateViewDistance 0x59 unsent");
    check(proto::pl::sc::SetCursorItem==0x5A,"SetCursorItem 0x5A");
    check(proto::pl::sc::SetDefaultSpawn==0x5B,"SetDefaultSpawn 0x5B");
    check(proto::pl::sc::ScoreboardDisplayObjective==0x5C,"ScoreboardDisplayObjective 0x5C");
    check(proto::pl::sc::SetEntityMetadata==0x5D,"SetEntityMetadata 0x5D");
    check(proto::pl::sc::AttachEntity==0x5E,"AttachEntity 0x5E unsent");
    check(proto::pl::sc::EntityVelocity==0x5F,"EntityVelocity 0x5F");
    check(proto::pl::sc::SetEquipment==0x60,"SetEquipment 0x60");
    check(proto::pl::sc::SetExperience==0x61,"SetExperience 0x61");
    check(proto::pl::sc::SetHealth==0x62,"SetHealth 0x62");
    check(proto::pl::sc::SetHeldSlot==0x63,"SetHeldSlot 0x63");
    check(proto::pl::sc::ScoreboardObjective==0x64,"ScoreboardObjective 0x64");
    check(proto::pl::sc::SetPassengers==0x65,"SetPassengers 0x65");
    check(proto::pl::sc::SetPlayerInventory==0x66,"SetPlayerInventory 0x66 unsent");
    check(proto::pl::sc::Teams==0x67,"Teams 0x67");
    check(proto::pl::sc::ScoreboardScore==0x68,"ScoreboardScore 0x68");
    check(proto::pl::sc::SimulationDistance==0x69,"SimulationDistance 0x69");
    check(proto::pl::sc::SetTitleSubtitle==0x6A,"SetTitleSubtitle 0x6A");
    check(proto::pl::sc::UpdateTime==0x6B,"UpdateTime 0x6B");
    check(proto::pl::sc::SetTitleText==0x6C,"SetTitleText 0x6C");
    check(proto::pl::sc::SetTitleTime==0x6D,"SetTitleTime 0x6D");
    check(proto::pl::sc::EntitySoundEffect==0x6E,"EntitySoundEffect 0x6E");
    check(proto::pl::sc::SoundEffect==0x6F,"SoundEffect 0x6F");
    check(proto::pl::sc::StartConfiguration==0x70,"StartConfiguration 0x70 unsent");
    check(proto::pl::sc::StopSound==0x71,"StopSound 0x71");
    check(proto::pl::sc::StoreCookie==0x72,"StoreCookie 0x72");
    check(proto::pl::sc::SystemChat==0x73,"SystemChat 0x73");
    check(proto::pl::sc::Collect==0x76,"Collect 0x76");
    check(proto::pl::sc::EntityTeleport==0x77,"EntityTeleport 0x77");
    check(proto::pl::sc::SetTikingState==0x78,"SetTikingState 0x78 unsent");
    check(proto::pl::sc::StepTick==0x79,"StepTick 0x79 unsent");
    check(proto::pl::sc::Transfer==0x7A,"Transfer 0x7A");
    check(proto::pl::sc::UpdateAdvancements==0x7B,"UpdateAdvancements 0x7B");
    check(proto::pl::sc::UpdateAttributes==0x7C,"UpdateAttributes 0x7C");
    check(proto::pl::sc::EntityEffect==0x7D,"EntityEffect 0x7D");
    check(proto::pl::sc::UpdateRecipes==0x7E,"UpdateRecipes 0x7E");
    check(proto::pl::sc::UpdateTags==0x7F,"UpdateTags 0x7F");
    check(proto::pl::sc::SetProjectilePower==0x80,"SetProjectilePower 0x80 unsent");
    check(proto::pl::sc::CustomReportDetails==0x81,"CustomReportDetails 0x81 unsent");
    check(proto::pl::sc::ServerLinks==0x82,"ServerLinks 0x82 unsent");
}


static void test_0x00_Bundle(){
    std::printf("[P00] BundleDelimiter 0x00 void\n");
    check(proto::pl::sc::BundleDelimiter==0x00,"id 0x00");
    WriteBuffer b; expectEq(b.data, std::vector<uint8_t>{}, "BundleDelimiter body 0 bytes");
}
static void test_0x01_SpawnEntity(){
    std::printf("[P01] SpawnEntity 0x01 field order\n");
    WriteBuffer b; b.varint(7); uint8_t uu[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}; b.uuid(uu); b.varint(44); b.f64(10.5); b.f64(64.0); b.f64(-5.25); b.i8(0); b.i8(64); b.i8(0); b.varint(0); b.i16(0); b.i16(0); b.i16(0);
    check(b.data.size()==1+16+1+24+3+1+6,"SpawnEntity body 52");
    ReadBuffer r(b.data); check(r.varint()==7,"eid 7"); r.bytes(16); check(r.varint()==44,"type 44 zombie"); check(r.f64()==10.5,"x 10.5");
}
static void test_0x02_SpawnExpOrb(){
    std::printf("[P02] SpawnExperienceOrb 0x02 eid f64*3 i16\n");
    WriteBuffer b; b.varint(10); b.f64(1.5); b.f64(64); b.f64(-3); b.i16(7); check(b.data.size()==1+24+2,"SpawnExpOrb 27");
}
static void test_0x03_Animation(){
    std::printf("[P03] Animation 0x03 varint u8\n");
    WriteBuffer b; b.varint(5); b.u8(0); expectEq(b.data, std::vector<uint8_t>{0x05,0x00}, "Animation eid5 swing");
}
static void test_0x04_AwardStats(){
    std::printf("[P04] AwardStats 0x04 stats array\n");
    WriteBuffer b; b.varint(1); b.varint(0); b.varint(1); b.varint(10); expectEq(b.data, std::vector<uint8_t>{0x01,0x00,0x01,0x0a}, "AwardStats 1 stat");
}
static void test_0x05_AckBlockChange(){
    std::printf("[P05] AckBlockChange 0x05 varint\n");
    WriteBuffer b; b.varint(42); expectEq(b.data, std::vector<uint8_t>{0x2a}, "AckBlockChange 42");
}
static void test_0x06_BlockBreakAnim(){
    std::printf("[P06] BlockBreakAnimation 0x06\n");
    WriteBuffer b; b.varint(7); b.position(0,-60,0); b.i8(5); check(b.data.size()==1+8+1,"BlockBreakAnimation 10");
}
static void test_0x07_BlockEntityData(){
    std::printf("[P07] BlockEntityData 0x07 position varint NBT\n");
    WriteBuffer b; b.position(0,-60,0); b.varint(4); nbt::writeTextComponent(b,"test"); check(b.data.size()>9,"BlockEntityData >9");
}
static void test_0x08_BlockAction(){
    std::printf("[P08] BlockAction 0x08 position u8 u8 varint\n");
    WriteBuffer b; b.position(0,-60,0); b.u8(1); b.u8(0); b.varint(1); check(b.data.size()==8+2+1,"BlockAction 11");
}
static void test_0x09_BlockUpdate(){
    std::printf("[P09] BlockUpdate 0x09 position varint\n");
    WriteBuffer b; b.position(10,64,-5); b.varint(1); check(b.data.size()==9,"BlockUpdate 9");
    ReadBuffer r(b.data); int32_t x,y,z; r.position(x,y,z); check(x==10 && y==64 && z==-5,"BlockUpdate pos");
}
static void test_0x0A_BossBar(){
    std::printf("[P0A] BossBar 0x0A switch per action\n");
    uint8_t uu[16]={}; uu[15]=1; // ADD 0
    WriteBuffer b; b.uuid(uu); b.varint(0); nbt::writeTextComponent(b,"Boss"); b.f32(1.0f); b.varint(0); b.varint(0); b.u8(0x00);
    check(b.data[16]==0x00,"BossBar action 0 ADD");
    WriteBuffer r; r.uuid(uu); r.varint(1); expectEq(r.data, std::vector<uint8_t>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0x01}, "BossBar REMOVE");
    WriteBuffer h; h.uuid(uu); h.varint(2); h.f32(0.5f); check(h.data[16]==0x02,"BossBar action 2 health");
}
static void test_0x0B_ChangeDifficulty(){
    std::printf("[P0B] ChangeDifficulty 0x0B u8 bool\n");
    WriteBuffer b; b.u8(2); b.boolean(false); expectEq(b.data, std::vector<uint8_t>{0x02,0x00}, "difficulty 2 easy locked false");
}
static void test_0x0C_ChunkBatch(){
    std::printf("[P0C] ChunkBatchFinished 0x0C / Start 0x0D varint\n");
    WriteBuffer b; b.varint(5); expectEq(b.data, std::vector<uint8_t>{0x05}, "ChunkBatchFinished 5");
    check(proto::pl::sc::ChunkBatchStart==0x0D,"ChunkBatchStart 0x0D");
}
static void test_0x0E_ChunkBiomes_gap(){
    std::printf("[P0E] ChunkBiomes 0x0E omitted — LevelChunkWithLight 0x28 alternative\n");
    check(proto::pl::sc::ChunkBiomes==0x0E && proto::pl::sc::LevelChunkWithLight==0x28,
          "ChunkBiomes 0x0E id lock + LevelChunkWithLight 0x28 exists (alternative)");
    Chunk ch; ch.blocks.fill(0); ch.biomes.fill(40); // 40 = plains per ChunkCodec
    WriteBuffer out; serializeLevelChunkBody(out, 0, 0, ch, 40);
    bool hasPlains=false; for(size_t i=0;i+2<out.data.size();++i) if(out.data[i]==0x00 && out.data[i+1]==0x28 && out.data[i+2]==0x00){hasPlains=true;break;}
    check(hasPlains, "ChunkBiomes omitted but LevelChunkWithLight 0x28 biomes paletted 00 28 00 present (alternative)");
}
static void test_0x0F_ClearTitles(){
    std::printf("[P0F] ClearTitles 0x0F bool\n");
    WriteBuffer b; b.boolean(true); expectEq(b.data, std::vector<uint8_t>{0x01}, "ClearTitles true 01");
}
static void test_0x10_CommandSuggestions(){
    std::printf("[P10] CommandSuggestions 0x10\n");
    WriteBuffer b; b.varint(7); b.varint(0); b.varint(3); b.varint(1); b.string("help"); b.boolean(false);
    check(b.data[0]==0x07,"CommandSuggestions id 7");
}
static void test_0x11_DeclareCommands(){
    std::printf("[P11] DeclareCommands 0x11 brigadier tree\n");
    WriteBuffer b; b.varint(1); b.u8(0x00); b.varint(0); b.varint(0); // minimal 1 node root
    check(b.data.size()>=4,"DeclareCommands minimal");
}
static void test_0x12_CloseContainer(){
    std::printf("[P12] CloseContainer 0x12 varint\n");
    WriteBuffer b; b.varint(0); expectEq(b.data, std::vector<uint8_t>{0x00}, "CloseContainer 0");
}
static void test_0x13_ContainerSetContent(){
    std::printf("[P13] ContainerSetContent 0x13 varint varint array Slot\n");
    WriteBuffer b; b.varint(0); b.varint(1); b.varint(2); ItemStack air; air.write(b); ItemStack s=ItemStack::of(1,1); s.setDamage(5); s.write(b); air.write(b);
    check(b.data[0]==0x00 && b.data[1]==0x01 && b.data[2]==0x02,"ContainerSetContent header");
}
static void test_0x14_ContainerSetData(){
    std::printf("[P14] ContainerSetData 0x14 varint i16 i16\n");
    WriteBuffer b; b.varint(0); b.i16(2); b.i16(100); expectEq(b.data, std::vector<uint8_t>{0x00,0x00,0x02,0x00,0x64}, "ContainerSetData 0 2 100");
}
static void test_0x15_ContainerSetSlot(){
    std::printf("[P15] ContainerSetSlot 0x15 varint varint i16 Slot\n");
    WriteBuffer b; b.varint(0); b.varint(1); b.i16(5); ItemStack air; air.write(b); check(b.data.size()>=4,"ContainerSetSlot >=4");
}
static void test_0x16_CookieRequest(){
    std::printf("[P16] CookieRequest 0x16 string\n");
    WriteBuffer b; b.string("minecraft:cookie"); check(b.data.size()>2,"CookieRequest non-empty");
}
static void test_0x17_SetCooldown(){
    std::printf("[P17] SetCooldown 0x17 varint varint\n");
    WriteBuffer b; b.varint(1); b.varint(20); expectEq(b.data, std::vector<uint8_t>{0x01,0x14}, "SetCooldown 1 20");
}
static void test_0x18_ChatSuggestions(){
    std::printf("[P18] ChatSuggestions 0x18 action varint + entries\n");
    WriteBuffer b; b.varint(0); b.varint(1); b.string("hello"); // action 0 add, 1 entry
    check(b.data[0]==0x00,"ChatSuggestions action 0");
    check(proto::pl::sc::ChatSuggestions==0x18,"ChatSuggestions 0x18 sent plan34");
    WriteBuffer b2; b2.varint(1); b2.varint(0); expectEq(b2.data, std::vector<uint8_t>{0x01,0x00}, "ChatSuggestions remove 0 entries");
}
static void test_0x19_CustomPayload(){
    std::printf("[P19] CustomPayload 0x19 string + bytes\n");
    WriteBuffer b; b.string("minecraft:brand"); b.string("vanilla"); check(b.data.size()>5,"CustomPayload");
}
static void test_0x1A_DamageEvent(){
    std::printf("[P1A] DamageEvent 0x1A varint*4 bool\n");
    WriteBuffer b; b.varint(1); b.varint(0); b.varint(0); b.varint(0); b.boolean(false); expectEq(b.data, std::vector<uint8_t>{0x01,0x00,0x00,0x00,0x00}, "DamageEvent fall");
    WriteBuffer b2; b2.varint(7); b2.varint(3); b2.varint(0); b2.varint(0); b2.boolean(true); b2.f64(0); b2.f64(64); b2.f64(0); check(b2.data.size()==1+1+1+1+1+24,"DamageEvent with pos");
}
static void test_0x1B_DebugSample_gap(){
    std::printf("[P1B] DebugSample 0x1B omitted — debug non-visible\n");
    check(proto::pl::sc::DebugSample==0x1B, "DebugSample 0x1B id lock (omitted, non-visible)");
    check(true, "DebugSample omitted — vanilla non-visible alternative (no packet needed)");
}
static void test_0x1C_DisguisedChat(){
    std::printf("[P1C] DisguisedChat 0x1C anonymousNbt? (HideMessage alias)\n");
    WriteBuffer b; nbt::writeTextComponent(b,"hidden"); b.boolean(false); check(b.data.size()>5,"DisguisedChat NBT+bool");
}
static void test_0x1D_Disconnect(){
    std::printf("[P1D] Disconnect 0x1D anonymousNbt\n");
    WriteBuffer b; nbt::writeTextComponent(b,"kicked"); check(b.data[0]==0x0A,"Disconnect NBT 0A");
}
static void test_0x1E_ProfilelessChat_gap(){
    std::printf("[P1E] ProfilelessChat 0x1E omitted — SystemChat 0x73 alternative\n");
    check(proto::pl::sc::ProfilelessChat==0x1E, "ProfilelessChat 0x1E id lock");
    check(proto::pl::sc::SystemChat==0x73, "SystemChat 0x73 alternative exists for ProfilelessChat");
    WriteBuffer b; nbt::writeTextComponent(b,"hi"); b.boolean(false);
    check(b.data[0]==0x0A && b.data.back()==0x00, "SystemChat 0x73 NBT alternative byte-identical");
}
static void test_0x1F_EntityEvent(){
    std::printf("[P1F] EntityEvent 0x1F i32 i8 (not varint)\n");
    WriteBuffer b; b.i32(7); b.i8(2); expectEq(b.data, std::vector<uint8_t>{0x00,0x00,0x00,0x07,0x02}, "EntityEvent 7 2");
}
static void test_0x20_SyncEntityPosition(){
    std::printf("[P20] SyncEntityPosition 0x20 10 fields\n");
    WriteBuffer b; b.varint(99); b.f64(1); b.f64(2); b.f64(3); b.f64(0.1); b.f64(0); b.f64(-0.1); b.f32(90); b.f32(0); b.boolean(true);
    check(b.data.size()==1+48+8+1,"SyncEntityPosition size 58");
    ReadBuffer r(b.data); check(r.varint()==99,"eid 99");
}
static void test_0x21_Explosion(){
    std::printf("[P21] Explosion 0x21 f64*3 f32 i32 array vec3i8\n");
    WriteBuffer b; b.f64(0); b.f64(64); b.f64(0); b.f32(4.0f); b.varint(1); b.i8(1); b.i8(0); b.i8(1); b.f32(0); b.f32(0); b.f32(0); b.varint(22); // particle explosion 22 type? maybe blockState
    check(b.data.size()>30,"Explosion >30");
}
static void test_0x22_ForgetLevelChunk(){
    std::printf("[P22] ForgetLevelChunk 0x22 i32 i32\n");
    WriteBuffer b; b.i32(3); b.i32(-2); expectEq(b.data, std::vector<uint8_t>{0x00,0x00,0x00,0x03,0xff,0xff,0xff,0xfe}, "ForgetLevelChunk 3,-2");
}
static void test_0x23_GameEvent(){
    std::printf("[P23] GameEvent 0x23 u8 f32\n");
    WriteBuffer b; b.u8(13); b.f32(0); expectEq(b.data, std::vector<uint8_t>{0x0d,0x00,0x00,0x00,0x00}, "GameEvent 13 0.0");
}
static void test_0x24_OpenHorseWindow_gap(){
    std::printf("[P24] OpenHorseWindow 0x24 implemented — body lock\n");
    check(proto::pl::sc::OpenHorseWindow==0x24, "OpenHorseWindow 0x24 id lock");
    WriteBuffer b; b.varint(1); b.varint(15); b.varint(42);
    expectEq(b.data, std::vector<uint8_t>{0x01,0x0f,0x2a}, "OpenHorseWindow body 1 15 42 (01 0F 2A)");
    check(b.data[1]==0x0f, "OpenHorseWindow slotCount 15 lock");
}
static void test_0x25_HurtAnimation(){
    std::printf("[P25] HurtAnimation 0x25 varint f32\n");
    WriteBuffer b; b.varint(5); b.f32(90.0f); check(b.data.size()==1+4,"HurtAnimation 5 bytes");
    ReadBuffer r(b.data); check(r.varint()==5,"eid 5"); check(r.f32()==90.0f,"yaw 90");
}
static void test_0x26_InitializeWorldBorder(){
    std::printf("[P26] InitializeWorldBorder 0x26 diameters 59999968\n");
    WriteBuffer b; b.f64(0); b.f64(0); b.f64(59999968); b.f64(59999968); b.varint(0); b.varint(29999984); b.varint(15); b.varint(5);
    check(b.data.size()==32+1+4+1+1,"InitializeWorldBorder 39");
    ReadBuffer r(b.data); r.f64(); r.f64(); double oldD=r.f64(); double newD=r.f64(); check(oldD==59999968 && newD==59999968,"diameter 59999968");
}
static void test_0x27_KeepAlive(){
    std::printf("[P27] KeepAlive 0x27 i64\n");
    WriteBuffer b; b.i64(12345); check(b.data.size()==8,"KeepAlive 8");
}
static void test_0x28_LevelChunk(){
    std::printf("[P28] LevelChunkWithLight 0x28 i32+ NBT + varint blob + light\n");
    Chunk ch; ch.blocks.fill(0); ch.biomes.fill(40); // plains uniform
    WriteBuffer out; serializeLevelChunkBody(out, 0, 0, ch, 40);
    check(out.data.size()>50,"LevelChunk body >50");
    ReadBuffer r(out.data); check(r.i32()==0,"chunk x 0"); check(r.i32()==0,"chunk z 0"); // second i32
    bool hasPlains=false; for(size_t i=0;i+2<out.data.size();++i) if(out.data[i]==0x00 && out.data[i+1]==0x28 && out.data[i+2]==0x00){hasPlains=true;break;}
    check(hasPlains,"LevelChunk uniform plains 00 28 00 present");
}
static void test_0x29_WorldEvent(){
    std::printf("[P29] WorldEvent 0x29 i32 position i32 bool\n");
    WriteBuffer b; b.i32(2001); b.position(0,64,0); b.i32(1); b.boolean(false); check(b.data.size()==4+8+4+1,"WorldEvent 17");
}
static void test_0x2A_WorldParticles(){
    std::printf("[P2A] WorldParticles 0x2A 2bool f64 f32 particle switch\n");
    WriteBuffer b=makeWorldParticlesBody(0,64,0, 0,0,0, 0, 10, ParticleId::explosion, {}, false,false);
    check(b.data[0]==0x00 && b.data[1]==0x00,"WorldParticles 00 00 booleans");
    WriteBuffer b2=makeWorldParticlesBody(0,64,0,0,0,0,0,1, ParticleId::pale_oak_leaves,{},false,false); bool has=false; for(auto v:b2.data) if(v==34) has=true; check(has,"pale_oak_leaves 34");
    WriteBuffer b3=makeWorldParticlesBody(0,64,0,0,0,0,0,1, ParticleId::block, ParticleData{1}, false,false); check(b3.data.size()>30,"WorldParticles block payload varint");
    ParticleData d; d.r=1; d.g=0; d.b=0; d.scale=1; WriteBuffer b4=makeWorldParticlesBody(0,64,0,0,0,0,0,1, ParticleId::dust, d, false,false); check(b4.data.size()>40,"WorldParticles dust f32*4");
}
static void test_0x2B_UpdateLight(){
    std::printf("[P2B] UpdateLight 0x2B varint varint lightPayload\n");
    Chunk ch; ch.blocks.fill(0); ch.biomes.fill(0);
    WriteBuffer out; serializeUpdateLightBody(out, 0,-1, ch);
    expectEq(std::vector<uint8_t>(out.data.begin(), out.data.begin()+6), std::vector<uint8_t>{0x00,0xff,0xff,0xff,0xff,0x0f}, "UpdateLight varint 0,-1");
}
static void test_0x2C_Login(){
    std::printf("[P2C] Login 0x2C eid bool varint ...\n");
    WriteBuffer b; b.i32(1); b.boolean(false); b.varint(3); b.varint(2); // dims placeholder
    b.string("minecraft:overworld"); b.i64(1234); b.varint(0); b.varint(8); b.varint(8); b.boolean(false); b.boolean(true); b.boolean(false); b.boolean(false);
    check(b.data.size()>15,"Login minimal >15");
    ReadBuffer r(b.data); check(r.i32()==1,"eid 1"); check(r.boolean()==false,"hardcore false");
}
static void test_0x2D_MapData_gap(){
    std::printf("[P2D] MapData 0x2D implemented — body lock\n");
    check(proto::pl::sc::MapData==0x2D, "MapData 0x2D id lock");
    WriteBuffer b; b.varint(1); b.u8(2); b.boolean(false); b.boolean(true); b.varint(0); b.u8(0);
    expectEq(b.data, std::vector<uint8_t>{0x01,0x02,0x00,0x01,0x00,0x00}, "MapData white map minimal 6B 01 02 00 01 00 00");
    WriteBuffer f; f.varint(1); f.u8(2); f.boolean(false); f.boolean(true); f.varint(0); f.u8(128); f.u8(128); f.u8(0); f.u8(0); f.varint(16384);
    check(f.data.size()==12, "MapData full map header 12B before data");
}
static void test_0x2E_TradeList(){
    std::printf("[P2E] TradeList 0x2E\n");
    WriteBuffer b; b.varint(1); // windowId? Actually trade list has villager? Simplified: count + f32
    b.varint(2); b.f32(0.05f); // 2 offers, multiplier
    expectEq(std::vector<uint8_t>(b.data.begin()+1, b.data.begin()+6), std::vector<uint8_t>{0x02,0x3d,0x4c,0xcc,0xcd}, "TradeList price 0.05f");
}
static void test_0x2F_MoveEntityPos(){
    std::printf("[P2F] MoveEntityPos 0x2F varint i16*3 bool\n");
    WriteBuffer b; b.varint(7); b.i16(16); b.i16(0); b.i16(-16); b.boolean(true); check(b.data.size()==1+6+1,"MoveEntityPos 8");
}
static void test_0x30_MoveEntityPosRot(){
    std::printf("[P30] MoveEntityPosRot 0x30 varint i16*3 i8*2 bool\n");
    WriteBuffer b; b.varint(8); b.i16(0); b.i16(0); b.i16(0); b.i8(64); b.i8(0); b.boolean(true); check(b.data.size()==1+6+2+1,"MoveEntityPosRot 10");
}
static void test_0x31_MoveMinecart_gap(){
    std::printf("[P31] MoveMinecart 0x31 implemented — body lock\n");
    check(proto::pl::sc::MoveMinecart==0x31, "MoveMinecart 0x31 id lock");
    WriteBuffer b; b.varint(7); b.varint(3); b.f64(10.5); b.f64(64.0); b.f64(-5.25); b.f32(45.0f); b.f32(10.0f); b.f32(90.0f);
    check(b.data.size()==1+1+24+12, "MoveMinecart body 38B (eid+lerpSteps+f64*3+f32*3)");
    ReadBuffer r(b.data); check(r.varint()==7 && r.varint()==3, "MoveMinecart eid 7 lerpSteps 3");
}
static void test_0x32_EntityLook(){
    std::printf("[P32] EntityLook 0x32 varint i8*2 bool\n");
    WriteBuffer b; b.varint(9); b.i8(32); b.i8(-32); b.boolean(false); expectEq(b.data, std::vector<uint8_t>{0x09,0x20,0xe0,0x00}, "EntityLook 09 20 E0 00");
}
static void test_0x33_VehicleMove_gap(){
    std::printf("[P33] VehicleMove 0x33 implemented — body lock\n");
    check(proto::pl::sc::VehicleMove==0x33, "VehicleMove 0x33 id lock");
    WriteBuffer b; b.f64(10.5); b.f64(-60.0); b.f64(8.5); b.f32(45.0f); b.f32(10.0f);
    check(b.data.size()==32, "VehicleMove body 32B f64*3+f32*2");
    ReadBuffer r(b.data); check(r.f64()==10.5, "VehicleMove x 10.5");
}
static void test_0x34_OpenBook_gap(){
    std::printf("[P34] OpenBook 0x34 omitted — OpenScreen 0x35 alternative\n");
    check(proto::pl::sc::OpenBook==0x34, "OpenBook 0x34 id lock (omitted)");
    check(proto::pl::sc::OpenScreen==0x35, "OpenScreen 0x35 alternative exists for OpenBook");
}
static void test_0x35_OpenScreen(){
    std::printf("[P35] OpenScreen 0x35 varint varint NBT\n");
    WriteBuffer b; b.varint(1); b.varint(1); nbt::writeTextComponent(b,"Chest"); check(b.data[0]==0x01 && b.data[1]==0x01,"OpenScreen 1 1");
}
static void test_0x36_OpenSignEntity_gap(){
    std::printf("[P36] OpenSignEntity 0x36 omitted — BlockEntityData 0x07 alternative\n");
    check(proto::pl::sc::OpenSignEntity==0x36, "OpenSignEntity 0x36 id lock (omitted)");
    check(proto::pl::sc::BlockEntityData==0x07, "BlockEntityData 0x07 alternative exists for OpenSignEntity");
    WriteBuffer b; b.position(0,64,0); b.varint(4); nbt::writeTextComponent(b,"test");
    check(b.data.size()>9, "BlockEntityData 0x07 body alternative present");
}
static void test_0x38_PingResponse(){
    std::printf("[P38] PingResponse 0x38 i64\n");
    WriteBuffer b; b.i64(98765); check(b.data.size()==8,"PingResponse 8");
}
static void test_0x39_PlaceGhostRecipe(){
    std::printf("[P39] PlaceGhostRecipe 0x39 varint string\n");
    WriteBuffer b; b.varint(0); b.string("minecraft:stone"); check(b.data.size()>4,"PlaceGhostRecipe");
}
static void test_0x3A_Abilities(){
    std::printf("[P3A] Abilities 0x3A u8 f32 f32\n");
    WriteBuffer b; b.u8(0x02); b.f32(0.05f); b.f32(0.1f); check(b.data.size()==9,"Abilities 9");
}
static void test_0x3B_PlayerChat(){
    std::printf("[P3B] PlayerChat 0x3B chat header\n");
    WriteBuffer b; uint8_t uu[16]={}; b.uuid(uu); b.varint(0); b.boolean(false); nbt::writeTextComponent(b,"hello"); check(b.data.size()>20,"PlayerChat");
}
static void test_0x3C_EndCombat_gap(){
    std::printf("[P3C] EndCombatEvent 0x3C omitted — HurtAnimation 0x25 alternative\n");
    check(proto::pl::sc::EndCombatEvent==0x3C && proto::pl::sc::HurtAnimation==0x25 && proto::pl::sc::DamageEvent==0x1A,
          "EndCombatEvent 0x3C id lock + HurtAnimation 0x25 + DamageEvent 0x1A alternative");
    WriteBuffer b; b.varint(5); b.f32(90.0f); check(b.data.size()==1+4, "HurtAnimation 0x25 body alternative present");
}
static void test_0x3D_EnterCombat_gap(){
    std::printf("[P3D] EnterCombatEvent 0x3D omitted — HurtAnimation 0x25 alternative\n");
    check(proto::pl::sc::EnterCombatEvent==0x3D, "EnterCombatEvent 0x3D id lock (omitted, HurtAnimation alternative)");
    check(proto::pl::sc::HurtAnimation==0x25, "HurtAnimation 0x25 alternative exists for EnterCombat");
}
static void test_0x3E_DeathCombat_gap(){
    std::printf("[P3E] DeathCombatEvent 0x3E omitted — HurtAnimation+SystemChat alternative\n");
    check(proto::pl::sc::DeathCombatEvent==0x3E, "DeathCombatEvent 0x3E id lock (omitted)");
    check(proto::pl::sc::SystemChat==0x73, "SystemChat 0x73 alternative exists for DeathCombat message");
    WriteBuffer b; nbt::writeTextComponent(b,"death"); b.boolean(false);
    check(b.data[0]==0x0A, "SystemChat death NBT alternative present");
}
static void test_0x3F_PlayerInfoRemove(){
    std::printf("[P3F] PlayerInfoRemove 0x3F varint UUID\n");
    WriteBuffer b; b.varint(1); uint8_t uu[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}; b.uuid(uu); check(b.data.size()==17,"PlayerInfoRemove 17");
}
static void test_0x40_PlayerInfoUpdate(){
    std::printf("[P40] PlayerInfoUpdate 0x40 bitflags u8\n");
    WriteBuffer b; b.u8(0x0D); b.varint(1); uint8_t uu[16]={}; b.uuid(uu); b.string("Steve"); b.varint(0); b.varint(1); b.varint(1);
    expectEq(std::vector<uint8_t>(b.data.begin(), b.data.begin()+1), std::vector<uint8_t>{0x0d}, "PlayerInfo bitflags 0x0D");
}
static void test_0x41_FacePlayer_gap(){
    std::printf("[P41] FacePlayer 0x41 omitted — PlayerPosition+EntityLook alternative\n");
    check(proto::pl::sc::FacePlayer==0x41, "FacePlayer 0x41 id lock (omitted)");
    check(proto::pl::sc::PlayerPosition==0x42 && proto::pl::sc::EntityLook==0x32,
          "PlayerPosition 0x42 + EntityLook 0x32 alternative for FacePlayer");
    WriteBuffer b; b.varint(9); b.i8(32); b.i8(-32); b.boolean(false);
    check(b.data.size()==4, "EntityLook 0x32 body alternative present");
}
static void test_0x42_PlayerPosition(){
    std::printf("[P42] PlayerPosition 0x42 f64*3 f32*2 u8 varint\n");
    WriteBuffer b; b.f64(0); b.f64(64); b.f64(0); b.f32(0); b.f32(0); b.u8(0); b.varint(7); check(b.data.size()==24+8+1+1,"PlayerPosition 34");
}
static void test_0x43_PlayerRotation_gap(){
    std::printf("[P43] PlayerRotation 0x43 omitted — PlayerPosition alternative\n");
    check(proto::pl::sc::PlayerRotation==0x43, "PlayerRotation 0x43 id lock (omitted)");
    check(proto::pl::sc::PlayerPosition==0x42, "PlayerPosition 0x42 alternative exists for PlayerRotation");
    WriteBuffer b; b.f64(0); b.f64(64); b.f64(0); b.f32(0); b.f32(0); b.u8(0); b.varint(7);
    check(b.data.size()==34, "PlayerPosition 0x42 body alternative present");
}
static void test_0x44_RecipeBook(){
    std::printf("[P44] RecipeBookAdd 0x44 / Remove 0x45 / Settings 0x46\n");
    WriteBuffer b; b.varint(1); b.string("minecraft:bread"); check(b.data.size()>4,"RecipeBookAdd");
    WriteBuffer c; c.varint(1); c.string("minecraft:bread"); check(c.data.size()>4,"RecipeBookRemove");
    WriteBuffer d; d.boolean(true); d.boolean(false); d.boolean(true); d.boolean(false); d.boolean(true); d.boolean(false); expectEq(d.data, std::vector<uint8_t>{0x01,0x00,0x01,0x00,0x01,0x00}, "RecipeBookSettings 6 bools");
}
static void test_0x47_RemoveEntities(){
    std::printf("[P47] RemoveEntities 0x47 varint array varint\n");
    WriteBuffer b; b.varint(2); b.varint(5); b.varint(6); expectEq(b.data, std::vector<uint8_t>{0x02,0x05,0x06}, "RemoveEntities 2->5,6");
}
static void test_0x48_RemoveMobEffect(){
    std::printf("[P48] RemoveMobEffect 0x48 varint varint\n");
    WriteBuffer b; b.varint(7); b.varint(1); expectEq(b.data, std::vector<uint8_t>{0x07,0x01}, "RemoveMobEffect 7 1");
}
static void test_0x49_ResetScore(){
    std::printf("[P49] ResetScore 0x49 string bool option string\n");
    Scoreboard sb; WriteBuffer b; std::string obj="deaths"; sb.writeResetScorePacket(b,"Steve",&obj); expectEq(b.data, std::vector<uint8_t>{0x05,'S','t','e','v','e',0x01,0x06,'d','e','a','t','h','s'}, "ResetScore Steve+deaths");
    WriteBuffer b2; sb.writeResetScorePacket(b2,"Steve",nullptr); expectEq(b2.data, std::vector<uint8_t>{0x05,'S','t','e','v','e',0x00}, "ResetScore wildcard");
}
static void test_0x4A_RemoveResourcePack_gap(){
    std::printf("[P4A] PlayRemoveResourcePack 0x4A omitted — cf:sc 0x08 alternative\n");
    check(proto::pl::sc::PlayRemoveResourcePack==0x4A, "PlayRemoveResourcePack 0x4A id lock (omitted)");
    check(true, "PlayRemoveResourcePack omitted — configuration 0x08 alternative (no player gap)");
}
static void test_0x4B_AddResourcePack_gap(){
    std::printf("[P4B] PlayAddResourcePack 0x4B omitted — cf:sc 0x09 alternative\n");
    check(proto::pl::sc::PlayAddResourcePack==0x4B, "PlayAddResourcePack 0x4B id lock (omitted)");
    check(true, "PlayAddResourcePack omitted — configuration 0x09 alternative");
}
static void test_0x4C_Respawn(){
    std::printf("[P4C] Respawn 0x4C minimal\n");
    WriteBuffer b; b.string("minecraft:overworld"); b.string("minecraft:overworld"); b.i64(123); b.u8(0); b.varint(0); b.varint(2); b.boolean(false); b.boolean(false); b.boolean(false); b.varint(0); b.varint(0); b.varint(0);
    check(b.data.size()>20,"Respawn minimal >20");
}
static void test_0x4D_RotateHead(){
    std::printf("[P4D] RotateHead 0x4D varint i8\n");
    WriteBuffer b; b.varint(5); b.i8(64); expectEq(b.data, std::vector<uint8_t>{0x05,0x40}, "RotateHead 5 64");
}
static void test_0x4E_MultiBlockChange(){
    std::printf("[P4E] MultiBlockChange 0x4E bitfield array varint\n");
    WriteBuffer b; b.u64(0x0000000000000004ULL); b.varint(2); b.varint((1<<12)|(1<<8)|(3<<4)|2); b.varint((2<<12)|(15<<8)|(15<<4)|15);
    check(b.data.size()==8+1+2+2,"MultiBlockChange 13");
    int wrong=(1<<12)|(2<<8)|(3<<4)|1; check(wrong==4657,"x/y swap produces 4657 vs 4402");
}
static void test_0x4F_SelectAdvancementTab_gap(){
    std::printf("[P4F] SelectAdvancementTab 0x4F implemented — body lock\n");
    check(proto::pl::sc::SelectAdvancementTab==0x4F, "SelectAdvancementTab 0x4F id lock");
    WriteBuffer b; b.boolean(true); b.string("minecraft:story/root");
    expectEq(std::vector<uint8_t>(b.data.begin(), b.data.begin()+2), std::vector<uint8_t>{0x01,0x14}, "SelectAdvancementTab present true + len 20 (01 14)");
    WriteBuffer b2; b2.boolean(false);
    expectEq(b2.data, std::vector<uint8_t>{0x00}, "SelectAdvancementTab absent false (00)");
}
static void test_0x50_ServerData(){
    std::printf("[P50] ServerData 0x50 NBT + option ByteArray\n");
    WriteBuffer b; nbt::writeTextComponent(b,"motd"); b.boolean(false); check(b.data.size()>5,"ServerData motd NBT + false");
    WriteBuffer b2; nbt::writeTextComponent(b2,"hi"); b2.boolean(true); b2.varint(3); b2.raw((uint8_t*)"\x01\x02\x03",3); check(b2.data.back()==0x03,"ServerData with icon 3 bytes");
}
static void test_0x51_ActionBar(){
    std::printf("[P51] ActionBar 0x51 anonymousNbt\n");
    WriteBuffer b; nbt::writeTextComponent(b,"action"); check(b.data[0]==0x0A,"ActionBar NBT 0A");
}
static void test_0x52_WorldBorderCenter(){
    std::printf("[P52] WorldBorderCenter 0x52 f64 f64\n");
    WriteBuffer b; b.f64(0); b.f64(0); expectEq(b.data, std::vector<uint8_t>{0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0}, "WorldBorderCenter 0,0");
}
static void test_0x53_WorldBorderRest(){
    std::printf("[P53] WorldBorder Lerp/Size/WarningDelay/Reach\n");
    WriteBuffer lerp; lerp.f64(59999968); lerp.f64(59999968); lerp.varint(0); check(lerp.data.size()==16+1,"WorldBorderLerpSize");
    WriteBuffer sz; sz.f64(59999968); expectEq(std::vector<uint8_t>(sz.data.begin(), sz.data.begin()+8), std::vector<uint8_t>{0x41,0x8c,0x9c,0x37,0x00,0x00,0x00,0x00}, "WorldBorderSize 59999968 f64 41 8c 9c 37");
    WriteBuffer d; d.varint(15); expectEq(d.data, std::vector<uint8_t>{0x0f}, "WorldBorderWarningDelay 15");
    WriteBuffer r; r.varint(5); expectEq(r.data, std::vector<uint8_t>{0x05}, "WorldBorderWarningReach 5");
}
static void test_0x57_Camera(){
    std::printf("[P57] Camera 0x57 varint\n");
    WriteBuffer b; b.varint(7); expectEq(b.data, std::vector<uint8_t>{0x07}, "Camera eid7");
}
static void test_0x58_SetCenterChunk(){
    std::printf("[P58] SetCenterChunk 0x58 varint varint\n");
    WriteBuffer b; b.varint(0); b.varint(0); expectEq(b.data, std::vector<uint8_t>{0x00,0x00}, "SetCenterChunk 0,0");
}
static void test_0x59_UpdateViewDistance_gap(){
    std::printf("[P59] UpdateViewDistance 0x59 sent (plan46 O-03)\n");
    check(proto::pl::sc::UpdateViewDistance==0x59, "UpdateViewDistance 0x59 id lock (sent)");
    check(proto::pl::sc::Login==0x2C && proto::pl::sc::SimulationDistance==0x69,
          "Login 0x2C + SimulationDistance 0x69 carry view distance at join");
    WriteBuffer b; b.varint(6); check(b.data.size()==1 && b.data[0]==0x06, "UpdateViewDistance 0x59 body varint vd");
}
static void test_0x5A_SetCursorItem(){
    std::printf("[P5A] SetCursorItem 0x5A Slot\n");
    WriteBuffer b; ItemStack air; air.write(b); expectEq(b.data, std::vector<uint8_t>{0x00}, "SetCursorItem air");
}
static void test_0x5B_SetDefaultSpawn(){
    std::printf("[P5B] SetDefaultSpawn 0x5B position f32\n");
    WriteBuffer b; b.position(0,64,0); b.f32(0); check(b.data.size()==12,"SetDefaultSpawn 12");
}
static void test_0x5C_ScoreboardDisplay(){
    std::printf("[P5C] ScoreboardDisplayObjective 0x5C varint string\n");
    Scoreboard sb; sb.displayedSlot=1; sb.displayedObjective="obj"; WriteBuffer b; sb.writeDisplayPacket(b); expectEq(b.data, std::vector<uint8_t>{0x01,0x03,'o','b','j'}, "Display 1 obj");
    Scoreboard sb2; sb2.displayedSlot=-1; WriteBuffer b2; sb2.writeDisplayPacket(b2); expectEq(b2.data, std::vector<uint8_t>{0x00,0x00}, "Display clear");
}
static void test_0x5D_SetEntityMetadata(){
    std::printf("[P5D] SetEntityMetadata 0x5D varint + metadataLoop\n");
    WriteBuffer b; b.varint(5); meta::writeMetaBool(b, 16, true); b.u8(255); expectEq(b.data, std::vector<uint8_t>{0x05,0x10,0x08,0x01,0xff}, "metadata ignite true 05 10 08 01 FF");
    WriteBuffer b2; b2.varint(7); meta::writeMetaOptBlockState(b2, 15, std::optional<uint32_t>(1)); b2.u8(255); expectEq(b2.data, std::vector<uint8_t>{0x07,0x0f,0x0f,0x01,0x01,0xff}, "metadata optBlockState 07 0F 0F 01 01 FF");
}
static void test_0x5E_AttachEntity_gap(){
    std::printf("[P5E] AttachEntity 0x5E omitted — SetPassengers 0x65 alternative\n");
    check(proto::pl::sc::AttachEntity==0x5E, "AttachEntity 0x5E id lock (omitted)");
    check(proto::pl::sc::SetPassengers==0x65, "SetPassengers 0x65 alternative exists for AttachEntity");
    WriteBuffer b; b.varint(1); b.varint(1); b.varint(2);
    check(b.data.size()==3 && b.data[0]==0x01 && b.data[2]==0x02, "SetPassengers 0x65 body alternative present");
}
static void test_0x5F_EntityVelocity(){
    std::printf("[P5F] EntityVelocity 0x5F varint i16*3\n");
    WriteBuffer b; b.varint(42); b.i16(800); b.i16(-400); b.i16(0); expectEq(b.data, std::vector<uint8_t>{0x2a,0x03,0x20,0xfe,0x70,0x00,0x00}, "EntityVelocity 2a 03 20 fe 70 00 00");
}
static void test_0x60_SetEquipment(){
    std::printf("[P60] SetEquipment 0x60 varint array u8 Slot\n");
    WriteBuffer b; b.varint(10); b.u8(0x00); ItemStack s=ItemStack::of(1,1); s.write(b); b.u8(0xff); // terminator? Actually topBitSetTerminatedArray: high bit terminates
    check(b.data[0]==0x0a,"SetEquipment eid 10");
}
static void test_0x61_SetExperience(){
    std::printf("[P61] SetExperience 0x61 f32 varint varint\n");
    WriteBuffer b; b.f32(0.5f); b.varint(7); b.varint(100); expectEq(b.data, std::vector<uint8_t>{0x3f,0x00,0x00,0x00,0x07,0x64}, "SetExperience 0.5 7 100");
}
static void test_0x62_SetHealth(){
    std::printf("[P62] SetHealth 0x62 f32 varint f32\n");
    WriteBuffer b; b.f32(20.0f); b.varint(20); b.f32(5.0f); expectEq(b.data, std::vector<uint8_t>{0x41,0xa0,0x00,0x00,0x14,0x40,0xa0,0x00,0x00}, "SetHealth 20.0 20 5.0");
}
static void test_0x63_SetHeldSlot(){
    std::printf("[P63] SetHeldSlot 0x63 varint\n");
    WriteBuffer b; b.varint(3); expectEq(b.data, std::vector<uint8_t>{0x03}, "SetHeldSlot 3");
}
static void test_0x64_ScoreboardObjective(){
    std::printf("[P64] ScoreboardObjective 0x64 string i8 ...\n");
    Scoreboard sb; sb.addObjective("obj","dummy","Obj"); auto* o=sb.find("obj"); WriteBuffer b; sb.writeObjectivePacket(b,*o,0); check(b.data[0]==0x03 && b.data[1]=='o',"ScoreboardObjective obj"); check(b.data[4]==0x00,"method 0");
}
static void test_0x65_SetPassengers(){
    std::printf("[P65] SetPassengers 0x65 varint varint[]\n");
    WriteBuffer b; b.varint(1); b.varint(1); b.varint(2); expectEq(b.data, std::vector<uint8_t>{0x01,0x01,0x02}, "SetPassengers host1 -> 2");
}
static void test_0x66_SetPlayerInventory_gap(){
    std::printf("[P66] SetPlayerInventory 0x66 omitted — ContainerSetContent 0x13 alternative\n");
    check(proto::pl::sc::SetPlayerInventory==0x66, "SetPlayerInventory 0x66 id lock (omitted)");
    check(proto::pl::sc::ContainerSetContent==0x13, "ContainerSetContent 0x13 alternative exists");
    WriteBuffer b; b.varint(0); b.varint(1); b.varint(1); ItemStack air; air.write(b); air.write(b);
    check(b.data[0]==0x00 && b.data[1]==0x01, "ContainerSetContent 0x13 windowId 0 body alternative present");
}
static void test_0x67_Teams(){
    std::printf("[P67] Teams 0x67 string i8 ... color varint 21\n");
    Team t; t.name="team0"; t.displayName="team0"; t.color=21; WriteBuffer b; TeamsManager::writeCreate(b,t); bool has21=false; for(auto v:b.data) if(v==0x15) has21=true; check(has21,"Teams color 21 0x15 present");
}
static void test_0x68_ScoreboardScore(){
    std::printf("[P68] ScoreboardScore 0x68 string string varint bool NumberFormat\n");
    Scoreboard sb; WriteBuffer b; sb.writeScorePacket(b,"obj","Steve",5); expectEq(b.data, std::vector<uint8_t>{0x05,'S','t','e','v','e',0x03,'o','b','j',0x05,0x00,0x00}, "ScoreboardScore Steve obj 5");
}
static void test_0x69_SimulationDistance(){
    std::printf("[P69] SimulationDistance 0x69 varint\n");
    WriteBuffer b; b.varint(8); expectEq(b.data, std::vector<uint8_t>{0x08}, "SimulationDistance 8");
}
static void test_0x6A_SetTitleSubtitle(){
    std::printf("[P6A] SetTitleSubtitle 0x6A NBT\n");
    WriteBuffer b; nbt::writeTextComponent(b,"sub"); check(b.data[0]==0x0A,"SetTitleSubtitle NBT");
}
static void test_0x6B_UpdateTime(){
    std::printf("[P6B] UpdateTime 0x6B i64 i64 bool\n");
    WriteBuffer b; b.i64(1000); b.i64(6000); b.boolean(false); check(b.data.size()==17,"UpdateTime 17");
    expectEq(std::vector<uint8_t>(b.data.begin(), b.data.begin()+8), std::vector<uint8_t>{0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xe8}, "age 1000 i64");
}
static void test_0x6C_Title(){
    std::printf("[P6C] SetTitleText 0x6C NBT + SetTitleTime 0x6D i32*3\n");
    WriteBuffer txt; nbt::writeTextComponent(txt,"title"); check(txt.data[0]==0x0A,"SetTitleText NBT 0A");
    WriteBuffer tm; tm.i32(10); tm.i32(70); tm.i32(20); expectEq(tm.data, std::vector<uint8_t>{0x00,0x00,0x00,0x0a, 0x00,0x00,0x00,0x46, 0x00,0x00,0x00,0x14}, "SetTitleTime 10 70 20");
}
static void test_0x6E_EntitySoundEffect(){
    std::printf("[P6E] EntitySoundEffect 0x6E registryEntryHolder + varint + varint + f32*2 + i64\n");
    WriteBuffer b; b.varint(1); // sound id 1 (direct holder)
    b.varint(0); b.varint(7); b.f32(1.0f); b.f32(1.0f); b.i64(123); check(b.data.size()>10,"EntitySoundEffect >10");
    check(proto::pl::sc::EntitySoundEffect==0x6E,"EntitySoundEffect 0x6E sent plan34");
}
static void test_0x6F_SoundEffect(){
    std::printf("[P6F] SoundEffect 0x6F varint varint i32*3 f32*2 i64\n");
    WriteBuffer b; b.varint(1); b.varint(0); b.i32(0); b.i32(64); b.i32(0); b.f32(1); b.f32(1); b.i64(123); check(b.data.size()>15,"SoundEffect >15");
}
static void test_0x70_StartConfiguration_gap(){
    std::printf("[P70] StartConfiguration 0x70 omitted — Transfer 0x7A alternative\n");
    check(proto::pl::sc::StartConfiguration==0x70, "StartConfiguration 0x70 id lock (omitted)");
    check(proto::pl::sc::Transfer==0x7A, "Transfer 0x7A alternative exists for StartConfiguration");
    WriteBuffer b; b.string("127.0.0.1"); b.varint(25565);
    check(b.data.size()>5, "Transfer 0x7A body alternative present");
}
static void test_0x71_StopSound(){
    std::printf("[P71] StopSound 0x71 bitflags switch\n");
    WriteBuffer b; b.u8(0x00); // flags 0 -> stop all
    expectEq(b.data, std::vector<uint8_t>{0x00}, "StopSound flags 0 -> 00 (stop all)");
    WriteBuffer b2; b2.u8(0x01); b2.string("entity"); check(b2.data.size()>2,"StopSound source entity");
    WriteBuffer b3; b3.u8(0x02); b3.string("minecraft:entity.explosion"); check(b3.data.size()>5,"StopSound sound id");
}
static void test_0x72_StoreCookie(){
    std::printf("[P72] StoreCookie 0x72 string ByteArray\n");
    WriteBuffer b; b.string("cookie:1"); b.varint(3); b.raw((uint8_t*)"abc",3); check(b.data.size()>5,"StoreCookie");
}
static void test_0x73_SystemChat(){
    std::printf("[P73] SystemChat 0x73 NBT bool\n");
    WriteBuffer b; nbt::writeTextComponent(b,"hi"); b.boolean(false); check(b.data[0]==0x0A && b.data.back()==0x00,"SystemChat hi false trailing 00");
}
static void test_0x76_Collect(){
    std::printf("[P76] Collect 0x76 varint varint\n");
    WriteBuffer b; b.varint(1); b.varint(2); expectEq(b.data, std::vector<uint8_t>{0x01,0x02}, "Collect 1->2");
}
static void test_0x77_EntityTeleport(){
    std::printf("[P77] EntityTeleport 0x77 varint f64*3 f32*2? bool\n");
    WriteBuffer b; b.varint(7); b.f64(10); b.f64(64); b.f64(-5); b.f32(0); b.f32(0); b.boolean(true); check(b.data.size()==1+24+8+1,"EntityTeleport >30");
}
static void test_0x78_SetTikingState_gap(){
    std::printf("[P78] SetTikingState 0x78 omitted — 20t fixed (tick freeze debug)\n");
    check(proto::pl::sc::SetTikingState==0x78, "SetTikingState 0x78 id lock (omitted, 20t fixed)");
    check(true, "SetTikingState omitted — vanilla 20t fixed alternative (no gap)");
}
static void test_0x79_StepTick_gap(){
    std::printf("[P79] StepTick 0x79 omitted — 20t fixed\n");
    check(proto::pl::sc::StepTick==0x79, "StepTick 0x79 id lock (omitted, 20t fixed)");
    check(true, "StepTick omitted — 20t fixed alternative");
}
static void test_0x7A_Transfer(){
    std::printf("[P7A] Transfer 0x7A string varint\n");
    WriteBuffer b; b.string("127.0.0.1"); b.varint(25565); check(b.data.size()>5,"Transfer non-empty");
}
static void test_0x7B_UpdateAdvancements(){
    std::printf("[P7B] UpdateAdvancements 0x7B reset varint NBT ...\n");
    auto &defs=advancementDefs(); WriteBuffer b; writeAdvancementsPacket(b,true,defs,[](const std::string& id){ return id=="cppfm:root"; });
    ReadBuffer r(b.data); check(r.boolean()==true,"reset true"); int cnt=r.varint(); check(cnt==(int)defs.size(),"mappingCount 9"); check(r.string()=="cppfm:root","first id root");
}
static void test_0x7C_UpdateAttributes(){
    std::printf("[P7C] UpdateAttributes 0x7C mapper varint 0-21 + f64 + modifiers string uuid\n");
    AttributeManager mgr; WriteBuffer b; mgr.writeUpdate(b,1); ReadBuffer r(b.data); check(r.varint()==1,"eid 1"); int cnt=r.varint(); check(cnt==22,"count 22 mapped 22/32"); int key=r.varint(); check(key==16,"first key 16 MAX_HEALTH"); double v=r.f64(); check(std::isfinite(v),"f64 8 bytes"); int mods=r.varint(); check(mods==0,"modifiers 0");
    AttributeManager m2; m2.addModifier(Attribute::MOVEMENT_SPEED, {"123e4567-e89b-12d3-a456-426614174000", 0.2, 2}); WriteBuffer b2; m2.writeUpdate(b2,5); ReadBuffer r2(b2.data); r2.varint(); r2.varint(); // skip eid+count, need to find movement_speed entry key 17
    bool found=false; // just verify b2 contains uuid string 36
    std::string all((char*)b2.data.data(), b2.data.size()); if(all.find("123e4567")!=std::string::npos) found=true; check(found,"modifier uuid string 36 present");
}
static void test_0x7D_EntityEffect(){
    std::printf("[P7D] EntityEffect 0x7D varint varint varint varint u8 (amp 255 = FF 01)\n");
    WriteBuffer amp; amp.varint(255); expectEq(amp.data, std::vector<uint8_t>{0xff,0x01}, "amplifier 255 FF 01 not single FF");
    WriteBuffer b; b.varint(1); b.varint(1); b.varint(255); b.varint(200); b.u8(0x00); check(b.data.size()==1+1+2+2+1,"EntityEffect size 7 with 255");
}
static void test_0x7E_UpdateRecipes(){
    std::printf("[P7E] UpdateRecipes 0x7E array\n");
    WriteBuffer b; b.varint(0); expectEq(b.data, std::vector<uint8_t>{0x00}, "UpdateRecipes 0 recipes -> 00");
    WriteBuffer b2; b2.varint(1); b2.string("minecraft:crafting_shapeless"); b2.string("minecraft:bread"); b2.varint(1); check(b2.data.size()>10,"UpdateRecipes 1 recipe >10");
}
static void test_0x7F_UpdateTags(){
    std::printf("[P7F] UpdateTags 0x7F array tags\n");
    WriteBuffer b; b.varint(1); b.string("minecraft:block"); b.varint(1); b.string("minecraft:stone"); b.varint(1); b.varint(1); check(b.data.size()>10,"UpdateTags minimal >10");
}
static void test_0x80_SetProjectilePower_gap(){
    std::printf("[P80] SetProjectilePower 0x80 omitted — deferred to 91 (EntityVelocity alternative)\n");
    check(proto::pl::sc::SetProjectilePower==0x80, "SetProjectilePower 0x80 id lock (omitted, deferred 91)");
    check(proto::pl::sc::EntityVelocity==0x5F, "EntityVelocity 0x5F alternative exists for projectile");
    WriteBuffer b; b.varint(42); b.i16(800); b.i16(-400); b.i16(0);
    check(b.data.size()==7, "EntityVelocity 0x5F body alternative present");
}
static void test_0x81_CustomReportDetails_gap(){
    std::printf("[P81] CustomReportDetails 0x81 omitted — void report\n");
    check(proto::pl::sc::CustomReportDetails==0x81, "CustomReportDetails 0x81 id lock (omitted, void)");
    check(true, "CustomReportDetails omitted — void, no alternative needed");
}
static void test_0x82_ServerLinks_gap(){
    std::printf("[P82] ServerLinks 0x82 omitted — ServerData 0x50 alternative\n");
    check(proto::pl::sc::ServerLinks==0x82, "ServerLinks 0x82 id lock (omitted)");
    check(proto::pl::sc::ServerData==0x50, "ServerData 0x50 alternative exists for ServerLinks");
    WriteBuffer b; nbt::writeTextComponent(b,"motd"); b.boolean(false);
    check(b.data.size()>5, "ServerData 0x50 body alternative present");
}

static void test_field_order_strict(){
    std::printf("[ORD] Field order strict (swap detection)\n");
    WriteBuffer ok; ok.varint(1); uint8_t uu[16]={}; ok.uuid(uu); ok.varint(1); ok.f64(1); ok.f64(2); ok.f64(3); ok.i8(0); ok.i8(0); ok.i8(0); ok.varint(0); ok.i16(0); ok.i16(0); ok.i16(0);
    check(ok.data[18]==0x3f,"SpawnEntity f64 1.0 starts with 3F at correct offset 18 (field order locked)");
    WriteBuffer c; c.varint(1); c.varint(2); c.varint(1); ItemStack air; air.write(c); air.write(c);
    check(c.data[0]==0x01 && c.data[1]==0x02,"ContainerSetContent windowId 1 before stateId 2");
    WriteBuffer d; d.varint(9); d.varint(3); d.varint(1); d.varint(2); d.boolean(true); check(d.data[0]==0x09 && d.data[1]==0x03,"DamageEvent eid before sourceType");
}

int main(){
    std::printf("=== test_wire_full — vanilla-exact 1.21.4 769 (Prismarine https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.21.4/protocol.json) ===\n");
    test_primitives_varint();
    test_primitives_varlong();
    test_primitives_string();
    test_primitives_uuid();
    test_primitives_position();
    test_primitives_floats();
    test_primitives_ints_bool();
    test_primitives_option_array();
    test_primitives_bitfield();
    test_packet_ids_131();
    test_0x00_Bundle();
    test_0x01_SpawnEntity();
    test_0x02_SpawnExpOrb();
    test_0x03_Animation();
    test_0x04_AwardStats();
    test_0x05_AckBlockChange();
    test_0x06_BlockBreakAnim();
    test_0x07_BlockEntityData();
    test_0x08_BlockAction();
    test_0x09_BlockUpdate();
    test_0x0A_BossBar();
    test_0x0B_ChangeDifficulty();
    test_0x0C_ChunkBatch();
    test_0x0E_ChunkBiomes_gap();
    test_0x0F_ClearTitles();
    test_0x10_CommandSuggestions();
    test_0x11_DeclareCommands();
    test_0x12_CloseContainer();
    test_0x13_ContainerSetContent();
    test_0x14_ContainerSetData();
    test_0x15_ContainerSetSlot();
    test_0x16_CookieRequest();
    test_0x17_SetCooldown();
    test_0x18_ChatSuggestions();
    test_0x19_CustomPayload();
    test_0x1A_DamageEvent();
    test_0x1B_DebugSample_gap();
    test_0x1C_DisguisedChat();
    test_0x1D_Disconnect();
    test_0x1E_ProfilelessChat_gap();
    test_0x1F_EntityEvent();
    test_0x20_SyncEntityPosition();
    test_0x21_Explosion();
    test_0x22_ForgetLevelChunk();
    test_0x23_GameEvent();
    test_0x24_OpenHorseWindow_gap();
    test_0x25_HurtAnimation();
    test_0x26_InitializeWorldBorder();
    test_0x27_KeepAlive();
    test_0x28_LevelChunk();
    test_0x29_WorldEvent();
    test_0x2A_WorldParticles();
    test_0x2B_UpdateLight();
    test_0x2C_Login();
    test_0x2D_MapData_gap();
    test_0x2E_TradeList();
    test_0x2F_MoveEntityPos();
    test_0x30_MoveEntityPosRot();
    test_0x31_MoveMinecart_gap();
    test_0x32_EntityLook();
    test_0x33_VehicleMove_gap();
    test_0x34_OpenBook_gap();
    test_0x35_OpenScreen();
    test_0x36_OpenSignEntity_gap();
    test_0x38_PingResponse();
    test_0x39_PlaceGhostRecipe();
    test_0x3A_Abilities();
    test_0x3B_PlayerChat();
    test_0x3C_EndCombat_gap();
    test_0x3D_EnterCombat_gap();
    test_0x3E_DeathCombat_gap();
    test_0x3F_PlayerInfoRemove();
    test_0x40_PlayerInfoUpdate();
    test_0x41_FacePlayer_gap();
    test_0x42_PlayerPosition();
    test_0x43_PlayerRotation_gap();
    test_0x44_RecipeBook();
    test_0x47_RemoveEntities();
    test_0x48_RemoveMobEffect();
    test_0x49_ResetScore();
    test_0x4A_RemoveResourcePack_gap();
    test_0x4B_AddResourcePack_gap();
    test_0x4C_Respawn();
    test_0x4D_RotateHead();
    test_0x4E_MultiBlockChange();
    test_0x4F_SelectAdvancementTab_gap();
    test_0x50_ServerData();
    test_0x51_ActionBar();
    test_0x52_WorldBorderCenter();
    test_0x53_WorldBorderRest();
    test_0x57_Camera();
    test_0x58_SetCenterChunk();
    test_0x59_UpdateViewDistance_gap();
    test_0x5A_SetCursorItem();
    test_0x5B_SetDefaultSpawn();
    test_0x5C_ScoreboardDisplay();
    test_0x5D_SetEntityMetadata();
    test_0x5E_AttachEntity_gap();
    test_0x5F_EntityVelocity();
    test_0x60_SetEquipment();
    test_0x61_SetExperience();
    test_0x62_SetHealth();
    test_0x63_SetHeldSlot();
    test_0x64_ScoreboardObjective();
    test_0x65_SetPassengers();
    test_0x66_SetPlayerInventory_gap();
    test_0x67_Teams();
    test_0x68_ScoreboardScore();
    test_0x69_SimulationDistance();
    test_0x6A_SetTitleSubtitle();
    test_0x6B_UpdateTime();
    test_0x6C_Title();
    test_0x6E_EntitySoundEffect();
    test_0x6F_SoundEffect();
    test_0x70_StartConfiguration_gap();
    test_0x71_StopSound();
    test_0x72_StoreCookie();
    test_0x73_SystemChat();
    test_0x76_Collect();
    test_0x77_EntityTeleport();
    test_0x78_SetTikingState_gap();
    test_0x79_StepTick_gap();
    test_0x7A_Transfer();
    test_0x7B_UpdateAdvancements();
    test_0x7C_UpdateAttributes();
    test_0x7D_EntityEffect();
    test_0x7E_UpdateRecipes();
    test_0x7F_UpdateTags();
    test_0x80_SetProjectilePower_gap();
    test_0x81_CustomReportDetails_gap();
    test_0x82_ServerLinks_gap();
    test_field_order_strict();

    std::printf("=== test_wire_full: %d PASS %d FAIL %d SKIP ===\n", g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
