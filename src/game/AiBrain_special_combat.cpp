// AI goal implementations grouped by behavior domain.

#include "AiBrain.hpp"
#include "AiBrainInternal.hpp"
#include "BehaviorTree.hpp"
#include "GameServer.hpp"
#include "AiBehaviorSupport.hpp"
#include "MobGrounding.hpp"
#include "MetadataTypes.hpp"
#include "MobBehaviorSpec.hpp"
#include "Particles.hpp"
#include "GameServerHelpers.hpp"
#include "../proto/Ids.hpp"
#include "../generated/BlockStates.hpp"
#include <optional>
#include <string_view>
#include <utility>

namespace cppfm {

using namespace ai_detail;
using ai_brain_detail::dimensionPlayer;
using ai_brain_detail::nearestPlayerView;
using ai_brain_detail::raycastObstructed;

bool WardenSonicBoomGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Warden) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()%5!=0) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 15*15;
}


bool WardenSonicBoomGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Warden) return false;
    if(now < m.wardenSonicCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t||ctx.nearestPlayerDist2>15*15) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    const float targetYaw = (float)(std::atan2(t->z-mobZ,t->x-mobX)*180/3.14159);
    if(ctx.srv){
        if(!raycastObstructed(dimensionWorld(ctx, m),t->x,t->y+1,t->z,
                              mobX,mobY+0.9,mobZ)){
            WriteBuffer vel; vel.varint(t->entityId); vel.i16(0); vel.i16((int16_t)(1.5*8000)); vel.i16(0);
            const WriteBuffer particle = makeWorldParticlesBody(
                mobX, mobY + 1.6, mobZ, 0, 0, 0, 0.1f, 1,
                ParticleId::sonic_boom, {}, true, false);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastHurtAnimationFor(
                        dimension, t->entityId, targetYaw);
                    ctx.srv->applyDamage(*t->player, 10.f,
                                         DamageSource::sonicBoom());
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.warden.sonic_boom",
                        mobX,mobY,mobZ,1.f,1.f,"hostile");
                    ctx.srv->broadcastEntitySoundFor(
                        dimension, entityId,
                        "minecraft:entity.warden.sonic_boom", 1.f, 1.f,
                        GameServer::SoundSource::Hostile);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityVelocity,
                        vel);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::WorldParticles,
                        particle);
                })) return false;
        }
    }
    m.wardenSonicCooldown=now+34;
    return true;
}


bool GuardianBeamGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Guardian && m.kind!=MobKind::ElderGuardian) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    return spec && dimensionPlayer(ctx, m, ctx.nearestPlayer) && spec->withinActionRangeSquared(ctx.nearestPlayerDist2);
}


bool GuardianBeamGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Guardian && m.kind!=MobKind::ElderGuardian) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.guardianBeamCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX=m.x, mobY=m.y, mobZ=m.z;
    double dx=t->x-mobX, dz=t->z-mobZ; double d=std::sqrt(dx*dx+dz*dz);
    if (!spec->withinActionRange(d)) return false;
    if(ctx.srv){
        const double magnitude = m.kind == MobKind::ElderGuardian
            ? spec->secondaryActionMagnitude() : spec->actionMagnitude();
        float dmg = static_cast<float>(magnitude);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->applyDamage(*t->player, dmg, DamageSource::magic());
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.guardian.attack",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
                ctx.srv->broadcastEntitySoundFor(
                    dimension, entityId, "minecraft:entity.guardian.attack",
                    1.f, 1.f, GameServer::SoundSource::Hostile);
            })) return false;
    }
    m.guardianBeamCooldown=now+spec->actionCooldown();
    return true;
}


bool HoglinRepelGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Hoglin) return false;
    if(!dimensionWorld(ctx, m)) return false;
    for(int dx=-7; dx<=7; ++dx) for(int dz=-7; dz<=7; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name); if(n.find("warped_fungus")!=std::string::npos || n.find("respawn_anchor")!=std::string::npos || n.find("nether_portal")!=std::string::npos) return true;
    }
    return false;
}


