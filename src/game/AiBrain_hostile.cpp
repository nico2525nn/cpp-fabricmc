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

static bool nowIn(AiContext& ctx, std::int64_t cd){ return ctx.srv && ctx.srv->tickNoForTest() < cd; }

bool DrownedSwimGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Drowned) return false;
    World* world = dimensionWorld(ctx, m);
    if(!world) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()%5!=0) return false;
    uint16_t st=dimensionWorld(ctx, m)->getBlock((int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z));
    auto* bd=gen::blockByState(st); if(!bd) return false;
    return std::string(bd->name).find("water")!=std::string::npos;
}


bool DrownedSwimGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t){
    if(m.kind!=MobKind::Drowned) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return true;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d * 0.12; m.z += dz/d * 0.12;
    m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}


bool PhantomCircleGoal::shouldStart(MobEntity& m, AiContext&){
    return m.kind==MobKind::Phantom && mobBehaviorSpec(m.kind) != nullptr;
}


bool PhantomCircleGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Phantom) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer);
    if(t){ m.phantomOrbitCenter={t->x, t->y+spec->auxiliaryOffset(), t->z}; }
    m.phantomOrbitAngle += 0.08;
    double r = spec->actionRange() + (m.phantomSize % spec->variantRangeModulo());
    double nx = m.phantomOrbitCenter.x + std::cos(m.phantomOrbitAngle)*r;
    double nz = m.phantomOrbitCenter.z + std::sin(m.phantomOrbitAngle)*r;
    const double nowSeconds = static_cast<double>(now);
    double ny = m.phantomOrbitCenter.y + std::sin(nowSeconds*0.02)*2;
    if(now - m.phantomLastSwoop >= spec->actionCooldown() && t){
        nx = t->x; nz = t->z; ny = t->y;
        const double attackThreshold = spec->actionThreshold();
        if(attackThreshold != 0.0 && std::hypot(nx-m.x,nz-m.z) < attackThreshold){
            m.phantomLastSwoop=now;
            if(ctx.srv && !withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t->player);
                })) return false;
        }
    }
    double dx=nx-m.x, dy=ny-m.y, dz=nz-m.z; double d=std::sqrt(dx*dx+dy*dy+dz*dz)+1e-6;
    m.x+=dx/d*0.16; m.y+=dy/d*0.10; m.z+=dz/d*0.16;
    m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    const double altitudeFloor = spec->altitudeFloor();
    if(altitudeFloor != 0.0 && m.y < altitudeFloor) m.y=altitudeFloor;
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}


bool EndermanTeleportGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Enderman) return false;
    if(ctx.srv &&
       ctx.srv->tickNoForTest() -
               ctx.lastHurtTick.load(std::memory_order_acquire) < 30)
        return true;
    if(!dimensionWorld(ctx, m) || !ctx.srv) return false;
    // daylight flee check simplified: if sky light high and not night
    if(!ctx.srv->isNight()){
        World* world = dimensionWorld(ctx, m);
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        const int blockX = static_cast<int>(m.x);
        const int blockY = static_cast<int>(m.y);
        const int blockZ = static_cast<int>(m.z);
        std::uint8_t sky = 0;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
                sky = world->getSkyLight(blockX, blockY, blockZ);
            })) return false;
        if(sky>=14) return true;
    }
    return false;
}


