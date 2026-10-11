#include "GameServer.hpp"
#include "Session.hpp"
#include "GameServerHelpers.hpp"
#include "PlayerCollision.hpp"
#include "BlockEvent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MetadataTypes.hpp"
#include "PositionPackets.hpp"
#include "PlayerEntityState.hpp"
#include "SessionInteractionSupport.hpp"
#include "Constants.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../generated/ItemIds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cppfm {
using namespace proto;

void Session::onMovement(ReadBuffer& input, const bool hasPosition,
                         const bool hasRotation,
                         SimulationDispatchGuard& simulationLock) {
    const auto movement = session_detail::snapshotPlayerForSession(*self_);
    // Ignore already-queued frames until the client confirms the latest
    // server-side teleport. They describe the player's previous position.
    if (movement.awaitingTeleportConfirmation) return;

    const double oldX = movement.x;
    const double oldY = movement.y;
    const double oldZ = movement.z;
    const double nextX = hasPosition ? input.f64() : movement.x;
    const double nextY = hasPosition ? input.f64() : movement.y;
    const double nextZ = hasPosition ? input.f64() : movement.z;
    const float nextYaw = hasRotation ? input.f32() : movement.yaw;
    const float nextPitch = hasRotation ? input.f32() : movement.pitch;
    const std::uint8_t moveFlags = input.u8();
    const bool onGround = (moveFlags & 0x01) != 0;

    if (hasPosition) {
        const double dx = nextX - oldX;
        const double dy = nextY - oldY;
        const double dz = nextZ - oldZ;
        const bool finitePosition = std::isfinite(nextX) &&
            std::isfinite(nextY) && std::isfinite(nextZ);
        const bool finiteRotation = std::isfinite(nextYaw) &&
            std::isfinite(nextPitch);
        // Keep border/range checks ahead of block-coordinate conversion so
        // huge finite client coordinates never reach the collision sampler.
        const bool insidePlayableBounds = finitePosition &&
            nextY >= -2048.0 && nextY <= 2048.0 &&
            srv_.isInsideBorder(nextX, nextZ);
        // Match the server's conservative ten-block per-packet displacement
        // envelope while leaving ordinary correction/lag frames untouched.
        const bool movedTooQuickly = dx * dx + dy * dy + dz * dz > 100.0;
        const double collisionHeight = movement.sneaking ? 1.5 : 1.8;
        const bool enteredCollision = insidePlayableBounds &&
            !movedTooQuickly &&
            movement.gamemode != 3 &&
            player_collision::intersectsSwept(
                srv_.worldFor(movement.dimension), oldX, oldY, oldZ,
                nextX, nextY, nextZ, collisionHeight);
        if (!finitePosition || !finiteRotation || !insidePlayableBounds ||
            movedTooQuickly || enteredCollision) {
            sendTeleport(oldX, oldY, oldZ, movement.yaw, movement.pitch);
            return;
        }
    } else if (!std::isfinite(nextYaw) || !std::isfinite(nextPitch)) {
        sendTeleport(oldX, oldY, oldZ, movement.yaw, movement.pitch);
        return;
    }

    {
        // Fabric getters can read position from a callback thread. Publish the
        // coordinate tuple under the same lock used by the JVM bridge so a
        // reader never observes a torn movement update.
        std::lock_guard playerLock(self_->stateMtx);
        if (hasPosition) {
            if (!self_->onGround && nextY < self_->y &&
                movement.gamemode == 0)
                self_->fallDist += self_->y - nextY;
            self_->x = nextX;
            self_->y = nextY;
            self_->z = nextZ;
        }
        if (hasRotation) {
            self_->yaw = nextYaw;
            self_->pitch = nextPitch;
        }
    }

    updateMovementPhysics(oldX, oldY, oldZ, movement.onGround, onGround,
                          hasPosition, simulationLock);
    if (hasPosition) freezeWaterWithFrostWalker();
    updateGroundMovementEnchantments();
    broadcastMovement();
    handleMovementPortal(simulationLock);

    if (!self_->spawned) return;
    if (!chunksStreamed_) streamInitialChunks(simulationLock);
    else tickChunksAround(self_->x, self_->z, simulationLock);
}

