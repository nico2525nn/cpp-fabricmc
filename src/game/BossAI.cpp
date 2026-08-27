#include "BossAI.hpp"
#include "GameServer.hpp"
#include "../core/NBT.hpp"
#include "../proto/Ids.hpp"
#include <cstdio>
#include <cmath>

namespace cppfm {

// ---------------------------------------------------------------- BossBarManager

std::array<std::uint8_t,16> BossBarManager::uuidForEntity(std::int32_t eid) {
    // Deterministic UUID derived from entity id (version 4 style variant)
    // Use FNV-like hash to fill 16 bytes, set version/variant bits for valid UUID.
    std::array<std::uint8_t,16> u{};
    // Simple deterministic fill
    uint32_t h = (uint32_t)eid * 0x9e3779b1u ^ 0x85ebca6bu;
    for (int i=0;i<4;i++) {
        u[i] = uint8_t((h >> (i*8)) & 0xFF);
        u[4+i] = uint8_t(((h*31) >> (i*8)) & 0xFF);
        u[8+i] = uint8_t(((h*0x27d4eb2d) >> (i*8)) & 0xFF);
        u[12+i]= uint8_t(((h*0x165667b1) >> (i*8)) & 0xFF);
    }
    // set version 4 and variant 8
    u[6] = (u[6] & 0x0F) | 0x40;
    u[8] = (u[8] & 0x3F) | 0x80;
    // ensure unique per eid for predictable testing
    u[12] ^= uint8_t(eid & 0xFF);
    u[13] ^= uint8_t((eid>>8)&0xFF);
    u[14] ^= uint8_t((eid>>16)&0xFF);
    u[15] ^= uint8_t((eid>>24)&0xFF);
    return u;
}

int BossBarManager::colorForKind(MobKind k) {
    switch(k) {
        case MobKind::Wither: return 4; // yellow? wither is black but packet uses 5 purple
        case MobKind::EnderDragon: return 5; // purple
        case MobKind::Warden: return 2; // red
        case MobKind::ElderGuardian: return 0; // pink
        default: return 6; // white
    }
}
std::string BossBarManager::titleForKind(MobKind k) {
    const char* n = MobEntity::kindName(k);
    std::string s(n);
    auto pos=s.find(':'); if(pos!=std::string::npos) s=s.substr(pos+1);
    // Capitalize first
    if(!s.empty()) s[0]=char(std::toupper((unsigned char)s[0]));
    // replace underscore with space
    for(char& c: s) if(c=='_') c=' ';
    return s;
}

void BossBarManager::sendAdd(GameServer& srv, const BossBar& bar) {
    WriteBuffer b;
    b.uuid(bar.uuid.data());
    b.varint(0); // ADD
    nbt::writeTextComponent(b, bar.title);
    b.f32(bar.health);
    b.varint(bar.color);
    b.varint(bar.division);
    b.u8(bar.flags);
    srv.broadcastPacketExcept(nullptr, proto::pl::sc::BossBar, b);
}
void BossBarManager::sendAddToPlayer(GameServer& srv, const BossBar& bar, Player& p) {
    (void)srv;
    if (!p.conn) return;
    WriteBuffer b;
    b.uuid(bar.uuid.data());
    b.varint(0);
    nbt::writeTextComponent(b, bar.title);
    b.f32(bar.health);
    b.varint(bar.color);
    b.varint(bar.division);
    b.u8(bar.flags);
    try { p.conn->sendPacket(proto::pl::sc::BossBar, b); } catch(...) {}
}
void BossBarManager::sendHealth(GameServer& srv, const BossBar& bar) {
    WriteBuffer b;
    b.uuid(bar.uuid.data());
    b.varint(2); // UPDATE_HEALTH
    b.f32(bar.health);
    srv.broadcastPacketExcept(nullptr, proto::pl::sc::BossBar, b);
}
void BossBarManager::sendRemove(GameServer& srv, const BossBar& bar) {
    WriteBuffer b;
    b.uuid(bar.uuid.data());
    b.varint(1); // REMOVE
    srv.broadcastPacketExcept(nullptr, proto::pl::sc::BossBar, b);
}

void BossBarManager::onBossSpawn(GameServer& srv, const MobEntity& mob) {
    if (!MobEntity::isBoss(mob.kind)) return;
    if (bars_.find(mob.entityId)!=bars_.end()) return;
    BossBar bar;
    bar.entityId = mob.entityId;
    bar.uuid = uuidForEntity(mob.entityId);
    bar.title = titleForKind(mob.kind);
    const float maxH = mobStats(mob.kind).maxHealth;
    bar.health = maxH>0 ? std::clamp(static_cast<float>(mob.health / maxH), 0.f, 1.f) : 1.f;
    bar.color = colorForKind(mob.kind);
    bar.division = 0;
    bar.flags = 0;
    bars_[mob.entityId] = bar;
    sendAdd(srv, bar);
    std::fprintf(stderr,"[cppfm] BossBar ADD %s eid=%d health=%.2f\n", bar.title.c_str(), mob.entityId, bar.health);
}
void BossBarManager::onBossDamage(GameServer& srv, const MobEntity& mob) {
    auto it = bars_.find(mob.entityId);
    if (it==bars_.end()) return;
    const float maxH = mobStats(mob.kind).maxHealth;
    float nh = maxH>0 ? std::clamp(static_cast<float>(mob.health / maxH), 0.f, 1.f) : 0.f;
    if (std::abs(it->second.health - nh) < 0.001f) return;
    it->second.health = nh;
    sendHealth(srv, it->second);
}
void BossBarManager::onBossRemove(GameServer& srv, std::int32_t eid) {
    auto it = bars_.find(eid);
    if (it==bars_.end()) return;
    sendRemove(srv, it->second);
    bars_.erase(it);
    std::fprintf(stderr,"[cppfm] BossBar REMOVE eid=%d\n", eid);
}
void BossBarManager::onPlayerJoin(GameServer& srv, Player& p) {
    for (auto& kv : bars_) {
        sendAddToPlayer(srv, kv.second, p);
    }
}

// ---------------------------------------------------------------- Wither / Dragon AI

void WitherBossAI::onSpawn(GameServer& srv, MobEntity& mob) {
    (void)srv; (void)mob;
    // init phase: invuln period? set witherSkullCooldown to 0, dragonPhase unused
    mob.witherSkullCooldown = 0;
}
void WitherBossAI::onDamage(GameServer& srv, MobEntity& mob) {
    (void)srv; (void)mob;
    // could trigger enrage when <50% health: faster skulls
}
void WitherBossAI::onDeath(GameServer& srv, MobEntity& mob) {
    (void)srv; (void)mob;
}
void WitherBossAI::tick(GameServer& srv, MobEntity& mob, AiContext& ctx, std::int64_t now) {
    (void)srv; (void)ctx; (void)now;
    // Wither specific tick is handled via BehaviorTree WitherSkullAction; here we just keep alive
    // Could add second phase: when health <150, spawn wither skulls more frequently: reduce cooldown
    const float maxH = mobStats(mob.kind).maxHealth;
    if (maxH>0 && mob.health < maxH*0.5f) {
        // enraged: halve cooldown if not already
        if (mob.witherSkullCooldown > now + 10) mob.witherSkullCooldown = (int)(now + 10);
    }
}

void DragonBossAI::onSpawn(GameServer& srv, MobEntity& mob) {
    (void)srv;
    mob.dragonPhase = 0;
    mob.dragonPhaseUntil = srv.tickNow() + 120;
}
void DragonBossAI::onDamage(GameServer& srv, MobEntity& mob) { (void)srv; (void)mob; }
void DragonBossAI::onDeath(GameServer& srv, MobEntity& mob) { (void)srv; (void)mob; }
void DragonBossAI::tick(GameServer& srv, MobEntity& mob, AiContext& ctx, std::int64_t now) {
    (void)srv; (void)ctx; (void)now;
    // Dragon phases handled in DragonBreathAction node; nothing extra
}

// ---------------------------------------------------------------- BossAIManager

BossAIManager::BossAIManager(GameServer& srv) : srv_(srv) {}

void BossAIManager::onSpawn(MobEntity& mob) {
    if (!MobEntity::isBoss(mob.kind)) return;
    bars_.onBossSpawn(srv_, mob);
    if (wither_.isBoss(mob.kind)) wither_.onSpawn(srv_, mob);
    else if (dragon_.isBoss(mob.kind)) dragon_.onSpawn(srv_, mob);
}
void BossAIManager::onDamage(MobEntity& mob) {
    if (!MobEntity::isBoss(mob.kind)) return;
    bars_.onBossDamage(srv_, mob);
    if (wither_.isBoss(mob.kind)) wither_.onDamage(srv_, mob);
    else if (dragon_.isBoss(mob.kind)) dragon_.onDamage(srv_, mob);
}
void BossAIManager::onDeath(MobEntity& mob) {
    if (!MobEntity::isBoss(mob.kind)) return;
    // keep bar until removal after death animation? immediate remove
    bars_.onBossRemove(srv_, mob.entityId);
    if (wither_.isBoss(mob.kind)) wither_.onDeath(srv_, mob);
    else if (dragon_.isBoss(mob.kind)) dragon_.onDeath(srv_, mob);
}
void BossAIManager::tick(MobEntity& mob, AiContext& ctx, std::int64_t now) {
    if (wither_.isBoss(mob.kind)) wither_.tick(srv_, mob, ctx, now);
    else if (dragon_.isBoss(mob.kind)) dragon_.tick(srv_, mob, ctx, now);
}
void BossAIManager::onPlayerJoin(Player& p) {
    bars_.onPlayerJoin(srv_, p);
}

} // namespace cppfm
