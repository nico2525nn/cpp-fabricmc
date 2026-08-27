#include "BehaviorTree.hpp"
#include "GameServer.hpp"
#include "World.hpp"
#include "../worldgen/MultiNoise.hpp"

namespace cppfm {

BTStatus IsHurtCondition::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.health <= 0) return BTStatus::Failure;
    const float maxH = mobStats(m.kind).maxHealth;
    if (m.health < maxH * 0.9f) return BTStatus::Success;
    if (ctx.lastHurtTick >=0 && ctx.srv && ctx.srv->tickNoForTest() - ctx.lastHurtTick < 20) return BTStatus::Success;
    return BTStatus::Failure;
}

BTStatus IsPlayerLookingCondition::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    Player* p = ctx.nearestPlayer;
    if (!p) return BTStatus::Failure;
    double dx = m.x - p->x;
    double dy = (m.y+1.6) - (p->y + 1.62);
    double dz = m.z - p->z;
    double len = std::sqrt(dx*dx+dy*dy+dz*dz);
    if (len < 1e-6 || len > 64) return BTStatus::Failure;
    dx/=len; dy/=len; dz/=len;
    double yawRad = p->yaw * 3.1415926535 / 180.0;
    double pitchRad = p->pitch * 3.1415926535 / 180.0;
    double lx = -std::sin(yawRad) * std::cos(pitchRad);
    double ly = -std::sin(pitchRad);
    double lz =  std::cos(yawRad) * std::cos(pitchRad);
    double dot = dx*lx + dy*ly + dz*lz;
    return dot > 0.985 ? BTStatus::Success : BTStatus::Failure;
}

BTStatus MoveToPlayerAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double dx = t->x - m.x, dz = t->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d < 1.9) return BTStatus::Success;
    if (d > 24) return BTStatus::Failure;
    m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
    m.x += dx/d * 0.10;
    m.z += dz/d * 0.10;
    if (ctx.srv && ctx.world) {
        ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
        int col=4;
        ctx.world->withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
            for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
        });
        m.y = kMinY + col + 1.0;
    }
    return BTStatus::Running;
}

BTStatus AttackPlayerAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double dx = t->x - m.x, dz = t->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d > 2.2) return BTStatus::Failure;
    if (now % 20 == 0 && ctx.srv) ctx.srv->mobAttackPlayer(m, *t);
    return BTStatus::Success;
}

BTStatus TeleportRandomAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (now - m.lastTeleportTick < 20) return BTStatus::Failure;
    if (!ctx.world) return BTStatus::Failure;
    for (int attempt=0; attempt<16; ++attempt) {
        double nx = m.x + (rand()/(double)RAND_MAX*64 -32);
        double nz = m.z + (rand()/(double)RAND_MAX*64 -32);
        int ix = (int)std::floor(nx);
        int iz = (int)std::floor(nz);
        ctx.world->generateChunkIfMissing(ix>>4, iz>>4);
        int col = 4;
        bool found=false;
        ctx.world->withChunk(ix>>4, iz>>4,[&](const Chunk& c){
            for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, iz&15, ix&15)]!=0){col=ry+1; found=true; break;}
        });
        if(!found) continue;
        int feetY = kMinY + col;
        std::uint16_t a1 = ctx.world->getBlock(ix, feetY, iz);
        std::uint16_t a2 = ctx.world->getBlock(ix, feetY+1, iz);
        std::uint16_t below = ctx.world->getBlock(ix, feetY-1, iz);
        if (a1==0 && a2==0 && below!=0) {
            m.x = ix + 0.5; m.z = iz + 0.5; m.y = feetY + 1.0;
            m.lastTeleportTick = now;
            if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.enderman.teleport", m.x,m.y,m.z,1.f,1.f,"hostile");
            return BTStatus::Success;
        }
    }
    return BTStatus::Failure;
}

BTStatus PickupBlockAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.carriedBlock !=0) return BTStatus::Failure;
    if (!ctx.world) return BTStatus::Failure;
    int bx=(int)std::floor(m.x), by=(int)std::floor(m.y), bz=(int)std::floor(m.z);
    std::uint16_t st = ctx.world->getBlock(bx, by-1, bz);
    if (st==0) st = ctx.world->getBlock(bx, by, bz);
    if (st==0) return BTStatus::Failure;
    auto* def = gen::blockByState(st);
    if (!def) return BTStatus::Failure;
    std::string_view n = def->name;
    if (n=="minecraft:bedrock"||n=="minecraft:obsidian") return BTStatus::Failure;
    m.carriedBlock = st;
    ctx.world->setBlock(bx, by-1, bz, 0);
    if (ctx.srv) ctx.srv->broadcastBlockChange(bx, by-1, bz, 0);
    return BTStatus::Success;
}

BTStatus StareAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    Player* p = ctx.nearestPlayer;
    if (!p) return BTStatus::Failure;
    double dx = p->x - m.x, dz = p->z - m.z;
    m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
    return BTStatus::Success;
}

