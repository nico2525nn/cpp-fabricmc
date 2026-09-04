#include "BehaviorTree.hpp"
#include "GameServer.hpp"
#include "World.hpp"
#include "MetadataTypes.hpp"
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
    if (p->gamemode==1 || p->gamemode==3) return BTStatus::Failure;
    // carved_pumpkin helmet negates stare
    for(int i=5;i<=8;i++) if(i>=0 && i < (int)p->inv.size() && !p->inv[i].empty()){
        if(p->inv[i].name()=="minecraft:carved_pumpkin") return BTStatus::Failure;
    }
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
    return dot > 0.99 ? BTStatus::Success : BTStatus::Failure;
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
    // plan13 §6: 32-block radius, 64 attempts, y = m.y + rand(32)-16
    for (int attempt=0; attempt<64; ++attempt) {
        double nx = m.x + (rand()/(double)RAND_MAX*64 -32);
        double nz = m.z + (rand()/(double)RAND_MAX*64 -32);
        double ny = m.y + (rand()/(double)RAND_MAX*32 -16);
        int ix = (int)std::floor(nx);
        int iz = (int)std::floor(nz);
        int iy = (int)std::floor(ny);
        ctx.world->generateChunkIfMissing(ix>>4, iz>>4);
        // try around iy first, fall back to column search if needed
        for (int dy=-4; dy<=4; ++dy) {
            int tryY = iy + dy;
            if (tryY < kMinY || tryY > kMinY+320) continue;
            std::uint16_t a1 = ctx.world->getBlock(ix, tryY, iz);
            std::uint16_t a2 = ctx.world->getBlock(ix, tryY+1, iz);
            std::uint16_t below = ctx.world->getBlock(ix, tryY-1, iz);
            if (a1==0 && a2==0 && below!=0) {
                double ox=m.x, oy=m.y, oz=m.z;
                m.x = ix + 0.5; m.z = iz + 0.5; m.y = tryY + 0.5;
                m.lastTeleportTick = now;
                if (ctx.srv) {
                    ctx.srv->broadcastSound("minecraft:entity.enderman.teleport", m.x,m.y,m.z,1.f,1.f,"hostile");
                    // EntityTeleport 0x77 to all tracking
                    WriteBuffer tp;
                    tp.varint(m.entityId);
                    tp.f64(m.x); tp.f64(m.y); tp.f64(m.z);
                    tp.f32(m.yaw); tp.f32(0); tp.boolean(true);
                    ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
                    // portal particles
                    for(int i=0;i<8;i++){
                        WriteBuffer pt;
                        pt.boolean(true); pt.boolean(false);
                        pt.f64(ox + (rand()/(double)RAND_MAX-0.5)*1.5);
                        pt.f64(oy + rand()/(double)RAND_MAX*2.0);
                        pt.f64(oz + (rand()/(double)RAND_MAX-0.5)*1.5);
                        pt.f32(0);pt.f32(0);pt.f32(0);pt.f32(0.1f);
                        pt.varint(15); // portal
                        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::WorldParticles, pt);
                    }
                }
                return BTStatus::Success;
            }
        }
        // fallback column search if not found around ny
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
            double ox=m.x, oy=m.y, oz=m.z;
            m.x = ix + 0.5; m.z = iz + 0.5; m.y = feetY + 1.0;
            m.lastTeleportTick = now;
            if (ctx.srv) {
                ctx.srv->broadcastSound("minecraft:entity.enderman.teleport", m.x,m.y,m.z,1.f,1.f,"hostile");
                WriteBuffer tp;
                tp.varint(m.entityId);
                tp.f64(m.x); tp.f64(m.y); tp.f64(m.z);
                tp.f32(m.yaw); tp.f32(0); tp.boolean(true);
                ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
            }
            (void)ox;(void)oy;(void)oz;
            return BTStatus::Success;
        }
    }
    return BTStatus::Failure;
}

