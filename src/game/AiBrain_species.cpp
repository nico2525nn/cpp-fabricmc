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

bool ArmadilloRollUpGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Armadillo) return false;
    if (!mobBehaviorSpec(m.kind)) return false;
    // Scan at the descriptor's configured cadence; danger comes from AiContext or pending TTL.
    return ctx.dangerDetectedRecently || m.armadilloDangerDetectedUntil > 0;
}


bool ArmadilloRollUpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Armadillo) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    bool danger = ctx.dangerDetectedRecently || now < m.armadilloDangerDetectedUntil;
    // water check: immediate unroll if in water (simplified: y below sea or block water)
    if (dimensionWorld(ctx, m)) {
        uint16_t st = dimensionWorld(ctx, m)->getBlock((int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z));
        auto* bd = gen::blockByState(st);
        if (bd && std::string(bd->name).find("water")!=std::string::npos) danger = false;
    }
    if (danger && !m.armadilloRolledUp) {
        m.armadilloRolledUp = true;
        m.armadilloRollUpUntil = now + spec->actionCooldown();
        if (ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const auto entityId = m.entityId;
            const double x = m.x, y = m.y, z = m.z;
            WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md, 16, 1); md.u8(255);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.armadillo.roll",
                        x, y, z, 1.f, 1.f, "neutral");
                })) return false;
        }
        return true;
    }
    if (m.armadilloRolledUp) {
        if (now > m.armadilloRollUpUntil) {
            if (!danger) {
                m.armadilloRolledUp = false;
                if (ctx.srv) {
                    const auto dimension = canonicalDimension(m.dimension);
                    const auto entityId = m.entityId;
                    WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md, 16, 0); md.u8(255);
                    if (!withoutMobStateLock(m, [&] {
                            ctx.srv->broadcastPacketExceptInDimension(
                                dimension, nullptr,
                                proto::pl::sc::SetEntityMetadata, md);
                        })) return false;
                }
                return false;
            } else {
                m.armadilloRollUpUntil = now + spec->secondaryCooldown();
            }
        }
        return true;
    }
    return false;
}


bool BeePollinateGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Bee) return false;
    if (m.beeHasNectar) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.beePollenUntil) return false;
    return true;
}


bool BeePollinateGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Bee) return false;
    if (m.beeHasNectar) return false;
    // scan for flower every 20 ticks
    if (now % 20 != 0) return false;
    if (!dimensionWorld(ctx, m)) return false;
    for (int dx=-8; dx<=8; ++dx) for (int dz=-8; dz<=8; ++dz) for (int dy=-2; dy<=2; ++dy){
        int bx=(int)std::floor(m.x)+dx, by=(int)std::floor(m.y)+dy, bz=(int)std::floor(m.z)+dz;
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name);
        if (n.find("flower")!=std::string::npos || n=="minecraft:dandelion" || n=="minecraft:poppy") {
            double ddx=bx+0.5 - m.x, ddz=bz+0.5 - m.z; double d=std::sqrt(ddx*ddx+ddz*ddz)+1e-6;
            m.x += ddx/d*0.08; m.z += ddz/d*0.08;
            if(d<1.2){
                m.beeHasNectar=true; m.beePollenUntil=now+400;
                if(ctx.srv){
                    const auto dimension = canonicalDimension(m.dimension);
                    const auto entityId = m.entityId;
                    WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,17,true); md.u8(255);
                    if (!withoutMobStateLock(m, [&] {
                            ctx.srv->broadcastPacketExceptInDimension(
                                dimension, nullptr,
                                proto::pl::sc::SetEntityMetadata, md);
                        })) return false;
                }
                return true;
            }
            return true;
        }
    }
    // no flower: wander
    return false;
}