void Session::updateMovementPhysics(const double oldX, const double oldY,
                                    const double oldZ,
                                    const bool wasOnGround,
                                    const bool nowGround,
                                    const bool hasPosition,
                                    SimulationDispatchGuard& simulationLock) {
    if (hasPosition) {
        if (nowGround && !self_->onGround)
            resolveMovementLanding(simulationLock);
        if (nowGround) self_->fallDist = 0.0F;

        if (self_->gamemode == 0) {
            const double dx = self_->x - oldX;
            const double dz = self_->z - oldZ;
            const double horizontalDistance = std::sqrt(dx * dx + dz * dz);
            if (horizontalDistance > 0.001) {
                const float multiplier = self_->isSwimming ? 0.01F
                    : (self_->isSprinting ? 0.10F : 0.0F);
                self_->exhaustion +=
                    static_cast<float>(horizontalDistance) * multiplier;
            }

            const double verticalDelta = self_->y - oldY;
            if (wasOnGround && !nowGround && verticalDelta > 0.05) {
                const float jumpCost = self_->isSprinting ? 0.2F : 0.05F;
                srv_.addHungerExhaustion(*self_, jumpCost);
            }
        }
        self_->spawned = true;
    }

    self_->onGround = nowGround;
    if (!self_->onGround && self_->gamemode == 0) {
        if (self_->fallDist < 0.5F) ++self_->flyingTicks;
        else self_->flyingTicks = 0;
        self_->isFlying = self_->flyingTicks > 10;
        if (!srv_.config().allowFlight && self_->flyingTicks > 80) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick,
                                    "Flying is not enabled on this server");
            if (self_->conn) {
                self_->conn->trySendPacket(pl::sc::Disconnect, kick);
                self_->conn->close();
            }
        }
    } else {
        self_->flyingTicks = 0;
        self_->isFlying = false;
    }
}

void Session::resolveMovementLanding(
    SimulationDispatchGuard& simulationLock) {
    if (self_->fallDist > 0.5F && !self_->isSneaking)
        trampleFarmlandOnLanding();

    const int blockX = static_cast<int>(std::floor(self_->x));
    const int blockY = static_cast<int>(std::floor(self_->y - 0.2));
    const int blockZ = static_cast<int>(std::floor(self_->z));
    const auto blockState =
        srv_.worldFor(self_->dimension).getBlock(blockX, blockY, blockZ);

    blockEventDispatcher().onEntityLand(
        self_.get(), blockX, blockY, blockZ, blockState, self_->fallDist);
    api::EntityLandEvent event;
    event.entity = self_.get();
    event.x = blockX;
    event.y = blockY;
    event.z = blockZ;
    event.blockState = blockState;
    event.fallDistance = self_->fallDist;
    api::events().entityLand.fire(event);

    if (self_->fallDist > 3.0F && !isFallMitigated() &&
        srv_.gamerules_.getBool("fallDamage")) {
        srv_.applyDamage(
            *self_, static_cast<float>(std::floor(self_->fallDist - 3.0F)),
            DamageSource("fall"), 0, &simulationLock);
    }
    self_->fallDist = 0.0F;
}

bool Session::isFallMitigated() const {
    if (self_->gamemode == 1 || self_->gamemode == 3) return true;

    const int blockX = static_cast<int>(std::floor(self_->x));
    const int blockY = static_cast<int>(std::floor(self_->y - 0.2));
    const int blockZ = static_cast<int>(std::floor(self_->z));
    const auto state =
        srv_.worldFor(self_->dimension).getBlock(blockX, blockY, blockZ);
    const auto* block = gen::blockByState(state);
    if (!block) return false;

    const std::string_view name(block->name);
    if (name == "minecraft:water" || name == "minecraft:slime_block" ||
        name == "minecraft:honey_block" || name == "minecraft:hay_block")
        return true;
    if (name != "minecraft:powder_snow") return false;
    return std::any_of(self_->effects.begin(), self_->effects.end(),
        [](const EffectInstance& effect) {
            return effect.type == effects::SlowFalling;
        });
}