BTStatus WitherSkullAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Wither) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double dx = t->x - m.x, dy = (t->y+1.0) - (m.y+1.5), dz = t->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>32) return BTStatus::Failure;
    double inv = 1.0/ (d+1e-6);
    double vx = dx*inv*1.1, vz = dz*inv*1.1, vy = dy*inv*0.6 + 0.2;
    if (ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::WitherSkull, m.x, m.y+1.5, m.z, vx, vy, vz, m.entityId, false);
    m.witherSkullCooldown = (int)(now + 40 + rand()%40);
    return BTStatus::Success;
}

BTStatus DragonBreathAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::EnderDragon) return BTStatus::Failure;
    if (m.dragonPhase == 0 && now > m.dragonPhaseUntil) {
        if (rand()%100 < 12) { m.dragonPhase = 1; m.dragonPhaseUntil = now + 40; }
    }
    if (m.dragonPhase == 1) {
        double dx=-m.x, dz=-m.z;
        double d=std::sqrt(dx*dx+dz*dz);
        if (d<4) { m.dragonPhase=2; m.dragonPhaseUntil = now + 80; }
        else {
            m.x += dx/d * 0.18; m.z += dz/d * 0.18;
            m.y = 65;
            return BTStatus::Running;
        }
    }
    if (m.dragonPhase == 2) {
        if (now % 20 == 0 && ctx.srv) {
            ctx.srv->spawnProjectile(ProjectileKind::DragonFireball, m.x, m.y, m.z, (rand()/(double)RAND_MAX-0.5)*0.6, -0.3, (rand()/(double)RAND_MAX-0.5)*0.6, m.entityId, false);
            ctx.srv->broadcastSound("minecraft:entity.ender_dragon.shoot", m.x,m.y,m.z,2.f,1.f,"hostile");
        }
        if (now > m.dragonPhaseUntil) { m.dragonPhase=3; m.dragonPhaseUntil=now+30; }
        return BTStatus::Running;
    }
    if (m.dragonPhase == 3) {
        double ang = now * 0.04;
        double rx = std::cos(ang)*32, rz = std::sin(ang)*32;
        double dx=rx-m.x, dz=rz-m.z;
        m.x += dx*0.08; m.z += dz*0.08; m.y += (70-m.y)*0.05;
        if (now > m.dragonPhaseUntil) { m.dragonPhase=0; m.dragonPhaseUntil=now+120+rand()%120; }
        return BTStatus::Running;
    }
    double ang = now * 0.03;
    double rx = std::cos(ang)*28, rz = std::sin(ang)*28;
    double dx=rx - m.x, dz=rz - m.z;
    m.x += dx*0.04; m.z += dz*0.04; m.y += (68 - m.y)*0.02;
    m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
    if (rand()%80==0 && ctx.srv) {
        ctx.srv->broadcastSound("minecraft:entity.ender_dragon.flap", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    return BTStatus::Running;
}

BTStatus BreedAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!m.inLove || now > m.loveUntilTick) return BTStatus::Failure;
    if (MobEntity::isBaby(m)) return BTStatus::Failure;
    if (!ctx.srv) return BTStatus::Failure;
    if (now < m.breedCooldownUntil) return BTStatus::Failure;
    if (now < m.loveUntilTick - 30*20 + 30) return BTStatus::Running;
    auto partner = ctx.srv->findLovePartner(m);
    if (!partner) return BTStatus::Running;
    double bx=(m.x+partner->x)/2.0, bz=(m.z+partner->z)/2.0;
    auto baby = std::make_shared<MobEntity>();
    baby->entityId = ctx.srv->nextEntityId();
    baby->kind = m.kind;
    baby->health = mobStats(m.kind).maxHealth;
    baby->age = -60*20;
    baby->x = bx; baby->y = m.y; baby->z = bz;
    ctx.srv->mobsForTest().push_back(baby);
    ctx.srv->broadcastMobSpawn(*baby);
    m.inLove=false; partner->inLove=false;
    m.breedCooldownUntil = now + 60*20;
    partner->breedCooldownUntil = now + 60*20;
    return BTStatus::Success;
}

BTStatus TradeAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.kind != MobKind::Villager) return BTStatus::Failure;
    if (!ctx.nearestPlayer) return BTStatus::Failure;
    double dx=ctx.nearestPlayer->x - m.x, dz=ctx.nearestPlayer->z - m.z;
    if (dx*dx+dz*dz > 36) return BTStatus::Failure;
    m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
    return BTStatus::Success;
}

BTStatus WanderAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!m.hasTarget) {
        double ang = (rand()/(double)RAND_MAX)*6.28318;
        double dist = 4 + (rand()%8);
        m.tx = m.x + std::cos(ang)*dist;
        m.tz = m.z + std::sin(ang)*dist;
        m.hasTarget=true;
        m.nextWanderAt = now + 3000 + rand()%4000;
    }
    double dx=m.tx-m.x, dz=m.tz-m.z;
    double d=std::sqrt(dx*dx+dz*dz);
    if (d<0.6 || now > m.nextWanderAt) { m.hasTarget=false; return BTStatus::Success; }
    m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
    m.x += dx/d * 0.05;
    m.z += dz/d * 0.05;
    if (ctx.world && ctx.srv) {
        ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
        int col=4;
        ctx.world->withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
            for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
        });
        m.y = kMinY + col + 1.0;
    }
    return BTStatus::Running;
}

