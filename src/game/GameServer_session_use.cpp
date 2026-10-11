#include "GameServer.hpp"
#include "BundleContents.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "../generated/BlockStates.hpp"
#include "../generated/ItemIds.hpp"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace cppfm {
using namespace proto;

namespace {

bool isArrowAmmo(const ItemStack& stack) {
    if (stack.empty()) return false;
    const auto name = stack.name();
    return name == "minecraft:arrow" ||
           name == "minecraft:spectral_arrow" ||
           name == "minecraft:tipped_arrow";
}

bool isStandardArrow(const ItemStack& stack) {
    return !stack.empty() && stack.name() == "minecraft:arrow";
}

} // namespace

bool Session::useBundleItem(const std::size_t inventorySlot,
                            const std::int32_t sequence) {
    ItemStack extracted = ItemStack::air();
    std::int8_t dimension = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double yaw = 0.0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        auto& bundle = self_->inv[inventorySlot];
        if (bundle.empty() || bundle.name() != "minecraft:bundle")
            return false;
        if (auto removed = bundle_contents::popSelected(bundle))
            extracted = std::move(*removed);
        dimension = self_->dimension;
        x = self_->x;
        y = self_->y + 1.2;
        z = self_->z;
        yaw = self_->yaw * 3.14159265358979323846 / 180.0;
    }

    if (!extracted.empty()) {
        const double vx = -std::sin(yaw) * 0.2;
        const double vz = std::cos(yaw) * 0.2;
        srv_.spawnItemDropFor(dimension, x - std::sin(yaw) * 0.3, y,
                              z + std::cos(yaw) * 0.3, extracted,
                              vx, 0.1, vz);
        srv_.resendInventory(*self_);
    }
    ack(sequence);
    return true;
}

bool Session::useTridentItem(ItemStack& stack,
                             const std::int32_t sequence) {
    if (stack.empty() || stack.name().find("trident") == std::string::npos)
        return false;

    const int riptideLevel = EnchantmentHelper::getRiptide(stack);
    const auto& world = srv_.worldFor(self_->dimension);
    const auto isWaterAt = [&](const double y) {
        const auto state = world.getBlock(
            static_cast<int>(std::floor(self_->x)),
            static_cast<int>(std::floor(y)),
            static_cast<int>(std::floor(self_->z)));
        const auto* block = gen::blockByState(state);
        return block && std::string(block->name).find("water") !=
                            std::string::npos;
    };
    const bool inWater = isWaterAt(self_->y) || isWaterAt(self_->y + 1.0);
    const bool raining = srv_.raining() || srv_.thundering();
    if (riptideLevel > 0 && (raining || inWater)) {
        const double yaw = self_->yaw * 3.14159265358979323846 / 180.0;
        const double pitch = self_->pitch * 3.14159265358979323846 / 180.0;
        const double force = 1.2 * static_cast<double>(riptideLevel);
        const double vx = -std::sin(yaw) * std::cos(pitch) * force;
        const double vy = -std::sin(pitch) * force;
        const double vz = std::cos(yaw) * std::cos(pitch) * force;
        WriteBuffer velocity;
        velocity.varint(self_->entityId);
        velocity.i16(static_cast<std::int16_t>(vx * 8000));
        velocity.i16(static_cast<std::int16_t>(vy * 8000));
        velocity.i16(static_cast<std::int16_t>(vz * 8000));
        if (self_->conn)
            self_->conn->trySendPacket(pl::sc::EntityVelocity, velocity);
        srv_.broadcastPacketExceptInDimension(
            self_->dimension, self_.get(), pl::sc::EntityVelocity, velocity);
        if (self_->gamemode == 0 && ItemStack::maxDamageFor(stack.itemId) > 0) {
            if (stack.applyDamage(1)) stack = ItemStack::air();
            srv_.resendInventory(*self_);
        }
        ack(sequence);
        return true;
    }

    const double yaw = self_->yaw * 3.14159265358979323846 / 180.0;
    const double pitch = self_->pitch * 3.14159265358979323846 / 180.0;
    const double vx = -std::sin(yaw) * std::cos(pitch) * 1.5;
    const double vy = -std::sin(pitch) * 1.5;
    const double vz = std::cos(yaw) * std::cos(pitch) * 1.5;
    auto projectile = srv_.spawnProjectileFor(
        self_->dimension, ProjectileKind::Trident, self_->x, self_->y + 1.6,
        self_->z, vx, vy, vz, self_->entityId, true);
    if (projectile)
        projectile->loyaltyLevel = EnchantmentHelper::getLoyalty(stack);
    if (self_->gamemode == 0 && ItemStack::maxDamageFor(stack.itemId) > 0) {
        if (stack.applyDamage(1)) stack = ItemStack::air();
        srv_.resendInventory(*self_);
    }
    ack(sequence);
    return true;
}

