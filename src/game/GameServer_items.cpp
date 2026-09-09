#include "GameServer.hpp"
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
#include "MobSpawner.hpp"
#include "MenuLogic.hpp"
#include "PotionBrewing.hpp"

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

bool sameStackData(const ItemStack& lhs, const ItemStack& rhs) {
    return lhs.itemId == rhs.itemId &&
           lhs.components == rhs.components &&
           lhs.removedComponents == rhs.removedComponents;
}
} // namespace

void GameServer::sendEquipment(const MobEntity& mob) {
    std::array<ItemStack, 6> equipment;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        equipment = mob.equipment;
        entityId = mob.entityId;
        dimension = mob.dimension;
    }
    EquipmentComponent comp(equipment);
    if (!comp.hasAny()) return;
    WriteBuffer b;
    b.varint(entityId);
    comp.writePayload(b);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::SetEquipment, b);
}
void GameServer::sendEquipmentSlot(const MobEntity& mob, int slot) {
    if (slot<0||slot>=6) return;
    std::array<ItemStack, 6> equipment;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        equipment = mob.equipment;
        entityId = mob.entityId;
        dimension = mob.dimension;
    }
    EquipmentComponent comp(equipment);
    WriteBuffer b;
    b.varint(entityId);
    comp.writePayloadSingle(b, slot);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::SetEquipment, b);
}
void GameServer::broadcastPlayerEquipment(const Player& p) {
    std::array<ItemStack,6> arr{};
    std::shared_ptr<Connection> connection;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        if (p.heldSlot>=0 && p.heldSlot<9) arr[0] = p.inv[36 + p.heldSlot];
        arr[1] = p.inv[45];
        arr[2] = p.inv[5];
        arr[3] = p.inv[6];
        arr[4] = p.inv[7];
        arr[5] = p.inv[8];
        connection = p.conn;
        entityId = p.entityId;
        dimension = p.dimension;
    }
    EquipmentComponent comp(arr);
    if (!comp.hasAny() || !connection) return;
    WriteBuffer b;
    b.varint(entityId);
    comp.writePayload(b);
    broadcastPacketExceptInDimension(dimension, &p,
                                     proto::pl::sc::SetEquipment, b);
    connection->trySendPacket(proto::pl::sc::SetEquipment, b);
}
void GameServer::syncEquipmentOnChange(Player& p){
    broadcastPlayerEquipment(p);
}
void GameServer::handleMoveVehicle(Player& p, double x, double y, double z, float yaw, float pitch) {
    std::int32_t vehicleId = -1;
    std::int8_t playerDimension = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        vehicleId = p.vehicleId;
        playerDimension = canonicalDimension(p.dimension);
    }
    if (vehicleId == -1) return;
    std::shared_ptr<MobEntity> veh;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard entityLock(*candidate->stateMtx);
        if (candidate->entityId == vehicleId) {
            veh = candidate;
            break;
        }
    }
    if (!veh) return;

    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    MobKind vehicleKind = MobKind::Pig;
    {
        // Player and vehicle state are updated as one operation.  scoped_lock
        // is intentional here because the tick path may acquire them in the
        // opposite order while resolving a passenger.
        std::scoped_lock stateLock(p.stateMtx, *veh->stateMtx);
        if (p.vehicleId != vehicleId ||
            canonicalDimension(p.dimension) != playerDimension ||
            canonicalDimension(veh->dimension) != playerDimension)
            return;
        if (!isInsideBorder(x, z)) {
            veh->velX = 0;
            veh->velZ = 0;
            return;
        }
        const double dx = x - veh->x;
        const double dz = z - veh->z;
        veh->velX = dx * 0.5;
        veh->velZ = dz * 0.5;
        veh->x = x;
        veh->y = y;
        veh->z = z;
        veh->yaw = yaw;
        p.x = x;
        p.y = y;
        p.z = z;
        p.yaw = yaw;
        p.pitch = pitch;
        dimension = canonicalDimension(veh->dimension);
        entityId = veh->entityId;
        vehicleKind = veh->kind;
    }
    WriteBuffer tp;
    tp.varint(entityId);
    tp.f64(x); tp.f64(y); tp.f64(z);
    tp.f32(yaw); tp.f32(pitch); tp.boolean(true);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::EntityTeleport, tp);
    WriteBuffer vm;
    vm.f64(x); vm.f64(y); vm.f64(z);
    vm.f32(yaw); vm.f32(pitch);
    broadcastPacketExceptInDimension(dimension, &p,
                                     proto::pl::sc::VehicleMove, vm);
    if (vehicleKind==MobKind::Minecart || vehicleKind==MobKind::ChestMinecart || vehicleKind==MobKind::FurnaceMinecart || vehicleKind==MobKind::TntMinecart || vehicleKind==MobKind::HopperMinecart || vehicleKind==MobKind::CommandBlockMinecart || vehicleKind==MobKind::SpawnerMinecart) {
        broadcastMoveMinecart(entityId, x, y, z, yaw, pitch, &p);
    }
}
void GameServer::handleHorseJump(Player& p, int power) {
    std::int32_t vehicleId = -1;
    std::int8_t playerDimension = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        vehicleId = p.vehicleId;
        playerDimension = canonicalDimension(p.dimension);
    }
    if (vehicleId == -1) return;
    std::shared_ptr<MobEntity> veh;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard entityLock(*candidate->stateMtx);
        if (candidate->entityId == vehicleId) {
            veh = candidate;
            break;
        }
    }
    if (!veh) return;
    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double velocityZ = 0.0;
    {
        std::scoped_lock stateLock(p.stateMtx, *veh->stateMtx);
        if (p.vehicleId != vehicleId ||
            canonicalDimension(p.dimension) != playerDimension ||
            canonicalDimension(veh->dimension) != playerDimension ||
            (veh->kind != MobKind::Horse && veh->kind != MobKind::Llama &&
             veh->kind != MobKind::Pig))
            return;
        float f = std::clamp(power/100.0f, 0.0f, 1.0f);
        veh->velY = 0.42 + f*0.6;
        veh->velX *= 1.05; veh->velZ *= 1.05;
        if(veh->velY>1.2) veh->velY=1.2;
        dimension = canonicalDimension(veh->dimension);
        entityId = veh->entityId;
        velocityX = veh->velX;
        velocityY = veh->velY;
        velocityZ = veh->velZ;
        veh->lastTeleportTick = tickNo_;
    }
    WriteBuffer vb;
    vb.varint(entityId);
    vb.i16((int16_t)(velocityX*8000)); vb.i16((int16_t)(velocityY*8000)); vb.i16((int16_t)(velocityZ*8000));
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::EntityVelocity, vb);
}
void GameServer::broadcastSetPassengers(std::int32_t vehicleId) {
    std::shared_ptr<MobEntity> veh;
    std::int8_t dimension = 0;
    std::int32_t riderEntityId = -1;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard entityLock(*candidate->stateMtx);
        if (candidate->entityId != vehicleId) continue;
        veh = candidate;
        dimension = candidate->dimension;
        riderEntityId = candidate->riderEntityId;
            break;
    }
    if (!veh) return;
    WriteBuffer b;
    b.varint(vehicleId);
    if (riderEntityId != -1) {
        b.varint(1);
        b.varint(riderEntityId);
    } else {
        b.varint(0);
    }
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::SetPassengers, b);
}

void GameServer::craftersTick() {
    craftersTickFor(0);
    craftersTickFor(-1);
    craftersTickFor(1);
}

void GameServer::craftersTickFor(std::int8_t dimension) {
    dimension = canonicalDimension(dimension);
    auto& blockEntities = blockEntitiesFor(dimension);
    auto& world = worldFor(dimension);
    auto& redstone = redstoneFor(dimension);

    struct TickIo final : MenuIo {
        BlockEntityStore& store;
        explicit TickIo(BlockEntityStore& s) : store(s) {}
        void dropFromPlayer(Player&, const ItemStack&, bool) override {}
        void blockEntityChanged(std::int64_t key) override {
            store.markDirty(key);
        }
        void itemCrafted(Player&, const ItemStack&) override {}
        void itemSmelted(Player&, const ItemStack&) override {}
    } io(blockEntities);

    auto setBooleanProperty = [&](std::int32_t x, std::int32_t y,
                                  std::int32_t z, std::string_view key,
                                  bool value) -> bool {
        const std::uint16_t oldState = world.getBlock(x, y, z);
        const auto* def = gen::blockByState(oldState);
        if (!def) return false;
        const auto oldProps = gen::propsOf(oldState);
        bool present = false;
        for (const auto& [name, ignored] : oldProps)
            if (name == key) { present = true; break; }
        if (!present) return false;
        const std::string_view valueName = value ? "true" : "false";
        std::vector<std::pair<std::string_view, std::string_view>> props;
        props.reserve(oldProps.size());
        for (const auto& [name, current] : oldProps)
            props.emplace_back(name, name == key ? valueName : current);
        const auto newState = static_cast<std::uint16_t>(
            gen::stateWithProps(*def, props));
        if (newState == oldState) return false;
        world.setBlock(x, y, z, newState);
        broadcastBlockChangeFor(dimension, x, y, z, newState);
        return true;
    };

    auto frontDirection = [&](std::uint16_t state) {
        std::string front = "north";
        for (const auto& [name, value] : gen::propsOf(state)) {
            if (name != "orientation") continue;
            const auto separator = value.find('_');
            front = std::string(value.substr(0, separator));
            break;
        }
        struct Direction { int x = 0, y = 0, z = -1; } result;
        if (front == "south") result = {0, 0, 1};
        else if (front == "east") result = {1, 0, 0};
        else if (front == "west") result = {-1, 0, 0};
        else if (front == "up") result = {0, 1, 0};
        else if (front == "down") result = {0, -1, 0};
        return result;
    };

    auto ensureContainer = [&](std::int32_t x, std::int32_t y,
                               std::int32_t z) -> BlockEntityStore::Owner {
        const auto key = posKey(x, y, z);
        if (auto existing = blockEntities.getShared(key)) return existing;
        const auto* def = gen::blockByState(world.getBlock(x, y, z));
        if (!def) return nullptr;
        const std::string name(def->name);
        BlockEntity::Kind kind;
        if (name.find("chest") != std::string::npos &&
            name.find("ender") == std::string::npos)
            kind = BlockEntity::Kind::Chest;
        else if (name == "minecraft:barrel") kind = BlockEntity::Kind::Barrel;
        else if (name.find("shulker_box") != std::string::npos)
            kind = BlockEntity::Kind::ShulkerBox;
        else if (name == "minecraft:hopper") kind = BlockEntity::Kind::Hopper;
        else if (name == "minecraft:dispenser") kind = BlockEntity::Kind::Dispenser;
        else if (name == "minecraft:dropper") kind = BlockEntity::Kind::Dropper;
        else if (name == "minecraft:furnace" ||
                 name == "minecraft:blast_furnace" ||
                 name == "minecraft:smoker")
            kind = BlockEntity::Kind::Furnace;
        else if (name == "minecraft:brewing_stand") kind = BlockEntity::Kind::Brewing;
        else if (name == "minecraft:crafter") kind = BlockEntity::Kind::Crafter;
        else return nullptr;
        return blockEntities.createShared(key, kind);
    };

    // Store keys, not BlockEntity values: each crafter owns mutable stacks and
    // the map can be invalidated by a block replacement during a callback.
    std::vector<std::int64_t> snapshot;
    blockEntities.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind == BlockEntity::Kind::Crafter) snapshot.push_back(key);
    });

    for (const auto key : snapshot) {
        auto beOwner = blockEntities.getShared(key);
        if (!beOwner) continue;
        std::unique_lock entityLock(*beOwner->stateMtx);
        auto* be = beOwner.get();
        if (be->kind != BlockEntity::Kind::Crafter) continue;
        const int x = posKeyUnpackX(key), y = posKeyUnpackY(key), z = posKeyUnpackZ(key);
        const auto state = world.getBlock(x, y, z);
        const auto* def = gen::blockByState(state);
        if (!def || def->name != "minecraft:crafter") {
            blockEntities.remove(key);
            continue;
        }

        bool stateTriggered = false;
        bool stateCrafting = false;
        for (const auto& [name, value] : gen::propsOf(state)) {
            if (name == "triggered") stateTriggered = value == "true";
            else if (name == "crafting") stateCrafting = value == "true";
        }
        be->crafter.triggered = stateTriggered;
        const bool powered = redstone.isPoweredHere(x, y, z);
        if (powered && !stateTriggered && be->crafter.redstoneCraftDueTick < 0) {
            be->crafter.triggered = true;
            be->crafter.redstoneCraftDueTick = tickNo_ + 4;
            setBooleanProperty(x, y, z, "triggered", true);
            blockEntities.markDirty(key);
        } else if (!powered && stateTriggered) {
            be->crafter.triggered = false;
            setBooleanProperty(x, y, z, "triggered", false);
            blockEntities.markDirty(key);
        }

        if (be->crafter.craftingTicksRemaining > 0) {
            --be->crafter.craftingTicksRemaining;
            if (be->crafter.craftingTicksRemaining == 0 && stateCrafting)
                setBooleanProperty(x, y, z, "crafting", false);
            blockEntities.markDirty(key);
        }

        if (be->crafter.redstoneCraftDueTick < 0 ||
            tickNo_ < be->crafter.redstoneCraftDueTick)
            continue;
        be->crafter.redstoneCraftDueTick = -1;

        Menu menu;
        menu.type = MenuType::Crafter;
        menu.blockKey = key;
        menu.container = be->crafter.slots;
        menu.containerCount = CrafterData::kSlots;
        menu.crafterDisabledSlots = &be->crafter.disabledSlots;
        menu.blockEntity = be;
        menu.blockEntityOwner = beOwner;
        ItemStack output = ItemStack::air();
        CrafterMenuLogic logic;
        if (!logic.craftOnRedstone(menu, recipes_, output, io)) {
            broadcastSoundFor(dimension, "minecraft:block.crafter.fail",
                              x + .5, y + .5, z + .5, 1.f, 1.f, "block");
            continue;
        }

        be->crafter.craftingTicksRemaining = 6;
        setBooleanProperty(x, y, z, "crafting", true);
        blockEntities.markDirty(key);

        const auto direction = frontDirection(state);
        const int tx = x + direction.x, ty = y + direction.y,
                  tz = z + direction.z;
        ItemStack remaining = output;
        if (auto targetOwner = ensureContainer(tx, ty, tz)) {
            std::lock_guard targetLock(*targetOwner->stateMtx);
            auto* target = targetOwner.get();
            const auto before = remaining.count;
            // Reuse the same item-stack matching and side rules as container
            // automation, while keeping the source/output coordinates local.
            auto insertTarget = [&](BlockEntity& destination) {
                auto insertSlots = [&](ItemStack* slots, int count,
                                       bool skipDisabled) {
                    for (int pass = 0; pass < 2 && !remaining.empty(); ++pass)
                        for (int i = 0; i < count && !remaining.empty(); ++i) {
                            if (skipDisabled && destination.crafter.isSlotDisabled(i)) continue;
                            auto& slot = slots[i];
                            if (pass == 0) {
                                if (slot.empty() || !sameStackData(slot, remaining)) continue;
                                const int limit = maxStackForId(slot.itemId);
                                if (slot.count >= limit) continue;
                                const int moved = std::min<int>(remaining.count,
                                                                limit - slot.count);
                                slot.count = static_cast<std::int16_t>(slot.count + moved);
                                remaining.count = static_cast<std::int16_t>(remaining.count - moved);
                            } else if (slot.empty()) {
                                const int moved = std::min<int>(remaining.count,
                                                                maxStackForId(remaining.itemId));
                                slot = remaining;
                                slot.count = static_cast<std::int16_t>(moved);
                                remaining.count = static_cast<std::int16_t>(remaining.count - moved);
                            }
                        }
                };
                switch (destination.kind) {
                case BlockEntity::Kind::Chest:
                case BlockEntity::Kind::Barrel:
                case BlockEntity::Kind::ShulkerBox:
                    insertSlots(destination.chest.slots, ChestData::kSlots, false);
                    break;
                case BlockEntity::Kind::Hopper:
                    insertSlots(destination.generic.slots, 5, false);
                    break;
                case BlockEntity::Kind::Dispenser:
                case BlockEntity::Kind::Dropper:
                    insertSlots(destination.generic.slots, 9, false);
                    break;
                case BlockEntity::Kind::Crafter:
                    insertSlots(destination.crafter.slots, CrafterData::kSlots, true);
                    break;
                case BlockEntity::Kind::Furnace:
                    if (direction.y > 0)
                        insertSlots(destination.furnace.slots + FurnaceData::kInput, 1, false);
                    else if (direction.y == 0 && isFuelItem(remaining.itemId))
                        insertSlots(destination.furnace.slots + FurnaceData::kFuel, 1, false);
                    break;
                default:
                    break;
                }
            };
            insertTarget(*target);
            if (remaining.count != before)
                blockEntities.markDirty(posKey(tx, ty, tz));
        }
        if (!remaining.empty()) {
            spawnItemDropFor(dimension, x + .5 + direction.x * .7,
                             y + .5 + direction.y * .7,
                             z + .5 + direction.z * .7, remaining,
                             direction.x * .2, direction.y * .2,
                             direction.z * .2);
        }
        broadcastSoundFor(dimension, "minecraft:block.crafter.craft",
                          x + .5, y + .5, z + .5, 1.f, 1.f, "block");
    }
}