bool VillagerScheduleGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Villager && m.kind != MobKind::WanderingTrader) return false;
    int tod = (int)(ctx.srv ? ctx.srv->dayTime()%24000 : now%24000);
    const char* act = activityFor(tod);
    std::string a(act);
    if (a == "work") {
        if (nextRandom()%40==0 && ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x=m.x, y=m.y, z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.villager.work",
                        x,y,z,0.5f,1.f,"neutral");
                })) return false;
        }
    } else if (a == "gather" || a == "mingle" || a == "wander" || a == "play") {
        // midday/afternoon movement: gather/mingle/play drift, wander wider
        if (nextRandom()%20==0){ double ang=nextRandom()/(double)RAND_MAX*6.28; double st=(a=="wander"?0.08:0.04); m.x+=std::cos(ang)*st; m.z+=std::sin(ang)*st; }
        if (a == "mingle" && nextRandom()%60==0 && ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x=m.x, y=m.y, z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.villager.ambient",
                        x,y,z,0.4f,1.f,"neutral");
                })) return false;
        }
    }
    // restock tick already in mobsTick
    return true;
}


bool PiglinBarterGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Piglin) return false;
    const auto target = dimensionPlayer(ctx, m, ctx.nearestPlayer);
    if (!target) return false;
    if (ctx.nearestPlayerDist2 > 8*8) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.piglinBarterCooldown) return false;
    // The held item is part of the same-tick player snapshot.  Do not reach
    // back into Player::inv while the Mob lock is held.
    const auto gold = gen::itemIdByName().find("minecraft:gold_ingot");
    return gold != gen::itemIdByName().end() &&
           !target->heldItemEmpty && target->heldItemId == gold->second;
}


bool PiglinBarterGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Piglin) return false;
    m.piglinBarterCooldown = now + 100;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.piglin.admiring_item",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}


bool CatScareGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Cat) return false;
    if (!dimensionPlayer(ctx, m, ctx.nearestPlayer)) return false;
    return ctx.nearestPlayerDist2 < 6*6;
}


bool CatScareGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Cat) return false;
    if (now < m.catScareCooldown) return false;
    // cat scares creeper: just sit and purr
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.cat.purr",
                    x,y,z,0.5f,1.f,"neutral");
            })) return false;
    }
    m.catScareCooldown = now + 60;
    // creeper avoid handled elsewhere
    return true;
}


bool FoxPounceGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Fox) return false;
    if (!dimensionPlayer(ctx, m, ctx.nearestPlayer)) return false;
    double d=std::sqrt(ctx.nearestPlayerDist2); return d>=2 && d<=6;
}


bool FoxPounceGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Fox) return false;
    if (now < m.foxPounceCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d*0.42; m.z += dz/d*0.42; m.y += 0.38;
    if(ctx.srv){
        const auto entityId = m.entityId;
        WriteBuffer vel; vel.varint(entityId); vel.i16((int16_t)(dx/d*0.42*8000)); vel.i16((int16_t)(0.38*8000)); vel.i16((int16_t)(dz/d*0.42*8000));
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastMobPositionUpdate(
                    m, proto::pl::sc::EntityVelocity, vel);
            })) return false;
    }
    m.foxPounceCooldown = now + 40;
    return true;
}


bool PandaRollGoal::shouldStart(MobEntity& m, AiContext&) {
    if (m.kind != MobKind::Panda) return false;
    return (nextRandom()%200==0);
}


bool PandaRollGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Panda) return false;
    if (now < m.pandaRollCooldown) return false;
    m.pandaRollCooldown = now + 100;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.panda.cant_breed",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}


bool DolphinPlayGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Dolphin) return false;
    if (!dimensionPlayer(ctx, m, ctx.nearestPlayer)) return false;
    return ctx.nearestPlayerDist2 < 10*10;
}


bool DolphinPlayGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Dolphin) return false;
    if (now < m.dolphinPlayCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d*0.13; m.z += dz/d*0.13; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (ctx.srv && nextRandom()%30==0) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.dolphin.play",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    m.dolphinPlayCooldown = now + 20;
    return true;
}


bool AxolotlPlayDeadGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Axolotl) return false; if(m.health > mobStats(m.kind).maxHealth*0.33) return false; return ctx.lastHurtTick.load(std::memory_order_acquire)>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick.load(std::memory_order_acquire)<20; }


bool AxolotlPlayDeadGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Axolotl) return false;
    if(now < m.axolotlPlayDeadUntil && m.axolotlPlayDeadUntil!=0) return true;
    m.axolotlPlayDeadUntil=now+200;
    m.health = std::min(m.health+2.0, (double)mobStats(m.kind).maxHealth);
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md, 16, true); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.axolotl.splash",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}


bool FrogTongueGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Frog) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()<m.frogTongueCooldown) return false;
    return true;
}