bool Session::useBowItem(ItemStack& stack, const std::int32_t sequence) {
    if (stack.empty() || stack.name() != "minecraft:bow") return false;

    const bool hasInfinity = EnchantmentHelper::hasInfinity(stack);
    const bool creative = self_->gamemode == 1;
    int arrowSlot = -1;
    for (int slot = 9; slot <= 44; ++slot) {
        if (isArrowAmmo(self_->inv[slot])) {
            arrowSlot = slot;
            break;
        }
    }
    if (arrowSlot == -1 && isArrowAmmo(self_->inv[45]))
        arrowSlot = 45;
    const bool hasArrow = arrowSlot != -1 && self_->inv[arrowSlot].count > 0;
    if (!hasArrow && !creative) {
        ack(sequence);
        return true;
    }
    const bool consumesArrow = !creative && hasArrow &&
        !(hasInfinity && isStandardArrow(self_->inv[arrowSlot]));

    const double yaw = self_->yaw * 3.14159265358979323846 / 180.0;
    const double pitch = self_->pitch * 3.14159265358979323846 / 180.0;
    const double vx = -std::sin(yaw) * std::cos(pitch) * 2.0;
    const double vy = -std::sin(pitch) * 2.0 + 0.15;
    const double vz = std::cos(yaw) * std::cos(pitch) * 2.0;
    srv_.spawnProjectileFor(self_->dimension, ProjectileKind::Arrow,
                            self_->x, self_->y + 1.6, self_->z, vx, vy, vz,
                            self_->entityId, true);
    if (self_->gamemode == 0 && ItemStack::maxDamageFor(stack.itemId) > 0 &&
        stack.applyDamage(1))
        stack = ItemStack::air();

    if (consumesArrow) {
        auto& arrow = self_->inv[arrowSlot];
        if (--arrow.count <= 0) arrow = ItemStack::air();
    }
    srv_.resendInventory(*self_);
    ack(sequence);
    return true;
}

bool Session::useCrossbowItem(ItemStack& stack,
                              const std::int32_t sequence) {
    if (stack.empty() || stack.name() != "minecraft:crossbow") return false;

    const bool creative = self_->gamemode == 1;
    int arrowSlot = -1;
    for (int slot = 9; slot <= 44; ++slot) {
        if (!self_->inv[slot].empty() &&
            self_->inv[slot].name().find("arrow") != std::string::npos) {
            arrowSlot = slot;
            break;
        }
    }
    if (arrowSlot == -1 && !self_->inv[45].empty() &&
        self_->inv[45].name().find("arrow") != std::string::npos)
        arrowSlot = 45;
    const bool hasArrow = arrowSlot != -1 && self_->inv[arrowSlot].count > 0;
    if (!hasArrow && !creative) {
        ack(sequence);
        return true;
    }

    const int shots = EnchantmentHelper::multishotShots(stack);
    const int piercing = EnchantmentHelper::getPiercing(stack);
    const double yaw = self_->yaw * 3.14159265358979323846 / 180.0;
    const double pitch = self_->pitch * 3.14159265358979323846 / 180.0;
    for (int shot = 0; shot < shots; ++shot) {
        const double spread = shots == 1 ? 0.0
            : (shot == 0 ? 0.0 : (shot == 1 ? 0.17 : -0.17));
        const double shotYaw = yaw + spread;
        const double vx = -std::sin(shotYaw) * std::cos(pitch) * 2.0;
        const double vy = -std::sin(pitch) * 2.0 + 0.15;
        const double vz = std::cos(shotYaw) * std::cos(pitch) * 2.0;
        auto projectile = srv_.spawnProjectileFor(
            self_->dimension, ProjectileKind::Arrow, self_->x, self_->y + 1.6,
            self_->z, vx, vy, vz, self_->entityId, true);
        if (projectile) projectile->piercingLevel = piercing;
    }

    if (self_->gamemode == 0 && ItemStack::maxDamageFor(stack.itemId) > 0 &&
        DamageComponent::applyDamage(stack, 1))
        stack = ItemStack::air();
    if (!creative && hasArrow) {
        auto& arrow = self_->inv[arrowSlot];
        if (--arrow.count <= 0) arrow = ItemStack::air();
    }
    srv_.resendInventory(*self_);
    ack(sequence);
    return true;
}