void Session::trampleFarmlandOnLanding() {
    const int blockX = static_cast<int>(std::floor(self_->x));
    const int blockY = static_cast<int>(std::floor(self_->y - 0.2));
    const int blockZ = static_cast<int>(std::floor(self_->z));
    World& world = srv_.worldFor(self_->dimension);
    const std::uint16_t state = world.getBlock(blockX, blockY, blockZ);
    const auto* farmland = gen::blockByState(state);
    if (!farmland || std::string_view(farmland->name) !=
                         "minecraft:farmland")
        return;

    const float probability = static_cast<float>(
        std::clamp(self_->fallDist - 0.5, 0.0, 1.0));
    const bool tramples = probability >= 1.0F ||
        static_cast<float>(nextRandom()) / static_cast<float>(RAND_MAX) <
            probability;
    if (!tramples) return;

    bool hasMoisture = false;
    for (const auto& [key, value] : gen::propsOf(state)) {
        (void)value;
        if (key == "moisture") {
            hasMoisture = true;
            break;
        }
    }

    const int cropY = blockY + 1;
    const auto cropState = world.getBlock(blockX, cropY, blockZ);
    if (cropState != 0) {
        const auto* crop = gen::blockByState(cropState);
        if (crop) {
            const std::string_view name(crop->name);
            const bool isCrop = name.find("wheat") != std::string_view::npos ||
                name.find("carrots") != std::string_view::npos ||
                name.find("potatoes") != std::string_view::npos ||
                name.find("beetroots") != std::string_view::npos;
            if (isCrop) {
                const auto item = gen::itemIdByName().find(crop->name);
                if (item != gen::itemIdByName().end()) {
                    srv_.spawnItemDropFor(
                        self_->dimension, blockX + 0.5, cropY + 0.2,
                        blockZ + 0.5, item->second, 1, 0, 0.1, 0);
                }
                world.setBlock(blockX, cropY, blockZ, 0);
                srv_.broadcastBlockChangeFor(
                    self_->dimension, blockX, cropY, blockZ, 0);
            }
        }
    }

    if (hasMoisture) {
        std::vector<std::pair<std::string_view, std::string_view>> properties;
        for (const auto& [key, value] : gen::propsOf(state))
            if (key != "moisture") properties.emplace_back(key, value);
        properties.emplace_back("moisture", "0");
        const auto dryState = static_cast<std::uint16_t>(
            gen::stateWithProps(*farmland, properties));
        world.setBlock(blockX, blockY, blockZ, dryState);
        srv_.broadcastBlockChangeFor(
            self_->dimension, blockX, blockY, blockZ, dryState);
    }

    const auto dirt = gen::blockNameToState().find("minecraft:dirt");
    if (dirt != gen::blockNameToState().end()) {
        const auto dirtState = static_cast<std::uint16_t>(dirt->second);
        world.setBlock(blockX, blockY, blockZ, dirtState);
        srv_.broadcastBlockChangeFor(
            self_->dimension, blockX, blockY, blockZ, dirtState);
    }
    srv_.broadcastWorldEventFor(
        self_->dimension, 2001, blockX, blockY, blockZ,
        static_cast<std::int32_t>(state), false);
}

void Session::freezeWaterWithFrostWalker() {
    if (!self_->onGround) return;
    const bool hasFrostWalker = std::any_of(
        self_->inv.begin() + 5, self_->inv.begin() + 9,
        [](const ItemStack& stack) {
            return !stack.empty() && EnchantmentHelper::hasFrostWalker(stack);
        });
    if (!hasFrostWalker) return;

    World& world = srv_.worldFor(self_->dimension);
    const int centerX = static_cast<int>(std::floor(self_->x));
    const int y = static_cast<int>(std::floor(self_->y - 0.5));
    const int centerZ = static_cast<int>(std::floor(self_->z));
    int level = 0;
    for (int slot = 5; slot <= 8; ++slot)
        if (!self_->inv[slot].empty())
            level = std::max(level,
                EnchantmentHelper::frostWalkerLevel(self_->inv[slot]));
    if (level == 0) {
        for (int slot = 5; slot <= 8; ++slot)
            if (!self_->inv[slot].empty())
                level = std::max(level,
                    self_->inv[slot].enchantLevel("frost_walker"));
    }
    const int radius = 2 + level;
    const auto frost = gen::blockNameToState().find("minecraft:frosted_ice");
    if (frost == gen::blockNameToState().end()) return;
    const auto frostedIce = static_cast<std::uint16_t>(frost->second);

    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (dx * dx + dz * dz > radius * radius) continue;
            const int blockX = centerX + dx;
            const int blockZ = centerZ + dz;
            const auto state = world.getBlock(blockX, y, blockZ);
            const auto* block = gen::blockByState(state);
            if (!block || std::string_view(block->name) != "minecraft:water")
                continue;
            const auto waterProperties = gen::propsOf(state);
            const bool sourceWater = std::any_of(
                waterProperties.begin(), waterProperties.end(),
                [](const auto& property) {
                    return property.first == "level" &&
                           property.second == "0";
                });
            if (!sourceWater || world.getBlock(blockX, y + 1, blockZ) != 0)
                continue;
            world.setBlock(blockX, y, blockZ, frostedIce);
            srv_.broadcastBlockChangeFor(
                self_->dimension, blockX, y, blockZ, frostedIce);
        }
    }
}

