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
std::optional<double> sweptEntitySupportY(World& world, double x, double z,
                                           double previousY,
                                           double currentY) {
    const auto blockX = static_cast<std::int32_t>(std::floor(x));
    const auto blockZ = static_cast<std::int32_t>(std::floor(z));
    world.generateChunkIfMissing(blockX >> 4, blockZ >> 4);
    const auto geometry = world.verticalGeometry();
    const auto scanStartY = static_cast<std::int32_t>(std::clamp(
        std::floor(previousY), static_cast<double>(geometry.minY),
        static_cast<double>(geometry.maxY() - 1)));
    // Entity vertical speed is capped below one block per tick. Three blocks
    // cover the tallest modeled collision shapes without scanning the entire
    // world column for every dropped item/orb on every tick.
    const auto scanEndY = std::max(geometry.minY, scanStartY - 2);
    std::optional<double> supportY;
    world.withChunk(blockX >> 4, blockZ >> 4, [&](const Chunk& chunk) {
        for (int y = scanStartY; y >= scanEndY; --y) {
            const int storageY = geometry.storageY(y);
            const auto state = chunk.blockAt(Chunk::index(
                storageY >> 4, storageY & 15, blockZ & 15, blockX & 15));
            const auto surface = entity_support::topCollisionSurfaceHeight(
                state);
            if (!surface) continue;
            const double targetY = y + *surface + 0.25;
            // Only resolve a downward crossing. A ceiling above an item cannot
            // become a floor and teleport the entity upward through itself.
            if (previousY >= targetY && currentY < targetY) {
                supportY = targetY;
                break;
            }
        }
    });
    return supportY;
}

} // namespace

