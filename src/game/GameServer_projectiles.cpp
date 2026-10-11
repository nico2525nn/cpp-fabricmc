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

namespace {
const char* projectileDamageType(ProjectileKind kind) noexcept {
    switch (kind) {
    case ProjectileKind::Arrow: return "arrow";
    case ProjectileKind::Trident: return "trident";
    case ProjectileKind::LlamaSpit: return "llama_spit";
    case ProjectileKind::ShulkerBullet: return "shulker_bullet";
    case ProjectileKind::WindCharge:
    case ProjectileKind::BreezeWindCharge: return "wind_charge";
    case ProjectileKind::WitherSkull: return "wither_skull";
    case ProjectileKind::Fireball: return "fireball";
    case ProjectileKind::DragonFireball: return "dragon_fireball";
    case ProjectileKind::Snowball: return "snowball";
    case ProjectileKind::Egg: return "egg";
    case ProjectileKind::Potion: return "indirect_magic";
    case ProjectileKind::EnderPearl: return "ender_pearl";
    }
    return "projectile";
}

bool isWindCharge(ProjectileKind kind) noexcept {
    return kind == ProjectileKind::WindCharge ||
           kind == ProjectileKind::BreezeWindCharge;
}

} // namespace

std::shared_ptr<ProjectileEntity> GameServer::spawnProjectile(ProjectileKind kind, double x, double y,
                                 double z, double vx, double vy, double vz,
                                 std::int32_t ownerId, bool ownerIsPlayer, bool charged) {
    return spawnProjectileFor(0, kind, x, y, z, vx, vy, vz, ownerId,
                              ownerIsPlayer, charged);
}
std::shared_ptr<ProjectileEntity> GameServer::spawnProjectileFor(
    std::int8_t dimension, ProjectileKind kind, double x, double y, double z,
    double vx, double vy, double vz, std::int32_t ownerId, bool ownerIsPlayer,
    bool charged) {
    if (mobStateLockOwnedByCurrentThread()) {
        std::shared_ptr<ProjectileEntity> result;
        runWithoutMobStateLock([this, &result, dimension, kind, x, y, z, vx,
                                vy, vz, ownerId, ownerIsPlayer, charged] {
            result = spawnProjectileFor(dimension, kind, x, y, z, vx, vy, vz,
                                        ownerId, ownerIsPlayer, charged);
        });
        return result;
    }
    auto e = std::make_shared<ProjectileEntity>();
    e->entityId = nextEntityId();
    e->dimension = canonicalDimension(dimension);
    e->kind = kind;
    e->x = x; e->y = y; e->z = z;
    e->vx = vx; e->vy = vy; e->vz = vz;
    e->ownerId = ownerId;
    e->ownerIsPlayer = ownerIsPlayer;
    e->charged = charged;
    {
        std::lock_guard lk(projectilesMtx_);
        projectiles_.push_back(e);
    }
    const auto& types = gen::entityTypeIdByName();
    static const char* kNames[] = {"minecraft:arrow", "minecraft:snowball",
                                   "minecraft:egg", "minecraft:ender_pearl",
                                   "minecraft:potion", "minecraft:wither_skull",
                                   "minecraft:fireball", "minecraft:dragon_fireball",
                                   "minecraft:trident", "minecraft:wind_charge",
                                   "minecraft:breeze_wind_charge", "minecraft:llama_spit",
                                   "minecraft:shulker_bullet"};
    int idx = static_cast<int>(kind);
    const char* entName = (idx >=0 && idx < (int)(sizeof(kNames)/sizeof(kNames[0]))) ? kNames[idx] : "minecraft:snowball";
    auto ti = types.find(entName);
    WriteBuffer b;
    b.varint(e->entityId);
    const auto uuid = entityUuidForId(e->entityId);
    b.uuid(uuid.data());
    b.varint(ti != types.end() ? static_cast<std::int32_t>(ti->second) : 0);
    b.f64(x); b.f64(y); b.f64(z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(1);                                        // objectData: velocity
    b.i16(static_cast<std::int16_t>(vx * 8000));
    b.i16(static_cast<std::int16_t>(vy * 8000));
    b.i16(static_cast<std::int16_t>(vz * 8000));
    e->sentX = x;
    e->sentY = y;
    e->sentZ = z;
    e->hasSent = true;
    broadcastPacketExceptInDimension(e->dimension, nullptr, pl::sc::SpawnEntity,
                                     b);
    return e;
}
void GameServer::projectilesTick() {
    struct Hit {
        std::shared_ptr<ProjectileEntity> p;
        std::shared_ptr<Player> player;
        std::shared_ptr<MobEntity> mob;
        float dmg;
    };
    std::vector<Hit> hits;
    struct Despawn { std::int8_t dimension; std::int32_t entityId; };
    std::vector<Despawn> despawn;
    struct MobState final {
        std::shared_ptr<MobEntity> entity;
        std::int32_t entityId = 0;
        std::int8_t dimension = 0;
        MobKind kind = MobKind::Pig;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        bool dead = false;
    };
    struct PlayerState final {
        std::shared_ptr<Player> entity;
        std::shared_ptr<Connection> connection;
        std::int32_t entityId = 0;
        std::int8_t dimension = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        bool dead = false;
        bool inPlay = false;
        std::array<InvSlot, kPlayerInventorySlotCount> inv{};
    };
    std::vector<std::shared_ptr<ProjectileEntity>> activeProjectiles;
    {
        std::lock_guard lk(projectilesMtx_);
        activeProjectiles.swap(projectiles_);
    }
    std::vector<MobState> activeMobs;
    for (const auto& mob : mobsSnapshot()) {
        if (!mob) continue;
        MobState state;
        state.entity = mob;
        {
            std::lock_guard mobLock(*mob->stateMtx);
            state.entityId = mob->entityId;
            state.dimension = canonicalDimension(mob->dimension);
            state.kind = mob->kind;
            state.x = mob->x;
            state.y = mob->y;
            state.z = mob->z;
            state.dead = mob->dead;
        }
        activeMobs.push_back(std::move(state));
    }
    std::vector<PlayerState> activePlayers;
    for (const auto& player : playersSnapshot()) {
        if (!player) continue;
        PlayerState state;
        state.entity = player;
        {
            std::lock_guard playerLock(player->stateMtx);
            state.connection = player->conn;
            state.entityId = player->entityId;
            state.dimension = canonicalDimension(player->dimension);
            state.x = player->x;
            state.y = player->y;
            state.z = player->z;
            state.yaw = player->yaw;
            state.pitch = player->pitch;
            state.dead = player->dead;
            state.inPlay = player->inPlay;
            state.inv = player->inv;
        }
        activePlayers.push_back(std::move(state));
    }
    {
        auto broadcastProjectileMovement = [&](const std::shared_ptr<ProjectileEntity>& projectile,
                                               std::int8_t dimension) {
            if (!projectile || !projectile->hasSent) return;
            const double dx = projectile->x - projectile->sentX;
            const double dy = projectile->y - projectile->sentY;
            const double dz = projectile->z - projectile->sentZ;
            if (std::abs(dx) + std::abs(dy) + std::abs(dz) <= 0.0001) return;
            if (std::abs(dx) >= 7.999 || std::abs(dy) >= 7.999 ||
                std::abs(dz) >= 7.999) {
                const WriteBuffer body = makeEntityTeleportBody(
                    projectile->entityId, projectile->x, projectile->y,
                    projectile->z, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0, false);
                broadcastPacketExceptInDimension(dimension, nullptr,
                                                 proto::pl::sc::EntityTeleport,
                                                 body);
            } else {
                WriteBuffer body;
                body.varint(projectile->entityId);
                body.i16(static_cast<std::int16_t>(dx * 4096.0));
                body.i16(static_cast<std::int16_t>(dy * 4096.0));
                body.i16(static_cast<std::int16_t>(dz * 4096.0));
                body.i8(0);
                body.i8(0);
                body.boolean(false);
                broadcastPacketExceptInDimension(dimension, nullptr,
                                                 proto::pl::sc::MoveEntityPosRot,
                                                 body);
            }
            projectile->sentX = projectile->x;
            projectile->sentY = projectile->y;
            projectile->sentZ = projectile->z;
        };
        for (auto it = activeProjectiles.begin(); it != activeProjectiles.end();) {
            auto& pr = *it;
            if (!pr) {
                it = activeProjectiles.erase(it);
                continue;
            }
            const auto dimension = canonicalDimension(pr->dimension);
            World& world = worldFor(dimension);
            ++pr->ageTicks;
            if ((pr->ageTicks > 1200) || (pr->stuck && pr->ageTicks > 600 + 1200)) {
                despawn.push_back({dimension, pr->entityId});
                it = activeProjectiles.erase(it);
                continue;
            }
            if (pr->returningToOwner && pr->kind == ProjectileKind::Trident) {
                double tx=0, ty=0, tz=0; bool found=false;
                if (pr->ownerIsPlayer) {
                    for (const auto& pp : activePlayers)
                        if (pp.entityId == pr->ownerId && !pp.dead &&
                            pp.dimension == dimension) {
                            tx=pp.x; ty=pp.y+1.0; tz=pp.z; found=true; break;
                        }
                } else {
                    for (const auto& mb : activeMobs)
                        if (mb.entityId == pr->ownerId && !mb.dead &&
                            mb.dimension == dimension) {
                            tx=mb.x; ty=mb.y+0.8; tz=mb.z; found=true; break;
                        }
                }
                if (found) {
                    double dx=tx-pr->x, dy=ty-pr->y, dz=tz-pr->z;
                    double d = std::sqrt(dx*dx+dy*dy+dz*dz);
                    if (d < 1.5) { // caught by owner (thrown trident used durability, not consumed)
                        broadcastSoundFor(dimension, "minecraft:item.trident.return",
                                          tx, ty, tz, 1.f, 1.f, "player");
                        despawn.push_back({dimension, pr->entityId});
                        it = activeProjectiles.erase(it);
                        continue;
                    }
                    double sp = std::min(d, 1.5);
                    pr->vx = dx/d*sp; pr->vy = dy/d*sp; pr->vz = dz/d*sp;
                    pr->x += pr->vx; pr->y += pr->vy; pr->z += pr->vz;
                    broadcastProjectileMovement(pr, dimension);
                    ++it;
                    continue;
                }
                // owner gone: fall through to normal physics (ages out)
            }
            if (pr->kind == ProjectileKind::ShulkerBullet && !pr->stuck &&
                pr->targetId >= 0) {
                // A shulker bullet keeps the target selected when it is
                // fired.  Smoothly steering the current velocity toward the
                // target gives the same important gameplay property as
                // vanilla's axis-by-axis homing without retargeting an
                // unrelated player when several players are nearby.
                double tx = 0.0, ty = 0.0, tz = 0.0;
                bool foundTarget = false;
                if (pr->targetIsPlayer) {
                    for (const auto& pp : activePlayers) {
                        if (pp.entityId != pr->targetId || pp.dead ||
                            !pp.inPlay || pp.dimension != dimension)
                            continue;
                        tx = pp.x;
                        ty = pp.y + 0.9;
                        tz = pp.z;
                        foundTarget = true;
                        break;
                    }
                } else {
                    for (const auto& mb : activeMobs) {
                        if (mb.entityId != pr->targetId || mb.dead ||
                            mb.dimension != dimension)
                            continue;
                        tx = mb.x;
                        ty = mb.y + 0.8;
                        tz = mb.z;
                        foundTarget = true;
                        break;
                    }
                }
                if (foundTarget) {
                    const double dx = tx - pr->x;
                    const double dy = ty - pr->y;
                    const double dz = tz - pr->z;
                    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (distance > 1e-6) {
                        const double currentSpeed = std::sqrt(
                            pr->vx * pr->vx + pr->vy * pr->vy + pr->vz * pr->vz);
                        const double speed = std::max(0.7, currentSpeed);
                        const double turn = 0.15;
                        double vx = pr->vx * (1.0 - turn) + dx / distance * speed * turn;
                        double vy = pr->vy * (1.0 - turn) + dy / distance * speed * turn;
                        double vz = pr->vz * (1.0 - turn) + dz / distance * speed * turn;
                        const double adjustedSpeed = std::sqrt(vx * vx + vy * vy + vz * vz);
                        if (adjustedSpeed > 1e-6) {
                            const double scale = speed / adjustedSpeed;
                            vx *= scale;
                            vy *= scale;
                            vz *= scale;
                        }
                        pr->vx = vx;
                        pr->vy = vy;
                        pr->vz = vz;
                    }
                }
            }
            if (!pr->stuck) {
                double g = 0.03;
                if (pr->kind == ProjectileKind::Arrow) g = 0.05;
                else if (pr->kind == ProjectileKind::LlamaSpit) g = 0.06;
                else if (pr->kind == ProjectileKind::ShulkerBullet) g = 0.0;
                else if (pr->kind == ProjectileKind::Fireball || pr->kind == ProjectileKind::WitherSkull || pr->kind == ProjectileKind::DragonFireball || isWindCharge(pr->kind)) g = 0.0;
                if (!isWindCharge(pr->kind)) {
                    for (const auto& mb : activeMobs) if (mb.kind==MobKind::Breeze &&
                                                mb.dimension == dimension) {
                        double bdx=mb.x - pr->x, bdy=(mb.y+1.0)-pr->y, bdz=mb.z - pr->z;
                        if (bdx*bdx+bdy*bdy+bdz*bdz < 2.25) { // 1.5²
                            pr->vx = -pr->vx; pr->vy = -pr->vy*0.6 + 0.2; pr->vz = -pr->vz;
                            broadcastSoundFor(dimension, "minecraft:entity.breeze.deflect",
                                              mb.x, mb.y, mb.z, 1.f, 1.f,
                                              "hostile");
                            break;
                        }
                    }
                }
                pr->vy -= g;
                pr->x += pr->vx; pr->y += pr->vy; pr->z += pr->vz;
                broadcastProjectileMovement(pr, dimension);
                world.generateChunkIfMissing(
                    static_cast<std::int32_t>(pr->x) >> 4,
                    static_cast<std::int32_t>(pr->z) >> 4);
                if (!pr->returningToOwner && world.getBlock(static_cast<std::int32_t>(pr->x),
                                    static_cast<std::int32_t>(pr->y),
                                    static_cast<std::int32_t>(pr->z)) != 0) {
                    if (pr->kind == ProjectileKind::Arrow) {
                        pr->stuck = true;
                    } else if (pr->kind == ProjectileKind::Trident && pr->loyaltyLevel > 0) {
                        pr->returningToOwner = true; // loyalty: bounce off blocks back to owner
                    } else if (pr->kind == ProjectileKind::EnderPearl) {
                        // pearl teleport: find owner player and teleport
                        std::shared_ptr<Player> owner;
                        for (const auto &pp : activePlayers)
                            if (pp.entityId == pr->ownerId &&
                                pr->ownerIsPlayer &&
                                pp.dimension == dimension) {
                                owner = pp.entity;
                                break;
                            }
                        if (owner) {
                            double tx = pr->x + 0.5;
                            double ty = pr->y + 0.5;
                            double tz = pr->z + 0.5;
                            // clamp to avoid inside block: raise by 0.5
                            std::shared_ptr<Connection> ownerConnection;
                            std::int32_t ownerEntityId = 0;
                            float ownerYaw = 0.0f;
                            float ownerPitch = 0.0f;
                            std::int8_t ownerDimension = dimension;
                            {
                                std::lock_guard ownerLock(owner->stateMtx);
                                owner->lastEnderPearlTick = tickNo_;
                                ownerConnection = owner->conn;
                                ownerEntityId = owner->entityId;
                                ownerYaw = owner->yaw;
                                ownerPitch = owner->pitch;
                                ownerDimension = canonicalDimension(owner->dimension);
                            }
                            // Route through the same confirmation sequence as
                            // commands and session-originated corrections.
                            teleportPlayer(*owner, tx, ty, tz);
                            // broadcast to others
                            {
                                const WriteBuffer tp = makeEntityTeleportBody(
                                    ownerEntityId, tx, ty, tz, 0.0, 0.0, 0.0,
                                    ownerYaw, ownerPitch, 0, false);
                                broadcastPacketExceptInDimension(
                                    ownerDimension, nullptr,
                                    proto::pl::sc::EntityTeleport, tp);
                            }
                            applyDamage(*owner, 5.f, "fall");
                            if (ownerConnection) {
                                auto pid = gen::itemIdByName().find("minecraft:ender_pearl");
                                if (pid != gen::itemIdByName().end()) {
                                    WriteBuffer cd;
                                    cd.varint(static_cast<int32_t>(pid->second));
                                    cd.varint(20); // 1 sec vanilla
                                    ownerConnection->trySendPacket(proto::pl::sc::SetCooldown, cd);
                                }
                            }
                        }
                        despawn.push_back({dimension, pr->entityId});
                        it = activeProjectiles.erase(it); continue;
                    } else {
                        despawn.push_back({dimension, pr->entityId});
                        it = activeProjectiles.erase(it); continue;
                    }
                } else {
                    // entity collision
                    bool hitSomething = false;
                    for (const auto& pp : activePlayers) {
                        if (pr->ownerIsPlayer && pp.entityId == pr->ownerId)
                            continue;
                        if (pp.dead || !pp.inPlay || !pp.connection ||
                            pp.dimension != dimension)
                            continue;
                        const double dx = pp.x - pr->x;
                        const double dy = pp.y + 0.9 - pr->y;
                        const double dz = pp.z - pr->z;
                        if (dx*dx + dy*dy + dz*dz < 0.55) {
                            float dmg = 0;
                            if (pr->kind == ProjectileKind::Arrow) {
                                float base=6.f;
                                dmg = base * static_cast<float>(std::min(1.0, std::sqrt(pr->vx*pr->vx+pr->vy*pr->vy+pr->vz*pr->vz)/2.0));
                            } else if (pr->kind == ProjectileKind::Trident) {
                                dmg = 8.f; // plan44 G-09: vanilla thrown-trident damage vs players
                            } else if (isWindCharge(pr->kind)) {
                                dmg = 1.f;
                            } else if (pr->kind == ProjectileKind::LlamaSpit) {
                                dmg = 1.f;
                            } else if (pr->kind == ProjectileKind::ShulkerBullet) {
                                dmg = 4.f;
                            }
                            if ((pr->kind == ProjectileKind::Arrow || pr->kind == ProjectileKind::Trident) && pr->piercingLevel <= 0) {
                                DamageSource asrc(pr->kind == ProjectileKind::Arrow ? "arrow" : "trident");
                                if (CombatManager::tryShieldBlock(*this, *pp.entity,
                                        asrc, pr->x, pr->z, false, dmg)) {
                                    hitSomething = true; break;
                                }
                            }
                            if (std::find(pr->piercedIds.begin(), pr->piercedIds.end(), pp.entityId) != pr->piercedIds.end())
                                continue;
                            if (dmg > 0)
                                hits.push_back({pr, pp.entity, nullptr, dmg});
                            else if (isWindCharge(pr->kind)) {
                                // wind charge knockback only even if dmg 1
                                hits.push_back({pr, pp.entity, nullptr, 1.f});
                            } else {
                                hitSomething = true; break;
                            }
                            // wind charge knockback
                            if (isWindCharge(pr->kind)) {
                                double inv=1.0/(std::sqrt(pr->vx*pr->vx+pr->vz*pr->vz)+1e-6);
                                double kx=pr->vx*inv*1.8, kz=pr->vz*inv*1.8;
                                WriteBuffer vel; vel.varint(pp.entityId); vel.i16((int16_t)(kx*8000)); vel.i16((int16_t)(0.35*8000)); vel.i16((int16_t)(kz*8000));
                                pp.connection->trySendPacket(proto::pl::sc::EntityVelocity, vel);
                            }
                            if (pr->kind == ProjectileKind::Arrow && pr->piercingLevel > 0) {
                                pr->piercedIds.push_back(pp.entityId);
                                pr->piercingLevel--;
                                continue;
                            }
                            hitSomething = true;
                            break;
                        }
                    }
                    if (!hitSomething) {
                        for (const auto& m : activeMobs) {
                            if (m.dimension != dimension)
                                continue;
                            if (!pr->ownerIsPlayer &&
                                m.entityId == pr->ownerId) continue;
                            const double dx = m.x - pr->x;
                            const double dy = m.y + 0.8 - pr->y;
                            const double dz = m.z - pr->z;
                            if (dx*dx + dy*dy + dz*dz < 0.55) {
                                if (std::find(pr->piercedIds.begin(), pr->piercedIds.end(), m.entityId) != pr->piercedIds.end())
                                    continue;
                                float dmg = 5.f;
                                if (isWindCharge(pr->kind)) dmg=1.f;
                                else if (pr->kind == ProjectileKind::LlamaSpit) dmg = 1.f;
                                else if (pr->kind == ProjectileKind::ShulkerBullet) dmg = 4.f;
                                hits.push_back({pr, nullptr, m.entity, dmg});
                                if (pr->kind == ProjectileKind::Arrow && pr->piercingLevel > 0) {
                                    pr->piercedIds.push_back(m.entityId);
                                    pr->piercingLevel--;
                                    continue;
                                }
                                hitSomething = true;
                                break;
                            }
                        }
                    }
                    if (hitSomething) {
                        if (pr->kind == ProjectileKind::Trident && pr->loyaltyLevel > 0 && !pr->returningToOwner) {
                            pr->returningToOwner = true;
                            ++it;
                            continue;
                        }
                        despawn.push_back({dimension, pr->entityId});
                        it = activeProjectiles.erase(it);
                        continue;
                    }
                }
            }
            ++it;
        }
    }
    {
        std::lock_guard lk(projectilesMtx_);
        projectiles_.insert(projectiles_.end(), activeProjectiles.begin(),
                            activeProjectiles.end());
    }
    for (auto& h : hits) {
        const char* damageType = projectileDamageType(h.p->kind);
        if (h.p->kind == ProjectileKind::Trident) {
            const auto dimension = canonicalDimension(h.p->dimension);
            bool isThundering = dimension == 0 && thundering();
            if (isThundering) {
                double lx = h.p->x;
                double ly = h.p->y;
                double lz = h.p->z;
                if (h.player) {
                    std::lock_guard playerLock(h.player->stateMtx);
                    lx = h.player->x;
                    ly = h.player->y;
                    lz = h.player->z;
                } else if (h.mob) {
                    std::lock_guard mobLock(*h.mob->stateMtx);
                    lx = h.mob->x;
                    ly = h.mob->y;
                    lz = h.mob->z;
                }
                bool hasChannel=false;
                for (const auto& pp : activePlayers)
                    if (pp.entityId == h.p->ownerId && h.p->ownerIsPlayer &&
                        pp.dimension == canonicalDimension(h.p->dimension)) {
                    for(int i=36;i<=44;i++) if(!pp.inv[i].empty() && EnchantmentHelper::hasChanneling(pp.inv[i])) hasChannel=true;
                    for(int i=5;i<=8;i++) if(!pp.inv[i].empty() && EnchantmentHelper::hasChanneling(pp.inv[i])) hasChannel=true;
                    // also check held trident directly if owner inventory not found via helper already, but also direct check
                    break;
                }
                if (hasChannel && isThundering) {
                    // canSeeSky: check sky light or no opaque blocks above target
                    bool canSeeSky = false;
                    {
                        int tx = (int)std::floor(lx);
                        int ty = (int)std::floor(ly);
                        int tz = (int)std::floor(lz);
                        // simplified: if sky light 15 at target y, consider canSeeSky
                        try{
                            World& hitWorld = worldFor(h.p->dimension);
                            uint8_t sky = hitWorld.getSkyLight(tx, ty, tz);
                            if(sky >= 15) canSeeSky = true;
                            else {
                                // fallback: scan up to maxY for non-air
                                bool blocked=false;
                                for(int y2=ty+1; y2<hitWorld.maxY(); ++y2){
                                    if(hitWorld.getBlock(tx, y2, tz)!=0){ blocked=true; break; }
                                }
                                canSeeSky = !blocked;
                            }
                        } catch(...){ canSeeSky = true; }
                    }
                    if(canSeeSky){
                        strikeLightningFor(h.p->dimension, lx, ly, lz);
                    }
                }
            }
        }
        if (h.player) {
            applyDamage(*h.player, h.dmg, damageType);
            std::shared_ptr<Connection> playerConnection;
            std::int32_t playerEntityId = 0;
            std::int8_t playerDimension = 0;
            bool applyLevitation = false;
            WriteBuffer effectPacket;
            if (h.p->kind == ProjectileKind::ShulkerBullet) {
                constexpr std::int32_t kLevitationDuration = 10 * 20;
                std::lock_guard playerLock(h.player->stateMtx);
                if (!h.player->dead) {
                    auto effect = std::find_if(
                        h.player->effects.begin(), h.player->effects.end(),
                        [](const EffectInstance& value) {
                            return value.type == effects::Levitation;
                        });
                    if (effect == h.player->effects.end()) {
                        EffectInstance levitation;
                        levitation.type = effects::Levitation;
                        levitation.amplifier = 0;
                        levitation.durationTicks = kLevitationDuration;
                        h.player->effects.push_back(levitation);
                        effect = std::prev(h.player->effects.end());
                    } else {
                        effect->amplifier =
                            std::max<std::int32_t>(effect->amplifier, 0);
                        effect->durationTicks = std::max(effect->durationTicks,
                                                         kLevitationDuration);
                    }
                    effectPacket.varint(h.player->entityId);
                    effectPacket.varint(effects::Levitation);
                    effectPacket.varint(0);
                    effectPacket.varint(kLevitationDuration);
                    effectPacket.u8(static_cast<std::uint8_t>(
                        effectFlags(*effect)));
                    applyLevitation = true;
                }
            }
            {
                std::lock_guard playerLock(h.player->stateMtx);
                playerConnection = h.player->conn;
                playerEntityId = h.player->entityId;
                playerDimension = canonicalDimension(h.player->dimension);
            }
            if (applyLevitation) {
                broadcastPacketExceptInDimension(
                    playerDimension, nullptr, pl::sc::EntityEffect,
                    effectPacket);
                onEffectsChanged(h.player.get());
            }
            WriteBuffer de;
            de.varint(playerEntityId);
            const auto dtid = gameData_.idOf(
                "minecraft:damage_type", std::string("minecraft:") + damageType);
            de.varint(dtid >= 0 ? dtid : 0);
            de.varint(0); de.varint(0);
            de.boolean(false);
            if (playerConnection)
                playerConnection->trySendPacket(pl::sc::DamageEvent, de);
        } else if (h.mob) {
            const bool damageApplied =
                applyDamageToMob(*h.mob, h.dmg, damageType);
            if (damageApplied && h.p->ownerIsPlayer)
                noteMobHurt(h.mob->entityId, h.p->ownerId, 0);
            std::int32_t mobEntityId = 0;
            std::int8_t mobDimension = 0;
            double mobX = 0.0;
            double mobY = 0.0;
            double mobZ = 0.0;
            MobKind mobKind = MobKind::Pig;
            bool mobDead = false;
            bool mobOnFire = false;
            {
                std::lock_guard mobLock(*h.mob->stateMtx);
                mobDead = h.mob->dead;
                mobEntityId = h.mob->entityId;
                mobDimension = canonicalDimension(h.mob->dimension);
                mobX = h.mob->x;
                mobY = h.mob->y;
                mobZ = h.mob->z;
                mobKind = h.mob->kind;
                mobOnFire = h.mob->onFireTicks > 0;
            }
            if (mobDead && h.mob->tryClaimDeathHandling()) {
                invalidateJvmMob(h.mob);
                broadcastMobRemoval(mobEntityId);
                auto lootContext = mobLootContextForDeath(
                    mobEntityId, mobOnFire);
                if (h.p->ownerIsPlayer) lootContext.wasKilledByPlayer = true;
                spawnMobLootFor(mobKind, mobDimension, mobX,
                                mobY + .4, mobZ, lootContext);
                spawnXpOrbsFor(mobDimension, mobX, mobY + .5, mobZ,
                               mobStats(mobKind).xpDrop, nullptr);
                {
                    std::lock_guard lk(entsMtx_);
                    mobs_.erase(std::remove(mobs_.begin(), mobs_.end(), h.mob),
                                mobs_.end());
                }
                eraseMobAi(mobEntityId);
            }
        }
    }
    for (const auto& gone : despawn) {
        WriteBuffer rm; rm.varint(1); rm.varint(gone.entityId);
        broadcastPacketExceptInDimension(gone.dimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }
}

} // namespace cppfm