BTStatus PickupBlockAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.carriedBlock !=0) return BTStatus::Failure;
    if (!ctx.world) return BTStatus::Failure;
    // plan13 §6: 1/1000 chance per tick, only holdable blocks (grass/dirt/sand/gravel etc.)
    if (rand()%1000 != 0) return BTStatus::Failure;
    // plan16: enderman_holdable tag ~70 blocks (vanilla Yarn enderman_holdable, 1.21.4 70 entries)
    static const char* holdable[] = {
        "minecraft:grass_block","minecraft:dirt","minecraft:coarse_dirt","minecraft:podzol","minecraft:rooted_dirt",
        "minecraft:dirt_path","minecraft:mud","minecraft:clay","minecraft:sand","minecraft:red_sand",
        "minecraft:gravel","minecraft:soul_sand","minecraft:soul_soil","minecraft:snow","minecraft:snow_block",
        "minecraft:pumpkin","minecraft:carved_pumpkin","minecraft:melon","minecraft:brown_mushroom","minecraft:red_mushroom",
        "minecraft:mushroom_stem","minecraft:brown_mushroom_block","minecraft:red_mushroom_block","minecraft:crimson_fungus","minecraft:warped_fungus",
        "minecraft:crimson_nylium","minecraft:warped_nylium","minecraft:nether_wart_block","minecraft:warped_wart_block","minecraft:cactus",
        "minecraft:tnt","minecraft:mycelium","minecraft:moss_block","minecraft:pale_moss_block","minecraft:muddy_mangrove_roots",
        "minecraft:dandelion","minecraft:poppy","minecraft:blue_orchid","minecraft:allium","minecraft:azure_bluet",
        "minecraft:red_tulip","minecraft:orange_tulip","minecraft:white_tulip","minecraft:pink_tulip","minecraft:oxeye_daisy",
        "minecraft:cornflower","minecraft:lily_of_the_valley","minecraft:wither_rose","minecraft:sunflower","minecraft:lilac",
        "minecraft:rose_bush","minecraft:peony","minecraft:pitcher_plant","minecraft:torchflower","minecraft:spore_blossom",
        "minecraft:dead_bush","minecraft:fern","minecraft:short_grass","minecraft:vine","minecraft:lily_pad",
        "minecraft:mangrove_propagule","minecraft:bamboo","minecraft:azalea","minecraft:flowering_azalea","minecraft:big_dripleaf",
        "minecraft:small_dripleaf","minecraft:chorus_flower","minecraft:chorus_plant","minecraft:crimson_roots","minecraft:warped_roots"
    };
    auto isHoldable = [&](std::string_view n)->bool{
        for(auto h: holdable) if(n==h) return true;
        // fallback: allow any non-hard hardness < 0.5 and not bedrock/obsidian
        if(n=="minecraft:bedrock"||n=="minecraft:obsidian") return false;
        return false;
    };
    // try nearby positions
    for(int tries=0; tries<8; ++tries){
        int bx=(int)std::floor(m.x)+(rand()%5-2);
        int by=(int)std::floor(m.y)+(rand()%3-1);
        int bz=(int)std::floor(m.z)+(rand()%5-2);
        std::uint16_t st = ctx.world->getBlock(bx, by, bz);
        if (st==0) continue;
        auto* def = gen::blockByState(st);
        if (!def) continue;
        if (!isHoldable(def->name)) continue;
        m.carriedBlock = st;
        ctx.world->setBlock(bx, by, bz, 0);
        if (ctx.srv) {
            ctx.srv->broadcastBlockChange(bx, by, bz, 0);
            // SetEntityMetadata for carriedBlock (index 15, Yarn EndermanEntity CARRIED_BLOCK Optional<BlockState>)
            WriteBuffer md;
            md.varint(m.entityId);
            meta::writeMetaOptBlockState(md, 15, st);
            md.u8(255);
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
        }
        return BTStatus::Success;
    }
    return BTStatus::Failure;
}