void GameServer::growResinNearHeart(int hx,int hy,int hz) {
    const std::int8_t dimension = brainTickGuard_ ? brainTickGuard_->dimension : 0;
    growResinNearHeartFor(dimension, hx, hy, hz);
}
void GameServer::growResinNearHeartFor(std::int8_t dimension, int hx, int hy,
                                       int hz) {
    if (!isNight()) return;
    World& world = worldFor(dimension);
    // find pale_oak_log within 8 of heart and place resin_clump on side
    for (int attempt=0; attempt<8; ++attempt) {
        int lx = hx + (nextRandom()%17 - 8);
        int ly = hy + (nextRandom()%9 - 4);
        int lz = hz + (nextRandom()%17 - 8);
        uint16_t st = world.getBlock(lx,ly,lz);
        auto* bd = gen::blockByState(st);
        if (!bd) continue;
        std::string n(bd->name);
        if (n!="minecraft:pale_oak_log" && n!="minecraft:stripped_pale_oak_log" && n!="minecraft:pale_oak_wood") continue;
        const int DX[4]={1,-1,0,0}, DZ[4]={0,0,1,-1};
        for (int d=0; d<4; ++d) {
            int rx=lx+DX[d], rz=lz+DZ[d];
            if (world.getBlock(rx,ly,rz)!=0) continue;
            auto it = gen::blockNameToState().find("minecraft:resin_clump");
            if (it==gen::blockNameToState().end()) continue;
            uint16_t place = static_cast<uint16_t>(it->second);
            world.setBlock(rx,ly,rz,place);
            broadcastBlockChangeFor(dimension, rx,ly,rz,place);
            broadcastSoundFor(dimension, "minecraft:block.resin.place",
                              rx+0.5, ly+0.5, rz+0.5, 1.f, 1.f, "block");
            return;
        }
    }
}
void GameServer::itemsTick() {
    struct Pickup {
        std::shared_ptr<ItemEntity> ent;
        std::shared_ptr<Player> collector;
    };
    struct Despawn {
        std::int8_t dimension;
        std::int32_t entityId;
    };
    const auto addStackToInventory = [&](Player& player,
                                         const ItemStack& source) {
        return insertPlayerStackPartial(
            player.inv, source, source.count, maxStackFor(source));
    };
    std::vector<Pickup> pickups;
    std::vector<Despawn> expired;
    std::vector<std::shared_ptr<ItemEntity>> active;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = itemDrops_.begin(); it != itemDrops_.end();) {
            const auto& e = *it;
            if (!e || e->collected || e->ageTicks > 6000) {
                if (e && !e->collected && e->ageTicks > 6000)
                    expired.push_back({canonicalDimension(e->dimension),
                                       e->entityId});
                it = itemDrops_.erase(it);
                continue;
            }
            ++it;
        }
        active = itemDrops_;
    }
    for (const auto& gone : expired) {
        WriteBuffer rm;
        rm.varint(1);
        rm.varint(gone.entityId);
        broadcastPacketExceptInDimension(gone.dimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }

    // Keep world scans and player snapshots out of the entity-container lock.
    // Event/JVM callbacks can re-enter the server while an item is ticking.
    const auto players = playersSnapshot();
    constexpr double kPlayerHalfWidth = 0.3;
    constexpr double kPlayerHeight = 1.8;
    constexpr double kItemHalfWidth = 0.125;
    constexpr double kItemHeight = 0.25;
    constexpr double kPickupReach = 1.0;
    for (const auto& e : active) {
        if (!e || e->collected) continue;
        World& world = worldFor(e->dimension);
        ++e->ageTicks;
        if (e->ageTicks > 6000) continue;
        const double previousY = e->y;
        e->vy -= 0.04;
        if (e->vy < -0.5) e->vy = -0.5;
        e->y += e->vy;
        e->x += e->vx;
        e->z += e->vz;
        if (const auto supportY = sweptEntitySupportY(
                world, e->x, e->z, previousY, e->y)) {
            e->y = *supportY;
            e->vy = 0;
            e->vx *= 0.6;
            e->vz *= 0.6;
        }
        if (e->ageTicks <= 10) continue;
        for (const auto& pp : players) {
            auto* pl = pp.get();
            bool eligible = false;
            double playerX = 0.0;
            double playerY = 0.0;
            double playerZ = 0.0;
            std::int8_t playerDimension = 0;
            {
                std::lock_guard playerLock(pl->stateMtx);
                eligible = pl->inPlay && static_cast<bool>(pl->conn) &&
                           !pl->dead;
                playerX = pl->x;
                playerY = pl->y;
                playerZ = pl->z;
                playerDimension = canonicalDimension(pl->dimension);
            }
            if (!eligible || playerDimension !=
                                canonicalDimension(e->dimension)) continue;
            // Item pickup is a player/item bounding-box collision, not a
            // sphere around the player's eye/body center. Measure the gap
            // between the two boxes and apply the one-block pickup reach;
            // using playerY + 0.9 made items resting just below a player's
            // feet unreachable even when their horizontal boxes overlapped.
            const auto horizontalGap = [](double delta) {
                return std::max(0.0, std::abs(delta) -
                    (kPlayerHalfWidth + kItemHalfWidth));
            };
            const double dx = horizontalGap(playerX - e->x);
            const double dz = horizontalGap(playerZ - e->z);
            const double itemTop = e->y + kItemHeight;
            const double playerTop = playerY + kPlayerHeight;
            const double dy = itemTop < playerY
                ? playerY - itemTop
                : (e->y > playerTop ? e->y - playerTop : 0.0);
            // This is an expanded-box test: the pickup radius is applied on
            // each axis, not as a sphere. A diagonal item can therefore be
            // inside the pickup bounds even when the 3D gap length is > 1.
            if (dx < kPickupReach && dy < kPickupReach &&
                dz < kPickupReach) {
                pickups.push_back({e, pp});
                break;
            }
        }
    }
    for (auto& pk : pickups) {
        if (!pk.ent || !pk.collector) continue;
        // Keep the shared-state lock order consistent with xpOrbsTick and
        // session handlers: player state first, entity storage second.  The
        // inventory merge below is therefore atomic with the item removal
        // without ever taking stateMtx while entsMtx_ is held.
        Player& collector = *pk.collector;
        ItemStack obtained;
        std::int8_t pickupDimension = 0;
        std::int32_t pickupEntityId = 0;
        std::int32_t collectorEntityId = 0;
        int obtainedCount = 0;
        bool fullyRemoved = false;
        bool removed = false;
        {
            std::lock_guard playerLock(collector.stateMtx);
            if (!collector.inPlay || collector.dead || !collector.conn ||
                canonicalDimension(collector.dimension) !=
                    canonicalDimension(pk.ent->dimension))
                continue;
            {
                std::lock_guard lk(entsMtx_);
                const auto it = std::find(itemDrops_.begin(), itemDrops_.end(),
                                          pk.ent);
                if (it != itemDrops_.end() && !(*it)->collected) {
                    const ItemStack current = (*it)->asStack();
                    const int moved = addStackToInventory(collector, current);
                    if (moved > 0) {
                        obtained = current;
                        obtained.count = static_cast<std::int16_t>(moved);
                        obtainedCount = moved;
                        pickupDimension = canonicalDimension((*it)->dimension);
                        pickupEntityId = (*it)->entityId;
                        collectorEntityId = collector.entityId;
                        if (moved >= current.count) {
                            (*it)->collected = true;
                            (*it)->count = 0;
                            (*it)->stack = ItemStack::air();
                            itemDrops_.erase(it);
                            fullyRemoved = true;
                        } else {
                            (*it)->count = static_cast<std::uint8_t>(
                                current.count - moved);
                            if (!(*it)->stack.empty())
                                (*it)->stack.count = (*it)->count;
                        }
                        removed = true;
                    }
                }
            }
        }
        if (!removed) continue;
        onItemObtained(collector, obtained, "picked_up");
        WriteBuffer c;
        c.varint(pickupEntityId);
        c.varint(collectorEntityId);
        c.varint(obtainedCount);
        broadcastPacketExceptInDimension(pickupDimension, nullptr,
                                         0x76 /*collect*/, c);
        resendInventory(collector);
        if (fullyRemoved) {
            WriteBuffer rm;
            rm.varint(1);
            rm.varint(pickupEntityId);
            broadcastPacketExceptInDimension(pickupDimension, nullptr,
                                             pl::sc::RemoveEntities, rm);
        } else {
            // A partial pickup keeps the entity alive but changes its Slot
            // metadata.  Without this update clients continue rendering the
            // pre-pickup count until a later respawn.
            broadcastItemMetadata(*pk.ent);
        }
    }
}
void GameServer::spawnItemDropFor(std::int8_t dimension, double x, double y,
                                  double z, std::uint32_t itemId,
                                  std::uint8_t cnt, double vx, double vy,
                                  double vz) {
    ItemStack s = (itemId==0 || cnt==0) ? ItemStack::air() : ItemStack::of(itemId, cnt);
    spawnItemDropFor(dimension, x, y, z, s, vx, vy, vz);
}
void GameServer::spawnItemDropFor(std::int8_t dimension, double x, double y,
                                  double z, const ItemStack& stack, double vx,
                                  double vy, double vz) {
    if (mobStateLockOwnedByCurrentThread()) {
        const ItemStack copiedStack = stack;
        runWithoutMobStateLock([this, dimension, x, y, z, copiedStack, vx, vy,
                                vz] {
            spawnItemDropFor(dimension, x, y, z, copiedStack, vx, vy, vz);
        });
        return;
    }
    auto e = std::make_shared<ItemEntity>();
    e->entityId = nextEntityId();
    e->dimension = canonicalDimension(dimension);
    e->x=x; e->y=y; e->z=z; e->vx=vx; e->vy=vy; e->vz=vz;
    e->setStack(stack);
    {
        std::lock_guard lk(entsMtx_);
        itemDrops_.push_back(e);
    }
    broadcastSpawnItem(*e);
}
void GameServer::broadcastSpawnItem(const ItemEntity& it) {
    WriteBuffer b;
    b.varint(it.entityId);
    const auto uuid = entityUuidForId(it.entityId);
    b.uuid(uuid.data());
    b.varint(static_cast<std::int32_t>(gen::entityTypeIdByName().at("minecraft:item")));
    b.f64(it.x); b.f64(it.y); b.f64(it.z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(1);                                        // objectData = 1 (item w/ stack)
    b.i16(static_cast<std::int16_t>(it.vx*8000));
    b.i16(static_cast<std::int16_t>(it.vy*8000));
    b.i16(static_cast<std::int16_t>(it.vz*8000));
    broadcastPacketExceptInDimension(it.dimension, nullptr, pl::sc::SpawnEntity,
                                     b);
    broadcastItemMetadata(it);
}
void GameServer::broadcastItemMetadata(const ItemEntity& it) {
    WriteBuffer md;
    md.varint(it.entityId);
    md.u8(8); md.u8(7);
    WriteBuffer slot;
    ItemStack s = it.asStack();
    s.write(slot);
    md.raw(slot.data.data(), slot.data.size());
    md.u8(255);
    broadcastPacketExceptInDimension(it.dimension, nullptr,
                                     pl::sc::SetEntityMetadata, md);
}
bool GameServer::addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count) {
    return addStackToInventory(p, ItemStack::of(itemId), count);
}
bool GameServer::addStackToInventory(Player& p, const ItemStack& source,
                                     std::uint16_t count) {
    if (count == 0) return true;
    if (source.empty()) return false;

    std::lock_guard playerLock(p.stateMtx);
    return insertCompletePlayerStack(p.inv, source, count, maxStackFor(source));
}
void GameServer::resendInventory(Player& p) {
    std::shared_ptr<Connection> connection;
    WriteBuffer b;
    std::unique_lock playerLock(p.stateMtx);
    std::unique_lock packetLock(p.inventoryPacketMtx);
    connection = p.conn;
    if (!connection) return;
    b.varint(0);                                          // window 0
    b.varint(++p.invStateId);
    b.varint(static_cast<std::int32_t>(kPlayerInventorySlotCount));
    for (std::size_t i = 0; i < kPlayerInventorySlotCount; ++i)
        p.inv[i].write(b);
    ItemStack::air().write(b);                            // carried
    playerLock.unlock();
    connection->trySendPacket(pl::sc::ContainerSetContent, b);
    packetLock.unlock();
    // Equipped stacks are another client-visible projection of this same
    // inventory. Publish it here so durability breaks, replacements, and
    // clears do not depend on every mutation caller remembering a second API.
    syncEquipmentOnChange(p);
}
void GameServer::sendSetExperience(Player& p) {
    std::shared_ptr<Connection> connection;
    WriteBuffer b;
    {
        std::lock_guard playerLock(p.stateMtx);
        connection = p.conn;
        if (!connection) return;
        b.f32(p.xp.progress);
        b.varint(p.xp.level);
        b.varint(p.xp.totalXp);
    }
    connection->trySendPacket(pl::sc::SetExperience, b);
}
void GameServer::furnacesTick() {
    furnacesTickFor(0);
    furnacesTickFor(-1);
    furnacesTickFor(1);
}

