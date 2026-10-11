#include "GameServer.hpp"
#include "Session.hpp"
#include "DeathProtection.hpp"
#include "FoodComponents.hpp"
#include "HungerManager.hpp"
#include "MetadataTypes.hpp"

#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace cppfm {
using namespace proto;

namespace {

std::optional<food_components::Food> foodProperties(
    const ItemStack& stack, bool& vanillaFood) {
    const std::string itemName = stack.name();
    const auto vanilla = HungerManager::foodTable().find(itemName);
    vanillaFood = vanilla != HungerManager::foodTable().end();

    if (food_components::payloadFor(stack, food_components::kFood))
        return food_components::readFood(stack);
    if (food_components::explicitlyRemoved(stack, food_components::kFood) ||
        !vanillaFood)
        return std::nullopt;

    const auto& info = vanilla->second;
    const float modifier = info.food > 0
        ? info.saturation / (2.0F * static_cast<float>(info.food))
        : 0.0F;
    return food_components::Food{info.food, modifier,
                                 info.canAlwaysEat};
}

std::optional<death_protection::Protection> consumableEffects(
    const ItemStack& stack, const bool vanillaConsumable) {
    if (food_components::explicitlyRemoved(
            stack, food_components::kConsumable))
        return std::nullopt;

    std::optional<death_protection::Protection> result;
    if (food_components::payloadFor(stack, food_components::kConsumable)) {
        const auto consumable = food_components::readConsumable(stack);
        if (!consumable) return std::nullopt;
        const auto* effects = consumable->get("on_consume_effects");
        result = effects
            ? death_protection::fromDeathEffects(effects)
            : std::optional<death_protection::Protection>(
                  death_protection::Protection{});
    } else if (vanillaConsumable) {
        // ItemStack::ofName stores explicit patches, not vanilla defaults.
        std::int32_t ominousAmplifier = 0;
        if (stack.name() == "minecraft:ominous_bottle") {
            const auto amplifier =
                food_components::readOminousBottleAmplifier(stack);
            if (!amplifier) return std::nullopt;
            ominousAmplifier = *amplifier;
        }
        result = food_components::vanillaConsumeEffects(
            stack.name(), ominousAmplifier);
    } else {
        return std::nullopt;
    }
    if (!result) return std::nullopt;

    if (food_components::payloadFor(
            stack, food_components::kSuspiciousStewEffects)) {
        auto stewEffects = food_components::readStewEffects(stack);
        if (!stewEffects) return std::nullopt;
        result->operations.insert(
            result->operations.end(),
            std::make_move_iterator(stewEffects->operations.begin()),
            std::make_move_iterator(stewEffects->operations.end()));
    }
    return result;
}

struct ConsumableProperties {
    float consumeSeconds = 0.0F;
    std::optional<food_components::Food> food;
    death_protection::Protection effects;
    std::optional<ItemStack> useRemainder;
};

std::optional<ConsumableProperties> consumableProperties(
    const ItemStack& stack) {
    if (stack.empty()) return std::nullopt;

    bool vanillaFood = false;
    auto food = foodProperties(stack, vanillaFood);
    if (food_components::payloadFor(stack, food_components::kFood) && !food)
        return std::nullopt;

    const std::string itemName = stack.name();
    const bool vanillaConsumable = vanillaFood ||
        itemName == "minecraft:milk_bucket" ||
        itemName == "minecraft:ominous_bottle";
    const auto seconds =
        food_components::consumeSeconds(stack, vanillaConsumable);
    if (!seconds) return std::nullopt;

    auto effects = consumableEffects(stack, vanillaConsumable);
    if (!effects) return std::nullopt;

    const bool hasCustomRemainder =
        food_components::payloadFor(stack,
                                    food_components::kUseRemainder) != nullptr;
    std::optional<ItemStack> remainder;
    if (hasCustomRemainder) {
        remainder = food_components::readUseRemainder(stack);
        if (!remainder) return std::nullopt;
    } else if (!food_components::explicitlyRemoved(
                   stack, food_components::kUseRemainder) &&
               vanillaConsumable) {
        remainder = food_components::defaultUseRemainder(stack);
    }

    return ConsumableProperties{*seconds, std::move(food),
                                std::move(*effects), std::move(remainder)};
}

} // namespace

bool Session::tryStartConsumableUse(const std::size_t inventorySlot,
                                    ItemStack& stack) {
    auto properties = consumableProperties(stack);
    if (!properties) return false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!self_->inPlay || self_->dead || self_->health <= 0.0F ||
            self_->gamemode == 3 ||
            (properties->food &&
             !food_components::canConsumeAtFoodLevel(
                 *properties->food, self_->food)))
            return false;
    }

    if (const auto activeSlot = consumableUse_.activeSlot()) {
        if (*activeSlot == inventorySlot &&
            consumableUse_.matchesActiveStack(inventorySlot, stack))
            return true;
        cancelConsumableUse();
    }
    if (!consumableUse_.start(inventorySlot,
                              properties->consumeSeconds, stack))
        return false;
    publishHandUseMetadata(true, inventorySlot == 45);
    return true;
}