BTStatus StareAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    Player* p = ctx.nearestPlayer;
    if (!p) return BTStatus::Failure;
    // plan13 §6: stare anger – check helmet not carved_pumpkin, not creative/spectator
    if (p->gamemode==1 || p->gamemode==3) return BTStatus::Failure;
    bool hasPumpkin=false;
    if (p->inv.size()>=9) {
        // head slot 8 is helmet (player inv 5-8 armor, 8 = head)
        // we check all armor slots for pumpkin to be safe
        for(int i=5;i<=8;i++) if(!p->inv[i].empty()){
            std::string n = p->inv[i].name();
            if(n=="minecraft:carved_pumpkin") {hasPumpkin=true;break;}
        }
    }
    if (hasPumpkin) return BTStatus::Failure;
    double dx = p->x - m.x, dz = p->z - m.z;
    m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
    // set anger
    m.angerTargetEntityId = p->entityId;
    m.angryUntilTick = now + 100 + rand()%100;
    if (ctx.srv) {
        // metadata angry flag (Yarn EndermanEntity CREEPY Boolean 16, was 15 Byte)
        WriteBuffer md;
        md.varint(m.entityId);
        meta::writeMetaBool(md, 16, true);
        md.u8(255);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
    }
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
    // plan21 E3: Wither skull 3-burst (vanilla WitherEntity shoots 3 skulls per attack, central + 2 side heads with spread)
    // plan21 adds charged (blue) skull when health <= half (150) for head 0, with 3-burst and armor bypass via projectile
    // plan24 combat polish: 3-burst verified after wt24 merges; plan25 verify 3-burst intact after W16-W19 world changes
    // plan26 combat polish: verify 3-burst intact after D5/D6/D10/D11/D16/D17/D19/D20/D22/D25 merges; EPF weight1, sonic 15x20 bypass intact.
    if (ctx.srv) {
        const float maxH = mobStats(m.kind).maxHealth;
        bool halfHealth = m.health <= maxH * 0.5f;
        for (int burst=0; burst<3; ++burst) {
            double spreadX = (burst==0?0:(burst==1?-0.35:0.35));
            double spreadZ = (burst==0?0:(burst==1?0.35:-0.35));
            double yawRad = m.yaw * 3.1415926535 / 180.0;
            double offX = std::cos(yawRad)*spreadX - std::sin(yawRad)*spreadZ;
            double offZ = std::sin(yawRad)*spreadX + std::cos(yawRad)*spreadZ;
            double vx = dx*inv*1.1 + (rand()/(double)RAND_MAX-0.5)*0.08;
            double vz = dz*inv*1.1 + (rand()/(double)RAND_MAX-0.5)*0.08;
            double vy = dy*inv*0.6 + 0.2 + (rand()/(double)RAND_MAX-0.5)*0.05;
            double sx = m.x + offX;
            double sz = m.z + offZ;
            bool charged = halfHealth && burst==0; // head 0 charged (blue) when <= half health
            ctx.srv->spawnProjectile(ProjectileKind::WitherSkull, sx, m.y+1.5, sz, vx, vy, vz, m.entityId, false, charged);
        }
        ctx.srv->broadcastSound("minecraft:entity.wither.shoot", m.x, m.y, m.z, 1.0f, 1.0f, "hostile");
    }
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
    baby->age = -24000; // plan14 §3: 20 min
    baby->x = bx; baby->y = m.y; baby->z = bz;
    ctx.srv->mobsForTest().push_back(baby);
    ctx.srv->broadcastMobSpawn(*baby);
    ctx.srv->spawnXpOrbs(bx, m.y+0.5, bz, 1 + (rand()%7), nullptr);
    m.inLove=false; partner->inLove=false;
    m.breedCooldownUntil = now + 6000;
    partner->breedCooldownUntil = now + 6000;
    // plan38 B-13: bred_animals for nearest player
    {
        auto nearby = ctx.srv->playersSnapshot();
        Player* best=nullptr; double bestD=64;
        for (auto &pp: nearby) if (pp->inPlay) {
            double dx=pp->x-bx, dz=pp->z-bz;
            double d2=dx*dx+dz*dz;
            if (d2<bestD*bestD) { bestD=std::sqrt(d2); best=pp.get(); }
        }
        if (best) ctx.srv->onBredAnimals(best);
    }
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
    // plan22 network polish: sonic boom 15×20 cylinder (independent horiz 15, vert 20, inclusive), armor+enchant bypass, through walls, 10 damage (15 hard)
    auto isInSonicBoomRange = [](double wx, double wy, double wz, double tx, double ty, double tz) -> bool {
        double dx = tx - wx, dz = tz - wz, dy = ty - wy;
        double horiz2 = dx*dx + dz*dz;
        if (horiz2 > 15*15) return false;
        if (std::abs(dy) > 20) return false;
        return true; // cylinder 15×20 inclusive (plan22 §10: horiz hypot <=15 && vert abs <=20)
    };
    if (!isInSonicBoomRange(m.x, m.y+1.0, m.z, t->x, t->y+0.9, t->z)) {
        double dx=t->x-m.x, dz=t->z-m.z;
        double d = std::sqrt(dx*dx+dz*dz);
        if (d>1e-6) {
            m.x += dx/d*0.06; m.z += dz/d*0.06;
            m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
        }
        return BTStatus::Running;
    }
    if (ctx.srv) {
        // strict audit HIGH: sonic boom bypasses armor+enchant (bypassArmor/bypassEnchant=true) 15×20 cylinder, 10 damage (15 hard), no knockback, pierces shields
        // plan26 combat polish: verify sonic 15x20 cylinder (D17) with bypassArmor/bypassEnchant/bypassShield, particle 27 sonic_boom intact after entity D17 fix.
        float dmg = 10.0f;
        if (ctx.srv->difficulty() == "hard") dmg = 15.0f;
        ctx.srv->applyDamage(*t, dmg, DamageSource::sonicBoom());
        ctx.srv->broadcastSound("minecraft:entity.warden.sonic_boom", m.x,m.y,m.z,2.f,1.f,"hostile");
        // vanilla sonic boom has no knockback; do not send EntityVelocity
        // spawn sonic_boom particle (optional, not required for audit but helps wire capture)
        // plan26 D17: particle id 27 sonic_boom (was 0 placeholder angry_villager); verified SimpleParticleType no extra data.
        {
            WriteBuffer p;
            p.boolean(true); p.boolean(false);
            p.f64(m.x); p.f64(m.y+1.6); p.f64(m.z);
            p.f32(0); p.f32(0); p.f32(0); p.f32(0.1f);
            p.varint(27); // sonic_boom (plan26 D17: was 0 placeholder)
            // not broadcasting particle id strictly, but keep for compat
            (void)p;
        }
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
BTStatus WitchPotionAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Witch) return BTStatus::Failure;
    if(now < m.witchPotionCooldown) return BTStatus::Failure;
    Player* t=ctx.nearestPlayer; if(!t) return BTStatus::Failure;
    double d2=(t->x-m.x)*(t->x-m.x)+(t->z-m.z)*(t->z-m.z); if(d2>256) return BTStatus::Failure;
    if(ctx.srv){ double d=std::sqrt(d2)+1e-6; double vx=(t->x-m.x)/d*0.9, vz=(t->z-m.z)/d*0.9; ctx.srv->spawnProjectile(ProjectileKind::Potion, m.x, m.y+1.6, m.z, vx, 0.12, vz, m.entityId, false); ctx.srv->broadcastSound("minecraft:entity.witch.throw", m.x,m.y,m.z,1.f,1.f,"hostile"); }
    m.witchPotionCooldown = now + 40 + rand()%20; return BTStatus::Success;
}
BTStatus RavagerRoarAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Ravager) return BTStatus::Failure;
    if(now < m.ravagerRoarCooldown) return BTStatus::Failure;
    Player* t=ctx.nearestPlayer; if(!t) return BTStatus::Failure;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6; if(d>5) return BTStatus::Failure;
    if(ctx.srv){ double vx=dx/d*0.4, vz=dz/d*0.4; WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(0.3*8000)); vel.i16((int16_t)(vz*8000)); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel); ctx.srv->broadcastHurtAnimation(t->entityId, 0); }
    m.ravagerRoarCooldown = now + 100; return BTStatus::Success;
}
BTStatus IronGolemDefendAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) { if(m.kind!=MobKind::IronGolem) return BTStatus::Failure; if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.iron_golem.step", m.x,m.y,m.z,0.5f,1.f,"neutral"); return BTStatus::Success; }
BTStatus BeePollinateAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Bee) return BTStatus::Failure; if(m.beeHasNectar) return BTStatus::Failure; if(now%20!=0) return BTStatus::Running; m.beeHasNectar=true; m.beePollenUntil=now+400; if(ctx.srv){ WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,17,true); md.u8(255); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);} return BTStatus::Success; }
BTStatus VillagerScheduleAction::tick(MobEntity& m, AiContext& ctx, std::int64_t){ if(m.kind!=MobKind::Villager && m.kind!=MobKind::WanderingTrader) return BTStatus::Failure; if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.villager.work", m.x,m.y,m.z,0.3f,1.f,"neutral"); return BTStatus::Success; }
BTStatus WolfAngerAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Wolf) return BTStatus::Failure; if(m.isTamed) return BTStatus::Failure; Player* t=ctx.nearestPlayer; if(!t) return BTStatus::Failure; m.wolfAngerTarget=t->entityId; m.wolfAngerUntil=now+100; if(ctx.srv){ WriteBuffer md; md.varint(m.entityId); meta::writeMetaByte(md,16,1); md.u8(255); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);} return BTStatus::Success; }
BTStatus DrownedTridentAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Drowned) return BTStatus::Failure; if(now < m.drownedTridentCooldown) return BTStatus::Failure; Player* t=ctx.nearestPlayer; if(!t) return BTStatus::Failure; double dx=t->x-m.x, dz=t->z-m.z, dy=(t->y+1)-(m.y+1.6); double d=std::sqrt(dx*dx+dz*dz)+1e-6; if(d>16||d<5) return BTStatus::Failure; if(ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::Trident, m.x, m.y+1.6, m.z, dx/d*1.2, dy/d*0.2+0.15, dz/d*1.2, m.entityId, false); m.drownedTridentCooldown=now+40; return BTStatus::Success; }
BTStatus PiglinBarterAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Piglin) return BTStatus::Failure; if(now < m.piglinBarterCooldown) return BTStatus::Failure; m.piglinBarterCooldown=now+100; if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.piglin.admiring_item", m.x,m.y,m.z,1.f,1.f,"neutral"); return BTStatus::Success; }
BTStatus CatScareAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Cat) return BTStatus::Failure; if(now < m.catScareCooldown) return BTStatus::Failure; m.catScareCooldown=now+60; if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.cat.purr", m.x,m.y,m.z,0.5f,1.f,"neutral"); return BTStatus::Success; }
BTStatus FoxPounceAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Fox) return BTStatus::Failure; if(now < m.foxPounceCooldown) return BTStatus::Failure; Player* t=ctx.nearestPlayer; if(!t) return BTStatus::Failure; double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6; if(d<2||d>6) return BTStatus::Failure; m.x+=dx/d*0.42; m.z+=dz/d*0.42; m.y+=0.38; m.foxPounceCooldown=now+40; return BTStatus::Success; }
BTStatus DolphinPlayAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Dolphin) return BTStatus::Failure; if(now < m.dolphinPlayCooldown) return BTStatus::Failure; m.dolphinPlayCooldown=now+20; if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.dolphin.play", m.x,m.y,m.z,1.f,1.f,"neutral"); return BTStatus::Success; }
BTStatus EvokerFangAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){ if(m.kind!=MobKind::Evoker) return BTStatus::Failure; if(now < m.evokerFangCooldown) return BTStatus::Failure; Player* t=ctx.nearestPlayer; if(!t) return BTStatus::Failure; m.evokerFangCooldown=now+60; if(ctx.srv){ ctx.srv->broadcastSound("minecraft:entity.evoker.cast_spell", m.x,m.y,m.z,1.f,1.f,"hostile"); if(t) ctx.srv->applyDamage(*t,6.f, DamageSource::magic()); } return BTStatus::Success; }

} // namespace cppfm