void GameServer::furnacesTickFor(std::int8_t dimension) {
    auto& dimensionBlockEntities = blockEntitiesFor(dimension);
    auto& dimensionWorld = worldFor(dimension);
    auto broadcastBlockChange = [this, dimension](std::int32_t x,
                                                   std::int32_t y,
                                                   std::int32_t z,
                                                   std::uint16_t state) {
        this->broadcastBlockChangeFor(dimension, x, y, z, state);
    };
    dimensionBlockEntities.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Furnace) return;
        FurnaceData& f = be.furnace;
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        dimensionWorld.generateChunkIfMissing(x >> 4, z >> 4);
        const std::uint16_t stateHere = dimensionWorld.getBlock(x, y, z);

        // fuel consumption
        if (f.burnTicks > 0) --f.burnTicks;
        const Recipe* recipe =
            f.slots[FurnaceData::kInput].empty()
                ? nullptr
                : recipes_.findSmelting(f.slots[FurnaceData::kInput].itemId);
        const bool canSmelt = recipe != nullptr;
        if (f.burnTicks <= 0 && canSmelt && !f.slots[FurnaceData::kFuel].empty()) {
            const int ft = furnaceFuelTicks(f.slots[FurnaceData::kFuel].itemId);
            if (ft > 0) {
                f.burnDuration = static_cast<std::int16_t>(ft);
                f.burnTicks = f.burnDuration;
                ItemStack& fuel = f.slots[FurnaceData::kFuel];
                if (--fuel.count <= 0) fuel = ItemStack::air();
                dimensionBlockEntities.markDirty(key);
            }
        }
        const bool burning = f.burnTicks > 0;
        if (canSmelt && burning) {
            if (++f.cookProgress >= f.cookTotal) {
                f.cookProgress = 0;
                auto out = recipe->result;
                auto& dst = f.slots[FurnaceData::kOutput];
                if (dst.empty()) dst = out;
                else if (dst.sameItemData(out)) dst.count += out.count;
                else { f.cookProgress = f.cookTotal; return; }
                ItemStack& in = f.slots[FurnaceData::kInput];
                if (--in.count <= 0) in = ItemStack::air();
                dimensionBlockEntities.markDirty(key);
                // xp orbs on manual collection only; skip here
            }
        } else {
            f.cookProgress = 0;
        }

        // lit-state block update (vanilla swaps furnace[lit=...])
        static const gen::BlockDef* fdef = gen::blockByName("minecraft:furnace");
        if ((fdef && stateHere == fdef->defaultState) || stateHere == 4351) {
            // NOTE(cleanup): parenthesized as evaluated — (fdef && stateHere == default) || stateHere == 4351.
            const std::uint16_t want = static_cast<std::uint16_t>(
                gen::stateWithPropsList("minecraft:furnace",
                    {{"lit", burning ? "true" : "false"}}));
            if (stateHere != want) {
                dimensionWorld.setBlock(x, y, z, want);
                broadcastBlockChange(x, y, z, want);
            }
        }
    });
}
void GameServer::brewingTick() {
    brewingTickFor(0);
    brewingTickFor(-1);
    brewingTickFor(1);
}