bool FrogTongueGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Frog) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.frogTongueCooldown) return false;
    // Find a small slime/magma cube within the spec's action range.
    if(ctx.srv){
        std::shared_ptr<MobEntity> prey;
        MobSnapshot preyState;
        const auto dimension = canonicalDimension(m.dimension);
        const double mobX=m.x, mobY=m.y, mobZ=m.z;
        double best=0.0;
        if (!withoutMobStateLock(m, [&] {
                for(auto& mm: ctx.srv->mobsSnapshot()) {
                    if (!mm || mm.get() == &m) continue;
                    const MobSnapshot view = snapshotMob(*mm);
                    if (view.dimension != dimension ||
                        (view.kind!=MobKind::Slime &&
                         view.kind!=MobKind::MagmaCube) ||
                        view.slimeSize!=0 || view.dead) continue;
                    const double dx=view.x-mobX, dz=view.z-mobZ;
                    const double d2=dx*dx+dz*dz;
                    if(spec->withinActionRangeSquared(d2) &&
                       (!prey || d2<best)) {
                        best=d2; prey=mm; preyState=view;
                    }
                }
            })) return false;
        if(prey){
            double dx=preyState.x-mobX, dz=preyState.z-mobZ;
            double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            if(!spec->beyondActionThreshold(d)){
                const auto slimeBall = gen::itemIdByName().at("minecraft:slime_ball");
                const auto preyId = preyState.entityId;
                bool consumed = false;
                if (!withoutMobStateLock(m, [&] {
                        std::lock_guard preyLock(*prey->stateMtx);
                        if (prey->entityId != preyId ||
                            canonicalDimension(prey->dimension) != dimension ||
                            prey->dead || prey->slimeSize != 0)
                            return;
                        prey->dead=true;
                        consumed = true;
                        ctx.srv->broadcastSoundFor(
                            dimension, "minecraft:entity.frog.eat",
                            mobX,mobY,mobZ,1.f,1.f,"neutral");
                        ctx.srv->spawnItemDropFor(
                            dimension, mobX,mobY,mobZ, slimeBall,
                            static_cast<std::uint8_t>(spec->actionMagnitude()));
                    })) return false;
                if (!consumed) return true;
            } else {
                m.x+=dx/d*0.12; m.z+=dz/d*0.12;
                const double x=m.x,y=m.y,z=m.z;
                if (!withoutMobStateLock(m, [&] {
                        ctx.srv->broadcastSoundFor(
                            dimension, "minecraft:entity.frog.tongue",
                            x,y,z,1.f,1.f,"neutral");
                    })) return false;
            }
        } else if(nextRandom()%spec->randomDenominator()==0){
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.frog.ambient",
                        mobX,mobY,mobZ,1.f,1.f,"neutral");
                })) return false;
        }
    }
    m.frogTongueCooldown=now+spec->actionCooldown();
    return true;
}


bool TurtleEggLayGoal::shouldStart(MobEntity& m, AiContext&){ if(m.kind!=MobKind::Turtle) return false; return m.turtleHomePos[0]!=INT_MAX; }


