#include "GameServer.hpp"
#include "EntityTrackingBroadcast.hpp"
#include "BlockEntityDrops.hpp"
#include "BlockEntityKinds.hpp"
#include "EntitySupport.hpp"
#include "MetadataTypes.hpp"
#include "PlayerEntityState.hpp"
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

void GameServer::effectsTick() {
    const std::int64_t tick = tickNo_.load(std::memory_order_acquire);
    const auto trackedViewers = playersSnapshot();
    for (const auto& pp : trackedViewers) {
        if (!pp) continue;
        Player& player = *pp;
        bool changed = false;
        bool healthChanged = false;
        float magicDamage = 0.0f;
        float poisonDamage = 0.0f;
        float witherDamage = 0.0f;
        std::vector<std::uint8_t> removedEffects;
        std::vector<EffectInstance> updatedEffects;
        std::shared_ptr<Connection> connection;
        std::int32_t entityId = 0;
        bool sendLevitation = false;
        WriteBuffer levitationPacket;
        WriteBuffer entityFlagsPacket;
        bool sendEntityFlags = false;
        {
            std::lock_guard playerLock(player.stateMtx);
            if (!player.inPlay || player.effects.empty()) continue;
            const auto oldEntityFlags =
                playerEntityMetadataState(player).entityFlags;
            connection = player.conn;
            entityId = player.entityId;
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
                tickHiddenEffects(*it);
                tickEffectDuration(*it);
                if (it->expired()) {
                    if (promoteHiddenEffect(*it)) {
                        updatedEffects.push_back(*it);
                    } else {
                        removedEffects.push_back(it->type);
                        it = player.effects.erase(it);
                        changed = true;
                        continue;
                    }
                    changed = true;
                }
                if (it->type == effects::Regeneration &&
                    effectPeriodicTickDue(it->durationTicks, 50,
                                          it->amplifier)) {
                    player.health = std::min(20.f, player.health + 1.f);
                    healthChanged = true;
                }
                if (it->type == effects::Poison &&
                    effectPeriodicTickDue(it->durationTicks, 25,
                                          it->amplifier) &&
                    player.health > 1.0f)
                    poisonDamage += 1.0f;
                if (it->type == effects::Wither &&
                    effectPeriodicTickDue(it->durationTicks, 40,
                                          it->amplifier))
                    witherDamage += 1.0f;
                if (it->type == effects::Saturation &&
                    effectPeriodicTickDue(it->durationTicks, 2,
                                          it->amplifier)) {
                    const auto foodBefore = player.food;
                    const auto saturationBefore = player.saturation;
                    HungerManager::addFoodAndSaturation(
                        player, 1, float(it->amplifier + 1));
                    healthChanged |= player.food != foodBefore ||
                                     player.saturation != saturationBefore;
                }
                if (it->type == effects::Hunger)
                    addHungerExhaustion(player,
                                       0.005f * float(it->amplifier + 1));
                ++it;
            }

            const auto entityFlags =
                playerEntityMetadataState(player).entityFlags;
            if (entityFlags != oldEntityFlags) {
                entityFlagsPacket.varint(entityId);
                meta::writeMetaByte(entityFlagsPacket, 0, entityFlags);
                entityFlagsPacket.u8(0xFF);
                sendEntityFlags = true;
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
                    levitationPacket = makeEntityTeleportBody(
                        player.entityId, player.x, player.y, player.z,
                        0.0, vy, 0.0, player.yaw, player.pitch, 0,
                        player.onGround);
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
        for (const auto effect : removedEffects) {
            WriteBuffer packet;
            packet.varint(entityId);
            packet.varint(effect);
            if (connection &&
                !connection->trySendPacket(pl::sc::RemoveMobEffect, packet))
                connection->close();
            sendPacketToTrackedEntityViewers(
                trackedViewers, entityId, pl::sc::RemoveMobEffect,
                packet);
        }
        for (const auto& effect : updatedEffects) {
            WriteBuffer packet;
            packet.varint(entityId);
            packet.varint(effect.type);
            packet.varint(effect.amplifier);
            packet.varint(effect.durationTicks);
            packet.u8(static_cast<std::uint8_t>(effectFlags(effect)));
            if (connection &&
                !connection->trySendPacket(pl::sc::EntityEffect, packet))
                connection->close();
            sendPacketToTrackedEntityViewers(
                trackedViewers, entityId, pl::sc::EntityEffect, packet);
        }
        if (sendEntityFlags) {
            if (connection && !connection->trySendPacket(
                                  pl::sc::SetEntityMetadata,
                                  entityFlagsPacket))
                connection->close();
            sendPacketToTrackedEntityViewers(
                trackedViewers, entityId, pl::sc::SetEntityMetadata,
                entityFlagsPacket);
        }
        if (changed) onEffectsChanged(&player);
        if (sendLevitation) {
            try {
                if (connection && !connection->trySendPacketLowPriority(
                                      pl::sc::EntityTeleport,
                                      levitationPacket))
                    connection->close();
                sendPacketToTrackedEntityViewers(
                    trackedViewers, entityId, pl::sc::EntityTeleport,
                    levitationPacket);
            } catch (...) {
            }
        }
    }
    for (const auto& pp : trackedViewers) {
        if (!pp) continue;
        Player& player = *pp;
        std::shared_ptr<Connection> connection;
        std::int32_t entityId = 0;
        WriteBuffer attributesPacket;
        bool sendAttributes = false;
        {
            std::lock_guard playerLock(player.stateMtx);
            if (!player.inPlay || !player.conn) continue;
            connection = player.conn;
            entityId = player.entityId;
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
        }
        if (sendAttributes) {
            if (!connection->trySendPacket(pl::sc::UpdateAttributes,
                                           attributesPacket))
                connection->close();
            sendPacketToTrackedEntityViewers(
                trackedViewers, entityId, pl::sc::UpdateAttributes,
                attributesPacket);
        }
    }
}

} // namespace cppfm