// ---------- plan7 extended actions ----------

BTStatus BlazeFireballAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Blaze) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double dx = t->x - m.x, dy = (t->y+1.0)-(m.y+1.0), dz = t->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>16 || d<4) return BTStatus::Failure;
    double inv = 1.0/(d+1e-6);
    double vx = dx*inv*1.0, vz = dz*inv*1.0, vy = dy*inv*0.2 + 0.1;
    if (ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::Fireball, m.x, m.y+1.0, m.z, vx, vy, vz, m.entityId, false);
    m.witherSkullCooldown = (int)(now + 30 + rand()%30);
    return BTStatus::Success;
}

BTStatus GuardianBeamAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Guardian && m.kind != MobKind::ElderGuardian) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double dx = t->x - m.x, dz = t->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>15) return BTStatus::Failure;
    if (ctx.srv) {
        ctx.srv->applyDamage(*t, 6.f, "magic");
        ctx.srv->broadcastSound("minecraft:entity.guardian.attack", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.witherSkullCooldown = (int)(now + 60);
    return BTStatus::Success;
}

BTStatus GhastFireballAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Ghast) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double dx = t->x - m.x, dy = (t->y+1.0)-(m.y+1.5), dz = t->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>32) return BTStatus::Failure;
    double inv=1.0/(d+1e-6);
    double vx=dx*inv*1.0, vz=dz*inv*1.0, vy=dy*inv*0.2;
    if (ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::Fireball, m.x, m.y+1.5, m.z, vx, vy, vz, m.entityId, false);
    m.witherSkullCooldown = (int)(now + 80 + rand()%40);
    return BTStatus::Success;
}

BTStatus PhantomSwoopAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Phantom) return BTStatus::Failure;
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double dx = t->x - m.x, dy = t->y - m.y, dz = t->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>30) {
        m.x += dx/d * 0.25; m.z += dz/d * 0.25;
        m.y += dy*0.05;
        return BTStatus::Running;
    }
    if (d<2.0 && now % 20==0 && ctx.srv) ctx.srv->mobAttackPlayer(m, *t);
    else {
        m.x += dx/d * 0.18; m.z += dz/d * 0.18; m.y += (t->y+3 - m.y)*0.08;
    }
    return BTStatus::Running;
}

BTStatus ShulkerBulletAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Shulker) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double d = std::sqrt((t->x-m.x)*(t->x-m.x)+(t->z-m.z)*(t->z-m.z));
    if (d>16) return BTStatus::Failure;
    double dx=t->x-m.x, dy=(t->y+0.5)-m.y, dz=t->z-m.z;
    double inv=1.0/(d+1e-6);
    if (ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::Arrow, m.x, m.y+0.5, m.z, dx*inv*0.7, dy*inv*0.7+0.1, dz*inv*0.7, m.entityId, false);
    m.witherSkullCooldown = (int)(now + 60 + rand()%40);
    if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.shulker.shoot", m.x,m.y,m.z,1.f,1.f,"hostile");
    return BTStatus::Success;
}

BTStatus WardenSonicBoomAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Warden) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    double d = std::sqrt((t->x-m.x)*(t->x-m.x)+(t->z-m.z)*(t->z-m.z));
    if (d>18) {
        // slowly approach
        double dx=t->x-m.x, dz=t->z-m.z;
        m.x += dx/d*0.06; m.z += dz/d*0.06;
        m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
        return BTStatus::Running;
    }
    if (ctx.srv) {
        ctx.srv->applyDamage(*t, 30.f, "sonic_boom");
        ctx.srv->broadcastSound("minecraft:entity.warden.sonic_boom", m.x,m.y,m.z,2.f,1.f,"hostile");
        // knockback
        double dx=t->x-m.x, dz=t->z-m.z;
        double inv=1.0/(d+1e-6);
        WriteBuffer v; v.varint(t->entityId); v.i16((int16_t)(dx*inv*12000)); v.i16((int16_t)(8000)); v.i16((int16_t)(dz*inv*12000));
        try{ t->conn->sendPacket(proto::pl::sc::EntityVelocity, v);}catch(...){}
    }
    m.witherSkullCooldown = (int)(now + 80);
    return BTStatus::Success;
}

BTStatus GenericRangedAttackAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    Player* t = ctx.nearestPlayer;
    if (!t) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    double dx=t->x-m.x, dy=(t->y+1.0)-(m.y+1.6), dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz);
    if (d<4 || d>16) return BTStatus::Failure;
    double inv=1.0/(d+1e-6);
    if (ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::Arrow, m.x, m.y+1.6, m.z, dx*inv*1.2, dy*inv+0.15, dz*inv*1.2, m.entityId, false);
    m.witherSkullCooldown=(int)(now+40+rand()%30);
    return BTStatus::Success;
}

} // namespace cppfm