bool HoglinRepelGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Hoglin) return false;
    if(now < m.hoglinRepelCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer);
    double dx, dz;
    if(t){ dx=m.x - t->x; dz=m.z - t->z; } else { dx=(nextRandom()/(double)RAND_MAX-0.5)*2; dz=(nextRandom()/(double)RAND_MAX-0.5)*2; }
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x+=dx/d*0.14; m.z+=dz/d*0.14; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    m.hoglinRepelCooldown=now+10;
    return true;
}


bool ZoglinFrenzyGoal::shouldStart(MobEntity& m, AiContext&) { return m.kind==MobKind::Zoglin; }


bool ZoglinFrenzyGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Zoglin) return false;
    if(now < m.zoglinFrenzyUntil) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<1.9){
        if(now%15==0 && ctx.srv) {
            WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(dx/d*1.0*8000)); vel.i16((int16_t)(0.4*8000)); vel.i16((int16_t)(dz/d*1.0*8000));
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t->player);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityVelocity,
                        vel);
                })) return false;
        }
        return true;
    }
    m.x+=dx/d*0.14; m.z+=dz/d*0.14; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    m.zoglinFrenzyUntil=now+10;
    return true;
}


bool WitherSkullBarrageGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Wither) return false; return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 24*24 && m.health <= mobStats(m.kind).maxHealth*0.5f; }


bool WitherSkullBarrageGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Wither) return false;
    if(now < m.witherBarrageCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    if(m.health > mobStats(m.kind).maxHealth*0.5f) return false;
    if(ctx.srv){
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double mobX=m.x,mobY=m.y,mobZ=m.z;
        const double dx=t->x-mobX, dy=(t->y+1)-(mobY+1.5), dz=t->z-mobZ;
        const double d=std::sqrt(dx*dx+dz*dz)+1e-6;
        if (!withoutMobStateLock(m, [&] {
                for(int i=0;i<3;++i){
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::WitherSkull,
                        mobX, mobY+1.5, mobZ,
                        dx/d*1.1+(nextRandom()/(double)RAND_MAX-0.5)*0.1,
                        dy/d*0.3+0.1,
                        dz/d*1.1+(nextRandom()/(double)RAND_MAX-0.5)*0.1,
                        entityId, false, true);
                }
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.wither.shoot",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
            })) return false;
    }
    m.witherBarrageCooldown=now+60;
    return true;
}


bool IllusionerInvisGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Illusioner) return false; return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 12*12; }


bool IllusionerInvisGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Illusioner) return false;
    if(now < m.illusionerInvisUntil) return false;
    m.illusionerInvisUntil=now+200;
    if(ctx.srv){
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double mobX=m.x,mobY=m.y,mobZ=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        const auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer);
        double vx=0,vz=0;
        bool shoot=false;
        WriteBuffer eff;
        if (t) {
            const double dx=t->x-mobX, dz=t->z-mobZ;
            const double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            vx=dx/d*1.2; vz=dz/d*1.2; shoot=true;
            eff.varint(t->entityId); eff.varint(15); eff.i8(1);
            eff.varint(100); eff.u8(0x01);
        }
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.illusioner.cast_spell",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
                if (shoot) {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::Arrow,
                        mobX, mobY+1.6, mobZ, vx, 0.12, vz,
                        entityId, false);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                }
            })) return false;
    }
    return true;
}


bool BoggedPoisonGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Bogged) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    return spec && dimensionPlayer(ctx, m, ctx.nearestPlayer) && spec->withinActionRangeSquared(ctx.nearestPlayerDist2);
}


bool BoggedPoisonGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Bogged) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.boggedPoisonCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension=canonicalDimension(m.dimension);
    const auto entityId=m.entityId;
    const double mobX=m.x,mobY=m.y,mobZ=m.z;
    double dx=t->x-mobX, dy=(t->y+1)-(mobY+1.6), dz=t->z-mobZ; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(!spec->withinActionRange(d) || !spec->beyondActionThreshold(d)) return false;
    if(ctx.srv){
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(19); eff.i8(0); eff.varint(160); eff.u8(0x01);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->spawnProjectileFor(
                    dimension, ProjectileKind::Arrow,
                    mobX, mobY+1.6, mobZ, dx/d*1.2,
                    dy/d*0.2+0.12, dz/d*1.2, entityId, false);
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.bogged.shoot",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
            })) return false;
    }
    m.boggedPoisonCooldown=now+spec->actionCooldown();
    return true;
}