void Session::updateGroundMovementEnchantments() {
    World& world = srv_.worldFor(self_->dimension);
    const int blockX = static_cast<int>(std::floor(self_->x));
    const int blockY = static_cast<int>(std::floor(self_->y - 0.2));
    const int blockZ = static_cast<int>(std::floor(self_->z));
    const auto state = world.getBlock(blockX, blockY, blockZ);
    const auto* block = gen::blockByState(state);
    const bool onSoulBlock = block &&
        (std::string_view(block->name) == "minecraft:soul_sand" ||
         std::string_view(block->name) == "minecraft:soul_soil");

    int soulSpeedLevel = 0;
    int swiftSneakLevel = 0;
    for (int slot = 5; slot <= 8; ++slot) {
        const auto& armor = self_->inv[slot];
        if (armor.empty()) continue;
        soulSpeedLevel = std::max(
            soulSpeedLevel, EnchantmentHelper::soulSpeedLevel(armor));
        if (armor.name().find("leggings") != std::string::npos)
            swiftSneakLevel = std::max(
                swiftSneakLevel, EnchantmentHelper::swiftSneakLevel(armor));
    }
    if (swiftSneakLevel == 0) {
        for (int slot = 5; slot <= 8; ++slot)
            if (!self_->inv[slot].empty())
                swiftSneakLevel = std::max(
                    swiftSneakLevel,
                    EnchantmentHelper::swiftSneakLevel(self_->inv[slot]));
    }

    const double oldSpeed = self_->attributes.getValue(
        Attribute::MOVEMENT_SPEED);
    self_->attributes.syncEnchantSpeed(
        soulSpeedLevel, swiftSneakLevel, self_->isSneaking, onSoulBlock);
    const double newSpeed = self_->attributes.getValue(
        Attribute::MOVEMENT_SPEED);
    if (std::abs(oldSpeed - newSpeed) > 1e-9) {
        WriteBuffer update;
        self_->attributes.writeUpdate(update, self_->entityId);
        if (self_->conn)
            self_->conn->trySendPacket(pl::sc::UpdateAttributes, update);
        srv_.serializePlayerTrackingPublication([&] {
            srv_.reconcilePlayerTrackingForChangedPlayerUnsequenced(*self_);
            srv_.broadcastTrackedPlayerPacketUnsequenced(
                *self_, pl::sc::UpdateAttributes, update);
        });
    }

    if (!onSoulBlock || soulSpeedLevel == 0 || self_->isSneaking ||
        nextRandom() % 60 != 0)
        return;
    for (int slot = 5; slot <= 8; ++slot) {
        auto& armor = self_->inv[slot];
        if (armor.empty() || !armor.isArmor() ||
            EnchantmentHelper::soulSpeedLevel(armor) <= 0)
            continue;
        if (DamageComponent::applyDamage(armor, 1))
            armor = ItemStack::air();
        srv_.resendInventory(*self_);
        break;
    }
}