bool Session::useThrowableItem(ItemStack& stack,
                               const std::int32_t sequence) {
    if (stack.empty()) return false;
    const std::string name = stack.name();
    const bool pearl = name.find("ender_pearl") != std::string::npos;
    const bool snowball = name.find("snowball") != std::string::npos;
    const bool egg = name == "minecraft:egg";
    if (!pearl && !snowball && !egg) return false;

    if (pearl && srv_.tickNow() - self_->lastEnderPearlTick < 20) {
        const auto itemId = gen::itemIdByName().find("minecraft:ender_pearl");
        if (itemId != gen::itemIdByName().end() && self_->conn) {
            WriteBuffer cooldown;
            cooldown.varint(static_cast<std::int32_t>(itemId->second));
            cooldown.varint(static_cast<std::int32_t>(
                20 - (srv_.tickNow() - self_->lastEnderPearlTick)));
            self_->conn->trySendPacket(pl::sc::SetCooldown, cooldown);
        }
        ack(sequence);
        return true;
    }

    const double yaw = self_->yaw * 3.14159265358979323846 / 180.0;
    const double pitch = self_->pitch * 3.14159265358979323846 / 180.0;
    const double vx = -std::sin(yaw) * std::cos(pitch) * 1.5;
    const double vy = -std::sin(pitch) * 1.5;
    const double vz = std::cos(yaw) * std::cos(pitch) * 1.5;
    const auto projectile = pearl ? ProjectileKind::EnderPearl
        : (snowball ? ProjectileKind::Snowball : ProjectileKind::Egg);
    srv_.spawnProjectileFor(self_->dimension, projectile, self_->x,
                            self_->y + 1.6, self_->z, vx, vy, vz,
                            self_->entityId, true);

    if (pearl) {
        self_->lastEnderPearlTick = srv_.tickNow();
        const auto itemId = gen::itemIdByName().find("minecraft:ender_pearl");
        if (itemId != gen::itemIdByName().end() && self_->conn) {
            WriteBuffer cooldown;
            cooldown.varint(static_cast<std::int32_t>(itemId->second));
            cooldown.varint(20);
            self_->conn->trySendPacket(pl::sc::SetCooldown, cooldown);
        }
    }
    if (self_->gamemode != 1) {
        if (--stack.count <= 0) stack = ItemStack::air();
        srv_.resendInventory(*self_);
    }
    ack(sequence);
    return true;
}

void Session::onUseItem(ReadBuffer& input) {
    const auto hand = input.varint();
    const auto sequence = input.varint();
    (void)input.f32();
    (void)input.f32();
    if (input.remaining() != 0) {
        ack(sequence);
        return;
    }
    if (hand < 0 || hand > 1) {
        ack(sequence);
        return;
    }

    std::size_t inventorySlot = 45;
    bool invalidMainHandSlot = false;
    if (hand == 0) {
        std::lock_guard playerLock(self_->stateMtx);
        if (self_->heldSlot < 0 || self_->heldSlot >= 9) {
            invalidMainHandSlot = true;
        } else {
            inventorySlot = static_cast<std::size_t>(36 + self_->heldSlot);
        }
    }
    if (invalidMainHandSlot) {
        ack(sequence);
        return;
    }
    auto& stack = self_->inv[inventorySlot];
    const auto activeConsumableSlot = consumableUse_.activeSlot();
    if (activeConsumableSlot &&
        (*activeConsumableSlot != inventorySlot ||
         !consumableUse_.matchesActiveStack(inventorySlot, stack)))
        cancelConsumableUse();
    if (useBundleItem(inventorySlot, sequence)) return;

    const bool usingShield = !stack.empty() &&
        stack.name().find("shield") != std::string::npos;
    bool shieldCanBlock = false;
    bool shieldStopped = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (usingShield && self_->shieldDisableTicks <= 0) {
            shieldCanBlock = true;
            self_->isBlocking = true;
        } else if (self_->isBlocking) {
            self_->isBlocking = false;
            self_->blockingTicks = 0;
            shieldStopped = true;
        }
    }
    if (shieldCanBlock) {
        cancelConsumableUse();
        publishHandUseMetadata(true, hand == 1);
    } else if (!usingShield && shieldStopped) {
        publishHandUseMetadata(false, false);
    }

    if (useTridentItem(stack, sequence) || useBowItem(stack, sequence) ||
        useCrossbowItem(stack, sequence) ||
        useThrowableItem(stack, sequence))
        return;
    (void)tryStartConsumableUse(inventorySlot, stack);
    ack(sequence);
}

} // namespace cppfm