void GameServer::hoppersTick() {
    if (tickNo_ % HOPPER_TRANSFER_INTERVAL_TICKS != 0) return;
    hoppersTickFor(0);
    hoppersTickFor(-1);
    hoppersTickFor(1);
}

void GameServer::hoppersTickFor(std::int8_t dimension) {
    dimension = canonicalDimension(dimension);
    // Keep the long-established hopper implementation readable while making
    // its world/store dependencies explicit at the boundary.  Local aliases
    // deliberately shadow the Overworld members below; all three dimensions
    // then execute the same transfer rules against their own state.
    auto& blockEntities_ = blockEntitiesFor(dimension);
    auto& world_ = worldFor(dimension);
    auto& redstone_ = redstoneFor(dimension);
    auto& fluidSim_ = fluidsFor(dimension);
    auto& blockTicks_ = blockTicksFor(dimension);
    auto& dispenserPower_ = dispenserPowerByDimension_[
        dimension == 0 ? 0 : (dimension < 0 ? 1 : 2)];
    auto broadcastPacketExcept = [this, dimension](
        const Player* except, std::uint8_t id, const WriteBuffer& body) {
        this->broadcastPacketExceptInDimension(dimension, except, id, body);
    };
    auto broadcastBlockChange = [this, dimension](std::int32_t x,
                                                   std::int32_t y,
                                                   std::int32_t z,
                                                   std::uint16_t state) {
        this->broadcastBlockChangeFor(dimension, x, y, z, state);
    };
    auto broadcastSound = [this, dimension](const char* name, double x,
                                             double y, double z, float volume,
                                             float pitch, const char* category) {
        this->broadcastSoundFor(dimension, name, x, y, z, volume, pitch,
                                category);
    };
    auto spawnItemDropStack = [this, dimension](double x, double y, double z,
                                                const ItemStack& stack,
                                                double vx, double vy,
                                                double vz) {
        this->spawnItemDropFor(dimension, x, y, z, stack, vx, vy, vz);
    };
    auto spawnPrimedTnt = [this, dimension](double x, double y, double z,
                                            double vx, double vy, double vz,
                                            int fuse) {
        this->spawnPrimedTntFor(dimension, x, y, z, vx, vy, vz, fuse);
    };
    const auto oneItem = [](const ItemStack& source) {
        ItemStack one = source;
        one.count = 1;
        return one;
    };
    // Store keys rather than copying BlockEntity.  BlockEntity owns its slot
    // arrays by value, so a value snapshot would make every transfer mutate a
    // detached copy and silently lose the result.  Re-resolve each key before
    // processing so the authoritative store receives the mutation.
    std::vector<std::pair<std::int64_t, BlockEntity::Kind>> snapshot;
    blockEntities_.forEach([&](std::int64_t k, BlockEntity& be) {
        if (be.kind == BlockEntity::Kind::Hopper ||
            be.kind == BlockEntity::Kind::Dispenser ||
            be.kind == BlockEntity::Kind::Dropper)
            snapshot.emplace_back(k, be.kind);
    });
    for (const auto& [key, kind] : snapshot) {
        auto currentOwner = blockEntities_.getShared(key);
        if (!currentOwner) continue;
        std::unique_lock entityLock(*currentOwner->stateMtx);
        auto& be = *currentOwner;
        if (be.kind != kind) continue;
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        if (be.kind == BlockEntity::Kind::Hopper && redstone_.isPoweredHere(x, y, z)) continue;
        ItemStack* slots = be.generic.slots;
        const int count = be.kind == BlockEntity::Kind::Hopper ? 5 : 9;

        auto mergeIntoFirstFit = [&](const ItemStack& src) -> bool {
            for (int i = 0; i < count; ++i) {
                auto& s = slots[i];
                if (s.empty()) { s = src; return true; }
                if (sameStackData(s, src) && s.count < 64) {
                    const int take = std::min<int>(64 - s.count, src.count);
                    s.count += take;
                    if (take >= src.count) return true;
                }
            }
            return false;
        };
        auto extractOneFrom = [&](const BlockEntityStore::Owner& otherOwner) -> bool {
            if (!otherOwner) return false;
            std::lock_guard otherLock(*otherOwner->stateMtx);
            BlockEntity* other = otherOwner.get();
            ItemStack* oslots = nullptr; int on = 0;
            switch (other->kind) {
            case BlockEntity::Kind::Chest: oslots = other->chest.slots; on = 27; break;
            case BlockEntity::Kind::Barrel:
            case BlockEntity::Kind::ShulkerBox:
                oslots = other->chest.slots;
                on = 27;
                break;
            case BlockEntity::Kind::Hopper: oslots = other->generic.slots; on = 5; break;
            case BlockEntity::Kind::Dispenser: oslots = other->generic.slots; on = 9; break;
            case BlockEntity::Kind::Dropper: oslots = other->generic.slots; on = 9; break;
            case BlockEntity::Kind::Furnace:
                oslots = other->furnace.slots + FurnaceData::kOutput;
                on = 1;
                break;
            case BlockEntity::Kind::Brewing: oslots = other->brewing.slots; on = 3; break;
            case BlockEntity::Kind::Crafter: oslots = other->crafter.slots; on = CrafterData::kSlots; break;
            default: return false;
            }
            for (int i = 0; i < on; ++i) {
                auto& s = oslots[i];
                if (s.empty()) continue;
                ItemStack one = oneItem(s);
                if (mergeIntoFirstFit(one)) {
                    if (--s.count <= 0) s = ItemStack::air();
                    blockEntities_.markDirty(posKey(x, y + 1, z));
                    blockEntities_.markDirty(key);
                    return true;
                }
            }
            return false;
        };

        // ---- pull from above
        if (auto otherOwner = blockEntities_.getShared(posKey(x, y + 1, z)))
            extractOneFrom(otherOwner);
        // ---- item entity pickup from the hopper cell itself
        WriteBuffer collected;
        bool collectedItem = false;
        {
            std::lock_guard lk(entsMtx_);
            for (auto& e : itemDrops_) {
                if (!e->collected && canonicalDimension(e->dimension) ==
                        canonicalDimension(dimension) &&
                    std::abs(e->x - (x + .5)) < 0.8 &&
                    std::abs(e->z - (z + .5)) < 0.8 &&
                    e->y > y - 0.2 && e->y < y + 1.3) {
                    ItemStack one = oneItem(e->asStack());
                    if (one.empty()) continue;
                    if (mergeIntoFirstFit(one)) {
                        if (--e->count <= 0) {
                            e->count = 0;
                            e->collected = true;
                            e->stack = ItemStack::air();
                        } else if (!e->stack.empty()) {
                            e->stack.count = e->count;
                        }
                        blockEntities_.markDirty(key);
                        collected.varint(e->entityId);
                        collected.varint(0);             // collector: hopper
                        collected.varint(1);
                        collectedItem = true;
                        break;
                    }
                }
            }
        }
        if (collectedItem)
            broadcastPacketExcept(nullptr, pl::sc::Collect, collected);
        // ---- push downward
        if (auto belowOwner = blockEntities_.getShared(posKey(x, y - 1, z))) {
            std::lock_guard belowLock(*belowOwner->stateMtx);
            auto* below = belowOwner.get();
            auto insertOneInto = [&](BlockEntity* target,
                                     const ItemStack& one) -> bool {
                if (!target) return false;
                ItemStack* destinations = nullptr;
                int destinationCount = 0;
                switch (target->kind) {
                case BlockEntity::Kind::Chest:
                case BlockEntity::Kind::Barrel:
                case BlockEntity::Kind::ShulkerBox:
                    destinations = target->chest.slots;
                    destinationCount = ChestData::kSlots;
                    break;
                case BlockEntity::Kind::Hopper:
                case BlockEntity::Kind::Dispenser:
                case BlockEntity::Kind::Dropper:
                    destinations = target->generic.slots;
                    destinationCount = target->kind == BlockEntity::Kind::Hopper ? 5 : 9;
                    break;
                case BlockEntity::Kind::Furnace:
                    // A hopper directly above a furnace feeds its input slot.
                    destinations = target->furnace.slots + FurnaceData::kInput;
                    destinationCount = 1;
                    break;
                case BlockEntity::Kind::Brewing:
                    // A hopper above a brewing stand feeds the ingredient slot.
                    destinations = target->brewing.slots + 3;
                    destinationCount = 1;
                    break;
                case BlockEntity::Kind::Crafter:
                    destinations = target->crafter.slots;
                    destinationCount = CrafterData::kSlots;
                    break;
                default:
                    return false;
                }
                for (int j = 0; j < destinationCount; ++j) {
                    if (target->kind == BlockEntity::Kind::Crafter &&
                        target->crafter.isSlotDisabled(j))
                        continue;
                    auto& dst = destinations[j];
                    if (dst.empty()) {
                        dst = one;
                        return true;
                    }
                    if (sameStackData(dst, one) && dst.count < 64) {
                        ++dst.count;
                        return true;
                    }
                }
                return false;
            };
            if (below != &be) {
                for (int i = 0; i < count; ++i) {
                    auto& s = slots[i];
                    if (s.empty()) continue;
                    ItemStack one = oneItem(s);
                    if (insertOneInto(below, one)) {
                        if (--s.count <= 0) s = ItemStack::air();
                        blockEntities_.markDirty(key);
                        blockEntities_.markDirty(posKey(x, y - 1, z));
                    }
                    break;
                }
            }
        }

        if (be.kind == BlockEntity::Kind::Dispenser ||
            be.kind == BlockEntity::Kind::Dropper) {
            bool powered = redstone_.isQuasiPowered(x, y, z);
            bool& was = dispenserPower_[key];
            if (powered && !was) {
                // detect dropper vs dispenser by world block name
                bool isDropper = be.kind == BlockEntity::Kind::Dropper;
                {
                    uint16_t bs = world_.getBlock(x, y, z);
                    const gen::BlockDef* bd = gen::blockByState(bs);
                    if (bd && std::string(bd->name)=="minecraft:dropper") isDropper=true;
                }
                // pick random non-empty slot (vanilla random)
                std::vector<int> nonEmpty;
                for(int i=0;i<9;++i) if(!slots[i].empty()) nonEmpty.push_back(i);
                if(!nonEmpty.empty()){
                    int pick = nonEmpty[nextRandom()%nonEmpty.size()];
                    auto& s = slots[pick];
                    double dx = 0, dy = 0, dz = 0;
                    std::string facing = "north";
                    std::uint16_t bstate = world_.getBlock(x, y, z);
                    if (bstate) {
                        for (auto& [pk, pv] : gen::propsOf(bstate))
                            if (pk == "facing") facing = std::string(pv);
                    }
                    if (facing == "north") dz = -1;
                    else if (facing == "south") dz = 1;
                    else if (facing == "west") dx = -1;
                    else if (facing == "east") dx = 1;
                    else if (facing == "up") dy = 1;
                    else if (facing == "down") dy = -1;
                    int tx = x + (int)dx, ty = y + (int)dy, tz = z + (int)dz;
                    double sx = x + .5 + dx * .7;
                    double sy = y + .5 + dy * .7;
                    double sz = z + .5 + dz * .7;
                    std::string iname = s.name();

                    auto doDropperInsert = [&]() -> bool {
                        auto targetOwner = blockEntities_.getShared(posKey(tx,ty,tz));
                        if(!targetOwner) return false;
                        std::lock_guard targetLock(*targetOwner->stateMtx);
                        auto* beT = targetOwner.get();
                        std::string insertDir;
                        if(facing=="north") insertDir="south";
                        else if(facing=="south") insertDir="north";
                        else if(facing=="west") insertDir="east";
                        else if(facing=="east") insertDir="west";
                        else if(facing=="up") insertDir="down";
                        else if(facing=="down") insertDir="up";
                        else insertDir="up";
                        ItemStack one = oneItem(s);
                        if(beT->kind==BlockEntity::Kind::Furnace){
                            int trySlot = (insertDir=="up") ? 0 : 1;
                            if(trySlot==1 && !isFuelItem(s.itemId)) return false;
                            auto &dst = beT->furnace.slots[trySlot];
                            if(dst.empty()){
                                dst = one;
                                blockEntities_.markDirty(posKey(tx,ty,tz));
                                return true;
                            } else if(sameStackData(dst, one) && dst.count<64){
                                ++dst.count;
                                blockEntities_.markDirty(posKey(tx,ty,tz));
                                return true;
                            } else return false;
                        }
                        if(beT->kind==BlockEntity::Kind::Brewing){
                            if(insertDir=="up"){
                                auto &dst = beT->brewing.slots[3];
                                if(dst.empty()){
                                    dst = one;
                                    blockEntities_.markDirty(posKey(tx,ty,tz));
                                    return true;
                                } else if(sameStackData(dst, one) && dst.count<64){
                                    ++dst.count;
                                    blockEntities_.markDirty(posKey(tx,ty,tz));
                                    return true;
                                } else return false;
                            }
                            for(int idx : {0,1,2,4}){
                                auto &d = beT->brewing.slots[idx];
                                if(d.empty()){ d=one; blockEntities_.markDirty(posKey(tx,ty,tz)); return true; }
                                if(sameStackData(d, one) && d.count<64){ ++d.count; blockEntities_.markDirty(posKey(tx,ty,tz)); return true; }
                            }
                            return false;
                        }
                        ItemStack* oslots=nullptr; int on=0;
                        switch(beT->kind){
                            case BlockEntity::Kind::Chest:
                            case BlockEntity::Kind::Barrel:
                            case BlockEntity::Kind::ShulkerBox:
                                oslots=beT->chest.slots; on=27; break;
                            case BlockEntity::Kind::Hopper:
                                oslots=beT->generic.slots; on=5; break;
                            case BlockEntity::Kind::Dispenser:
                            case BlockEntity::Kind::Dropper:
                                oslots=beT->generic.slots; on=9; break;
                            case BlockEntity::Kind::Crafter:
                                oslots=beT->crafter.slots; on=CrafterData::kSlots; break;
                            default: return false;
                        }
                        if(oslots){
                            for(int j=0;j<on;++j){
                                if (beT->kind == BlockEntity::Kind::Crafter &&
                                    beT->crafter.isSlotDisabled(j))
                                    continue;
                                auto &d=oslots[j];
                                if(d.empty()){ d=one; blockEntities_.markDirty(posKey(tx,ty,tz)); return true; }
                                if(sameStackData(d, one) && d.count<64){ ++d.count; blockEntities_.markDirty(posKey(tx,ty,tz)); return true; }
                            }
                        }
                        return false;
                    };

                    if(isDropper){
                        // Dropper: always try insert, else drop item (never projectile)
                        bool inserted = doDropperInsert();
                        if(!inserted){
                            spawnItemDropStack(tx+0.5, ty+0.5, tz+0.5, oneItem(s), dx*0.25, 0.15, dz*0.25);
                        }
                        if (--s.count <= 0) s = ItemStack::air();
                        broadcastSound("minecraft:block.dispenser.dispense", x+.5,y+.5,z+.5,1.f,1.f,"block");
                        blockEntities_.markDirty(key);
                    } else {
                        bool handled = false;
                        if (iname.find("shulker_box") != std::string::npos) {
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            if (tSt==0) {
                                uint16_t belowSt = world_.getBlock(tx,ty-1,tz);
                                std::string shulkerFacing = (belowSt==0 ? facing : "up");
                                const gen::BlockDef* def = gen::blockByName(iname);
                                if (!def) def = gen::blockByName("minecraft:shulker_box");
                                if (def) {
                                    uint16_t ns = static_cast<uint16_t>(gen::stateWithProps(*def, {{"facing", shulkerFacing}}));
                                    world_.setBlock(tx,ty,tz, ns);
                                    broadcastBlockChange(tx,ty,tz, ns);
                                    auto beNOwner = blockEntities_.getShared(posKey(tx,ty,tz));
                                    if (!beNOwner)
                                        beNOwner = blockEntities_.createShared(
                                            posKey(tx,ty,tz), BlockEntity::Kind::ShulkerBox);
                                    std::lock_guard beNLock(*beNOwner->stateMtx);
                                    auto* beN = beNOwner.get();
                                    if (beN->kind != BlockEntity::Kind::ShulkerBox) {
                                        beN->kind = BlockEntity::Kind::ShulkerBox;
                                        blockEntities_.markDirty(posKey(tx,ty,tz));
                                    }
                                    if (--s.count <= 0) s = ItemStack::air();
                                    blockEntities_.markDirty(key);
                                    broadcastSound("minecraft:block.dispenser.dispense", x+.5,y+.5,z+.5,1.f,1.f,"block");
                                    handled = true;
                                }
                            }
                            if (!handled) {
                                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                    dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled = true;
                            }
                        }
                        // bucket fluid dispense
                        if(!handled && (iname=="minecraft:water_bucket" || iname=="minecraft:lava_bucket" || iname=="minecraft:powder_snow_bucket")){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            bool replaceable = (tSt==0);
                            // check replaceable: air or non-solid? simplified air only
                            if(replaceable){
                                std::string fluid = iname=="minecraft:lava_bucket" ? "minecraft:lava" : (iname=="minecraft:powder_snow_bucket" ? "minecraft:powder_snow" : "minecraft:water");
                                uint16_t fluidSt = 0;
                                if(fluid=="minecraft:powder_snow"){
                                    auto it=gen::blockNameToState().find(fluid);
                                    if(it!=gen::blockNameToState().end()) fluidSt=static_cast<uint16_t>(it->second);
                                } else {
                                    fluidSt = static_cast<uint16_t>(gen::stateWithPropsList(fluid, {{"level","0"}}));
                                    if(fluidSt==0){ auto it=gen::blockNameToState().find(fluid); if(it!=gen::blockNameToState().end()) fluidSt=static_cast<uint16_t>(it->second); }
                                }
                                // Nether water evaporates
                                if(fluid=="minecraft:water" && world_.dimensionId()==-1){
                                    // evaporate with particles/sound
                                    broadcastSound("minecraft:block.fire.extinguish", tx+0.5,ty+0.5,tz+0.5,0.5f,2.6f,"block");
                                } else {
                                    world_.setBlock(tx,ty,tz,fluidSt);
                                    broadcastBlockChange(tx,ty,tz,fluidSt);
                                    if(fluid=="minecraft:water" || fluid=="minecraft:lava"){
                                        fluidSim_.touch(tx,ty,tz);
                                    }
                                }
                                // replace with empty bucket
                                s = ItemStack::ofName("minecraft:bucket",1);
                                handled=true;
                            } else {
                                // fallback drop
                                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                    dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            }
                        } else if(!handled && iname=="minecraft:bucket"){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            const gen::BlockDef* td = gen::blockByState(tSt);
                            bool isWater=false,isLava=false,isPowder=false;
                            if(td){
                                if(td->name=="minecraft:water"){
                                    for(auto&[k,v]: gen::propsOf(tSt)) if(k=="level"&&v=="0") isWater=true;
                                } else if(td->name=="minecraft:lava"){
                                    for(auto&[k,v]: gen::propsOf(tSt)) if(k=="level"&&v=="0") isLava=true;
                                } else if(td->name=="minecraft:powder_snow") isPowder=true;
                            }
                            if(isWater||isLava||isPowder){
                                world_.setBlock(tx,ty,tz,0);
                                broadcastBlockChange(tx,ty,tz,0);
                                std::string newName = isLava?"minecraft:lava_bucket":(isPowder?"minecraft:powder_snow_bucket":"minecraft:water_bucket");
                                s = ItemStack::ofName(newName,1);
                                handled=true;
                            }
                        } else if(!handled && (iname.find("splash_potion")!=std::string::npos || iname.find("lingering_potion")!=std::string::npos || iname=="minecraft:potion")){
                            // strict B23: potion projectile should be Potion entity, not Snowball
                            spawnProjectileFor(dimension, ProjectileKind::Potion, sx, sy, sz, dx*1.1, dy*0.2+0.12, dz*1.1, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled=true;
                        } else if(!handled && (iname.find("_helmet")!=std::string::npos || iname.find("_chestplate")!=std::string::npos || iname.find("_leggings")!=std::string::npos || iname.find("_boots")!=std::string::npos || iname.find("horse_armor")!=std::string::npos || iname=="minecraft:elytra" || iname=="minecraft:turtle_helmet" || iname=="minecraft:carved_pumpkin" || iname=="minecraft:skull")){
                            // strict B24: dispenser armor equip (vanilla Dispenser armor)
                            bool equipped=false;
                            std::shared_ptr<Player> equippedPlayer;
                            // Try players at target
                            for(auto &pp : playersSnapshot()){
                                int slot=-1;
                                if(iname.find("_helmet")!=std::string::npos || iname=="minecraft:turtle_helmet" || iname=="minecraft:carved_pumpkin" || iname.find("skull")!=std::string::npos) slot=8;
                                else if(iname.find("_chestplate")!=std::string::npos || iname=="minecraft:elytra") slot=7;
                                else if(iname.find("_leggings")!=std::string::npos) slot=6;
                                else if(iname.find("_boots")!=std::string::npos) slot=5;
                                else if(iname.find("horse_armor")!=std::string::npos) slot=-1; // not for player
                                if (slot < 5 || slot > 8) continue;

                                // The session thread and item pickup/tick code can
                                // touch the same inventory concurrently.  Keep
                                // only the position check and the actual equip
                                // mutation under the player's state lock; the
                                // attribute/equipment notifications below may
                                // send packets and therefore stay outside it.
                                std::unique_lock playerLock(pp->stateMtx);
                                if (!pp->inPlay || canonicalDimension(pp->dimension) !=
                                    canonicalDimension(dimension)) continue;
                                const int px=(int)std::floor(pp->x);
                                const int py=(int)std::floor(pp->y);
                                const int pz=(int)std::floor(pp->z);
                                // target is tx,ty,tz; allow one block tolerance for standing entity (ty may be feet)
                                if (px != tx || pz != tz ||
                                    (py != ty && py != ty+1 && py != ty-1)) continue;
                                if (pp->inv[slot].empty()) {
                                    pp->inv[slot]=oneItem(s);
                                    equippedPlayer = pp;
                                    break;
                                }
                            }
                            if (equippedPlayer) {
                                syncPlayerArmorAttributes(*equippedPlayer);
                                broadcastPlayerEquipment(*equippedPlayer);
                                equipped=true;
                            }
                            if(!equipped){
                                // Try mobs at target
                                std::shared_ptr<MobEntity> equippedMob;
                                int equippedMobSlot = -1;
                                for (const auto& m : mobsSnapshot()) {
                                    if (!m) continue;
                                    std::lock_guard mobLock(*m->stateMtx);
                                    if (canonicalDimension(m->dimension) !=
                                        canonicalDimension(dimension)) continue;
                                    const int mx = static_cast<int>(std::floor(m->x));
                                    const int my = static_cast<int>(std::floor(m->y));
                                    const int mz = static_cast<int>(std::floor(m->z));
                                    if (mx != tx || mz != tz ||
                                        (my != ty && my != ty + 1 && my != ty - 1))
                                        continue;
                                    int eslot = -1;
                                    if (iname.find("_helmet") != std::string::npos ||
                                        iname == "minecraft:turtle_helmet" ||
                                        iname == "minecraft:carved_pumpkin")
                                        eslot = 5;
                                    else if (iname.find("_chestplate") != std::string::npos ||
                                             iname == "minecraft:elytra")
                                        eslot = 4;
                                    else if (iname.find("_leggings") != std::string::npos)
                                        eslot = 3;
                                    else if (iname.find("_boots") != std::string::npos)
                                        eslot = 2;
                                    else if (iname.find("horse_armor") != std::string::npos &&
                                             (m->kind == MobKind::Horse ||
                                              m->kind == MobKind::Donkey ||
                                              m->kind == MobKind::Mule ||
                                              m->kind == MobKind::Llama ||
                                              m->kind == MobKind::TraderLlama))
                                        eslot = 4;
                                    if (eslot < 2 || eslot > 5 ||
                                        !m->equipment[eslot].empty())
                                        continue;
                                    m->equipment[eslot] = oneItem(s);
                                    equippedMob = m;
                                    equippedMobSlot = eslot;
                                    equipped = true;
                                    break;
                                }
                                if (equippedMob)
                                    sendEquipmentSlot(*equippedMob, equippedMobSlot);
                            }
                            if(equipped){
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            } else {
                                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                    dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            }
                        } else if(!handled && iname.find("arrow") != std::string::npos) {
                            spawnProjectileFor(dimension, ProjectileKind::Arrow, sx, sy, sz, dx*1.2, dy*0.2+0.15, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("snowball") != std::string::npos) {
                            spawnProjectileFor(dimension, ProjectileKind::Snowball, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname == "minecraft:egg") {
                            spawnProjectileFor(dimension, ProjectileKind::Egg, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("ender_pearl") != std::string::npos) {
                            spawnProjectileFor(dimension, ProjectileKind::EnderPearl, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("fire_charge") != std::string::npos) {
                            spawnProjectileFor(dimension, ProjectileKind::Fireball, sx, sy, sz, dx*0.5, dy*0.5, dz*0.5, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("_spawn_egg") != std::string::npos) {
                            MobSpawner spawner2(*this);
                            if (spawner2.spawnFromDispenserFor(dimension, iname,
                                                               x, y, z, facing)) {
                                if(--s.count<=0) s=ItemStack::air();
                            } else {
                                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                    dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                            }
                            handled = true;
                        } else if(!handled && iname=="minecraft:shears"){
                            // try shear sheep at target
                            std::shared_ptr<MobEntity> shearedMob;
                            std::int32_t shearedEntityId = 0;
                            std::int8_t shearedDimension = canonicalDimension(dimension);
                            double shearedX = 0.0;
                            double shearedY = 0.0;
                            double shearedZ = 0.0;
                            int shearedColor = 0;
                            {
                                for (const auto& m : mobsSnapshot()) {
                                    if (!m) continue;
                                    std::lock_guard mobLock(*m->stateMtx);
                                    if (canonicalDimension(m->dimension) !=
                                        canonicalDimension(dimension)) continue;
                                    const int mx = static_cast<int>(std::floor(m->x));
                                    const int my = static_cast<int>(std::floor(m->y));
                                    const int mz = static_cast<int>(std::floor(m->z));
                                    if (mx == tx && my == ty && mz == tz &&
                                        m->kind == MobKind::Sheep && !m->sheared) {
                                        m->sheared = true;
                                        shearedMob = m;
                                        shearedEntityId = m->entityId;
                                        shearedDimension = canonicalDimension(m->dimension);
                                        shearedX = m->x;
                                        shearedY = m->y;
                                        shearedZ = m->z;
                                        shearedColor = m->woolColor % 16;
                                        break;
                                    }
                                }
                            }
                            if(shearedMob){
                                // Do not call entity-spawn/broadcast helpers
                                // while entsMtx_ is held: both helpers may
                                // take the same lock or snapshot players.
                                static const char* woolNamesD[] = {
                                    "minecraft:white_wool","minecraft:orange_wool","minecraft:magenta_wool","minecraft:light_blue_wool",
                                    "minecraft:yellow_wool","minecraft:lime_wool","minecraft:pink_wool","minecraft:gray_wool",
                                    "minecraft:light_gray_wool","minecraft:cyan_wool","minecraft:purple_wool","minecraft:blue_wool",
                                    "minecraft:brown_wool","minecraft:green_wool","minecraft:red_wool","minecraft:black_wool"
                                };
                                int colD = shearedColor;
                                auto woolIt=gen::itemIdByName().find(woolNamesD[colD]);
                                if(woolIt!=gen::itemIdByName().end()){
                                    int cnt=1+nextRandom()%3;
                                    spawnItemDropFor(shearedDimension, shearedX,
                                                  shearedY + 0.8, shearedZ,
                                                  woolIt->second,
                                                  (uint8_t)cnt,
                                                  (nextRandom()/(double)RAND_MAX-.5)*0.12,
                                                  0.12,
                                                  (nextRandom()/(double)RAND_MAX-.5)*0.12);
                                }
                                WriteBuffer md; md.varint(shearedEntityId);
                                md.u8(17); md.u8(8); md.u8(1); md.u8(255);
                                broadcastPacketExceptInDimension(
                                    shearedDimension, nullptr,
                                    proto::pl::sc::SetEntityMetadata, md);
                                if(s.applyDamage(1)) s=ItemStack::air();
                                handled=true;
                            } else {
                                // check for snow_golem/mooshroom simplified: just drop if not sheared
                                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                    dx * .25, .15, dz * .25);
                                // don't consume? vanilla consumes durability only on success, but we treat as not consumed
                                handled=true; // don't double-decrement
                            }
                        } else if(!handled && iname=="minecraft:flint_and_steel"){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            uint16_t below = world_.getBlock(tx,ty-1,tz);
                            const gen::BlockDef* td=gen::blockByState(tSt);
                            const gen::BlockDef* bd=gen::blockByState(below);
                            bool isAir = tSt==0;
                            bool belowSolid = bd && td==nullptr; // simplified: any non-air below is solid
                            // also check for TNT, campfire, portal
                            bool handledFS=false;
                            if(td && std::string(td->name)=="minecraft:tnt"){
                                spawnPrimedTnt(tx+0.5, ty+0.5, tz+0.5, 0, 0.2, 0, 80);
                                broadcastSound("minecraft:entity.tnt.primed", tx+0.5, ty+0.5, tz+0.5, 1.f, 1.f, "block");
                                world_.setBlock(tx,ty,tz,0); broadcastBlockChange(tx,ty,tz,0);
                                handledFS=true;
                            } else if(td && (std::string(td->name)=="minecraft:campfire" || std::string(td->name)=="minecraft:soul_campfire")){
                                std::string lit=getPropStr(tSt,"lit");
                                if(lit=="false"){
                                    std::vector<std::pair<std::string_view,std::string_view>> props;
                                    for(auto&[k,v]: gen::propsOf(tSt)) if(k!="lit") props.emplace_back(k,v);
                                    props.emplace_back("lit","true");
                                    uint16_t ns=static_cast<uint16_t>(gen::stateWithProps(*td, props));
                                    world_.setBlock(tx,ty,tz,ns); broadcastBlockChange(tx,ty,tz,ns);
                                    handledFS=true;
                                }
                            } else if(isAir && belowSolid){
                                bool soulBase = false;
                                if (bd) {
                                    auto &tags = tagManager_.blockTags;
                                    auto it = tags.find("minecraft:soul_fire_base_blocks");
                                    if (it != tags.end()) {
                                        auto nit = gen::blockNameToState().find(std::string(bd->name));
                                        if (nit != gen::blockNameToState().end()) soulBase = it->second.count(static_cast<uint32_t>(nit->second))>0;
                                    }
                                    if (!soulBase) soulBase = std::string(bd->name)=="minecraft:soul_sand"||std::string(bd->name)=="minecraft:soul_soil";
                                }
                                std::string fn = soulBase?"minecraft:soul_fire":"minecraft:fire";
                                auto it=gen::blockNameToState().find(fn);
                                if(it!=gen::blockNameToState().end()){
                                    uint16_t fs=static_cast<uint16_t>(it->second);
                                    world_.setBlock(tx,ty,tz,fs); broadcastBlockChange(tx,ty,tz,fs);
                                    handledFS=true;
                                }
                            }
                            if(handledFS){
                                if(s.applyDamage(1)) s=ItemStack::air();
                                handled=true;
                            } else {
                                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                    dx * .25, .15, dz * .25);
                                handled=true;
                            }
                        } else if(!handled && iname=="minecraft:bone_meal"){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            const gen::BlockDef* td=gen::blockByState(tSt);
                            bool fertilized=false;
                            if(td){
                                auto* beh=blockTicks_.behaviorFor(std::string(td->name));
                                if(beh && beh->fertilize(world_, tx,ty,tz,tSt,this)){
                                    uint16_t ns=world_.getBlock(tx,ty,tz);
                                    broadcastBlockChange(tx,ty,tz,ns);
                                    broadcastSound("minecraft:item.bone_meal.use", tx+0.5,ty+0.5,tz+0.5,1.f,1.f,"block");
                                    fertilized=true;
                                }
                            }
                            if(fertilized){
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            } else {
                                spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                    dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            }
                        } else if(!handled && (iname=="minecraft:tnt" || iname.find("tnt") != std::string::npos)) {
                            spawnPrimedTnt(x + dx + 0.5, y + 0.3, z + dz + 0.5, dx*0.2, 0.2, dz*0.2, 80);
                            broadcastSound("minecraft:entity.tnt.primed", x+dx+0.5, y+dy+0.5, z+dz+0.5, 1.f, 1.f, "block");
                            if(--s.count<=0) s=ItemStack::air();
                            handled=true;
                        } else if(!handled) {
                            // default drop
                            spawnItemDropStack(sx, sy, sz, oneItem(s),
                                                dx * .25, .15, dz * .25);
                            if(--s.count<=0) s=ItemStack::air();
                            handled=true;
                        }
                        if(handled){
                            broadcastSound("minecraft:block.dispenser.dispense", x + .5, y + .5, z + .5, 1.f, 1.f, "block");
                            blockEntities_.markDirty(key);
                        }
                    }
                }
            }
            was = powered;
        }
    }
}
ItemStack* GameServer::containerAt(std::int32_t x, std::int32_t y,
                                   std::int32_t z, int& countOut,
                                   BlockEntity::Kind& kindOut) {
    return containerAtFor(0, x, y, z, countOut, kindOut);
}
ItemStack* GameServer::containerAtFor(std::int8_t dimension,
                                      std::int32_t x, std::int32_t y,
                                      std::int32_t z, int& countOut,
                                      BlockEntity::Kind& kindOut) {
    auto* be = blockEntitiesFor(dimension).getAt(x, y, z);
    if (!be) return nullptr;
    kindOut = be->kind;
    switch (be->kind) {
    case BlockEntity::Kind::Chest:
    case BlockEntity::Kind::Barrel:
    case BlockEntity::Kind::ShulkerBox: countOut = 27; return be->chest.slots;
    case BlockEntity::Kind::Hopper: countOut = 5; return be->generic.slots;
    case BlockEntity::Kind::Dispenser:
    case BlockEntity::Kind::Dropper: countOut = 9; return be->generic.slots;
    case BlockEntity::Kind::Crafter: countOut = CrafterData::kSlots; return be->crafter.slots;
    case BlockEntity::Kind::Furnace: countOut = 3; return be->furnace.slots;
    case BlockEntity::Kind::Brewing: countOut = 5; return be->brewing.slots;
    default: return nullptr;
    }
}
const std::vector<TradeOffer>& GameServer::tradeTable() {
    using TO = TradeOffer;
    static const std::vector<TradeOffer> table = [] {
        auto id = [](const char* n) {
            auto it = gen::itemIdByName().find(n);
            return it!=gen::itemIdByName().end()? it->second : 0u;
        };
        return std::vector<TO>{
            {id("minecraft:wheat"), 20, 0, 0, id("minecraft:emerald"), 1, 16, 2, 0.05f},
            {id("minecraft:coal"), 15, 0, 0, id("minecraft:emerald"), 1, 16, 2, 0.05f},
            {id("minecraft:emerald"), 1, 0, 0, id("minecraft:bread"), 4, 16, 2, 0.05f},
            {id("minecraft:emerald"), 3, 0, 0, id("minecraft:iron_pickaxe"), 1, 12, 10, 0.05f},
            {id("minecraft:porkchop"), 7, 0, 0, id("minecraft:emerald"), 1, 16, 5, 0.05f},
        };
    }();
    return table;
}
static std::string professionToString(VillagerData::Profession p){
    switch(p){
        case VillagerData::ARMORER: return "minecraft:armorer";
        case VillagerData::BUTCHER: return "minecraft:butcher";
        case VillagerData::CARTOGRAPHER: return "minecraft:cartographer";
        case VillagerData::CLERIC: return "minecraft:cleric";
        case VillagerData::FARMER: return "minecraft:farmer";
        case VillagerData::FISHERMAN: return "minecraft:fisherman";
        case VillagerData::FLETCHER: return "minecraft:fletcher";
        case VillagerData::LEATHERWORKER: return "minecraft:leatherworker";
        case VillagerData::LIBRARIAN: return "minecraft:librarian";
        case VillagerData::MASON: return "minecraft:mason";
        case VillagerData::SHEPHERD: return "minecraft:shepherd";
        case VillagerData::TOOLSMITH: return "minecraft:toolsmith";
        case VillagerData::WEAPONSMITH: return "minecraft:weaponsmith";
        default: return "minecraft:farmer";
    }
}
static const std::unordered_map<std::string, std::array<std::vector<TradeOffer>,5>>& professionTrades(){
    using TO = TradeOffer;
    static std::unordered_map<std::string, std::array<std::vector<TO>,5>> m;
    static bool init=false;
    if(init) return m;
    init=true;
    auto id = [](const char* n)->uint32_t{
        auto it = gen::itemIdByName().find(n);
        return it!=gen::itemIdByName().end()? it->second : 0u;
    };
    auto emerald=id("minecraft:emerald");
    m["minecraft:farmer"] = {{
        std::vector<TO>{{emerald,1,0,0,id("minecraft:bread"),6,16,2,0.05f},{id("minecraft:wheat"),20,0,0,emerald,1,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:pumpkin"),6,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:pumpkin_pie"),4,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:melon_slice"),4,0,0,emerald,1,16,7,0.05f},{emerald,1,0,0,id("minecraft:cookie"),18,12,10,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cake"),1,12,15,0.05f},{emerald,1,0,0,id("minecraft:suspicious_stew"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:golden_carrot"),3,12,30,0.05f},{emerald,1,0,0,id("minecraft:glistering_melon_slice"),3,12,30,0.05f}}
    }};
    m["minecraft:librarian"] = {{
        std::vector<TO>{{emerald,1,0,0,id("minecraft:bookshelf"),1,12,2,0.05f},{id("minecraft:paper"),24,0,0,emerald,1,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:book"),4,0,0,emerald,1,12,5,0.05f},{emerald,5,0,0,id("minecraft:clock"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:written_book"),1,0,0,emerald,1,12,10,0.05f},{emerald,5,0,0,id("minecraft:name_tag"),1,12,10,0.05f}},
        std::vector<TO>{{emerald,8,0,0,id("minecraft:enchanted_book"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,5,0,0,id("minecraft:enchanted_book"),1,12,30,0.20f}}
    }};
    m["minecraft:cleric"] = {{
        std::vector<TO>{{id("minecraft:rotten_flesh"),32,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:redstone"),2,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:gold_ingot"),3,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:glowstone"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit_foot"),1,0,0,emerald,1,12,10,0.05f},{emerald,1,0,0,id("minecraft:ender_pearl"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:scute"),4,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:bottle_o_enchanting"),1,12,15,0.05f}},
        std::vector<TO>{{id("minecraft:nether_wart"),1,0,0,emerald,1,12,30,0.05f},{emerald,1,0,0,id("minecraft:ender_pearl"),1,12,30,0.05f}}
    }};
    m["minecraft:armorer"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:iron_leggings"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_boots"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:lava_bucket"),1,0,0,emerald,1,12,10,0.05f},{emerald,4,0,0,id("minecraft:iron_helmet"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,6,0,0,id("minecraft:diamond_chestplate"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_chestplate"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_boots"),1,12,30,0.05f}}
    }};
    m["minecraft:weaponsmith"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:iron_axe"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_sword"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:iron_sword"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,8,0,0,id("minecraft:diamond_axe"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_sword"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_axe"),1,12,30,0.05f}}
    }};
    m["minecraft:toolsmith"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:stone_axe"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_pickaxe"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:iron_pickaxe"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,6,0,0,id("minecraft:diamond_pickaxe"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_pickaxe"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_shovel"),1,12,30,0.05f}}
    }};
    m["minecraft:butcher"] = {{
        std::vector<TO>{{id("minecraft:chicken"),14,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:cooked_chicken"),8,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:porkchop"),7,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:cooked_porkchop"),5,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:mutton"),7,0,0,emerald,1,16,7,0.05f},{emerald,1,0,0,id("minecraft:cooked_mutton"),7,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit"),4,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:rabbit_stew"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cooked_rabbit"),5,12,30,0.05f},{emerald,1,0,0,id("minecraft:cooked_mutton"),7,12,30,0.05f}}
    }};
    m["minecraft:fisherman"] = {{
        std::vector<TO>{{id("minecraft:string"),20,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:cooked_cod"),6,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:coal"),10,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:cooked_salmon"),6,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:cod"),15,0,0,emerald,1,16,10,0.05f},{emerald,1,0,0,id("minecraft:fishing_rod"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:salmon"),13,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:bucket"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cooked_cod"),6,12,30,0.05f},{emerald,1,0,0,id("minecraft:nautilus_shell"),1,12,30,0.05f}}
    }};
    m["minecraft:fletcher"] = {{
        std::vector<TO>{{id("minecraft:stick"),32,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:arrow"),16,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,16,5,0.05f},{emerald,2,0,0,id("minecraft:bow"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:string"),14,0,0,emerald,1,16,10,0.05f},{emerald,3,0,0,id("minecraft:crossbow"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:feather"),24,0,0,emerald,1,12,15,0.05f},{emerald,3,0,0,id("minecraft:bow"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,5,0,0,id("minecraft:arrow"),16,12,30,0.05f},{emerald,5,0,0,id("minecraft:tipped_arrow"),5,12,30,0.05f}}
    }};
    m["minecraft:leatherworker"] = {{
        std::vector<TO>{{id("minecraft:leather"),6,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:leather_leggings"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),26,0,0,emerald,1,12,5,0.05f},{emerald,2,0,0,id("minecraft:leather_chestplate"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit_hide"),9,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:leather_helmet"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:scute"),6,0,0,emerald,1,12,15,0.05f},{emerald,5,0,0,id("minecraft:leather_chestplate"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,6,0,0,id("minecraft:saddle"),1,12,30,0.05f},{emerald,4,0,0,id("minecraft:leather_horse_armor"),1,12,30,0.05f}}
    }};
    m["minecraft:mason"] = {{
        std::vector<TO>{{id("minecraft:clay_ball"),10,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:brick"),10,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:stone"),20,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:chiseled_stone_bricks"),4,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:granite"),16,0,0,emerald,1,16,10,0.05f},{emerald,1,0,0,id("minecraft:diorite"),1,16,10,0.05f}},
        std::vector<TO>{{id("minecraft:quartz"),12,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:quartz_pillar"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:quartz_block"),1,12,30,0.05f},{emerald,1,0,0,id("minecraft:clay"),10,12,30,0.05f}}
    }};
    m["minecraft:shepherd"] = {{
        std::vector<TO>{{id("minecraft:wool"),18,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:shears"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:dye"),12,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:bed"),1,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:wool"),18,0,0,emerald,1,16,10,0.05f},{emerald,2,0,0,id("minecraft:banner"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:string"),14,0,0,emerald,1,12,15,0.05f},{emerald,3,0,0,id("minecraft:painting"),3,12,15,0.05f}},
        std::vector<TO>{{emerald,3,0,0,id("minecraft:bed"),1,12,30,0.05f},{emerald,2,0,0,id("minecraft:shears"),1,12,30,0.05f}}
    }};
    m["minecraft:cartographer"] = {{
        std::vector<TO>{{id("minecraft:paper"),24,0,0,emerald,1,16,2,0.05f},{emerald,7,0,0,id("minecraft:empty_map"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:glass_pane"),11,0,0,emerald,1,16,5,0.05f},{emerald,7,0,0,id("minecraft:explorer_map"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:compass"),1,0,0,emerald,1,12,10,0.05f},{emerald,7,0,0,id("minecraft:explorer_map"),1,12,10,0.05f}},
        std::vector<TO>{{emerald,12,0,0,id("minecraft:explorer_map"),1,12,15,0.05f},{emerald,7,0,0,id("minecraft:item_frame"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,20,0,0,id("minecraft:explorer_map"),1,12,30,0.05f},{emerald,7,0,0,id("minecraft:banner"),1,12,30,0.05f}}
    }};
    return m;
}
bool GameServer::openTrading(Player& p, MobEntity& v) {
    if (!p.conn) return false;
    const int windowId = ++villagerWindowSeq_;
    WriteBuffer b;
    b.varint(windowId);
    b.varint(menus::kMerchant);
    nbt::writeTextComponent(b, "Villager");
    p.conn->trySendPacket(proto::pl::sc::OpenScreen, b);
    WriteBuffer tl;
    tl.varint(windowId);
    // NITWIT: no trades
    if (v.villagerData.profession == VillagerData::NITWIT) {
        tl.varint(0);
        tl.varint(0); tl.varint(0);
        int lvl = std::clamp(v.villagerData.level,1,5);
        tl.varint(lvl); tl.i32(v.villagerXp); tl.boolean(false);
        p.conn->trySendPacket(proto::pl::sc::TradeList, tl);
        return true;
    }
    int lvl = std::clamp(v.villagerData.level,1,5);
    if (v.villagerLevel != lvl) lvl = std::clamp(v.villagerLevel,1,5);
    std::string profStr = professionToString(v.villagerData.profession);
    const auto& pt = professionTrades();
    const std::vector<TradeOffer>* offersPtr = nullptr;
    std::vector<TradeOffer> fallback;
    auto it = pt.find(profStr);
    if (it != pt.end()) {
        offersPtr = &it->second[lvl-1];
    } else {
        const auto& t = tradeTable();
        int num = std::min<int>((int)t.size(), lvl*2);
        if (num==0) num = std::min<int>((int)t.size(), 2);
        fallback.assign(t.begin(), t.begin()+num);
        offersPtr = &fallback;
    }
    const auto& offers = *offersPtr;
    int num = (int)offers.size();
    // lvl*2 slicing is already per-level vector size <=2*lvl, but enforce min 2 for lvl1 fallback display For farmer lvl1 size 2 already OK
    tl.varint(static_cast<std::int32_t>(num));
    int gossipRep = v.gossip.get(p.uuid);
    for (int i=0;i<num;++i) {
        const auto& t = offers[i];
        float baseMult = t.priceMultiplier;
        float gossipDiscount = std::min(0.5f, gossipRep * 0.02f);
        // villager Workaround: priceMult stays baseMult, specialPrice carries discount
        float priceMult = baseMult;
        int specialPrice = - int(std::floor(gossipDiscount * t.inCount));
        // firstBuy
        tl.varint(static_cast<std::int32_t>(t.inItem));
        tl.varint(t.inCount);
        tl.varint(0);
        ItemStack::of(t.outItem, t.outCount).write(tl);
        if (t.inItem2 != 0) {
            tl.boolean(true);
            tl.varint(static_cast<std::int32_t>(t.inItem2));
            tl.varint(t.inCount2);
            tl.varint(0);
        } else {
            tl.boolean(false);
        }
        tl.boolean(false);
        tl.i32(0);
        tl.i32(t.maxUses);
        tl.i32(t.xp);
        tl.i32(specialPrice);
        tl.f32(priceMult);
        tl.i32(t.demand);
    }
    tl.varint(0);
    tl.varint(0);
    tl.varint(lvl);
    tl.i32(v.villagerXp);
    tl.boolean(true);
    p.conn->trySendPacket(proto::pl::sc::TradeList, tl);
    return true;
}
bool GameServer::selectTrade(Player& p, std::int32_t index,
                             std::int32_t villagerEntityId) {
    // The session and the tick thread can both touch a player's inventory.
    // Resolve the villager first, then perform the complete input/output
    // transaction while holding the player state lock.  No packet, event, or
    // world callback is made under either lock.
    std::unique_lock playerLock(p.stateMtx);
    if (!p.inPlay || p.dead || !p.conn) return false;

    TradeOffer t{};
    std::shared_ptr<MobEntity> villager;
    const auto playerDimension = canonicalDimension(p.dimension);
    const double playerX = p.x;
    const double playerY = p.y;
    const double playerZ = p.z;
    const auto playerUuid = p.uuid;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard mobLock(*candidate->stateMtx);
        if (candidate->kind != MobKind::Villager ||
            canonicalDimension(candidate->dimension) != playerDimension)
            continue;
        if (villagerEntityId >= 0 && candidate->entityId != villagerEntityId)
            continue;
        const double dx = candidate->x - playerX;
        const double dz = candidate->z - playerZ;
        if (dx * dx + dz * dz >= 128.0) {
            if (villagerEntityId >= 0) return false;
            continue;
        }
        villager = candidate;
        if (candidate->villagerData.profession == VillagerData::NITWIT)
            return false;
        const std::string profession =
            professionToString(candidate->villagerData.profession);
        const auto it = professionTrades().find(profession);
        if (it != professionTrades().end()) {
            int level = std::clamp(candidate->villagerData.level, 1, 5);
            if (candidate->villagerLevel != level)
                level = std::clamp(candidate->villagerLevel, 1, 5);
            const auto& offers = it->second[static_cast<std::size_t>(level - 1)];
            if (index < 0 || index >= static_cast<std::int32_t>(offers.size()))
                return false;
            t = offers[static_cast<std::size_t>(index)];
        }
        break;
    }
    if (!villager) {
        // The fallback is kept for the legacy direct GameServer API.  A live
        // merchant session always supplies its entity id and therefore cannot
        // trade against an unrelated nearby/default offer.
        if (villagerEntityId >= 0) return false;
        const auto& trades = tradeTable();
        if (index < 0 || static_cast<std::size_t>(index) >= trades.size())
            return false;
        t = trades[static_cast<std::size_t>(index)];
    }
    auto countItem = [&](std::uint32_t itemId) {
        int total = 0;
        for (const auto& stack : p.inv)
            if (!stack.empty() && stack.itemId == itemId) total += stack.count;
        return total;
    };
    if (countItem(t.inItem) < t.inCount ||
        (t.inItem2 != 0 && countItem(t.inItem2) < t.inCount2))
        return false;

    // Keep a rollback point for the complete transaction.  The preflight
    // count checks above should make input consumption infallible, but the
    // snapshot also protects this boundary if the inventory layout or a
    // future trade rule changes.
    const auto originalInventory = p.inv;
    int need = t.inCount;
    int need2 = 0;
    for (auto& s : p.inv) {
        if (need <= 0) break;
        if (!s.empty() && s.itemId == t.inItem) {
            const int take = std::min<int>(s.count, need);
            s.count -= take; need -= take;
            if (s.count <= 0) s = ItemStack::air();
        }
    }
    if (t.inItem2 != 0) {
        need2 = t.inCount2;
        for (auto& s: p.inv) {
            if (need2<=0) break;
            if (!s.empty() && s.itemId == t.inItem2) {
                const int take = std::min<int>(s.count, need2);
                s.count -= take; need2 -= take;
                if (s.count <= 0) s = ItemStack::air();
            }
        }
    }
    if (need != 0 || (t.inItem2 != 0 && need2 != 0)) {
        p.inv = originalInventory;
        return false;
    }
    // addToInventory works on a trial copy and commits only on success.  If
    // the result cannot fit, restore the inputs so a failed trade is atomic.
    if (!addToInventory(p, t.outItem, t.outCount)) {
        p.inv = originalInventory;
        return false;
    }
    const auto tradeDimension = playerDimension;
    playerLock.unlock();
    resendInventory(p);
    spawnXpOrbsFor(tradeDimension, playerX, playerY + 1, playerZ, 2, &p);
    {
        std::string soldName = "minecraft:emerald";
        for (const auto& [name, id] : gen::itemIdByName())
            if (id == t.inItem) { soldName = name; break; }
        onVillagerTraded(p, soldName, t.inCount);
    }
    broadcastSoundFor(tradeDimension, "minecraft:entity.villager.yes",
                      playerX, playerY, playerZ, .8f, 1.f, "neutral");
    bool villagerLevelledUp = false;
    std::int8_t levelUpDimension = 0;
    double levelUpX = 0.0;
    double levelUpY = 0.0;
    double levelUpZ = 0.0;
    bool stillPresent = false;
    {
        const auto currentMobs = mobsSnapshot();
        stillPresent = villager &&
                       std::find(currentMobs.begin(), currentMobs.end(), villager) !=
                           currentMobs.end();
    }
    if (stillPresent) {
        std::lock_guard mobLock(*villager->stateMtx);
        if (canonicalDimension(villager->dimension) == tradeDimension) {
            const double dx = villager->x - playerX;
            const double dz = villager->z - playerZ;
            if (dx * dx + dz * dz < 64.0) {
                auto& m = *villager;
                m.villagerXp += 3 + (nextRandom()%4);
                m.gossip.add(playerUuid, 2);
                // Level up check: every 10 xp -> level++ (vanilla xp thresholds 10,70 etc simplified)
                if (m.villagerXp >= m.villagerLevel * 10 && m.villagerLevel < 5) {
                    m.setVillagerLevel(m.villagerLevel+1);
                    villagerLevelledUp = true;
                    levelUpDimension = canonicalDimension(m.dimension);
                    levelUpX = m.x;
                    levelUpY = m.y;
                    levelUpZ = m.z;
                } else {
                    m.syncVillagerLevel();
                }
                // Restock: 2/day (vanilla: work POI, 6000-12000 ticks, max 2 per day)
                std::int64_t curDay = tickNo_ / 24000;
                if (curDay != m.villagerLastRestockDay) {
                    m.villagerRestocksToday = 0;
                    m.villagerLastRestockDay = curDay;
                }
                if (m.villagerRestocksToday >= 2) {
                    // already restocked twice today, schedule next day morning
                    m.restockUntil = (curDay+1)*24000 + 2000;
                } else {
                    if (m.restockUntil < tickNo_) {
                        m.restockUntil = tickNo_ + MobEntity::kRestockSecondWindowTicks + (nextRandom()%2000);
                    }
                }
            }
        }
    }
    if (villagerLevelledUp) {
        broadcastSoundFor(levelUpDimension,
                          "minecraft:entity.villager.levelup",
                          levelUpX, levelUpY, levelUpZ, 1.f, 1.f, "neutral");
    }
    return true;
}
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
        if (source.empty()) return 0;
        int remaining = source.count;
        const auto insertInto = [&](const int* slots, std::size_t slotCount) {
            for (std::size_t n = 0; n < slotCount && remaining > 0; ++n) {
                auto& destination = player.inv[slots[n]];
                if (!sameStackData(destination, source) || destination.count >= 64)
                    continue;
                const int moved = std::min<int>(64 - destination.count,
                                                remaining);
                destination.count += static_cast<std::int16_t>(moved);
                remaining -= moved;
            }
        };
        const auto fillEmpty = [&](const int* slots, std::size_t slotCount) {
            for (std::size_t n = 0; n < slotCount && remaining > 0; ++n) {
                auto& destination = player.inv[slots[n]];
                if (!destination.empty()) continue;
                destination = source;
                destination.count = static_cast<std::int16_t>(
                    std::min(64, remaining));
                remaining -= destination.count;
            }
        };
        static constexpr int kHotbar[] = {36, 37, 38, 39, 40, 41, 42, 43, 44};
        static constexpr int kMain[] = {9, 10, 11, 12, 13, 14, 15, 16, 17,
                                        18, 19, 20, 21, 22, 23, 24, 25, 26,
                                        27, 28, 29, 30, 31, 32, 33, 34, 35};
        insertInto(kHotbar, std::size(kHotbar));
        insertInto(kMain, std::size(kMain));
        fillEmpty(kHotbar, std::size(kHotbar));
        fillEmpty(kMain, std::size(kMain));
        return static_cast<int>(source.count) - remaining;
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
    for (const auto& e : active) {
        if (!e || e->collected) continue;
        World& world = worldFor(e->dimension);
        ++e->ageTicks;
        if (e->ageTicks > 6000) continue;
        e->vy -= 0.04;
        if (e->vy < -0.5) e->vy = -0.5;
        e->y += e->vy;
        e->x += e->vx;
        e->z += e->vz;
        world.generateChunkIfMissing(static_cast<std::int32_t>(e->x) >> 4,
                                     static_cast<std::int32_t>(e->z) >> 4);
        int col = 4;
        world.withChunk(static_cast<std::int32_t>(e->x) >> 4,
                        static_cast<std::int32_t>(e->z) >> 4,
                        [&](const Chunk& c) {
                            for (int ry = kSectionsPerChunk * 16 - 1;
                                 ry >= 0; --ry) {
                                if (c.blocks[Chunk::index(
                                        ry >> 4, ry & 15,
                                        static_cast<std::int32_t>(e->z) & 15,
                                        static_cast<std::int32_t>(e->x) & 15)] != 0) {
                                    col = ry + 1;
                                    break;
                                }
                            }
                        });
        const double gy = kMinY + col + 0.25;
        if (e->y < gy) {
            e->y = gy;
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
            const double dx = playerX - e->x;
            const double dy = (playerY + 0.9) - e->y;
            const double dz = playerZ - e->z;
            if (dx * dx + dy * dy + dz * dz < 2.0) {
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
void GameServer::spawnItemDrop(double x,double y,double z,std::uint32_t itemId,std::uint8_t cnt,
                               double vx,double vy,double vz) {
    spawnItemDropFor(0, x, y, z, itemId, cnt, vx, vy, vz);
}
void GameServer::spawnItemDrop(double x,double y,double z,const ItemStack& stack,
                               double vx,double vy,double vz) {
    spawnItemDropFor(0, x, y, z, stack, vx, vy, vz);
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
    std::uint8_t zero[16] = {};
    b.uuid(zero);
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
    std::lock_guard playerLock(p.stateMtx);
    // Treat the operation as atomic.  Several callers use the boolean result
    // to decide whether to drop the original stack; mutating a few slots and
    // then returning false would duplicate those items when the caller drops
    // the unchanged source.
    auto trial = p.inv;
    // merge into existing stacks (hotbar 36..44, main 9..35)
    for (int pass = 0; pass < 2; ++pass) {
        for (int i : (pass == 0 ? std::initializer_list<int>{36,37,38,39,40,41,42,43,44}
                                : std::initializer_list<int>{9,10,11,12,13,14,15,16,17,18,19,
                                                             20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35})) {
            auto& s = trial[i];
            if (pass == 0 && s.itemId == itemId && s.count > 0 && s.count < 64) {
                const auto take = std::min<int16_t>((int16_t)(64 - s.count), (int16_t)count);
                s.count += take; count -= take;
                if (count == 0) {
                    p.inv = std::move(trial);
                    return true;
                }
            } else if (pass == 1 && s.count == 0) {
                s.itemId = itemId; s.count = std::min<int16_t>(64, (int16_t)count);
                count -= s.count;
                if (count == 0) {
                    p.inv = std::move(trial);
                    return true;
                }
            }
        }
    }
    return false;                                       // inventory full: stays on ground
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
    b.varint(46);
    for (int i = 0; i < 46; ++i) p.inv[i].write(b);
    ItemStack::air().write(b);                            // carried
    playerLock.unlock();
    connection->trySendPacket(pl::sc::ContainerSetContent, b);
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
void GameServer::effectsTick() {
    const std::int64_t tick = tickNo_.load(std::memory_order_acquire);
    for (const auto& pp : playersSnapshot()) {
        if (!pp) continue;
        Player& player = *pp;
        bool changed = false;
        bool healthChanged = false;
        float magicDamage = 0.0f;
        float poisonDamage = 0.0f;
        float witherDamage = 0.0f;
        std::vector<std::int32_t> removedEffects;
        std::shared_ptr<Connection> connection;
        std::int32_t entityId = 0;
        std::int8_t dimension = 0;
        bool sendLevitation = false;
        WriteBuffer levitationPacket;
        {
            std::lock_guard playerLock(player.stateMtx);
            if (!player.inPlay || player.effects.empty()) continue;
            connection = player.conn;
            entityId = player.entityId;
            dimension = canonicalDimension(player.dimension);
            for (auto it = player.effects.begin(); it != player.effects.end();) {
                if (it->type == effects::InstantHealth && !it->expired()) {
                    player.health = std::min(20.f,
                        player.health + 4.f * (it->amplifier + 1));
                    healthChanged = true;
                    it = player.effects.erase(it);
                    changed = true;
                    continue;
                }
                if (it->type == effects::InstantDamage && !it->expired()) {
                    magicDamage += 6.f * (it->amplifier + 1);
                    it = player.effects.erase(it);
                    changed = true;
                    continue;
                }
                --it->durationTicks;
                if (it->expired()) {
                    removedEffects.push_back(it->type);
                    it = player.effects.erase(it);
                    changed = true;
                    continue;
                }
                if (it->type == effects::Regeneration &&
                    tick % std::max(1, 50 >> it->amplifier) == 0) {
                    player.health = std::min(20.f, player.health + 1.f);
                    healthChanged = true;
                }
                if (it->type == effects::Poison &&
                    tick % std::max(1, 25 >> it->amplifier) == 0 &&
                    player.health > 1.0f)
                    poisonDamage += 1.0f;
                if (it->type == effects::Wither &&
                    tick % std::max(1, 40 >> it->amplifier) == 0)
                    witherDamage += 1.0f;
                if (it->type == effects::Saturation &&
                    tick % std::max(1, 2 >> it->amplifier) == 0)
                    addFoodAndSaturation(player, 1,
                                         float(it->amplifier + 1));
                if (it->type == effects::Hunger)
                    addHungerExhaustion(player,
                                       0.005f * float(it->amplifier + 1));
                ++it;
            }

            // Per-tick metadata effects: invisibility/glowing/levitation/
            // slow-falling.  Only the state mutation and packet construction
            // belong under the player lock; transport is deferred below.
            static thread_local std::unordered_map<std::int32_t, double> levVy;
            const int levAmp = amplifierFor(player.effects, effects::Levitation);
            if (levAmp >= 0 && !player.isSwimming && player.vehicleId == -1) {
                const double target = levitationVelocity(levAmp);
                double& vy = levVy[player.entityId];
                vy += (target - vy) * 0.2;
                player.y += vy;
                player.fallDist = 0;
                player.prevFeetY = player.y;
                if (connection) {
                    levitationPacket.varint(player.entityId);
                    levitationPacket.f64(player.x);
                    levitationPacket.f64(player.y);
                    levitationPacket.f64(player.z);
                    levitationPacket.i8(static_cast<std::int8_t>(
                        player.yaw * 256.f / 360.f));
                    levitationPacket.i8(static_cast<std::int8_t>(
                        player.pitch * 256.f / 360.f));
                    levitationPacket.boolean(player.onGround);
                    sendLevitation = true;
                }
            } else if (levAmp >= 0) {
                // Levitating but swimming/riding still suppresses fall damage.
                player.fallDist = 0;
                levVy.erase(player.entityId);
            } else {
                levVy.erase(player.entityId);
                if (hasEffect(player.effects, effects::SlowFalling) &&
                    player.fallDist > 0)
                    player.fallDist *= 0.9;
            }
        }

        if (magicDamage > 0.0f) applyDamage(player, magicDamage, "magic");
        if (poisonDamage > 0.0f) applyDamage(player, poisonDamage, "poison");
        if (witherDamage > 0.0f) applyDamage(player, witherDamage, "wither");
        if (healthChanged) sendSetHealth(player);
        if (connection) {
            for (const auto effect : removedEffects) {
                WriteBuffer packet;
                packet.varint(entityId);
                packet.varint(effect);
                connection->trySendPacket(pl::sc::RemoveMobEffect, packet);
            }
        }
        if (changed) onEffectsChanged(&player);
        if (sendLevitation) {
            try {
                broadcastPacketExceptInDimension(
                    dimension, nullptr, pl::sc::EntityTeleport,
                    levitationPacket);
            } catch (...) {
            }
        }
    }
    for (const auto& pp : playersSnapshot()) {
        if (!pp) continue;
        Player& player = *pp;
        std::shared_ptr<Connection> connection;
        std::int32_t entityId = 0;
        std::int8_t dimension = 0;
        WriteBuffer attributesPacket;
        WriteBuffer metadataPacket;
        bool sendAttributes = false;
        bool sendMetadata = false;
        {
            std::lock_guard playerLock(player.stateMtx);
            if (!player.inPlay || !player.conn) continue;
            connection = player.conn;
            entityId = player.entityId;
            dimension = canonicalDimension(player.dimension);
            player.attributes.applyEffectModifiers(player.effects);
            if (tick % 20 == 0 &&
                (!player.effects.empty() ||
                 player.attributes.getValue(Attribute::MOVEMENT_SPEED) != 0.10 ||
                 player.attributes.getValue(Attribute::MAX_HEALTH) != 20.0 ||
                 player.attributes.getValue(Attribute::ARMOR) != 0 ||
                 player.attributes.getValue(Attribute::ATTACK_DAMAGE) != 1.0)) {
                player.attributes.writeUpdate(attributesPacket, entityId);
                sendAttributes = true;
            }
            // Sync invisibility/glowing metadata: index 0 flags, index 6 pose
            // already has its own update path.
            if (tick % 20 == 0) {
                const bool invis = isInvisible(player.effects);
                const bool glow = isGlowing(player.effects);
                if (invis || glow) {
                    metadataPacket.varint(entityId);
                    if (invis) {
                        metadataPacket.u8(0);
                        metadataPacket.varint(0);
                        metadataPacket.u8(0x20);
                    }
                    if (glow) {
                        metadataPacket.u8(0);
                        metadataPacket.varint(0);
                        metadataPacket.u8(0x40);
                    }
                    metadataPacket.u8(255);
                    sendMetadata = metadataPacket.data.size() > 2;
                }
            }
        }
        if (sendAttributes) {
            connection->trySendPacket(pl::sc::UpdateAttributes, attributesPacket);
            broadcastPacketExceptInDimension(dimension, &player,
                                             pl::sc::UpdateAttributes,
                                             attributesPacket);
        }
        if (sendMetadata) {
            try {
                broadcastPacketExceptInDimension(
                    dimension, nullptr, pl::sc::SetEntityMetadata,
                    metadataPacket);
            } catch (...) {
            }
        }
    }
}
void GameServer::furnacesTick() {
    furnacesTickFor(0);
    furnacesTickFor(-1);
    furnacesTickFor(1);
}

void GameServer::furnacesTickFor(std::int8_t dimension) {
    auto& blockEntities_ = blockEntitiesFor(dimension);
    auto& world_ = worldFor(dimension);
    auto broadcastBlockChange = [this, dimension](std::int32_t x,
                                                   std::int32_t y,
                                                   std::int32_t z,
                                                   std::uint16_t state) {
        this->broadcastBlockChangeFor(dimension, x, y, z, state);
    };
    blockEntities_.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Furnace) return;
        FurnaceData& f = be.furnace;
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        world_.generateChunkIfMissing(x >> 4, z >> 4);
        const std::uint16_t stateHere = world_.getBlock(x, y, z);

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
                blockEntities_.markDirty(key);
            }
        }
        const bool burning = f.burnTicks > 0;
        if (canSmelt && burning) {
            if (++f.cookProgress >= f.cookTotal) {
                f.cookProgress = 0;
                auto out = recipe->result;
                auto& dst = f.slots[FurnaceData::kOutput];
                if (dst.empty()) dst = out;
                else if (dst.itemId == out.itemId) dst.count += out.count;
                else { f.cookProgress = f.cookTotal; return; }
                ItemStack& in = f.slots[FurnaceData::kInput];
                if (--in.count <= 0) in = ItemStack::air();
                blockEntities_.markDirty(key);
                // xp orbs on manual collection only; skip here
            }
        } else {
            f.cookProgress = 0;
        }

        // lit-state block update (vanilla swaps furnace[lit=...])
        static const gen::BlockDef* fdef = gen::blockByName("minecraft:furnace");
        if ((fdef && stateHere == fdef->defaultState) || stateHere == 4351) {
            // NOTE(cleanup): parenthesized as evaluated — (fdef && stateHere == default) || stateHere == 4351.
            const std::uint16_t want = gen::stateWithPropsList("minecraft:furnace",
                {{"lit", burning ? "true" : "false"}});
            if (stateHere != want) {
                world_.setBlock(x, y, z, want);
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
    auto& blockEntities_ = blockEntitiesFor(dimension);
    // Brewing stand: fuel (blaze powder -> kFuelPerBlaze) + brewTime kBrewTicks.
    const auto itBlaze = gen::itemIdByName().find("minecraft:blaze_powder");
    const std::uint32_t blazeId = itBlaze != gen::itemIdByName().end() ? itBlaze->second : 0;
    blockEntities_.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Brewing) return;
        BrewingData& b = be.brewing;
        // replenish fuel from blaze powder in slot 4
        if (b.fuel <= 0 && !b.slots[4].empty() && (blazeId == 0 || b.slots[4].itemId == blazeId)) {
            if (--b.slots[4].count <= 0) b.slots[4] = ItemStack::air();
            b.fuel = BrewingData::kFuelPerBlaze;
            blockEntities_.markDirty(key);
        }
        if (b.brewTime > 0) {
            --b.brewTime;
            blockEntities_.markDirty(key);
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
                    blockEntities_.markDirty(key);
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
                blockEntities_.markDirty(key);
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
        e->vy -= 0.03;
        if (e->vy < -0.4) e->vy = -0.4;
        e->y += e->vy;
        world.generateChunkIfMissing(static_cast<std::int32_t>(e->x) >> 4,
                                     static_cast<std::int32_t>(e->z) >> 4);
        int col = 4;
        world.withChunk(static_cast<std::int32_t>(e->x) >> 4,
                        static_cast<std::int32_t>(e->z) >> 4,
                        [&](const Chunk& c) {
                            for (int ry = kSectionsPerChunk * 16 - 1;
                                 ry >= 0; --ry) {
                                if (c.blocks[Chunk::index(
                                        ry >> 4, ry & 15,
                                        static_cast<std::int32_t>(e->z) & 15,
                                        static_cast<std::int32_t>(e->x) & 15)] != 0) {
                                    col = ry + 1;
                                    break;
                                }
                            }
                        });
        const double gy = kMinY + col + 0.25;
        if (e->y < gy) {
            e->y = gy;
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
            for (int i=0;i<46;++i) if(!p.inv[i].empty() && p.inv[i].mendingLevel()>0 && p.inv[i].getDamage()>0) mendingSlots.push_back(i);
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
    std::uint8_t zero[16] = {};
    b.uuid(zero);
    b.varint(ti != types.end() ? static_cast<std::int32_t>(ti->second) : 0);
    b.f64(x); b.f64(y); b.f64(z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(1);                                        // objectData: velocity
    b.i16(static_cast<std::int16_t>(vx * 8000));
    b.i16(static_cast<std::int16_t>(vy * 8000));
    b.i16(static_cast<std::int16_t>(vz * 8000));
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
        std::array<InvSlot, 46> inv{};
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
                                owner->x = tx;
                                owner->y = ty;
                                owner->z = tz;
                                owner->lastEnderPearlTick = tickNo_;
                                ownerConnection = owner->conn;
                                ownerEntityId = owner->entityId;
                                ownerYaw = owner->yaw;
                                ownerPitch = owner->pitch;
                                ownerDimension = canonicalDimension(owner->dimension);
                            }
                            // teleport packet
                            if (ownerConnection) {
                                WriteBuffer tb;
                                tb.varint(0); // teleport id not tracked for pearl? use 0
                                tb.f64(tx); tb.f64(ty); tb.f64(tz);
                                tb.f64(0); tb.f64(0); tb.f64(0);
                                tb.f32(ownerYaw); tb.f32(ownerPitch);
                                tb.u32(0);
                                ownerConnection->trySendPacket(proto::pl::sc::PlayerPosition, tb);
                            }
                            // broadcast to others
                            {
                                WriteBuffer tp;
                                tp.varint(ownerEntityId);
                                tp.f64(tx); tp.f64(ty); tp.f64(tz);
                                tp.i8(static_cast<int8_t>(ownerYaw*256.f/360.f));
                                tp.i8(static_cast<int8_t>(ownerPitch*256.f/360.f));
                                tp.boolean(false);
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
                            if ((pr->kind == ProjectileKind::Arrow || pr->kind == ProjectileKind::Trident) && pr->piercingLevel <= 0) {
                                DamageSource asrc(pr->kind == ProjectileKind::Arrow ? "arrow" : "trident");
                                if (CombatManager::tryShieldBlock(*this, *pp.entity, asrc, pr->x, pr->z, false)) {
                                    hitSomething = true; break;
                                }
                            }
                            if (std::find(pr->piercedIds.begin(), pr->piercedIds.end(), pp.entityId) != pr->piercedIds.end())
                                continue;
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
                                for(int y2=ty+1; y2<kMaxY; ++y2){
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
                        effect->amplifier = std::max<std::int8_t>(effect->amplifier, 0);
                        effect->durationTicks = std::max(effect->durationTicks,
                                                         kLevitationDuration);
                    }
                    effectPacket.varint(h.player->entityId);
                    effectPacket.varint(effects::Levitation);
                    effectPacket.varint(0);
                    effectPacket.varint(kLevitationDuration);
                    effectPacket.u8(effectFlags(*effect));
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
            applyDamageToMob(*h.mob, h.dmg, damageType);
            std::int32_t mobEntityId = 0;
            std::int8_t mobDimension = 0;
            double mobX = 0.0;
            double mobY = 0.0;
            double mobZ = 0.0;
            MobKind mobKind = MobKind::Pig;
            bool mobDead = false;
            {
                std::lock_guard mobLock(*h.mob->stateMtx);
                mobDead = h.mob->dead;
                mobEntityId = h.mob->entityId;
                mobDimension = canonicalDimension(h.mob->dimension);
                mobX = h.mob->x;
                mobY = h.mob->y;
                mobZ = h.mob->z;
                mobKind = h.mob->kind;
            }
            if (mobDead) {
                WriteBuffer rm; rm.varint(1); rm.varint(mobEntityId);
                broadcastPacketExceptInDimension(mobDimension, nullptr,
                                                 pl::sc::RemoveEntities, rm);
                const auto drop = MobEntity::dropFor(mobKind);
                if (drop.itemId)
                    spawnItemDropFor(mobDimension, mobX,
                                     mobY + .4, mobZ, drop.itemId,
                                     drop.count);
                {
                    std::lock_guard lk(entsMtx_);
                    mobs_.erase(std::remove(mobs_.begin(), mobs_.end(), h.mob),
                                mobs_.end());
                }
                eraseMobAi(mobEntityId);
                invalidateJvmMob(h.mob);
            }
        }
    }
    for (const auto& gone : despawn) {
        WriteBuffer rm; rm.varint(1); rm.varint(gone.entityId);
        broadcastPacketExceptInDimension(gone.dimension, nullptr,
                                         pl::sc::RemoveEntities, rm);
    }
}
void GameServer::minecartsTick() {
    std::vector<std::shared_ptr<MobEntity>> carts;
    for (const auto& m : mobsSnapshot()) {
        if (!m) continue;
        std::lock_guard mobLock(*m->stateMtx);
        if (MobEntity::isMinecartKind(m->kind)) carts.push_back(m);
    }
    for (auto &cart : carts) {
        if (!cart) continue;
        std::unique_lock cartLock(*cart->stateMtx);
        if (cart->dead) continue;
        const auto dimension = canonicalDimension(cart->dimension);
        World& world = worldFor(dimension);
        // Find rail under or at cart pos (check y, y-1, y+1 per vanilla)
        int bx = static_cast<int>(std::floor(cart->x));
        int by = static_cast<int>(std::floor(cart->y));
        int bz = static_cast<int>(std::floor(cart->z));
        std::uint16_t railState = 0;
        const gen::BlockDef* railDef = nullptr;
        int rx=bx, ry=by, rz=bz;
        std::string railShape="north_south";
        std::string railName;
        bool found=false;
        for (int dy : {0,-1,1}) {
            std::uint16_t st = world.getBlock(bx, by+dy, bz);
            const gen::BlockDef* bd = gen::blockByState(st);
            if (!bd) continue;
            std::string n(bd->name);
            if (n=="minecraft:rail" || n=="minecraft:powered_rail" || n=="minecraft:detector_rail" || n=="minecraft:activator_rail") {
                railState = st; railDef = bd; ry = by+dy; rx=bx; rz=bz; railName=n; found=true;
                for (auto &pr : gen::propsOf(st)) if (pr.first=="shape") railShape=std::string(pr.second);
                break;
            }
        }
        // Detector rail: powered when cart on it
        if (found && railName=="minecraft:detector_rail") {
            poweredDetectorRailsByDimension_[dimension == 0 ? 0 :
                                             (dimension < 0 ? 1 : 2)]
                .insert(posKey(rx, ry, rz));
            bool curPowered=false;
            for (auto &pr : gen::propsOf(railState)) if (pr.first=="powered" && pr.second=="true") curPowered=true;
            bool wantPowered = true; // cart present implies powered
            // Check distance: cart must be within 0.3 of center to count? Use 0.5 for simplicity always true if found
            if (curPowered != wantPowered) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for (auto &pr : gen::propsOf(railState)) if (pr.first!="powered") props.emplace_back(pr.first, pr.second);
                props.emplace_back("powered", wantPowered?"true":"false");
                std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*railDef, props));
                world.setBlock(rx, ry, rz, ns);
                // analog output 15 implied via emissionLevel when powered true
            }
        }
        // If not on rail, apply free physics (gravity + friction)
        if (!found) {
            cart->velY -= 0.04; // gravity
            cart->velX *= 0.98; cart->velY *= 0.98; cart->velZ *= 0.98;
            // ground check
            if (world.getBlock(bx, by-1, bz) != 0) {
                if (cart->velY < 0) cart->velY = 0;
                cart->velX *= 0.7; cart->velZ *= 0.7;
            }
            cart->x += cart->velX; cart->y += cart->velY; cart->z += cart->velZ;
        } else {
            // On rail: snap y to rail top and apply rail-directed movement
            double targetY = ry + 0.125 + 0.5; // rail top ~0.625 above block? vanilla 0.5; use 0.5
            if (std::abs(cart->y - targetY) > 0.1) cart->y = targetY;
            // Powered rail boost
            if (railName=="minecraft:powered_rail") {
                bool powered=false;
                for (auto &pr : gen::propsOf(railState)) if (pr.first=="powered" && pr.second=="true") powered=true;
                if (powered) {
                    double ax=0, az=0;
                    if (railShape=="north_south" || railShape=="ascending_north" || railShape=="ascending_south") {
                        // Z axis
                        double dir = (cart->velZ >= 0) ? 1.0 : -1.0;
                        if (std::abs(cart->velZ) < 0.01) dir = 1.0; // default north->south
                        az = dir * 0.06;
                        // ascending adds Y
                        if (railShape=="ascending_north" || railShape=="ascending_south") cart->velY += 0.04;
                    } else if (railShape=="east_west" || railShape=="ascending_east" || railShape=="ascending_west") {
                        double dir = (cart->velX >= 0) ? 1.0 : -1.0;
                        if (std::abs(cart->velX) < 0.01) dir = 1.0;
                        ax = dir * 0.06;
                        if (railShape=="ascending_east" || railShape=="ascending_west") cart->velY += 0.04;
                    } else {
                        // curved: accelerate along dominant axis
                        if (std::abs(cart->velX) > std::abs(cart->velZ)) ax = (cart->velX >=0?1:-1)*0.06;
                        else az = (cart->velZ >=0?1:-1)*0.06;
                    }
                    cart->velX += ax; cart->velZ += az;
                    // clamp speed
                    double speed = std::sqrt(cart->velX*cart->velX + cart->velZ*cart->velZ);
                    if (speed > 0.4) { double f=0.4/speed; cart->velX*=f; cart->velZ*=f; }
                    {
                        WriteBuffer vb;
                        vb.varint(cart->entityId);
                        vb.i16(static_cast<std::int16_t>(cart->velX * 8000));
                        vb.i16(static_cast<std::int16_t>(cart->velY * 8000));
                        vb.i16(static_cast<std::int16_t>(cart->velZ * 8000));
                        broadcastPacketExceptInDimension(dimension, nullptr,
                                                         proto::pl::sc::EntityVelocity, vb);
                    }
                } else {
                    // unpowered powered rail slows down
                    cart->velX *= 0.5; cart->velZ *= 0.5;
                }
            }
            // Activator rail eject
            if (railName=="minecraft:activator_rail") {
                bool powered=false;
                for (auto &pr : gen::propsOf(railState)) if (pr.first=="powered" && pr.second=="true") powered=true;
                if (powered && cart->riderEntityId != -1) {
                    // eject rider
                    int rider = cart->riderEntityId;
                    cart->riderEntityId = -1;
                    // find rider mob/player and clear vehicleId
                    for (const auto& m : mobsSnapshot()) {
                        if (!m) continue;
                        std::lock_guard riderLock(*m->stateMtx);
                        if (m->entityId == rider) {
                            m->vehicleId = -1;
                            break;
                        }
                    }
                    for (const auto& pp : playersSnapshot()) {
                        if (!pp) continue;
                        std::lock_guard playerLock(pp->stateMtx);
                        if (pp->entityId == rider) {
                            pp->vehicleId = -1;
                            break;
                        }
                    }
                    WriteBuffer passengers;
                    passengers.varint(cart->entityId);
                    passengers.varint(0);
                    broadcastPacketExceptInDimension(dimension, nullptr,
                                                     proto::pl::sc::SetPassengers,
                                                     passengers);
                    // also try to move rider slightly off
                }
            }
            // General rail friction and motion
            cart->velX *= 0.98; cart->velY *= 0.98; cart->velZ *= 0.98;
            // Apply movement along rail shape (constrain to rail axis)
            if (railShape=="north_south" || railShape=="ascending_north" || railShape=="ascending_south") {
                cart->velX *= 0.9; // damp X
                // keep Z
            } else if (railShape=="east_west" || railShape=="ascending_east" || railShape=="ascending_west") {
                cart->velZ *= 0.9;
            } else if (railShape=="south_east" || railShape=="north_west" || railShape=="south_west" || railShape=="north_east") {
                // curved: reduce speed a bit
                cart->velX *= 0.9; cart->velZ *= 0.9;
            }
            cart->x += cart->velX;
            cart->y += cart->velY;
            cart->z += cart->velZ;
            // Snap X/Z to rail center for straight rails
            if (railShape=="north_south") cart->x = rx + 0.5;
            else if (railShape=="east_west") cart->z = rz + 0.5;
            if (railShape=="ascending_east" || railShape=="ascending_west") cart->z = rz + 0.5;
            if (railShape=="ascending_north" || railShape=="ascending_south") cart->x = rx + 0.5;
        }
        // Broadcast movement if moved
        if (!cart->hasSent || std::abs(cart->x-cart->sentX)+std::abs(cart->y-cart->sentY)+std::abs(cart->z-cart->sentZ) > 0.01) {
            WriteBuffer b;
            b.varint(cart->entityId);
            b.i16(static_cast<std::int16_t>((cart->x-cart->sentX)*4096));
            b.i16(static_cast<std::int16_t>((cart->y-cart->sentY)*4096));
            b.i16(static_cast<std::int16_t>((cart->z-cart->sentZ)*4096));
            b.i8(0); b.i8(0);
            b.boolean(true);
            broadcastPacketExceptInDimension(dimension, nullptr,
                                             proto::pl::sc::MoveEntityPosRot, b);
            cart->sentX = cart->x; cart->sentY = cart->y; cart->sentZ = cart->z; cart->hasSent = true;
        }
        // Handle detector rail unpower when cart left (scan nearby rails for no cart)
        // Do second pass for detector rails near previous position? Simplified: leave powered true while cart exists; will be cleared by next tick when no cart nearby if we scan.
    }
    // Clear every detector rail we have touched when no cart remains on its
    // exact block.  This also handles the final cart being removed or moved
    // between ticks, which a scan around current cart positions cannot see.
    for (int dimIndex = 0; dimIndex < 3; ++dimIndex) {
        const std::int8_t dimension = dimIndex == 0 ? 0 :
            (dimIndex == 1 ? static_cast<std::int8_t>(-1) :
                             static_cast<std::int8_t>(1));
        World& world = worldFor(dimension);
        auto& known = poweredDetectorRailsByDimension_[dimIndex];
        for (auto it = known.begin(); it != known.end();) {
            const auto key = *it;
            const int nx = posKeyUnpackX(key);
            const int ny = posKeyUnpackY(key);
            const int nz = posKeyUnpackZ(key);
            const std::uint16_t st = world.getBlock(nx, ny, nz);
            const gen::BlockDef* bd = gen::blockByState(st);
            if (!bd || std::string(bd->name) != "minecraft:detector_rail") {
                it = known.erase(it);
                continue;
            }
            bool powered = false;
            for (auto& pr : gen::propsOf(st))
                if (pr.first == "powered" && pr.second == "true") powered = true;
            bool hasCart = false;
            for (const auto& cart : carts) {
                if (!cart) continue;
                std::lock_guard cartLock(*cart->stateMtx);
                if (canonicalDimension(cart->dimension) != dimension) continue;
                const int cbx = static_cast<int>(std::floor(cart->x));
                const int cby = static_cast<int>(std::floor(cart->y));
                const int cbz = static_cast<int>(std::floor(cart->z));
                for (int ddy : {0, -1, 1})
                    if (cbx == nx && cby + ddy == ny && cbz == nz)
                        hasCart = true;
            }
            if (!hasCart && powered) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for (auto& pr : gen::propsOf(st))
                    if (pr.first != "powered") props.emplace_back(pr.first, pr.second);
                props.emplace_back("powered", "false");
                const std::uint16_t ns = static_cast<std::uint16_t>(
                    gen::stateWithProps(*bd, props));
                world.setBlock(nx, ny, nz, ns);
            }
            ++it;
        }
    }
}
void GameServer::boatsTick() {
    std::vector<std::shared_ptr<MobEntity>> boats;
    for (const auto& m : mobsSnapshot()) {
        if (!m) continue;
        std::lock_guard mobLock(*m->stateMtx);
        if (MobEntity::isBoat(m->kind)) boats.push_back(m);
    }
    for (auto &b : boats) {
        if (!b) continue;
        std::unique_lock boatLock(*b->stateMtx);
        if (b->dead) continue;
        const auto dimension = canonicalDimension(b->dimension);
        World& world = worldFor(dimension);
        int bx=(int)std::floor(b->x), by=(int)std::floor(b->y), bz=(int)std::floor(b->z);
        auto stBelow = world.getBlock(bx, by-1, bz);
        const gen::BlockDef* dBelow = gen::blockByState(stBelow);
        bool inWater = FluidSim::getFluidState(world, bx, by, bz).isWater();
        bool onLand = false;
        if (!inWater && dBelow && dBelow->name!="minecraft:air" && dBelow->name!="minecraft:water") onLand=true;
        if (inWater) {
            double waterY = by + 0.35;
            if (b->y < waterY) b->velY += 0.05;
            else if (b->y > waterY+0.2) b->velY -= 0.05;
            else b->velY *= 0.6;
            b->velX *= 0.90; b->velZ *= 0.90;
            b->velY *= 0.90;
        } else if (onLand) {
            b->velY -= 0.05;
            b->velX *= 0.60; b->velZ *= 0.60;
            b->velY *= 0.6;
            if (world.getBlock(bx, by-1, bz)!=0 && b->velY<0) b->velY=0;
        } else {
            b->velY -= 0.05;
            b->velX *= 0.98; b->velY *= 0.98; b->velZ *= 0.98;
        }
        double horiz = std::sqrt(b->velX*b->velX + b->velZ*b->velZ);
        if (horiz>0.4){ double f=0.4/horiz; b->velX*=f; b->velZ*=f; }
        b->x += b->velX; b->y += b->velY; b->z += b->velZ;
        if (inWater && std::abs(b->y - (by+0.35))<0.1) b->y = by+0.35;
        if (!b->hasSent || std::abs(b->x-b->sentX)+std::abs(b->y-b->sentY)+std::abs(b->z-b->sentZ)>0.01){
            WriteBuffer pkt;
            pkt.varint(b->entityId);
            pkt.i16((int16_t)((b->x-b->sentX)*4096));
            pkt.i16((int16_t)((b->y-b->sentY)*4096));
            pkt.i16((int16_t)((b->z-b->sentZ)*4096));
            pkt.i8(0); pkt.i8(0); pkt.boolean(true);
            broadcastPacketExceptInDimension(dimension, nullptr,
                                             proto::pl::sc::MoveEntityPosRot,
                                             pkt);
            b->sentX=b->x; b->sentY=b->y; b->sentZ=b->z; b->hasSent=true;
        }
    }
}
} // namespace cppfm