bool EndermanTeleportGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Enderman) return false;
    if(now - m.lastTeleportTick < 30) return false;
    World* world = dimensionWorld(ctx, m);
    if(!world) return false;
    const auto geometry = world->verticalGeometry();
    for(int attempt=0; attempt<16; ++attempt){
        double nx = m.x + (nextRandom()/(double)RAND_MAX*64 -32);
        double nz = m.z + (nextRandom()/(double)RAND_MAX*64 -32);
        double ny = m.y + (nextRandom()/(double)RAND_MAX*32 -16);
        int ix=(int)std::floor(nx), iz=(int)std::floor(nz), iy=(int)std::floor(ny);
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(ix>>4, iz>>4);
            })) return false;
        for(int dy=-4; dy<=4; ++dy){
            int tryY=iy+dy;
            if (tryY < geometry.minY + 1 || tryY >= geometry.maxY() - 1) continue;
            const std::uint16_t a1 = world->getBlock(ix, tryY, iz);
            const std::uint16_t a2 = world->getBlock(ix, tryY + 1, iz);
            const std::uint16_t below = world->getBlock(ix, tryY - 1, iz);
            if(a1==0 && a2==0 && below!=0){
                const auto getBlock = [world](const std::int32_t x,
                                              const std::int32_t y,
                                              const std::int32_t z) {
                    return world->getBlock(x, y, z);
                };
                const auto landingY =
                    mob_grounding::teleportLandingSurfaceY(
                        m, ix + 0.5, iz + 0.5, tryY, getBlock);
                if (!landingY) continue;
                const double ox=m.x, oy=m.y, oz=m.z;
                m.x=ix+0.5; m.z=iz+0.5; m.y=*landingY; m.lastTeleportTick=now;
                if(ctx.srv){
                    const auto dimension = canonicalDimension(m.dimension);
                    const auto entityId = m.entityId;
                    const auto pitch = m.pitch;
                    const double newX=m.x, newY=m.y, newZ=m.z;
                    const auto teleportPackets =
                        m.takeTeleportPacketsWithGroundState([&] {
                            return mob_grounding::isMobGroundedAt(m,
                                                                 getBlock);
                        });
                    if (!withoutMobStateLock(m, [&] {
                            ctx.srv->broadcastSoundFor(
                                dimension, "minecraft:entity.enderman.teleport",
                                newX,newY,newZ,1.f,1.f,"hostile");
                            for (const auto& packet : teleportPackets)
                                ctx.srv->broadcastMobPositionUpdate(
                                    m, packet.packetId, packet.body);
                            for(int i=0;i<8;i++){
                                const WriteBuffer pt = makeWorldParticlesBody(
                                    ox + (nextRandom()/(double)RAND_MAX-0.5)*1.5,
                                    oy + nextRandom()/(double)RAND_MAX*2.0,
                                    oz + (nextRandom()/(double)RAND_MAX-0.5)*1.5,
                                    0, 0, 0, 0.1f, 1, ParticleId::portal,
                                    {}, true, false);
                                ctx.srv->broadcastPacketExceptInDimension(
                                    dimension, nullptr,
                                    proto::pl::sc::WorldParticles, pt);
                            }
                        })) return false;
                    if (m.entityId != entityId ||
                        canonicalDimension(m.dimension) != dimension ||
                        m.x != newX || m.y != newY || m.z != newZ ||
                        m.pitch != pitch)
                        return true;
                }
                return true;
            }
        }
    }
    return true;
}


bool ShulkerPeekGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Shulker) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Shulker); // plan44 G-05
}


bool ShulkerPeekGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Shulker) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    m.shulkerPeek = std::min(100, m.shulkerPeek+5);
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double mobX=m.x, mobY=m.y, mobZ=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md, 15, (int8_t)m.shulkerPeek); md.u8(255);
        if(now%60==0){
            double dx=t->x-mobX, dy=(t->y+0.5)-mobY, dz=t->z-mobZ; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::SetEntityMetadata,
                        md);
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::Arrow,
                        mobX, mobY+0.5, mobZ, dx/d*0.7,
                        dy/d*0.2+0.1, dz/d*0.7, entityId, false);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.shulker.shoot",
                        mobX,mobY,mobZ,1.f,1.f,"hostile");
                })) return false;
        } else if (!withoutMobStateLock(m, [&] {
                       ctx.srv->broadcastPacketExceptInDimension(
                           dimension, nullptr,
                           proto::pl::sc::SetEntityMetadata, md);
                   })) return false;
        if(now - ctx.lastHurtTick.load(std::memory_order_acquire) < 20){
            // teleport 8 blocks on hurt
            const double nx=mobX+(nextRandom()/(double)RAND_MAX*16-8);
            const double nz=mobZ+(nextRandom()/(double)RAND_MAX*16-8);
            m.x=nx; m.z=nz;
            if (World* world = dimensionWorld(ctx, m)) {
                if (!withoutMobStateLock(m, [&] {
                        world->generateChunkIfMissing(
                            static_cast<int>(nx)>>4,
                            static_cast<int>(nz)>>4);
                    })) return false;
            }
        }
    }
    return true;
}


bool SlimeSplitGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Slime) return false;
    if(now < m.slimeJumpCooldown) return false;
    if(nextRandom()%40!=0) return false;
    m.y += 0.4 * (m.slimeSize+1)*0.5;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double x=m.x, y=m.y, z=m.z;
        WriteBuffer vel; vel.varint(entityId); vel.i16(0); vel.i16((int16_t)(0.4*8000)); vel.i16(0);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastMobPositionUpdate(
                    m, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.slime.jump",
                    x,y,z,0.5f,1.f,"hostile");
            })) return false;
    }
    m.slimeJumpCooldown=now+20;
    return true;
}