bool VexChargeGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Vex) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Vex); // plan44 G-05
}


bool VexChargeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Vex) return false;
    if (now < m.vexChargeCooldown) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dy=(t->y+1)-(m.y+1), dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            const auto dimension=canonicalDimension(m.dimension);
            const double x=m.x,y=m.y,z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t->player);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.vex.charge",
                        x,y,z,1.f,1.f,"hostile");
                })) return false;
        }
        m.vexChargeCooldown = now+20;
        return true;
    }
    // charge through air at 0.3 (vex ignores gravity while charging)
    m.x += dx/d*0.30; m.z += dz/d*0.30; m.y += dy*0.05;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    m.vexChargeCooldown = now+5;
    return true;
}


bool ZombifiedPiglinAngerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::ZombifiedPiglin) return false;
    return ctx.srv &&
        (ctx.srv->tickNoForTest() -
             ctx.lastHurtTick.load(std::memory_order_acquire) < 200);
}


bool ZombifiedPiglinAngerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::ZombifiedPiglin) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv && !withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t->player);
            })) return false;
        return true;
    }
    // pack anger: nearby zombified piglins converge (vanilla anger propagation)
    if (ctx.srv) {
        const auto dimension=canonicalDimension(m.dimension);
        const double mobX=m.x,mobZ=m.z;
        std::vector<std::pair<std::shared_ptr<MobEntity>, MobSnapshot>> pack;
        if (!withoutMobStateLock(m, [&] {
                for (auto& mm : ctx.srv->mobsSnapshot()) {
                    if (!mm || mm.get()==&m) continue;
                    const MobSnapshot view=snapshotMob(*mm);
                    if (view.dimension != dimension ||
                        view.kind!=MobKind::ZombifiedPiglin || view.dead)
                        continue;
                    const double ox=view.x-mobX, oz=view.z-mobZ;
                    if (ox*ox+oz*oz < 16*16)
                        pack.emplace_back(mm, view);
                }
            })) return false;
        for (auto& entry : pack) {
            const auto& mm=entry.first;
            const auto& view=entry.second;
            if (!withoutMobStateLock(m, [&] {
                    std::scoped_lock pairLock(*m.stateMtx, *mm->stateMtx);
                    if (mm->entityId != view.entityId || mm->dead ||
                        canonicalDimension(mm->dimension) != dimension)
                        return;
                    mm->x += dx/d*0.08;
                    mm->z += dz/d*0.08;
                })) return false;
        }
    }
    m.x += dx/d*0.11; m.z += dz/d*0.11;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}


bool GiantStompGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Giant) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 24*24;
}


bool GiantStompGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Giant) return false;
    if (now < m.giantStompCooldown) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 2.5) {
        if (ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x = m.x, y = m.y, z = m.z;
            const auto entityId = m.entityId;
            WriteBuffer vel; vel.varint(t->entityId);
            vel.i16((int16_t)(dx/d*1.2*8000)); vel.i16((int16_t)(0.5*8000)); vel.i16((int16_t)(dz/d*1.2*8000));
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t->player);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.giant.stomp",
                        x, y, z, 1.f, 1.f, "hostile");
                })) return false;
            if (m.entityId != entityId ||
                canonicalDimension(m.dimension) != dimension)
                return false;
        }
        m.giantStompCooldown = now+40;
        return true;
    }
    m.x += dx/d*0.08; m.z += dz/d*0.08;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}


bool EvokerFangsSnapGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::EvokerFangs) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.evokerFangsSnapCooldown) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 2*2;
}


bool EvokerFangsSnapGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::EvokerFangs) return false;
    if (now < m.evokerFangsSnapCooldown) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->applyDamage(*t->player, 6.f, "magic");
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.evoker_fangs.attack",
                    x, y, z, 1.f, 1.f, "hostile");
            })) return false;
    }
    m.evokerFangsSnapCooldown = now+40;
    return true;
}


} // namespace cppfm