void Session::handleMovementPortal(
    SimulationDispatchGuard& simulationLock) {
    if (srv_.tickNow() <= self_->portalCooldownUntilTick) return;

    World& currentWorld = srv_.worldFor(self_->dimension);
    const int blockX = static_cast<int>(std::floor(self_->x));
    const int blockY = static_cast<int>(std::floor(self_->y));
    const int blockZ = static_cast<int>(std::floor(self_->z));
    bool inNetherPortal = false;
    bool inEndPortal = false;
    for (int offsetY = 0; offsetY <= 1; ++offsetY) {
        const auto state = currentWorld.getBlock(
            blockX, blockY + offsetY, blockZ);
        const auto* block = gen::blockByState(state);
        if (!block) continue;
        const std::string_view name(block->name);
        inNetherPortal |= name == "minecraft:nether_portal";
        inEndPortal |= name == "minecraft:end_portal";
    }

    std::int8_t targetDimension = 127;
    if (inNetherPortal) {
        if (self_->dimension == 0) targetDimension = -1;
        else if (self_->dimension == -1 || self_->dimension == 1)
            targetDimension = 0;
    } else if (inEndPortal) {
        if (self_->dimension == 0) targetDimension = 1;
        else if (self_->dimension == 1 || self_->dimension == -1)
            targetDimension = 0;
    }
    if (targetDimension == 127) return;

    const auto sourceDimension = GameServer::canonicalDimension(
        self_->dimension);
    if (!PortalHandler::tryTeleport(
            srv_, *self_, targetDimension, simulationLock))
        return;

    {
        std::lock_guard transitionLock(self_->digTransitionMtx);
        // A dimension change invalidates in-flight callbacks even if the dig
        // was already inactive when this transition began.
        const auto ended = self_->digProgress.cancel(std::nullopt, true);
        if (ended && ended->active)
            srv_.broadcastDigStageAt(
                *self_, sourceDimension, ended->x, ended->y, ended->z, -1);
        self_->entityTracking.clear();
        lastCx_ = INT32_MAX;
        lastCz_ = INT32_MAX;
        // PortalHandler already removed the source tracking entry while hiding the
        // player from join snapshots. Re-enable and publish at the destination now.
        publishPlayerTrackingAfterDimensionTransfer();
    }
    if (auto* runtime = srv_.jvmRuntime())
        runtime->onPlayerWorldChange(*self_, sourceDimension,
                                     targetDimension);
}

void Session::broadcastMovement() {
    srv_.serializePlayerTrackingPublication([&] {
        std::int32_t entityId = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        bool onGround = false;
        bool spawned = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            entityId = self_->entityId;
            x = self_->x;
            y = self_->y;
            z = self_->z;
            yaw = self_->yaw;
            pitch = self_->pitch;
            onGround = self_->onGround;
            spawned = self_->spawned;
        }
        if (!spawned) return;

        // Publish movement to the viewers that already track this player
        // before reconciling visibility. A viewer entering range on this very
        // movement packet must get a current-position spawn, not that spawn
        // followed by a delta measured from the subject's old global baseline.
        if (!hasSent_) {
            const auto absolutePosition = makeEntityTeleportBody(
                entityId, x, y, z, 0.0, 0.0, 0.0, yaw, pitch, 0, onGround);
            srv_.broadcastTrackedPlayerPacketUnsequenced(
                *self_, pl::sc::EntityTeleport, absolutePosition);
            sentX_ = x;
            sentY_ = y;
            sentZ_ = z;
            sentYaw_ = yaw;
            sentPitch_ = pitch;
            hasSent_ = true;
            srv_.reconcilePlayerTrackingForChangedPlayerUnsequenced(*self_);
            return;
        }

        const double dx = x - sentX_;
        const double dy = y - sentY_;
        const double dz = z - sentZ_;
        const bool rotated = yaw != sentYaw_ || pitch != sentPitch_;
        constexpr double kMaxRelativeDelta = 7.999;
        const bool hasTranslation = dx * dx + dy * dy + dz * dz > 0.0001;

        if (hasTranslation && std::abs(dx) < kMaxRelativeDelta &&
            std::abs(dy) < kMaxRelativeDelta &&
            std::abs(dz) < kMaxRelativeDelta) {
            WriteBuffer movement;
            movement.varint(entityId);
            movement.i16(static_cast<std::int16_t>(dx * 4096));
            movement.i16(static_cast<std::int16_t>(dy * 4096));
            movement.i16(static_cast<std::int16_t>(dz * 4096));
            if (rotated) {
                movement.i8(constants::protocolSignedAngleByte(yaw));
                movement.i8(constants::protocolSignedAngleByte(pitch));
                movement.boolean(onGround);
                srv_.broadcastTrackedPlayerPacketUnsequenced(
                    *self_, pl::sc::MoveEntityPosRot, movement);
            } else {
                movement.boolean(onGround);
                srv_.broadcastTrackedPlayerPacketUnsequenced(
                    *self_, pl::sc::MoveEntityPos, movement);
            }
            WriteBuffer headRotation;
            headRotation.varint(entityId);
            headRotation.i8(constants::protocolSignedAngleByte(yaw));
            srv_.broadcastTrackedPlayerPacketUnsequenced(
                *self_, pl::sc::RotateHead, headRotation);
        } else if (hasTranslation) {
            const auto absolutePosition = makeEntityTeleportBody(
                entityId, x, y, z, 0.0, 0.0, 0.0, yaw, pitch, 0,
                onGround);
            srv_.broadcastTrackedPlayerPacketUnsequenced(
                *self_, pl::sc::EntityTeleport, absolutePosition);
        } else if (rotated) {
            WriteBuffer look;
            look.varint(entityId);
            look.i8(constants::protocolSignedAngleByte(yaw));
            look.i8(constants::protocolSignedAngleByte(pitch));
            look.boolean(onGround);
            srv_.broadcastTrackedPlayerPacketUnsequenced(
                *self_, pl::sc::EntityLook, look);

            WriteBuffer headRotation;
            headRotation.varint(entityId);
            headRotation.i8(constants::protocolSignedAngleByte(yaw));
            srv_.broadcastTrackedPlayerPacketUnsequenced(
                *self_, pl::sc::RotateHead, headRotation);
        }

        sentX_ = x;
        sentY_ = y;
        sentZ_ = z;
        sentYaw_ = yaw;
        sentPitch_ = pitch;
        hasSent_ = true;
        // Existing viewers have received the final delta before any departing
        // viewer is removed; new viewers are introduced only at current state.
        // Spawn publication still observes the target-chunk barrier.
        srv_.reconcilePlayerTrackingForChangedPlayerUnsequenced(*self_);
    });
}