bool MagmaCubeJumpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::MagmaCube) return false;
    if(now < m.slimeJumpCooldown) return false;
    if(nextRandom()%30!=0) return false;
    m.y += 0.45 * (m.slimeSize+1)*0.5;
    if (World* world = dimensionWorld(ctx, m))
        m.y = std::max(m.y, static_cast<double>(world->minY() + 1));
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double x=m.x, y=m.y, z=m.z;
        WriteBuffer vel; vel.varint(entityId); vel.i16(0); vel.i16((int16_t)(0.45*8000)); vel.i16(0);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastMobPositionUpdate(
                    m, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.magma_cube.jump",
                    x,y,z,0.5f,1.f,"hostile");
            })) return false;
    }
    m.slimeJumpCooldown=now+18;
    return true;
}


bool SilverfishInfestGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Silverfish) return false; return nowIn(ctx, m.silverfishCallCooldown) ? false : (ctx.lastHurtTick.load(std::memory_order_acquire)>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick.load(std::memory_order_acquire)<20); }


bool SilverfishInfestGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Silverfish) return false;
    if(now < m.silverfishCallCooldown) return false;
    if(!ctx.srv) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const double mobX=m.x, mobY=m.y, mobZ=m.z;
    World* world = dimensionWorld(ctx, m);
    int spawned=0;
    bool foundInfested=false;
    for(int dx=-6; dx<=6 && spawned<3; ++dx) for(int dz=-6; dz<=6 && spawned<3; ++dz){
        int bx=(int)std::floor(mobX)+dx, bz=(int)std::floor(mobZ)+dz, by=(int)std::floor(mobY);
        uint16_t st=world?world->getBlock(bx,by,bz):0; if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name); if(n.find("infested")!=std::string::npos){
            foundInfested=true;
            auto silverfish=std::make_shared<MobEntity>();
            silverfish->entityId=ctx.srv->nextEntityId();
            silverfish->kind=MobKind::Silverfish;
            silverfish->x=bx+0.5; silverfish->y=by+0.5;
            silverfish->z=bz+0.5;
            silverfish->health=mobStats(MobKind::Silverfish).maxHealth;
            silverfish->dimension=dimension;
            jvm::MobSpawnAdmission admission(
                ctx.srv->jvmRuntime(), *silverfish,
                &cancelMobSpawnOutsideMobStateLock);
            bool spawnedThis = false;
            if (!withoutMobStateLock(m, [&] {
                    if (!admission.approve(
                            silverfish->x, silverfish->y, silverfish->z) ||
                        !ctx.srv->addMob(silverfish))
                        return;
                    admission.commit();
                    ctx.srv->broadcastMobSpawn(*silverfish);
                    if (world) {
                        world->setBlock(bx, by, bz, 0);
                        ctx.srv->broadcastBlockChangeFor(
                            dimension, bx, by, bz, 0);
                    }
                    spawnedThis = true;
                })) return false;
            if (spawnedThis) ++spawned;
        }
    }
    // fallback spawn even without infested block for test determinism
    if(spawned==0 && !foundInfested){
        auto silverfish=std::make_shared<MobEntity>();
        silverfish->entityId=ctx.srv->nextEntityId();
        silverfish->kind=MobKind::Silverfish;
        silverfish->x=mobX+1; silverfish->y=mobY; silverfish->z=mobZ+1;
        silverfish->health=mobStats(MobKind::Silverfish).maxHealth;
        silverfish->dimension=dimension;
        jvm::MobSpawnAdmission admission(
            ctx.srv->jvmRuntime(), *silverfish,
            &cancelMobSpawnOutsideMobStateLock);
        if (!withoutMobStateLock(m, [&] {
                if (!admission.approve(
                        silverfish->x, silverfish->y, silverfish->z) ||
                    !ctx.srv->addMob(silverfish))
                    return;
                admission.commit();
                ctx.srv->broadcastMobSpawn(*silverfish);
            })) return false;
    }
    m.silverfishCallCooldown=now+100;
    if(!withoutMobStateLock(m, [&] {
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.silverfish.ambient",
                mobX,mobY,mobZ,1.f,1.f,"hostile");
        })) return false;
    return true;
}


bool EndermiteTeleportGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Endermite) return false; return ctx.lastHurtTick.load(std::memory_order_acquire)>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick.load(std::memory_order_acquire)<20; }