void GameServer::brewingTickFor(std::int8_t dimension) {
    auto& dimensionBlockEntities = blockEntitiesFor(dimension);
    // Brewing stand: fuel (blaze powder -> kFuelPerBlaze) + brewTime kBrewTicks.
    const auto itBlaze = gen::itemIdByName().find("minecraft:blaze_powder");
    const std::uint32_t blazeId = itBlaze != gen::itemIdByName().end() ? itBlaze->second : 0;
    dimensionBlockEntities.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Brewing) return;
        BrewingData& b = be.brewing;
        // replenish fuel from blaze powder in slot 4
        if (b.fuel <= 0 && !b.slots[4].empty() && (blazeId == 0 || b.slots[4].itemId == blazeId)) {
            if (--b.slots[4].count <= 0) b.slots[4] = ItemStack::air();
            b.fuel = BrewingData::kFuelPerBlaze;
            dimensionBlockEntities.markDirty(key);
        }
        if (b.brewTime > 0) {
            --b.brewTime;
            dimensionBlockEntities.markDirty(key);
            if (b.brewTime == 0) {
                // brew complete: consume ingredient slot 3 and transform potions (strict audit MEDIUM I7)
                if (!b.slots[3].empty()) {
                    std::uint32_t ingId = b.slots[3].itemId;
                    if (--b.slots[3].count <= 0) b.slots[3] = ItemStack::air();
                    auto idOf = [&](const char* n)->std::uint32_t{
                        auto it = gen::itemIdByName().find(n);
                        return it != gen::itemIdByName().end() ? it->second : 0;
                    };
                    std::uint32_t potionId = idOf("minecraft:potion");
                    std::uint32_t splashId = idOf("minecraft:splash_potion");
                    std::uint32_t lingeringId = idOf("minecraft:lingering_potion");
                    std::uint32_t gunpowderId = idOf("minecraft:gunpowder");
                    std::uint32_t dragonBreathId = idOf("minecraft:dragon_breath");
                    for (int pi = 0; pi < 3; ++pi) {
                        auto &stk = b.slots[pi];
                        if (stk.empty()) continue;
                        // handle gunpowder -> splash and dragon breath -> lingering via itemId change (PotionBrewing splash transform)
                        if (ingId == gunpowderId && potionId != 0 && splashId != 0) {
                            if (stk.itemId == potionId) {
                                std::vector<std::uint8_t> saved;
                                for (auto &pr : stk.components) if (pr.first==ItemStack::kPotionContentsComponentId) saved = pr.second;
                                stk.itemId = splashId;
                                if (!saved.empty()) {
                                    bool has=false;
                                    for (auto &pr: stk.components) if(pr.first==ItemStack::kPotionContentsComponentId) has=true;
                                    if (!has) stk.components.emplace_back(ItemStack::kPotionContentsComponentId, saved);
                                }
                                continue;
                            }
                        }
                        if (ingId == dragonBreathId && splashId != 0 && lingeringId != 0) {
                            if (stk.itemId == splashId) {
                                std::vector<std::uint8_t> saved;
                                for (auto &pr : stk.components) if (pr.first==ItemStack::kPotionContentsComponentId) saved = pr.second;
                                stk.itemId = lingeringId;
                                if (!saved.empty()) {
                                    bool has=false;
                                    for (auto &pr: stk.components) if(pr.first==ItemStack::kPotionContentsComponentId) has=true;
                                    if (!has) stk.components.emplace_back(ItemStack::kPotionContentsComponentId, saved);
                                }
                                continue;
                            }
                        }
                        bool isPotionItem = (stk.itemId == potionId || stk.itemId == splashId || stk.itemId == lingeringId);
                        if (!isPotionItem) continue;
                        int curId = stk.getPotionId();
                        bool hasContents = stk.hasPotionContents();
                        int target = PotionBrewing::mix(curId, hasContents, ingId);
                        if (target >= 0) {
                            stk.setPotionId(target);
                        }
                    }
                    dimensionBlockEntities.markDirty(key);
                } else {
                    // no ingredient but timer expired? just reset
                    b.brewTime = 0;
                }
            }
        } else {
            // idle: try to start brewing if we have ingredient + at least one potion and fuel
            bool hasIngredient = !b.slots[3].empty();
            bool hasPotion = !b.slots[0].empty() || !b.slots[1].empty() || !b.slots[2].empty();
            if (hasIngredient && hasPotion && b.fuel > 0) {
                // consume 1 fuel per operation
                --b.fuel;
                b.brewTime = BrewingData::kBrewTicks;
                dimensionBlockEntities.markDirty(key);
            }
        }
    });
}
void GameServer::spawnXpOrbs(double x, double y, double z, int totalPoints,
                             Player* directTo) {
    spawnXpOrbsFor(0, x, y, z, totalPoints, directTo);
}
void GameServer::spawnXpOrbsFor(std::int8_t dimension, double x, double y,
                                double z, int totalPoints, Player* directTo) {
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, dimension, x, y, z, totalPoints, directTo] {
            spawnXpOrbsFor(dimension, x, y, z, totalPoints, directTo);
        });
        return;
    }
    static const int kSizes[] = {1, 3, 7, 17, 37, 73, 149, 307, 617, 1237, 2477};
    std::vector<int> orbs;
    while (totalPoints > 0) {
        int pick = 0;
        for (int i = 0; i < 11; ++i)
            if (kSizes[i] <= totalPoints) pick = i;
        if (pick == 0 && totalPoints < 1) break;
        const int v = kSizes[pick];
        orbs.push_back(std::min(v, totalPoints));
        totalPoints -= std::min(v, totalPoints);
        if (orbs.size() >= 16) break;                    // sanity cap
    }
    if (orbs.empty()) return;
    std::vector<std::shared_ptr<XpOrbEntity>> created;
    {
        std::lock_guard lk(entsMtx_);
        for (int v : orbs) {
            auto e = std::make_shared<XpOrbEntity>();
            e->entityId = nextEntityId();
            e->dimension = canonicalDimension(dimension);
            e->value = static_cast<std::uint16_t>(v);
            e->x = x + ((nextRandom() % 5) - 2) * 0.1;
            e->y = y; e->z = z + ((nextRandom() % 5) - 2) * 0.1;
            e->vy = 0.08;
            xpOrbs_.push_back(e);
            created.push_back(e);
        }
    }
    for (auto& e : created) {
        WriteBuffer b;
        b.varint(e->entityId);
        b.f64(e->x); b.f64(e->y); b.f64(e->z);
        b.i16(static_cast<std::int16_t>(e->value));
        broadcastPacketExceptInDimension(e->dimension, nullptr,
                                         pl::sc::SpawnExperienceOrb, b);
    }
}
void GameServer::xpOrbsTick() {
    struct Pickup {
        std::shared_ptr<XpOrbEntity> orb;
        std::shared_ptr<Player> p;
    };
    struct Despawn {
        std::int8_t dimension;
        std::int32_t entityId;
    };
    std::vector<Pickup> pickups;
    std::vector<Despawn> expired;
    std::vector<std::shared_ptr<XpOrbEntity>> active;
    {
        std::lock_guard lk(entsMtx_);
        active = xpOrbs_;
    }
    const auto players = playersSnapshot();
    for (const auto& e : active) {
        if (!e) continue;
        World& world = worldFor(e->dimension);
        ++e->ageTicks;
        if (e->ageTicks > 6000) {
            bool removed = false;
            std::lock_guard lk(entsMtx_);
            const auto it = std::find(xpOrbs_.begin(), xpOrbs_.end(), e);
            if (it != xpOrbs_.end()) {
                xpOrbs_.erase(it);
                removed = true;
            }
            if (removed)
                expired.push_back({canonicalDimension(e->dimension),
                                   e->entityId});
            continue;
        }
        const double previousY = e->y;
        e->vy -= 0.03;
        if (e->vy < -0.4) e->vy = -0.4;
        e->y += e->vy;
        if (const auto supportY = sweptEntitySupportY(
                world, e->x, e->z, previousY, e->y)) {
            e->y = *supportY;
            e->vy = 0;
        }
        if (e->ageTicks <= 10) continue;
        for (const auto& pp : players) {
            auto* pl = pp.get();
            bool eligible = false;
            double playerX = 0.0;
            double playerY = 0.0;
            double playerZ = 0.0;
            std::int8_t playerDimension = 0;
            {
                std::lock_guard playerLock(pl->stateMtx);
                eligible = pl->inPlay && static_cast<bool>(pl->conn) &&
                           !pl->dead && pl->gamemode == 0;
                playerX = pl->x;
                playerY = pl->y;
                playerZ = pl->z;
                playerDimension = canonicalDimension(pl->dimension);
            }
            if (!eligible || playerDimension !=
                                canonicalDimension(e->dimension)) continue;
            const double dx = playerX - e->x;
            const double dy = (playerY + 0.9) - e->y;
            const double dz = playerZ - e->z;
            if (dx * dx + dy * dy + dz * dz < 2.5) {
                pickups.push_back({e, pp});
                break;
            }
        }
    }
    for (const auto& gone : expired) {
        WriteBuffer rm;
        rm.varint(1);
        rm.varint(gone.entityId);
        broadcastPacketExceptInDimension(gone.dimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }
    for (auto& pk : pickups) {
        if (!pk.orb || !pk.p) continue;
        Player& p = *pk.p;
        bool removed = false;
        bool inventoryChanged = false;
        bool armorChanged = false;
        int xp = 0;
        std::int32_t playerEntityId = 0;
        const auto orbDimension = canonicalDimension(pk.orb->dimension);
        {
            std::lock_guard playerLock(p.stateMtx);
            if (!p.inPlay || p.dead || p.gamemode != 0 || !p.conn ||
                canonicalDimension(p.dimension) != orbDimension)
                continue;
            {
                std::lock_guard lk(entsMtx_);
                const auto it = std::find(xpOrbs_.begin(), xpOrbs_.end(), pk.orb);
                if (it != xpOrbs_.end()) {
                    xpOrbs_.erase(it);
                    removed = true;
                }
            }
            if (!removed) continue;
            xp = pk.orb->value;
            std::vector<int> mendingSlots;
            for (std::size_t i = 0; i < kPlayerInventorySlotCount; ++i) {
                if (!p.inv[i].empty() && p.inv[i].mendingLevel() > 0 &&
                    p.inv[i].getDamage() > 0)
                    mendingSlots.push_back(static_cast<int>(i));
            }
            if(!mendingSlots.empty() && xp>0){
                int pick = mendingSlots[nextRandom() % mendingSlots.size()];
                ItemStack &target = p.inv[pick];
                int dmg = target.getDamage();
                int repair = std::min(dmg, xp * 2);
                target.setDamage(dmg - repair);
                xp -= repair / 2;
                inventoryChanged = true;
                armorChanged = pick >= 5 && pick <= 8;
            }
            if(xp>0) p.xp.addPoints(xp);
            playerEntityId = p.entityId;
        }
        if (inventoryChanged) resendInventory(p);
        if (armorChanged) syncPlayerArmorAttributes(p);
        sendSetExperience(p);
        WriteBuffer c;
        c.varint(pk.orb->entityId);
        c.varint(playerEntityId);
        c.varint(1);
        broadcastPacketExceptInDimension(orbDimension, nullptr,
                                         pl::sc::Collect, c);
        WriteBuffer rm;
        rm.varint(1); rm.varint(pk.orb->entityId);
        broadcastPacketExceptInDimension(orbDimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }
}
} // namespace cppfm
