#include "GameServer.hpp"
#include "SessionInteractionSupport.hpp"
#include "SessionEntitySupport.hpp"
#include "../generated/ItemIds.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace cppfm {
using namespace proto;
using namespace session_entity_detail;

namespace {

bool isHorseLikeKind(const MobKind kind) {
    switch (kind) {
    case MobKind::Horse:
    case MobKind::Donkey:
    case MobKind::Mule:
    case MobKind::Llama:
    case MobKind::TraderLlama:
    case MobKind::Camel:
    case MobKind::SkeletonHorse:
    case MobKind::ZombieHorse:
        return true;
    default:
        return false;
    }
}

} // namespace

// Entity interaction stays separate from the combat handler so the packet
// decoder only routes attack actions into the dedicated combat translation
// unit.
void Session::onUseEntity(ReadBuffer& in) {
    const std::int32_t target = in.varint();
    const std::int32_t mouse = in.varint();
    if (mouse == 2) { (void)in.f32(); (void)in.f32(); (void)in.f32(); }
    if (mouse != 1) {
        // INTERACT (0) / INTERACT_AT (2)
        if (mouse == 0 || mouse == 2) {
            const int hand = in.varint();
            const bool sneaking = in.boolean();
            if (hand != 0 && hand != 1) return;
            const session_detail::SessionPlayerSnapshot playerState = session_detail::snapshotPlayerForSession(*self_);
            if (!canInteractWithNonPlayerMob(playerState.gamemode)) return;
            if (hand == 0 &&
                (playerState.heldSlot < 0 || playerState.heldSlot >= 9))
                return;
            const std::size_t handInventorySlot = hand == 1
                ? std::size_t{45}
                : static_cast<std::size_t>(36 + playerState.heldSlot);
            const std::int8_t playerDimension = playerState.dimension;
            std::shared_ptr<MobEntity> targetMob;
            for (const auto& m : srv_.mobsSnapshot()) {
                if (!m) continue;
                std::lock_guard entityLock(*m->stateMtx);
                if (m->entityId == target && !m->dead &&
                    GameServer::canonicalDimension(m->dimension) == playerDimension) {
                    targetMob = m;
                    break;
                }
            }
            if (targetMob) {
                const SessionMobSnapshot targetState = snapshotMobForSession(*targetMob);
                if (!withinEntityInteractionRange(
                        playerState, targetState.x, targetState.y, targetState.z,
                        targetState.kind, targetState.slimeSize))
                    return;
                // Shearing is one transaction across the player's held item
                // and the sheep state.  Capture the packet/drop data while
                // both model locks are held, then do extension/transport work
                // after releasing them.
                bool sheared = false;
                std::int8_t sheepDimension = 0;
                std::int32_t sheepEntityId = 0;
                int sheepColor = 0;
                double sheepX = 0.0, sheepY = 0.0, sheepZ = 0.0;
                {
                    std::scoped_lock stateLock(self_->stateMtx,
                                               *targetMob->stateMtx);
                    const bool currentPlayer = self_->inPlay && !self_->dead &&
                        canInteractWithNonPlayerMob(self_->gamemode) &&
                        GameServer::canonicalDimension(self_->dimension) ==
                            playerDimension &&
                        (hand == 1 || self_->heldSlot == playerState.heldSlot);
                    const bool currentTarget = !targetMob->dead &&
                        targetMob->entityId == target &&
                        GameServer::canonicalDimension(targetMob->dimension) ==
                            playerDimension;
                    if (currentPlayer && currentTarget &&
                        targetMob->kind == MobKind::Sheep &&
                        !targetMob->sheared) {
                        auto& held = self_->inv[handInventorySlot];
                        const auto shearsIdIt = gen::itemIdByName().find("minecraft:shears");
                        if (shearsIdIt != gen::itemIdByName().end() &&
                            held.itemId == shearsIdIt->second) {
                            targetMob->sheared = true;
                            if (held.applyDamage(1)) held = ItemStack::air();
                            sheared = true;
                            sheepDimension = targetMob->dimension;
                            sheepEntityId = targetMob->entityId;
                            sheepColor = targetMob->woolColor % 16;
                            sheepX = targetMob->x;
                            sheepY = targetMob->y;
                            sheepZ = targetMob->z;
                        }
                    }
                }
                if (sheared) {
                    static const char* woolNames[] = {
                        "minecraft:white_wool","minecraft:orange_wool","minecraft:magenta_wool","minecraft:light_blue_wool",
                        "minecraft:yellow_wool","minecraft:lime_wool","minecraft:pink_wool","minecraft:gray_wool",
                        "minecraft:light_gray_wool","minecraft:cyan_wool","minecraft:purple_wool","minecraft:blue_wool",
                        "minecraft:brown_wool","minecraft:green_wool","minecraft:red_wool","minecraft:black_wool"
                    };
                    const auto wit = gen::itemIdByName().find(woolNames[sheepColor]);
                    if (wit != gen::itemIdByName().end()) {
                        const int count = 1 + (nextRandom() % 3);
                        srv_.spawnItemDropFor(
                            sheepDimension, sheepX, sheepY + 0.8, sheepZ,
                            wit->second, static_cast<std::uint8_t>(count),
                            (nextRandom() / (double)RAND_MAX - .5) * 0.12,
                            0.12,
                            (nextRandom() / (double)RAND_MAX - .5) * 0.12);
                    }
                    WriteBuffer md;
                    md.varint(sheepEntityId);
                    md.u8(17); md.u8(8); md.u8(1);
                    md.u8(255);
                    srv_.broadcastPacketExceptInDimension(
                        sheepDimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                    srv_.resendInventory(*self_);
                    return;
                }
                // Take a value snapshot before any transport or extension
                // call.  `openTrading` and `tryBreedFeed` can send packets or
                // re-enter the server; neither is allowed to run while the
                // live MobEntity lock is held.
                MobEntity interactionSnapshot;
                bool openHorseWindow = false;
                {
                    std::scoped_lock stateLock(self_->stateMtx,
                                               *targetMob->stateMtx);
                    const bool currentPlayer = self_->inPlay && !self_->dead &&
                        canInteractWithNonPlayerMob(self_->gamemode) &&
                        GameServer::canonicalDimension(self_->dimension) ==
                            playerDimension;
                    const bool currentTarget = !targetMob->dead &&
                        targetMob->entityId == target &&
                        GameServer::canonicalDimension(targetMob->dimension) ==
                            playerDimension;
                    if (!currentPlayer || !currentTarget)
                        return;
                    interactionSnapshot = *targetMob;
                    openHorseWindow = canOpenHorseInventory(*targetMob,
                                                            sneaking);
                }
                if (openHorseWindow) {
                    closeOpenMenu(true);
                    constexpr int slotCount = 15;
                    int windowId = ++menuWindowCounter_;
                    if (windowId == 0 || windowId > 100) {
                        menuWindowCounter_ = 1;
                        windowId = 1;
                    }
                    std::int32_t stateId = 0;
                    {
                        std::lock_guard playerLock(self_->stateMtx);
                        stateId = ++self_->invStateId;
                    }
                    WriteBuffer ow;
                    ow.varint(windowId);
                    ow.varint(slotCount);
                    ow.varint(interactionSnapshot.entityId);
                    conn_->trySendPacket(proto::pl::sc::OpenHorseWindow, ow);
                    // Horse inventory storage is not modelled yet, but keep
                    // the wire snapshot internally consistent and emit it
                    // only after the MobEntity lock has been released.
                    WriteBuffer cc;
                    cc.varint(windowId);
                    cc.varint(stateId);
                    cc.varint(slotCount);
                    for (int i = 0; i < slotCount; ++i) ItemStack::air().write(cc);
                    ItemStack::air().write(cc);
                    conn_->trySendPacket(proto::pl::sc::ContainerSetContent, cc);
                    return;
                }

                // After any explicit horse-window action, a valid food item
                // takes precedence over the mount fallback. The transaction
                // revalidates both entities and the selected hand.
                if (srv_.tryBreedFeed(*self_, *targetMob,
                                      handInventorySlot))
                    return;

                bool mounted = false;
                std::int32_t mountedEntityId = -1;
                {
                    // The dead/dimension/rider checks and the two-way vehicle
                    // link must be one transaction.  scoped_lock provides a
                    // deadlock-safe order against the server's Player->Mob
                    // paths while no packet is sent in this scope.
                    std::scoped_lock stateLock(self_->stateMtx,
                                               *targetMob->stateMtx);
                    const bool currentPlayer = self_->inPlay && !self_->dead &&
                        canInteractWithNonPlayerMob(self_->gamemode) &&
                        GameServer::canonicalDimension(self_->dimension) ==
                            GameServer::canonicalDimension(
                                targetMob->dimension);
                    const bool currentTarget =
                        !targetMob->dead && targetMob->entityId == target &&
                        GameServer::canonicalDimension(targetMob->dimension) ==
                            GameServer::canonicalDimension(self_->dimension);
                    const bool mountable =
                        isHorseLikeKind(targetMob->kind) ||
                        targetMob->kind == MobKind::Pig ||
                        MobEntity::isBoat(targetMob->kind) ||
                        targetMob->kind == MobKind::Minecart;
                    const bool adultMount =
                        !isHorseLikeKind(targetMob->kind) ||
                        !MobEntity::isBaby(*targetMob);
                    if (currentPlayer && currentTarget && mountable &&
                        adultMount && self_->vehicleId == -1 &&
                        targetMob->riderEntityId == -1) {
                        self_->vehicleId = targetMob->entityId;
                        targetMob->riderEntityId = self_->entityId;
                        mounted = true;
                        mountedEntityId = targetMob->entityId;
                    }
                }
                if (mounted) {
                    srv_.broadcastSetPassengers(mountedEntityId);
                    return;
                }

                if (interactionSnapshot.kind == MobKind::Villager) {
                    MobEntity villagerSnapshot;
                    bool stillValid = false;
                    {
                        std::lock_guard entityLock(*targetMob->stateMtx);
                        stillValid = !targetMob->dead &&
                            targetMob->entityId == target &&
                            GameServer::canonicalDimension(targetMob->dimension) ==
                                playerDimension;
                        if (stillValid) villagerSnapshot = *targetMob;
                    }
                    if (stillValid) {
                        // Preserve inputs/cursor from an already-open screen
                        // before the merchant screen is emitted.  Do not
                        // discard openMenu_ by assigning nullptr.
                        closeOpenMenu(true);
                        if (srv_.openTrading(*self_, villagerSnapshot)) {
                            std::lock_guard playerLock(self_->stateMtx);
                            if (self_->inPlay && !self_->dead &&
                                GameServer::canonicalDimension(self_->dimension) ==
                                    playerDimension)
                                tradingVillager_ = target;
                        }
                    }
                }
                }
        } else {
            in.skipRest();
        }
        return;
    }
    (void)in.boolean();
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!canInteractWithNonPlayerMob(self_->gamemode)) return;
    }
    handleEntityAttack(target);
}

} // namespace cppfm