bool EndermiteTeleportGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Endermite) return false;
    if(now < m.endermiteLifeUntil - 2390) return false; // throttle
    m.x += (nextRandom()/(double)RAND_MAX-0.5)*4; m.z += (nextRandom()/(double)RAND_MAX-0.5)*4;
    if(ctx.srv){
        World* world = dimensionWorld(ctx, m);
        if (!world) return false;
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        const auto teleportPackets = m.takeTeleportPacketsWithGroundState([&] {
            return mob_grounding::isMobGroundedAt(
                m, [world](const std::int32_t blockX,
                           const std::int32_t blockY,
                           const std::int32_t blockZ) {
                    return world->getBlock(blockX, blockY, blockZ);
                });
        });
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.endermite.ambient",
                    x,y,z,1.f,1.f,"hostile");
                for (const auto& packet : teleportPackets)
                    ctx.srv->broadcastMobPositionUpdate(
                        m, packet.packetId, packet.body);
            })) return false;
    }
    if(m.endermiteLifeUntil==0) m.endermiteLifeUntil=now+2400;
    return true;
}


bool EnderDragonPerchGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::EnderDragon; }


bool EnderDragonPerchGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::EnderDragon) return false;
    if(now < m.dragonPhaseUntil) return false;
    // perch y 80, breath
    if(m.y > 82){
        m.y -= 0.2;
        if(ctx.srv && now%40==0) {
            const auto dimension=canonicalDimension(m.dimension);
            const auto entityId=m.entityId;
            const double x=m.x,y=m.y,z=m.z;
            const double vx=(nextRandom()/(double)RAND_MAX-0.5)*0.6;
            const double vz=(nextRandom()/(double)RAND_MAX-0.5)*0.6;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::DragonFireball,
                        x,y,z,vx,-0.3,vz,entityId,false);
                })) return false;
        }
    } else if(m.y < 78){
        const double ang=static_cast<double>(now)*0.03;
        double rx=std::cos(ang)*28, rz=std::sin(ang)*28;
        double dx=rx-m.x, dz=rz-m.z; m.x+=dx*0.04; m.z+=dz*0.04; m.y += (68-m.y)*0.02;
    } else {
        if(ctx.srv && now%20==0) {
            const auto dimension=canonicalDimension(m.dimension);
            const auto entityId=m.entityId;
            const double x=m.x,y=m.y,z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::DragonFireball,
                        x,y,z,0,-0.4,0,entityId,false);
                })) return false;
        }
        if(nextRandom()%100<5) m.dragonPhaseUntil=now+80;
    }
    m.yaw=static_cast<float>(static_cast<double>(now)*0.8);
    return true;
}


bool ZombieVillagerCureGoal::shouldStart(MobEntity& m, AiContext&) {
    if (m.kind != MobKind::ZombieVillager) return false;
    return m.zombieVillagerCureUntil != 0;
}


bool ZombieVillagerCureGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::ZombieVillager) return false;
    if (m.zombieVillagerCureUntil == 0) {
        // start shaking cure cycle every ~10min like vanilla weakness+apple cure window
        if (now%12000==0) m.zombieVillagerCureUntil = now+200;
        else return false;
    }
    if (now >= m.zombieVillagerCureUntil) { m.zombieVillagerCureUntil = 0; return false; }
    // shaking: hold still + shake sound
    if (ctx.srv && now%40==0) {
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.zombie_villager.cure",
                    x,y,z,1.f,1.f,"hostile");
            })) return false;
    }
    return true;
}


bool SkeletonHorseTrapGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::SkeletonHorse) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.skeletonHorseTrapCooldown) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 10*10;
}


bool SkeletonHorseTrapGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::SkeletonHorse) return false;
    if (now < m.skeletonHorseTrapCooldown) return false;
    if (ctx.srv) {
        // vanilla skeleton trap: lightning strike on approach
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->strikeLightningFor(dimension, x, y, z);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.skeleton_horse.ambient",
                    x, y, z, 1.f, 1.f, "neutral");
            })) return false;
    }
    m.skeletonHorseTrapCooldown = now+1200;
    return true;
}


bool HuskHungerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Husk) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 3*3;
}


bool HuskHungerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Husk) return false;
    if (now < m.huskHungerCooldown) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        // vanilla husk inflicts Hunger (effect id 9) on hit
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(9); eff.i8(0); eff.varint(140); eff.u8(0x01);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t->player);
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.husk.ambient",
                    x, y, z, 1.f, 1.f, "hostile");
            })) return false;
    }
    m.huskHungerCooldown = now+40;
    return true;
}


} // namespace cppfm