void Session::clearVehiclePassengerLink(const std::int32_t vehicleId,
                                       const std::int32_t playerEntityId,
                                       const std::int8_t dimension) {
    if (vehicleId < 0) return;

    for (const auto& mob : srv_.mobsSnapshot()) {
        if (!mob) continue;
        std::lock_guard entityLock(*mob->stateMtx);
        if (mob->entityId != vehicleId ||
            mob->riderEntityId != playerEntityId)
            continue;
        mob->riderEntityId = -1;
        break;
    }

    srv_.broadcastSetPassengersEmptyFor(dimension, vehicleId);
}

void Session::onPlayerInput(ReadBuffer& in) {
    try {
        float sideways = 0.0F;
        float forward = 0.0F;
        std::uint8_t flags = 0;
        if (in.remaining() >= 9) {
            sideways = in.f32();
            forward = in.f32();
            flags = in.u8();
        } else if (in.remaining() >= 1) {
            flags = in.u8();
        } else {
            in.skipRest();
            return;
        }

        const bool wantSneak = (flags & 0x02) != 0;
        const bool wantJump = (flags & 0x01) != 0;
        std::int32_t vehicleId = -1;
        std::int32_t playerEntityId = 0;
        std::int8_t dimension = 0;
        bool dismounted = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            playerEntityId = self_->entityId;
            if (wantSneak && self_->vehicleId != -1) {
                vehicleId = self_->vehicleId;
                dimension = GameServer::canonicalDimension(self_->dimension);
                self_->vehicleId = -1;
                dismounted = true;
            }
        }
        if (dismounted)
            clearVehiclePassengerLink(vehicleId, playerEntityId, dimension);

        bool riding = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            riding = self_->vehicleId != -1;
        }
        if (wantJump && riding) srv_.handleHorseJump(*self_, 80);
        (void)sideways;
        (void)forward;
    } catch (...) {
        in.skipRest();
    }
}

void Session::onMoveVehicle(ReadBuffer& in) {
    try {
        const double x = in.f64();
        const double y = in.f64();
        const double z = in.f64();
        const float yaw = in.f32();
        const float pitch = in.f32();
        srv_.handleMoveVehicle(*self_, x, y, z, yaw, pitch);
    } catch (...) {
        in.skipRest();
    }
}