bool TurtleEggLayGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Turtle) return false;
    if(now < m.turtleEggCooldown) return false;
    if(m.turtleHomePos[0]==INT_MAX){
        m.turtleHomePos={(int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z)};
    }
    double tx=m.turtleHomePos[0]+0.5, tz=m.turtleHomePos[2]+0.5;
    double dx=tx-m.x, dz=tz-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d>1.5){
        m.x+=dx/d*0.07; m.z+=dz/d*0.07; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
        if (World* world=dimensionWorld(ctx, m)) {
            const int chunkX=static_cast<int>(m.x)>>4;
            const int chunkZ=static_cast<int>(m.z)>>4;
            if (!withoutMobStateLock(m, [&] {
                    world->generateChunkIfMissing(chunkX, chunkZ);
                })) return false;
        }
        return true;
    }
    // lay 1-4 eggs (simulate by placing turtle_egg block)
    if(dimensionWorld(ctx, m) && ctx.srv){
        int ex=(int)std::floor(m.x), ey=(int)std::floor(m.y), ez=(int)std::floor(m.z);
        auto* bd=gen::blockByName("minecraft:turtle_egg");
        if(bd){
            World* world=dimensionWorld(ctx, m);
            const auto dimension=canonicalDimension(m.dimension);
            const double mobX=m.x,mobY=m.y,mobZ=m.z;
            auto baby=std::make_shared<MobEntity>();
            baby->entityId=ctx.srv->nextEntityId();
            baby->kind=MobKind::Turtle;
            baby->health=mobStats(MobKind::Turtle).maxHealth;
            baby->age=-24000; baby->x=ex+0.5; baby->y=ey+1;
            baby->z=ez+0.5; baby->dimension=dimension;
            if (!withoutMobStateLock(m, [&] {
                        const auto eggState = static_cast<std::uint16_t>(bd->defaultState);
                        world->setBlock(ex,ey,ez, eggState);
                        ctx.srv->broadcastBlockChangeFor(
                        dimension, ex,ey,ez, eggState);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.turtle.lay_egg",
                        mobX,mobY,mobZ,1.f,1.f,"neutral");
                    jvm::MobSpawnAdmission admission(
                        ctx.srv->jvmRuntime(), *baby,
                        &cancelMobSpawnOutsideMobStateLock);
                    if (admission.approve(baby->x, baby->y, baby->z) &&
                        ctx.srv->addMob(baby)) {
                        admission.commit();
                        ctx.srv->broadcastMobSpawn(*baby);
                    }
                })) return false;
        }
    }
    m.turtleEggCooldown=now+6000;
    auto& rm=m.turtleHomePos; rm={INT_MAX,INT_MAX,INT_MAX};
    return true;
}


bool ParrotDanceGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Parrot) return false;
    if(!dimensionWorld(ctx, m)) return false;
    // near jukebox playing: check within 6 for jukebox block
    for(int dx=-6; dx<=6; ++dx) for(int dz=-6; dz<=6; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        if(std::string(bd->name).find("jukebox")!=std::string::npos) return true;
    }
    return false;
}


bool ParrotDanceGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Parrot) return false;
    m.parrotDancing=true; m.parrotDanceUntil=now+40;
    m.yaw += 18; if(m.yaw>360) m.yaw-=360;
    if(ctx.srv && now%20==0) {
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.parrot.imitate.warden",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}


bool OcelotTrustGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Ocelot) return false;
    return static_cast<bool>(dimensionPlayer(ctx, m, ctx.temptingPlayer)) ||
           (dimensionPlayer(ctx, m, ctx.nearestPlayer) &&
            ctx.nearestPlayerDist2 < 10*10);
}


bool OcelotTrustGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Ocelot) return false;
    if(now < m.ocelotTrustCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.temptingPlayer);
    if (!t) t = dimensionPlayer(ctx, m, ctx.nearestPlayer);
    if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<2.5){
        m.isTamed=true;
        if(ctx.srv){
            const auto dimension=canonicalDimension(m.dimension);
            const auto entityId=m.entityId;
            const double x=m.x,y=m.y,z=m.z;
            WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.ocelot.ambient",
                        x,y,z,1.f,1.f,"neutral");
                })) return false;
        }
        m.ocelotTrustCooldown=now+100;
        return true;
    }
    // sprint 0.18 when creeper approach 6: already handled via Avoid? just move toward player
    m.x+=dx/d*0.09; m.z+=dz/d*0.09; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    m.ocelotTrustCooldown=now+20;
    return true;
}


bool SnowGolemSnowTrailGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::SnowGolem; }


bool SnowGolemSnowTrailGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::SnowGolem) return false;
    if(now < m.snowGolemTrailCooldown) return false;
    if(dimensionWorld(ctx, m) && ctx.srv){
        World* world = dimensionWorld(ctx, m);
        const auto dimension = canonicalDimension(m.dimension);
        int bx=(int)std::floor(m.x), by=(int)std::floor(m.y)-1, bz=(int)std::floor(m.z);
        uint16_t below=world->getBlock(bx,by,bz); if(below!=0){
            auto* bdSnow=gen::blockByName("minecraft:snow");
            if(bdSnow){
                int snowY=by+1;
                uint16_t at=world->getBlock(bx,snowY,bz);
                if(at==0){
                    if (!withoutMobStateLock(m, [&] {
                            const auto snowState = static_cast<std::uint16_t>(bdSnow->defaultState);
                            world->setBlock(bx,snowY,bz, snowState);
                            ctx.srv->broadcastBlockChangeFor(
                                dimension, bx,snowY,bz, snowState);
                        })) return false;
                }
            }
        }
        // shoot snowball
        const auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer);
        if(t && ctx.nearestPlayerDist2 < 16*16 && now%40==0){
            const double mobX=m.x,mobY=m.y,mobZ=m.z;
            const auto entityId=m.entityId;
            const double dx=t->x-mobX, dy=(t->y+1)-(mobY+1.2), dz=t->z-mobZ;
            const double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::Snowball,
                        mobX, mobY+1.2, mobZ, dx/d*1.2,
                        dy/d*0.2+0.1, dz/d*1.2, entityId, false);
                })) return false;
        }
        // melt in nether/desert biom check simplified: if y>60 and isNight false and biome desert -> melt damage
        std::string biome; try{ biome=dimensionWorld(ctx, m)->sampledBiome(bx,by,bz);}catch(...){}
        if(biome.find("desert")!=std::string::npos || biome.find("nether")!=std::string::npos){
            if(now%40==0 && ctx.srv &&
               !withoutMobStateLock(m, [&] {
                   ctx.srv->applyDamageToMob(m, 1.f, "burned to death");
               })) return false;
        }
    }
    m.snowGolemTrailCooldown=now+10;
    return true;
}


bool StriderLavaWalkGoal::shouldStart(MobEntity& m, AiContext&){
    return m.kind == MobKind::Strider && mobBehaviorSpec(m.kind) != nullptr;
}


bool StriderLavaWalkGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Strider) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(!dimensionWorld(ctx, m)) return false;
    uint16_t st=dimensionWorld(ctx, m)->getBlock((int)std::floor(m.x),(int)std::floor(m.y)-1,(int)std::floor(m.z));
    auto* bd=gen::blockByState(st);
    bool onLava = bd && std::string(bd->name).find("lava")!=std::string::npos;
    if(!onLava){
        // shiver when cold
        if(!m.striderShivering){
            m.striderShivering=true;
            m.striderShiverUntil=now+spec->actionCooldown();
            if(ctx.srv) {
                const auto dimension=canonicalDimension(m.dimension);
                const double x=m.x,y=m.y,z=m.z;
                if (!withoutMobStateLock(m, [&] {
                        ctx.srv->broadcastSoundFor(
                            dimension, "minecraft:entity.strider.ambient",
                            x,y,z,0.5f,1.f,"neutral");
                    })) return false;
            }
        }
        m.y -= 0.02;
    } else {
        m.striderShivering=false;
        // lava walk no sink, steer toward player if saddled
        if(const auto target=dimensionPlayer(ctx, m, ctx.nearestPlayer)){
            double dx=target->x-m.x, dz=target->z-m.z;
            double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            m.x+=dx/d*0.09; m.z+=dz/d*0.09;
        }
        if (World* world = dimensionWorld(ctx, m))
            m.y = std::max(m.y, static_cast<double>(world->minY() + 2));
    }
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}


bool SnifferDigGoal::shouldStart(MobEntity& m, AiContext&){
    return m.kind==MobKind::Sniffer && mobBehaviorSpec(m.kind) != nullptr;
}


bool SnifferDigGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Sniffer) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.snifferDigCooldown) return false;
    if(nextRandom()%spec->actionInterval()!=0) return false;
    // sniff 6s -> dig
    if(ctx.srv) {
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.sniffer.scenting",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    m.snifferDigCooldown=now+spec->actionCooldown();
    // after sniff, dig ancient seed after 6s simplified to immediate drop
    if(nextRandom()%spec->randomDenominator()==0 && ctx.srv){
        // drop torchflower seeds
        auto it = gen::itemIdByName().find("minecraft:torchflower_seeds");
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        const auto entityId=m.entityId;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                if(it!=gen::itemIdByName().end())
                    ctx.srv->spawnItemDropFor(
                        dimension, x,y,z, it->second,
                        static_cast<std::uint8_t>(spec->actionMagnitude()));
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.sniffer.digging",
                    x,y,z,1.f,1.f,"neutral");
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
            })) return false;
    }
    return true;
}


