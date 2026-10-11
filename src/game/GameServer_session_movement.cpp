#include "GameServer.hpp"
#include "Session.hpp"
#include "GameServerHelpers.hpp"
#include "PlayerCollision.hpp"
#include "BlockEvent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "PositionPackets.hpp"
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
        srv_.broadcastPacketExceptInDimension(
            self_->dimension, self_.get(), pl::sc::UpdateAttributes, update);
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

    std::lock_guard transitionLock(self_->digTransitionMtx);
    std::int32_t digX = 0;
    std::int32_t digY = 0;
    std::int32_t digZ = 0;
    bool wasDigging = false;
    {
        std::lock_guard digLock(self_->digMtx);
        wasDigging = self_->digActive;
        digX = self_->digX;
        digY = self_->digY;
        digZ = self_->digZ;
        self_->digActive = false;
        self_->digTotalTicks = 0;
        self_->digLastStage = 255;
        ++self_->digRevision;
    }
    if (wasDigging)
        srv_.broadcastDigStageAt(
            *self_, sourceDimension, digX, digY, digZ, -1);
    self_->entityTracking.clear();
    lastCx_ = INT32_MAX;
    lastCz_ = INT32_MAX;
    // PortalHandler already removed the source tracking entry while hiding the
    // player from join snapshots. Re-enable and publish at the destination now.
    publishPlayerTrackingAfterDimensionTransfer();
}

void Session::broadcastMovement() {
    if (!self_->spawned) return;
    if (!hasSent_) {
        const auto absolutePosition = makeEntityTeleportBody(
            self_->entityId, self_->x, self_->y, self_->z,
            0.0, 0.0, 0.0, self_->yaw, self_->pitch, 0, self_->onGround);
        srv_.broadcastPacketExceptInDimension(
            self_->dimension, self_.get(), pl::sc::EntityTeleport,
            absolutePosition);
        sentX_ = self_->x;
        sentY_ = self_->y;
        sentZ_ = self_->z;
        sentYaw_ = self_->yaw;
        sentPitch_ = self_->pitch;
        hasSent_ = true;
        return;
    }

    const double dx = self_->x - sentX_;
    const double dy = self_->y - sentY_;
    const double dz = self_->z - sentZ_;
    const bool rotated = self_->yaw != sentYaw_ || self_->pitch != sentPitch_;
    constexpr double kMaxRelativeDelta = 7.999;
    const bool hasTranslation = dx * dx + dy * dy + dz * dz > 0.0001;

    if (hasTranslation && std::abs(dx) < kMaxRelativeDelta &&
        std::abs(dy) < kMaxRelativeDelta &&
        std::abs(dz) < kMaxRelativeDelta) {
        WriteBuffer movement;
        movement.varint(self_->entityId);
        movement.i16(static_cast<std::int16_t>(dx * 4096));
        movement.i16(static_cast<std::int16_t>(dy * 4096));
        movement.i16(static_cast<std::int16_t>(dz * 4096));
        if (rotated) {
            movement.i8(constants::protocolSignedAngleByte(self_->yaw));
            movement.i8(constants::protocolSignedAngleByte(self_->pitch));
            movement.boolean(self_->onGround);
            srv_.broadcastPacketExceptInDimension(
                self_->dimension, self_.get(), pl::sc::MoveEntityPosRot,
                movement);
        } else {
            movement.boolean(self_->onGround);
            srv_.broadcastPacketExceptInDimension(
                self_->dimension, self_.get(), pl::sc::MoveEntityPos,
                movement);
        }
        WriteBuffer headRotation;
        headRotation.varint(self_->entityId);
        headRotation.i8(constants::protocolSignedAngleByte(self_->yaw));
        srv_.broadcastPacketExceptInDimension(
            self_->dimension, self_.get(), pl::sc::RotateHead,
            headRotation);
    } else if (hasTranslation) {
        const auto absolutePosition = makeEntityTeleportBody(
            self_->entityId, self_->x, self_->y, self_->z,
            0.0, 0.0, 0.0, self_->yaw, self_->pitch, 0,
            self_->onGround);
        srv_.broadcastPacketExceptInDimension(
            self_->dimension, self_.get(), pl::sc::EntityTeleport,
            absolutePosition);
    } else if (rotated) {
        WriteBuffer look;
        look.varint(self_->entityId);
        look.i8(constants::protocolSignedAngleByte(self_->yaw));
        look.i8(constants::protocolSignedAngleByte(self_->pitch));
        look.boolean(self_->onGround);
        srv_.broadcastPacketExceptInDimension(
            self_->dimension, self_.get(), pl::sc::EntityLook, look);

        WriteBuffer headRotation;
        headRotation.varint(self_->entityId);
        headRotation.i8(constants::protocolSignedAngleByte(self_->yaw));
        srv_.broadcastPacketExceptInDimension(
            self_->dimension, self_.get(), pl::sc::RotateHead,
            headRotation);
    }

    sentX_ = self_->x;
    sentY_ = self_->y;
    sentZ_ = self_->z;
    sentYaw_ = self_->yaw;
    sentPitch_ = self_->pitch;
    hasSent_ = true;
}

} // namespace cppfm