void Session::onEntityAction(ReadBuffer& in) {
    const std::int32_t entityId = in.varint();
    const std::int32_t action = in.varint();
    const std::int32_t jumpBoost = in.varint();

    // Entity Action mutates this player while the tick loop may also read it.
    // Publish state under the player lock; repair the vehicle link and emit
    // packets only after releasing model locks.
    bool stateChanged = false;
    bool sneaking = false;
    bool sprinting = false;
    bool attributeChanged = false;
    bool dismounted = false;
    std::int32_t playerEntityId = 0;
    std::int32_t vehicleId = -1;
    std::int8_t playerDimension = 0;
    WriteBuffer poseMetadata;
    WriteBuffer flagsMetadata;
    WriteBuffer attributeUpdate;

    {
        std::lock_guard playerLock(self_->stateMtx);
        if (entityId != self_->entityId) return;

        const bool wasSneaking = self_->isSneaking;
        const bool wasSprinting = self_->isSprinting;
        if (action == 0) self_->isSneaking = true;
        else if (action == 1) self_->isSneaking = false;
        else if (action == 3) self_->isSprinting = true;
        else if (action == 4) self_->isSprinting = false;

        playerEntityId = self_->entityId;
        playerDimension = GameServer::canonicalDimension(self_->dimension);
        sneaking = self_->isSneaking;
        sprinting = self_->isSprinting;
        stateChanged = wasSneaking != sneaking || wasSprinting != sprinting;

        if (action == 0 && self_->vehicleId != -1) {
            vehicleId = self_->vehicleId;
            self_->vehicleId = -1;
            dismounted = true;
        }

        if (stateChanged) {
            if (wasSneaking != sneaking) {
                poseMetadata.varint(playerEntityId);
                meta::writeMetaPose(
                    poseMetadata, 6, playerEntityMetadataState(*self_).pose);
                poseMetadata.u8(255);
            }

            flagsMetadata.varint(playerEntityId);
            meta::writeMetaByte(
                flagsMetadata, 0,
                playerEntityMetadataState(*self_).entityFlags);
            flagsMetadata.u8(255);

            int swiftLevel = 0;
            for (int slot = 5; slot <= 8; ++slot) {
                if (self_->inv[slot].empty()) continue;
                const std::string name = self_->inv[slot].name();
                if (name.find("leggings") != std::string::npos)
                    swiftLevel = std::max(
                        swiftLevel,
                        EnchantmentHelper::swiftSneakLevel(self_->inv[slot]));
            }
            if (swiftLevel == 0) {
                for (int slot = 5; slot <= 8; ++slot) {
                    if (!self_->inv[slot].empty())
                        swiftLevel = std::max(
                            swiftLevel,
                            EnchantmentHelper::swiftSneakLevel(self_->inv[slot]));
                }
            }

            const double before =
                self_->attributes.getValue(Attribute::MOVEMENT_SPEED);
            if (sneaking && swiftLevel > 0)
                self_->attributes.applySwiftSneak(swiftLevel);
            else
                self_->attributes.removeModifier(
                    Attribute::MOVEMENT_SPEED, "swift_sneak");
            const double after =
                self_->attributes.getValue(Attribute::MOVEMENT_SPEED);
            attributeChanged = std::abs(before - after) > 1e-9;
            if (attributeChanged)
                self_->attributes.writeUpdate(attributeUpdate, playerEntityId);
        }
    }

    if (dismounted)
        clearVehiclePassengerLink(vehicleId, playerEntityId, playerDimension);

    const auto sendTrackedPlayerState = [&](std::uint8_t packetId,
                                            const WriteBuffer& body) {
        srv_.serializePlayerTrackingPublication([&] {
            conn_->trySendPacket(packetId, body);
            srv_.reconcilePlayerTrackingForChangedPlayerUnsequenced(*self_);
            srv_.broadcastTrackedPlayerPacketUnsequenced(
                *self_, packetId, body);
        });
    };
    if (!poseMetadata.data.empty())
        sendTrackedPlayerState(pl::sc::SetEntityMetadata, poseMetadata);
    if (!flagsMetadata.data.empty())
        sendTrackedPlayerState(pl::sc::SetEntityMetadata, flagsMetadata);
    if (attributeChanged)
        sendTrackedPlayerState(proto::pl::sc::UpdateAttributes,
                               attributeUpdate);

    // Actions 5/6 only report horse-jump charging; action 7 opens the vehicle
    // inventory. Jump simulation remains on the explicit jump-power path.
    (void)jumpBoost;
}

} // namespace cppfm