bool CamelDashGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Camel) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    return spec && dimensionPlayer(ctx, m, ctx.nearestPlayer) && spec->withinActionRangeSquared(ctx.nearestPlayerDist2);
}


bool CamelDashGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Camel) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.camelDashCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(!spec->withinActionRange(d) || !spec->beyondActionThreshold(d)) return false;
    m.x+=dx/d*0.42*10*0.1; m.z+=dz/d*0.42*10*0.1; // dash ~4.2 blocks scaled by tick
    if(ctx.srv){
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer vel; vel.varint(entityId); vel.i16((int16_t)(dx/d*0.42*8000)); vel.i16(0); vel.i16((int16_t)(dz/d*0.42*8000));
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastMobPositionUpdate(
                    m, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.camel.dash",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    m.camelDashCooldown=now+spec->actionCooldown();
    return true;
}


bool AllayDuplicateGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Allay) return false;
    if(!dimensionWorld(ctx, m)) return false;
    for(int dx=-4; dx<=4; ++dx) for(int dz=-4; dz<=4; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        if(std::string(bd->name).find("jukebox")!=std::string::npos) return true;
    }
    return false;
}


bool AllayDuplicateGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Allay) return false;
    if(now < m.allayDuplicateCooldown) return false;
    if(ctx.srv){
        // duplicate amethyst_shard emit note
        auto it=gen::itemIdByName().find("minecraft:amethyst_shard");
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                if(it!=gen::itemIdByName().end())
                    ctx.srv->spawnItemDropFor(dimension, x,y+1,z,
                                              it->second, 1);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:block.note_block.chime",
                    x,y,z,1.f,1.f,"block");
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
            })) return false;
    }
    m.allayDuplicateCooldown=now+120;
    return true;
}


bool FishSwimGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isAquaticKind(m.kind); }


bool FishSwimGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!MobEntity::isAquaticKind(m.kind)) return false;
    // vanilla FishSwimGoal: drift in water, speed from mobStats moveSpeed
    if (now - m.nextWanderAt > 60 || !m.hasTarget) {
        double ang = (nextRandom()/(double)RAND_MAX)*6.28318;
        m.tx = m.x + std::cos(ang)*4.0; m.tz = m.z + std::sin(ang)*4.0;
        m.hasTarget = true; m.nextWanderAt = now;
    }
    double dx = m.tx - m.x, dz = m.tz - m.z;
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 0.4) { m.hasTarget = false; return true; }
    float sp = mobStats(m.kind).moveSpeed;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    m.x += dx/d*sp; m.z += dz/d*sp;
    return true;
}


bool GrazeGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isGrazerKind(m.kind); }


bool GrazeGoal::tick(MobEntity& m, AiContext&, std::int64_t now) {
    if (!MobEntity::isGrazerKind(m.kind)) return false;
    // vanilla EatGrassGoal: head-down pause ~40t every ~120t cycle
    return (now % 120) < 40;
}


bool ChickenLayEggGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Chicken) return false;
    return ctx.srv && ctx.srv->tickNoForTest() >= m.chickenLayCooldown;
}


bool ChickenLayEggGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Chicken) return false;
    if (!ctx.srv || now < m.chickenLayCooldown) return false;
    auto it = gen::itemIdByName().find("minecraft:egg");
    const auto dimension = canonicalDimension(m.dimension);
    const double x = m.x, y = m.y, z = m.z;
    if (!withoutMobStateLock(m, [&] {
            if (it != gen::itemIdByName().end())
                ctx.srv->spawnItemDropFor(dimension, x, y, z, it->second, 1);
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.chicken.egg",
                x, y, z, 1.f, 1.f, "neutral");
        })) return false;
    m.chickenLayCooldown = now+6000+(nextRandom()%6000); // vanilla 5-10min
    return true;
}


bool BatRoostGoal::shouldStart(MobEntity& m, AiContext&) { return m.kind==MobKind::Bat; }


bool BatRoostGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Bat) return false;
    // vanilla Bat: roosts hanging upside-down when idle (day), flies at night.
    // Day branch holds still (roosting); night falls through to FlyWander json.
    bool night = ctx.srv ? ctx.srv->isNight() : (now%24000 > 13000);
    if (!night) return true; // roosting: hang still
    return false; // night: let fly_wander behavior drive
}


} // namespace cppfm
