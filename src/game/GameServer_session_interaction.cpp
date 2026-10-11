#include "GameServer.hpp"
#include "SessionInteractionSupport.hpp"
#include "Messages.hpp"
#include "BlockEvent.hpp"
#include "AdventureBlockBreak.hpp"
#include "../generated/BlockStates.hpp"
#include "EnchantmentHelper.hpp"
#include "MiningCalculator.hpp"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace cppfm {
using namespace proto;

namespace {
std::optional<ItemStack> snapshotHeldMiningItem(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    if (player.heldSlot < 0 || player.heldSlot >= 9) return std::nullopt;
    const auto& held = player.inv[36 + player.heldSlot];
    return held.empty() ? std::nullopt : std::optional<ItemStack>(held);
}

bool waterAtEyeLevel(const GameServer& server, std::int8_t dimension,
                     double playerX, double playerY, double playerZ) {
    const auto& world = server.worldFor(dimension);
    const int x = static_cast<int>(std::floor(playerX));
    const int y = static_cast<int>(std::floor(playerY + 1.62));
    const int z = static_cast<int>(std::floor(playerZ));
    const auto* def = gen::blockByState(world.getBlock(x, y, z));
    return def && def->name == "minecraft:water";
}

MiningContext miningContextFor(const GameServer& server, const Player& player) {
    MiningContext context;
    std::optional<ItemStack> held;
    std::int8_t dimension = 0;
    double playerX = 0.0;
    double playerY = 0.0;
    double playerZ = 0.0;
    bool aquaAffinity = false;
    {
        // Effects, equipment, position and onGround are updated by other
        // simulation/session paths. Capture the complete mining input under
        // one state lock so packet handling never races a status/movement tick
        // or combines values from different player-state revisions.
        std::lock_guard playerLock(player.stateMtx);
        if (player.heldSlot >= 0 && player.heldSlot < 9) {
            const auto& item = player.inv[36 + player.heldSlot];
            if (!item.empty()) held = item;
        }
        const int hasteAmp = amplifierFor(player.effects, effects::Haste);
        const int fatigueAmp =
            amplifierFor(player.effects, effects::MiningFatigue);
        context.haste = hasteAmp >= 0 ? hasteAmp + 1 : 0;
        context.fatigue = fatigueAmp >= 0 ? fatigueAmp + 1 : 0;
        context.onGround = player.onGround;
        dimension = player.dimension;
        playerX = player.x;
        playerY = player.y;
        playerZ = player.z;
        for (int slot = 5; slot <= 8; ++slot) {
            if (!player.inv[slot].empty() &&
                EnchantmentHelper::hasAquaAffinity(player.inv[slot])) {
                aquaAffinity = true;
                break;
            }
        }
    }
    const std::string itemName = held ? held->name() : "minecraft:air";
    context.tool = MiningCalculator::toolKindFromItemName(itemName);
    context.tier = MiningCalculator::toolTierFromItemName(itemName);
    context.efficiency = held ? held->efficiencyLevel() : 0;
    context.aquaAffinity = aquaAffinity;
    context.inWater = waterAtEyeLevel(
        server, dimension, playerX, playerY, playerZ);
    return context;
}

bool withinBlockInteractionRange(
    const session_detail::SessionPlayerSnapshot& player,
    std::int32_t x, std::int32_t y, std::int32_t z) {
    if (!player.inPlay || player.dead ||
        !std::isfinite(player.x) || !std::isfinite(player.y) ||
        !std::isfinite(player.z))
        return false;
    const double eyeX = player.x;
    const double eyeY = player.y + 1.62;
    const double eyeZ = player.z;
    const double range = player.gamemode == 1 ? 5.0 : 4.5;
    return session_detail::squaredDistanceToBox(
               eyeX, eyeY, eyeZ, static_cast<double>(x),
               static_cast<double>(y), static_cast<double>(z),
               static_cast<double>(x) + 1.0,
               static_cast<double>(y) + 1.0,
               static_cast<double>(z) + 1.0) <= range * range + 1e-6;
}
} // namespace