void Session::publishHandUseMetadata(const bool active, const bool offhand) {
    const std::optional<bool> nextHand =
        active ? std::optional<bool>(offhand) : std::nullopt;
    if (activeUseOffhand_ == nextHand) return;
    activeUseOffhand_ = nextHand;

    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        entityId = self_->entityId;
        dimension = GameServer::canonicalDimension(self_->dimension);
    }

    std::uint8_t flags = 0;
    if (active) {
        flags |= 0x01; // LivingEntity.USING_ITEM_FLAG (1.21.4)
        if (offhand) flags |= 0x02; // LivingEntity.OFF_HAND_ACTIVE_FLAG
    }
    WriteBuffer metadata;
    metadata.varint(entityId);
    meta::writeMetaByte(metadata, 8, flags);
    metadata.u8(0xFF);
    if (conn_)
        conn_->trySendPacket(pl::sc::SetEntityMetadata, metadata);
    srv_.broadcastPacketExceptInDimension(
        dimension, self_.get(), pl::sc::SetEntityMetadata, metadata);
}

void Session::cancelConsumableUse() {
    if (!consumableUse_.activeSlot()) return;
    consumableUse_.cancel();
    publishHandUseMetadata(false, false);
}

void Session::stopAllItemUse() {
    cancelConsumableUse();
    if (activeUseOffhand_) publishHandUseMetadata(false, false);
    std::lock_guard playerLock(self_->stateMtx);
    self_->isBlocking = false;
    self_->blockingTicks = 0;
}

void Session::finishConsumableUse() {
    const auto completedSlot = consumableUse_.takeCompletedSlot();
    if (completedSlot) publishHandUseMetadata(false, false);
    if (completedSlot && *completedSlot < self_->inv.size())
        completeConsumableUse(self_->inv[*completedSlot]);
}

void Session::tickConsumableUse() {
    const auto activeSlot = consumableUse_.activeSlot();
    if (!activeSlot) return;

    bool useInvalid = false;
    ConsumableUseState::TickResult result =
        ConsumableUseState::TickResult::Inactive;
    {
        std::lock_guard playerLock(self_->stateMtx);
        useInvalid = !self_->inPlay || self_->dead ||
            self_->health <= 0.0F || self_->gamemode == 3 ||
            *activeSlot >= self_->inv.size();
        if (!useInvalid)
            useInvalid = !consumableUse_.matchesActiveStack(
                *activeSlot, self_->inv[*activeSlot]);
        if (!useInvalid) result = consumableUse_.advanceOneTick();
    }
    if (useInvalid) {
        cancelConsumableUse();
        return;
    }
    if (result == ConsumableUseState::TickResult::Ready)
        finishConsumableUse();
}

void Session::completeConsumableUse(ItemStack& stack) {
    auto properties = consumableProperties(stack);
    if (!properties) return;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!self_->inPlay || self_->dead || self_->health <= 0.0F ||
            self_->gamemode == 3)
            return;
    }
    if (properties->food) {
        srv_.addFoodAndSaturation(
            *self_, properties->food->nutrition,
            food_components::saturationGain(*properties->food));
    }

    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    double soundX = 0.0;
    double soundY = 0.0;
    double soundZ = 0.0;
    std::shared_ptr<Connection> connection;
    death_protection::EffectChanges changes;
    std::vector<EffectInstance> updatedEffects;
    {
        std::lock_guard playerLock(self_->stateMtx);
        entityId = self_->entityId;
        dimension = self_->dimension;
        soundX = self_->x;
        soundY = self_->y;
        soundZ = self_->z;
        connection = self_->conn;
        changes = death_protection::apply(
            properties->effects, self_->effects,
            [] {
                return static_cast<double>(nextRandom()) /
                       static_cast<double>(RAND_MAX);
            }, [this](const std::string& tag,
                      const std::uint8_t effectId) {
                const auto* members = srv_.datapackManager_.tagManager.getStatusEffectTag(tag);
                return members && members->contains(effectId);
            });
        updatedEffects = persistentEntityEffectUpdates(
            changes.updated, self_->effects);
    }
    srv_.publishPlayerEffectChanges(
        *self_, entityId, dimension, connection, changes.removed,
        updatedEffects,
        !changes.removed.empty() || !changes.updated.empty());
    for (const auto diameter : changes.randomTeleportDiameters)
        (void)srv_.tryRandomTeleport(*self_, diameter);
    for (const auto& sound : changes.sounds)
        srv_.broadcastSoundFor(dimension, sound.id.c_str(), soundX, soundY,
                               soundZ, 1.0F, 1.0F, "player",
                               sound.fixedRange);

    if (properties->food) srv_.addHungerExhaustion(*self_, 0.005F);
    ItemStack consumed = stack;
    consumed.count = 1;
    srv_.onConsumeItem(*self_, consumed);

    bool creative = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        creative = self_->gamemode == 1;
    }
    if (!creative) {
        if (stack.count <= 1) {
            stack = properties->useRemainder.value_or(InvSlot::air());
        } else {
            --stack.count;
            if (properties->useRemainder &&
                !properties->useRemainder->empty() &&
                !srv_.addStackToInventory(
                    *self_, *properties->useRemainder,
                    static_cast<std::uint16_t>(
                        properties->useRemainder->count))) {
                std::int8_t dropDimension = 0;
                double dropX = 0.0;
                double dropY = 0.0;
                double dropZ = 0.0;
                {
                    std::lock_guard playerLock(self_->stateMtx);
                    dropDimension = self_->dimension;
                    dropX = self_->x;
                    dropY = self_->y + 0.5;
                    dropZ = self_->z;
                }
                srv_.spawnItemDropFor(dropDimension, dropX, dropY, dropZ,
                                      *properties->useRemainder);
            }
        }
    }
    srv_.resendInventory(*self_);
}

} // namespace cppfm