void Session::onHeldSlot(ReadBuffer& in) {
    const std::int16_t slot = in.i16();
    if (slot >= 0 && slot < 9) {
        bool heldSlotChanged = false;
        bool cancelUse = false;
        bool stopShield = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            heldSlotChanged = self_->heldSlot != slot;
            stopShield = heldSlotChanged && self_->isBlocking &&
                !activeUseOffhand_.value_or(false);
            self_->heldSlot = slot;
            if (stopShield) {
                self_->isBlocking = false;
                self_->blockingTicks = 0;
            }
            const auto activeSlot = consumableUse_.activeSlot();
            cancelUse = activeSlot && *activeSlot >= 36 &&
                *activeSlot <= 44 &&
                *activeSlot != static_cast<std::size_t>(36 + slot);
        }
        if (cancelUse) cancelConsumableUse();
        if (stopShield) publishHandUseMetadata(false, false);
        if (heldSlotChanged) srv_.syncEquipmentOnChange(*self_);
    }
}
void Session::onPlayerAction(ReadBuffer& in) {
    const std::int32_t status = in.varint();
    std::int32_t x, y, z;
    in.position(x, y, z);
    (void)in.i8();                                    // face
    const std::int32_t sequence = in.varint();

    if (status == 5) { // RELEASE_USE_ITEM
        const bool wasUsingConsumable = consumableUse_.activeSlot().has_value();
        const bool completed = consumableUse_.release();
        if (completed) {
            finishConsumableUse();
        } else if (wasUsingConsumable) {
            publishHandUseMetadata(false, false);
        }
        bool wasBlocking = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            wasBlocking = self_->isBlocking;
            self_->isBlocking = false;
            self_->blockingTicks = 0;
        }
        if (wasBlocking && !wasUsingConsumable)
            publishHandUseMetadata(false, false);
        ack(sequence);
        return;
    }

    std::unique_lock digTransitionLock(
        self_->digTransitionMtx, std::defer_lock);
    if (status == 0 || status == 1 || status == 2)
        digTransitionLock.lock();

    const session_detail::SessionPlayerSnapshot playerState = session_detail::snapshotPlayerForSession(*self_);
    World& world = srv_.worldFor(playerState.dimension);
    auto sendAuthoritativeBlock = [&](std::int32_t bx, std::int32_t by,
                                      std::int32_t bz, std::uint16_t state) {
        WriteBuffer rb;
        rb.position(bx, by, bz);
        rb.varint(state);
        conn_->trySendPacketLowPriority(proto::pl::sc::BlockUpdate, rb);
    };
    auto resyncCurrentBlock = [&](std::uint16_t previousState) {
        const auto currentState = world.getBlock(x, y, z);
        if (currentState != previousState) {
            WriteBuffer update;
            update.position(x, y, z);
            update.varint(currentState);
            srv_.broadcastPacketExceptInDimension(
                playerState.dimension, self_.get(),
                proto::pl::sc::BlockUpdate, update);
        }
        sendAuthoritativeBlock(x, y, z, currentState);
    };
    auto cancelDig = [&](std::optional<std::uint64_t> expectedRevision =
                             std::nullopt) {
        std::int32_t digX = 0, digY = 0, digZ = 0;
        bool wasActive = false;
        {
            std::lock_guard digLock(self_->digMtx);
            if (expectedRevision &&
                self_->digRevision != *expectedRevision)
                return;
            wasActive = self_->digActive;
            digX = self_->digX;
            digY = self_->digY;
            digZ = self_->digZ;
            self_->digActive = false;
            self_->digTotalTicks = 0;
            self_->digLastStage = 255;
            if (wasActive) ++self_->digRevision;
        }
        if (wasActive)
            srv_.broadcastDigStageAt(*self_, playerState.dimension,
                                     digX, digY, digZ, -1);
    };
    std::optional<nbt::Value> targetBlockEntityNbt;
    bool targetBlockEntityNbtLoaded = false;
    const auto snapshotTargetBlockEntityNbt = [&]() -> const nbt::Value* {
        if (!targetBlockEntityNbtLoaded) {
            if (const auto expectedKind =
                    blockEntityKindForBlockState(world.getBlock(x, y, z))) {
                targetBlockEntityNbt =
                    srv_.blockEntitiesFor(playerState.dimension)
                        .snapshotNbtAt(x, y, z, expectedKind);
            }
            targetBlockEntityNbtLoaded = true;
        }
        return targetBlockEntityNbt ? &*targetBlockEntityNbt : nullptr;
    };

    // BlockDig status 1 is an independent abort action. It must be handled
    // before the start/finish path so it cannot be hidden by that condition.
    if (status == 1) {
        cancelDig();
        ack(sequence);
        return;
    }

    if ((status == 0 || status == 2) &&
        !withinBlockInteractionRange(playerState, x, y, z)) {
        // A client is allowed to abort an existing dig from anywhere, but a
        // start/finish must be close enough to the block.  Canceling and
        // returning the authoritative state prevents a forged far-away
        // packet from starting or completing a server-side dig.
        cancelDig();
        sendAuthoritativeBlock(x, y, z, world.getBlock(x, y, z));
        ack(sequence);
        return;
    }

    if ((status==0 || status==2) && self_->dimension==0 && srv_.isSpawnProtected(x, z) && !srv_.isOp(self_->name)) {
        // cancel: re-send block and ack
        const std::uint16_t cur = world.getBlock(x, y, z);
        cancelDig();
        sendAuthoritativeBlock(x, y, z, cur);
        sendSystemText((msg::kRed + "Spawn protection prevents building here"));
        ack(sequence);
        return;
    }

    if (status == 0) {                                // start dig
        const std::uint16_t oldState = world.getBlock(x, y, z);
        cancelDig();
        const auto canMineInAdventure = [&] {
            const auto held = snapshotHeldMiningItem(*self_);
            return held && canPlayerBreakBlockInAdventure(
                playerState.gamemode, *held, oldState, srv_.tagManager_,
                snapshotTargetBlockEntityNbt);
        };

        if (playerState.gamemode == 1) {               // creative: instant break
            if (oldState != 0) {
                api::BlockBreakEvent ev;
                ev.player = self_.get();
                ev.x = x; ev.y = y; ev.z = z;
                ev.oldState = oldState;
                if (!blockEventDispatcher().onBlockBreak(x, y, z, oldState, self_.get())) {
                    resyncCurrentBlock(oldState);
                    ack(sequence);
                    return;
                }
                if (!srv_.events().blockBreak.fire(ev)) {
                    resyncCurrentBlock(oldState);
                    ack(sequence);
                    return;
                }
                if (srv_.jvmRuntime() &&
                    !srv_.jvmRuntime()->onBlockBreak(*self_, x, y, z, oldState)) {
                    resyncCurrentBlock(oldState);
                    ack(sequence);
                    return;
                }
                if (world.getBlock(x, y, z) != oldState) {
                    resyncCurrentBlock(oldState);
                    ack(sequence);
                    return;
                }
                world.setBlock(x, y, z, 0);
                srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, 0);
                if (const auto* broken = gen::blockByState(oldState);
                    broken && std::string(broken->name).find("_bed") != std::string::npos) {
                    srv_.invalidateRespawnPointsAt(self_->dimension, x, y, z);
                }
                world.scheduleNeighborUpdates(x, y, z);
            }
        } else if (oldState != 0 &&
                   (playerState.gamemode == 0 || canMineInAdventure())) {
            const gen::BlockDef* def = gen::blockByState(oldState);
            if (def && def->hardness >= 0.f) {
                const MiningContext context = miningContextFor(srv_, *self_);
                const MiningResult result = MiningCalculator::calculate(*def, context);
                if (result.ticks != MiningCalculator::kUnbreakable) {
                    {
                        std::lock_guard digLock(self_->digMtx);
                        self_->digActive = true;
                        self_->digX = x; self_->digY = y; self_->digZ = z;
                        self_->digStartTick = MiningCalculator::packDigStartTick(
                            srv_.tickNoForTest(), oldState);
                        // Survival blocks with a zero-tick result still complete in
                        // the authoritative tick loop, never in the packet handler.
                        self_->digTotalTicks = std::max(1, result.ticks);
                        self_->digLastStage = MiningCalculator::packDigStage(
                            0, result.harvest);
                        ++self_->digRevision;
                    }
                    srv_.broadcastDigStageAt(
                        *self_, playerState.dimension, x, y, z, 0);
                }
            }
        } else if (oldState != 0) {
            cancelDig();
            sendAuthoritativeBlock(x, y, z, oldState);
        }
    } else if (status == 2) {                         // finish (client-side timing)
        const std::uint16_t oldState = world.getBlock(x, y, z);
        const gen::BlockDef* def = gen::blockByState(oldState);
        bool digActive = false;
        std::int32_t digX = 0, digY = 0, digZ = 0;
        std::int64_t digStartTick = 0;
        std::int32_t digTotalTicks = 0;
        std::uint64_t digRevision = 0;
        {
            std::lock_guard digLock(self_->digMtx);
            digActive = self_->digActive;
            digX = self_->digX;
            digY = self_->digY;
            digZ = self_->digZ;
            digStartTick = self_->digStartTick;
            digTotalTicks = self_->digTotalTicks;
            digRevision = self_->digRevision;
        }
        const bool sameTarget = digActive &&
            digX == x && digY == y && digZ == z;
        const bool sameState = digActive &&
            MiningCalculator::digStartingState(digStartTick) == oldState;

        const auto held = snapshotHeldMiningItem(*self_);
        const bool canContinueMining = playerState.gamemode == 0 ||
            (playerState.gamemode == 2 && held &&
             canPlayerBreakBlockInAdventure(
                 playerState.gamemode, *held, oldState, srv_.tagManager_,
                 snapshotTargetBlockEntityNbt));
        if (playerState.gamemode == 1) {
            // A gamemode change must not let a survival dig complete later.
            cancelDig(digRevision);
        } else if (!canContinueMining) {
            cancelDig(digRevision);
            sendAuthoritativeBlock(x, y, z, oldState);
        } else if (!sameTarget || !sameState || oldState == 0 || !def || def->hardness < 0.f) {
            // Finish is never an implicit start. Reject untracked/wrong-target
            // packets and re-synchronise the requested block.
            cancelDig(digRevision);
            sendAuthoritativeBlock(x, y, z, oldState);
        } else {
            const std::int64_t elapsed = srv_.tickNoForTest() -
                MiningCalculator::unpackDigStartTick(digStartTick);
            if (elapsed < digTotalTicks) {
                // Clients can report completion a tick before the server's
                // authoritative deadline because their local mining clock and
                // the server tick are not phase-aligned. Treat this as an
                // early completion notice, not an abort: tickDigs keeps the
                // same target and completes it only at the stored deadline.
            }
            // At/after the authoritative deadline, tickDigs owns mutation,
            // event dispatch, durability, and drops.
        }
    }
    ack(sequence);                                      // ALWAYS ack sequences
}
} // namespace cppfm
