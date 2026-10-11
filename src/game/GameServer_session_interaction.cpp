#include "GameServer.hpp"
#include "SessionInteractionSupport.hpp"
#include "BlockEntityKinds.hpp"
#include "BlockEntityDrops.hpp"
#include "SecureChatPolicy.hpp"
#include "Messages.hpp"
#include "BlockEvent.hpp"
#include "MetadataTypes.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "PositionPackets.hpp"
#include "StairsHelper.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "BehaviorTree.hpp"
#include "EquipmentComponent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "CombatManager.hpp"
#include "MobSpawner.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
#include "PotionBrewing.hpp"
#include "Particles.hpp"
#include "MiningCalculator.hpp"
#include "HungerManager.hpp"
#include "AdventureBlockBreak.hpp"

#include <optional>

namespace cppfm {
using namespace proto;

namespace {
constexpr std::uint32_t kFoodComponentId = 21;
constexpr std::uint32_t kConsumableComponentId = 22;

struct FoodUseProperties {
    std::int32_t nutrition = 0;
    float saturationModifier = 0.0F;
    bool canAlwaysEat = false;
    bool vanillaDefault = false;
};

const std::vector<std::uint8_t>* itemComponentPayload(
    const ItemStack& stack, const std::uint32_t componentId) {
    const auto found = std::find_if(
        stack.components.begin(), stack.components.end(),
        [componentId](const auto& entry) {
            return entry.first == componentId;
        });
    return found == stack.components.end() ? nullptr : &found->second;
}

bool itemComponentRemoved(const ItemStack& stack,
                          const std::uint32_t componentId) {
    return std::find(stack.removedComponents.begin(),
                     stack.removedComponents.end(), componentId) !=
           stack.removedComponents.end();
}

bool isDefaultAlwaysEdibleFood(const std::string& itemName) {
    return itemName == "minecraft:golden_apple" ||
           itemName == "minecraft:enchanted_golden_apple" ||
           itemName == "minecraft:chorus_fruit";
}

std::optional<FoodUseProperties> foodUseProperties(
    const ItemStack& stack, bool& malformed) {
    malformed = false;
    if (const auto* payload =
            itemComponentPayload(stack, kFoodComponentId)) {
        try {
            ReadBuffer input(*payload);
            const auto nutrition = input.varint();
            const float saturationModifier = input.f32();
            const bool canAlwaysEat = input.boolean();
            if (input.remaining() != 0 || nutrition < 0 || nutrition > 20 ||
                !std::isfinite(saturationModifier) ||
                saturationModifier < 0.0F)
                throw std::runtime_error("invalid food component payload");
            return FoodUseProperties{nutrition, saturationModifier,
                                     canAlwaysEat, false};
        } catch (const std::exception&) {
            malformed = true;
            return std::nullopt;
        }
    }
    if (itemComponentRemoved(stack, kFoodComponentId)) return std::nullopt;

    const std::string itemName = stack.name();
    // Cake is a placeable block: its bites are consumed through UseItemOn,
    // never by using the cake item in the air.
    if (itemName == "minecraft:cake") return std::nullopt;
    const auto found = HungerManager::foodTable().find(itemName);
    if (found == HungerManager::foodTable().end()) return std::nullopt;
    const auto& food = found->second;
    const float modifier = food.food > 0
        ? food.saturation / (2.0F * static_cast<float>(food.food))
        : 0.0F;
    return FoodUseProperties{food.food, modifier,
                             isDefaultAlwaysEdibleFood(itemName), true};
}

std::optional<std::uint32_t> defaultConsumeTicks(
    const ItemStack& stack, const bool hasFoodComponent) {
    const std::string itemName = stack.name();
    if (itemName == "minecraft:cake") return std::nullopt;
    float seconds = 0.0F;
    if (itemName == "minecraft:dried_kelp") {
        seconds = 0.8F;
    } else if (itemName == "minecraft:honey_bottle") {
        seconds = 2.0F;
    } else if (hasFoodComponent ||
               HungerManager::foodTable().contains(itemName)) {
        seconds = 1.6F;
    } else if (itemName == "minecraft:milk_bucket" ||
               itemName == "minecraft:ominous_bottle") {
        seconds = 1.6F;
    } else {
        return std::nullopt;
    }
    return Session::ConsumableUseState::consumeTicks(seconds);
}

std::optional<std::uint32_t> configuredConsumeTicks(
    const ItemStack& stack, const bool hasFoodComponent, bool& malformed) {
    malformed = false;
    if (const auto* payload =
            itemComponentPayload(stack, kConsumableComponentId)) {
        const auto ticks =
            Session::ConsumableUseState::consumeTicksFromPayload(*payload);
        malformed = !ticks.has_value();
        return ticks;
    }
    if (itemComponentRemoved(stack, kConsumableComponentId)) {
        return std::nullopt;
    }
    return defaultConsumeTicks(stack, hasFoodComponent);
}

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

void Session::tickConsumableUse() {
    const auto slot = consumableUse_.activeSlot();
    if (!slot) return;

    ItemStack usedStack;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (*slot >= self_->inv.size() ||
            !consumableUse_.matchesActiveStack(*slot, self_->inv[*slot])) {
            consumableUse_.cancel();
            self_->isEating = false;
            self_->eatTicks = 0;
            return;
        }
        usedStack = self_->inv[*slot];
        if (self_->eatTicks < std::numeric_limits<std::int32_t>::max())
            ++self_->eatTicks;
    }

    if (consumableUse_.continueOneTick() !=
        ConsumableUseState::TickResult::Ready)
        return;
    const auto completedSlot = consumableUse_.takeCompletedSlot();
    if (!completedSlot || *completedSlot != *slot) return;

    {
        std::lock_guard playerLock(self_->stateMtx);
        self_->isEating = false;
        self_->eatTicks = 0;
    }

    bool malformedFood = false;
    const auto food = foodUseProperties(usedStack, malformedFood);
    const std::string itemName = usedStack.name();
    if (food && !malformedFood) {
        if (food->vanillaDefault) {
            // Preserve vanilla bowl/bottle remainders handled by the legacy
            // food table; unlike the old UseItem path, this is deferred until
            // the consume deadline and is keyed by component presence, not by
            // whether hunger happened to change.
            srv_.handleFoodConsume(*self_, itemName);
        } else {
            srv_.addFoodAndSaturation(
                *self_, food->nutrition,
                std::min(20.0F,
                         static_cast<float>(food->nutrition) *
                             food->saturationModifier * 2.0F));
        }
        srv_.addHungerExhaustion(*self_, 0.005F);
    }

    // This older fork lacks the shared data-driven food-effect module present
    // in the parent tree. Keep only the vanilla golden-apple effects needed by
    // its black-box regression; the parent should retain its broader effect
    // implementation when porting the state-machine changes.
    std::vector<EffectInstance> consumeEffects;
    if (itemName == "minecraft:golden_apple") {
        consumeEffects = {
            EffectInstance{effects::Regeneration, 1, 100, false, true, true},
            EffectInstance{effects::Absorption, 0, 2400, false, true, true}};
    } else if (itemName == "minecraft:enchanted_golden_apple") {
        consumeEffects = {
            EffectInstance{effects::Regeneration, 1, 400, false, true, true},
            EffectInstance{effects::Absorption, 3, 2400, false, true, true},
            EffectInstance{effects::Resistance, 0, 6000, false, true, true},
            EffectInstance{effects::FireResistance, 0, 6000, false, true, true}};
    }
    std::vector<EffectInstance> updatedEffects;
    std::shared_ptr<Connection> connection;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        connection = self_->conn;
        entityId = self_->entityId;
        dimension = self_->dimension;
        for (const auto& next : consumeEffects) {
            const auto current = std::find_if(
                self_->effects.begin(), self_->effects.end(),
                [&](const EffectInstance& effect) {
                    return effect.type == next.type;
                });
            if (current == self_->effects.end()) {
                self_->effects.push_back(next);
                updatedEffects.push_back(next);
            } else if (current->amplifier <= next.amplifier &&
                       (current->amplifier < next.amplifier ||
                        current->durationTicks < next.durationTicks)) {
                *current = next;
                updatedEffects.push_back(next);
            }
        }
    }
    for (const auto& effect : updatedEffects) {
        WriteBuffer packet;
        packet.varint(entityId);
        packet.varint(effect.type);
        packet.varint(effect.amplifier);
        packet.varint(effect.durationTicks);
        packet.u8(static_cast<std::uint8_t>(effectFlags(effect)));
        if (connection)
            connection->trySendPacket(proto::pl::sc::EntityEffect, packet);
        srv_.broadcastPacketExceptInDimension(
            dimension, self_.get(), proto::pl::sc::EntityEffect, packet);
    }
    if (!updatedEffects.empty()) srv_.onEffectsChanged(self_.get());

    ItemStack consumed = usedStack;
    consumed.count = 1;
    srv_.onConsumeItem(*self_, consumed);
    {
        std::lock_guard playerLock(self_->stateMtx);
        auto& current = self_->inv[*slot];
        if (!current.empty() && current.sameItemData(usedStack) &&
            self_->gamemode != 1) {
            if (--current.count <= 0) current = InvSlot::air();
        }
    }
    srv_.resendInventory(*self_);
}

void Session::onHeldSlot(ReadBuffer& in) {
    const std::int16_t slot = in.i16();
    if (slot >= 0 && slot < 9) {
        bool changed = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            changed = self_->heldSlot != slot;
            self_->heldSlot = slot;
        }
        const auto activeSlot = consumableUse_.activeSlot();
        if (changed && activeSlot && *activeSlot >= 36 && *activeSlot <= 44) {
            consumableUse_.cancel();
            std::lock_guard playerLock(self_->stateMtx);
            self_->isEating = false;
            self_->eatTicks = 0;
        }
    }
}
void Session::onPlayerAction(ReadBuffer& in) {
    const std::int32_t status = in.varint();
    std::int32_t x, y, z;
    in.position(x, y, z);
    (void)in.i8();                                    // face
    const std::int32_t sequence = in.varint();

    // Yarn 1.21.4 PlayerActionC2SPacket.Action.RELEASE_USE_ITEM is action 5.
    // It shares the packet with block-dig actions but has no block semantics.
    if (status == 5) {
        (void)consumableUse_.release();
        {
            std::lock_guard playerLock(self_->stateMtx);
            self_->isEating = false;
            self_->eatTicks = 0;
        }
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
struct Session::UseItemOnRequest {
    std::int32_t x, y, z, tx, ty, tz;
    int face;
    bool survival;
    ItemUseContext context;
    session_detail::SessionPlayerSnapshot playerState;
    InvSlot heldItem;
};

bool Session::handleUseItemOnInteractions(const UseItemOnRequest& request) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto& ctx = request.context;
    if (request.playerState.dimension == 0 &&
        srv_.isSpawnProtected(tx, tz) && !srv_.isOp(self_->name)) {
        // check if placing a block (held is block item) – cancel
        const bool isBlockPlace = !request.heldItem.empty() &&
            gen::blockByName(request.heldItem.name()) != nullptr;
        if (isBlockPlace) {
            sendSystemText((msg::kRed + "Spawn protection prevents building here"));
            return true;
        }
    }

    {
        const std::uint16_t _clickedSt =
            srv_.worldFor(request.playerState.dimension).getBlock(x, y, z);
        if (!blockEventDispatcher().onBlockClicked(x, y, z, _clickedSt, d, self_.get())) return true;
        if (!session_detail::interactionContextUnchanged(
                *self_, request.playerState, request.heldItem)) return true;
        api::BlockClickedEvent _bcev; _bcev.player=self_.get(); _bcev.x=x; _bcev.y=y; _bcev.z=z; _bcev.state=_clickedSt; _bcev.face=d;
        if (!api::events().blockClicked.fire(_bcev)) return true;
        if (!session_detail::interactionContextUnchanged(
                *self_, request.playerState, request.heldItem)) return true;
        if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onBlockClicked(*self_, x, y, z, _clickedSt, d)) return true;
        if (!session_detail::interactionContextUnchanged(
                *self_, request.playerState, request.heldItem)) return true;
    }
    // right-click on interactive blocks opens menus (vanilla behaviour)
    {
        const std::uint16_t clickedState =
            srv_.worldFor(request.playerState.dimension).getBlock(x, y, z);
        const gen::BlockDef* bdef = gen::blockByState(clickedState);
        if (bdef) {
            const std::string bn(bdef->name);
            bool isMenuBlock = bn.find("chest") != std::string::npos ||
                bn == "minecraft:furnace" || bn == "minecraft:blast_furnace" ||
                bn == "minecraft:smoker" ||
                bn == "minecraft:hopper" || bn == "minecraft:dispenser" ||
                bn == "minecraft:dropper" ||
                bn == "minecraft:crafting_table" ||
                bn == "minecraft:enchanting_table" ||
                bn.find("anvil") != std::string::npos ||
                bn == "minecraft:brewing_stand" ||
                bn == "minecraft:stonecutter" ||
                bn == "minecraft:grindstone" ||
                bn.find("smithing") != std::string::npos ||
                bn == "minecraft:beacon" ||
                bn == "minecraft:loom" ||
                bn == "minecraft:barrel" ||
                bn.find("shulker_box") != std::string::npos ||
                bn == "minecraft:crafter" ||
                bn == "minecraft:cartography_table" ||
                bn == "minecraft:lectern";
            if (isMenuBlock) {
                // Allow opening from any face if not sneaking; ensure sneaking bypass
                if (ctx.isSneaking && !bn.empty()) {
                    // sneaking still places block, so skip menu
                } else {
                    openMenuAt(x, y, z, clickedState);
                    return true;
                }
            }
            if (d == 1) {
            // redstone interactables (lever / button / comparator) consume the click
            if (bn == "minecraft:lever" ||
                bn.find("_button") != std::string::npos ||
                bn.find("comparator") != std::string::npos) {
                srv_.redstoneFor(request.playerState.dimension).onInteract(
                    x, y, z, srv_.tickNoForTest());
                return true;
            }
            if (bn.find("_bed") != std::string::npos &&
                bn.rfind("minecraft:", 0) == 0 && bn != "minecraft:bedrock") {
                const bool night = srv_.isNight();
                if (!night) {
                    sendSystemText((msg::kGray + "You can only sleep at night"));
                    return true;
                }
                self_->sleeping = true;
                self_->bedX = x; self_->bedY = y; self_->bedZ = z;
                self_->hasRespawnPoint = true;
                self_->respawnX = x; self_->respawnY = y; self_->respawnZ = z;
                self_->respawnDimension = request.playerState.dimension;
                self_->respawnAngle = self_->yaw;
                srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_);
                WriteBuffer sp;
                sp.position(x, y, z);
                sp.f32(self_->respawnAngle);
                conn_->trySendPacket(proto::pl::sc::SetDefaultSpawn, sp);
                int sleepingCount = 0, survivalCount = 0;
                for (auto& p : srv_.playersSnapshot()) {
                    if (!p->inPlay || p->gamemode != 0) continue;
                    ++survivalCount;
                    if (p->sleeping) ++sleepingCount;
                }
                if (sleepingCount >= survivalCount) {
                    srv_.setTimeOfDay(0);              // morning
                    if (srv_.raining()) srv_.forceWeatherClear();
                    for (auto& p : srv_.playersSnapshot())
                        if (p->sleeping) {
                            p->sleeping = false;
                            double wx = p->bedX + 1.5, wz = p->bedZ + 0.5;
                            srv_.teleportPlayer(*p, wx, p->bedY + 0.5, wz,
                                std::array<float, 2>{p->yaw, 0.0f});
                        }
                    srv_.broadcastSystemText((msg::kGray + "Good morning!"));
                } else {
                    sendSystemText((msg::kGray + "Sleeping... (" +
                                   std::to_string(sleepingCount) + "/" +
                                   std::to_string(survivalCount) + ")"));
                }
                return true;
            }
        }
    }

    {
        World& ww = srv_.worldFor(request.playerState.dimension);
        uint16_t cst = ww.getBlock(x,y,z);
        auto* cdef = gen::blockByState(cst);
        if (cdef && std::string(cdef->name)=="minecraft:cake") {
            if (HungerManager::handleCakeBlockConsume(srv_, *self_, x,y,z)) {
                return true;
            }
        }
    }
    }

    return false;
}
bool Session::handleUseItemOnToolActions(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto survival = request.survival;
    {
        InvSlot heldCopy = (self_->heldSlot >= 0 && self_->heldSlot < 9) ? self_->inv[36 + self_->heldSlot] : InvSlot::air();
        const std::string heldNameForPortal = heldCopy.empty() ? std::string() : heldCopy.name();
        bool isFlint = heldNameForPortal == "minecraft:flint_and_steel";
        bool isFireCharge = heldNameForPortal == "minecraft:fire_charge";
        if ((isFlint || isFireCharge) && !heldCopy.empty()) {
            World& w = srv_.worldFor(self_->dimension);
            std::uint16_t clickedSt = w.getBlock(x, y, z);
            const gen::BlockDef* cd = gen::blockByState(clickedSt);
            bool clickedIsObsidian = cd && std::string(cd->name) == "minecraft:obsidian";
            if (clickedIsObsidian) {
                const auto& mp = gen::blockNameToState();
                auto obsIt = mp.find("minecraft:obsidian");
                std::uint16_t obsidian = obsIt != mp.end() ? static_cast<std::uint16_t>(obsIt->second) : 2397;
                const gen::BlockDef* portalDef = gen::blockByName("minecraft:nether_portal");
                bool ignited = false;
                auto fillInterior = [&](int orient, int ox, int oy, int oz) {
                    std::uint16_t portalState = 6033;
                    if (portalDef) {
                        if (orient == 0) portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","x"}}));
                        else portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","z"}}));
                    } else {
                        auto it2 = mp.find("minecraft:nether_portal");
                        if (it2 != mp.end()) portalState = static_cast<std::uint16_t>(it2->second);
                    }
                    for (int dy=1; dy<=3; ++dy) for (int dx=1; dx<=2; ++dx) {
                        int32_t wx, wz;
                        if (orient==0) { wx = ox+dx; wz = oz; }
                        else { wx = ox; wz = oz+dx; }
                        int32_t wy = oy+dy;
                        w.setBlock(wx, wy, wz, portalState);
                        srv_.broadcastBlockChangeFor(self_->dimension, wx, wy, wz, portalState);
                        if (srv_.blockTicks()) srv_.blockTicks()->schedule(wx, wy, wz, srv_.tickNow() + 1 + (nextRandom()%20));
                    }
                    int32_t cxp = ox+1 + (orient==0?1:0);
                    int32_t czp = oz + (orient==1?1:0);
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:block.portal.ambient",
                                           cxp+0.5, oy+2, czp+0.5, 0.8f, 1.0f,
                                           "block");
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:item.flintandsteel.use",
                                           x+0.5, y+0.5, z+0.5, 1.f, 1.f,
                                           "block");
                };
                const auto geometry = w.verticalGeometry();
                for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                    for (int ox = x - 3; ox <= x && !ignited; ++ox) {
                        if (oy < geometry.minY || oy+4 >= geometry.maxY()) continue;
                        bool valid = true;
                        for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                            int32_t wx = ox+dx; int32_t wy = oy+dy; int32_t wz = z;
                            w.generateChunkIfMissing(wx>>4, wz>>4);
                            std::uint16_t st = w.getBlock(wx, wy, wz);
                            bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                            if (isBorder) { if (st != obsidian) valid=false; }
                            else { if (st != 0) valid=false; }
                        }
                        if (!valid) continue;
                        fillInterior(0, ox, oy, z);
                        ignited = true;
                    }
                }
                if (!ignited) {
                    for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                        for (int oz = z - 3; oz <= z && !ignited; ++oz) {
                            if (oy < geometry.minY || oy+4 >= geometry.maxY()) continue;
                            bool valid = true;
                            for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                                int32_t wx = x; int32_t wy = oy+dy; int32_t wz = oz+dx;
                                w.generateChunkIfMissing(wx>>4, wz>>4);
                                std::uint16_t st = w.getBlock(wx, wy, wz);
                                bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                                if (isBorder) { if (st != obsidian) valid=false; }
                                else { if (st != 0) valid=false; }
                            }
                            if (!valid) continue;
                            fillInterior(1, x, oy, oz);
                            ignited = true;
                        }
                    }
                }
                if (ignited) {
                    if (self_->gamemode == 0) {
                        if (isFlint) {
                            auto* slot = &self_->inv[36 + self_->heldSlot];
                            bool broken = slot->applyDamage(1);
                            if (broken) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        } else if (isFireCharge) {
                            auto* slot = &self_->inv[36 + self_->heldSlot];
                            if (--slot->count <= 0) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        }
                    }
                    return true;
                }
            }
        }
    }

    if (!heldItem.empty() && (heldItem.name()=="minecraft:flint_and_steel" || heldItem.name()=="minecraft:fire_charge")) {
        std::uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x,y,z);
        const gen::BlockDef* cbd = gen::blockByState(clickedSt);
        if (cbd && std::string(cbd->name)=="minecraft:tnt") {
            srv_.worldFor(self_->dimension).setBlock(x,y,z,0);
            srv_.broadcastBlockChangeFor(self_->dimension, x,y,z,0);
            srv_.spawnPrimedTntFor(self_->dimension, x+0.5, y+0.5, z+0.5,
                                   0, 0.2, 0, 80);
            srv_.broadcastSoundFor(self_->dimension,
                                   "minecraft:entity.tnt.primed", x+0.5,
                                   y+0.5, z+0.5, 1.f, 1.f, "block");
            if (survival) {
                auto& mh = self_->inv[36 + self_->heldSlot];
                if (heldItem.name()=="minecraft:flint_and_steel") { if (mh.applyDamage(1)) mh = ItemStack::air(); }
                else { if (--mh.count <= 0) mh = ItemStack::air(); }
                srv_.resendInventory(*self_);
            }
            return true;
        }
    }

    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName == "minecraft:water_bucket" || heldName == "minecraft:lava_bucket") {
            std::uint16_t target = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            bool replaceable = (target == 0);
            // also consider replaceable plants? treat only air for now
            World& targetWorld = srv_.worldFor(self_->dimension);
            if (replaceable && targetWorld.verticalGeometry().containsY(ty)) {
                std::string fluidName = (heldName == "minecraft:water_bucket") ? "minecraft:water" : "minecraft:lava";
                std::uint16_t fluidState = static_cast<std::uint16_t>(gen::stateWithPropsList(fluidName, {{"level","0"}}));
                if (fluidState==0) {
                    auto it = gen::blockNameToState().find(fluidName);
                    if (it != gen::blockNameToState().end()) fluidState = static_cast<std::uint16_t>(it->second);
                }
                if (!targetWorld.setBlock(tx, ty, tz, fluidState)) return true;
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, fluidState);
                if (survival) {
                    auto* mh = &self_->inv[36 + self_->heldSlot];
                    *mh = ItemStack::ofName("minecraft:bucket", 1);
                    srv_.resendInventory(*self_);
                }
                srv_.broadcastSoundFor(self_->dimension,
                                       "minecraft:item.bucket.empty", tx+0.5,
                                       ty+0.5, tz+0.5, 1.f, 1.f, "block");
                return true;
            }
        } else if (heldName == "minecraft:bucket") {
            auto tryPick = [&](std::int32_t px,std::int32_t py,std::int32_t pz)->bool{
                std::uint16_t bs = srv_.worldFor(self_->dimension).getBlock(px,py,pz);
                const gen::BlockDef* bd = gen::blockByState(bs);
                if (!bd) return false;
                bool isWater=false,isLava=false;
                if (bd->name=="minecraft:water") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isWater=true;
                } else if (bd->name=="minecraft:lava") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isLava=true;
                }
                if (!isWater && !isLava) return false;
                srv_.worldFor(self_->dimension).setBlock(px,py,pz, 0);
                srv_.broadcastBlockChangeFor(self_->dimension, px,py,pz, 0);
                if (survival) {
                    auto* mh = &self_->inv[36 + self_->heldSlot];
                    std::string newName = isWater ? "minecraft:water_bucket" : "minecraft:lava_bucket";
                    *mh = ItemStack::ofName(newName, 1);
                    srv_.resendInventory(*self_);
                    srv_.onBucketFilled(*self_, newName);
                } else {
                    std::string newName = isWater ? "minecraft:water_bucket" : "minecraft:lava_bucket";
                    srv_.onBucketFilled(*self_, newName);
                }
                srv_.broadcastSoundFor(self_->dimension,
                                       "minecraft:item.bucket.fill", px+0.5,
                                       py+0.5, pz+0.5, 1.f, 1.f, "block");
                return true;
            };
            if (tryPick(x,y,z) || tryPick(tx,ty,tz)) {
                return true;
            }
        } else if (heldName == "minecraft:flint_and_steel" || heldName == "minecraft:fire_charge") {
            std::uint16_t target = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            if (target == 0) {
                if (!srv_.worldFor(self_->dimension).verticalGeometry().containsY(ty))
                    return true;
                bool canPlace = true;
                if (srv_.gameRules().contains("doFireTick") && !srv_.gameRules().getBool("doFireTick")) canPlace = false;
                if (canPlace) {
                    std::uint16_t belowSt = srv_.worldFor(self_->dimension).getBlock(tx, ty-1, tz);
                    const gen::BlockDef* belowDef = gen::blockByState(belowSt);
                    bool soulBase = false;
                    if (belowDef) {
                        auto &tags = srv_.tagManager_.blockTags;
                        auto it = tags.find("minecraft:soul_fire_base_blocks");
                        if (it != tags.end()) {
                            auto nit = gen::blockNameToState().find(std::string(belowDef->name));
                            if (nit != gen::blockNameToState().end()) {
                                uint32_t defId = static_cast<uint32_t>(nit->second);
                                soulBase = it->second.count(defId) > 0;
                            }
                        }
                        if (!soulBase) {
                            soulBase = std::string(belowDef->name)=="minecraft:soul_sand" || std::string(belowDef->name)=="minecraft:soul_soil";
                        }
                    }
                    std::string fireName = soulBase ? "minecraft:soul_fire" : "minecraft:fire";
                    auto it = gen::blockNameToState().find(fireName);
                    if (it == gen::blockNameToState().end()) it = gen::blockNameToState().find("minecraft:fire");
                    if (it != gen::blockNameToState().end()) {
                        std::uint16_t fireState = static_cast<std::uint16_t>(it->second);
                        if (!srv_.worldFor(self_->dimension).setBlock(
                                tx, ty, tz, fireState))
                            return true;
                        srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, fireState);
                        if (survival) {
                            auto* mh = &self_->inv[36 + self_->heldSlot];
                            if (heldName=="minecraft:flint_and_steel") {
                                if (mh->applyDamage(1)) *mh = ItemStack::air();
                            } else {
                                if (--mh->count <=0) *mh = ItemStack::air();
                            }
                            srv_.resendInventory(*self_);
                        }
                        srv_.broadcastSoundFor(self_->dimension,
                                               "minecraft:item.flintandsteel.use",
                                               tx+0.5, ty+0.5, tz+0.5, 1.f,
                                               1.f, "block");
                    }
                }
                return true;
            }
        }
    }

    // ---- bone meal fertilize hook ----
    if (!heldItem.empty() && heldItem.name() == "minecraft:bone_meal") {
        const std::uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        if (clickedSt != 0) {
            const gen::BlockDef* cb = gen::blockByState(clickedSt);
            if (cb) {
                const std::string bn(cb->name);
                auto* beh = srv_.blockTicksFor(self_->dimension).behaviorFor(bn);
                if (beh && beh->fertilize(srv_.worldFor(self_->dimension), x, y, z, clickedSt, &srv_)) {
                    const std::uint16_t newSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
                    srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, newSt);
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:item.bone_meal.use",
                                           x + 0.5, y + 0.5, z + 0.5);
                    if (survival) {
                        auto* mh = &self_->inv[36 + self_->heldSlot];
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
                    return true;
                }
            }
        }
    }

    return false;
}
bool Session::handleUseItemOnDoorAndSlab(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto dir = request.face;
    const auto survival = request.survival;
    const auto& ctx = request.context;
    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName.size() > 5 && heldName.rfind("_door", heldName.size() - 5) != std::string::npos) {
            auto& doorWorld = srv_.worldFor(self_->dimension);
            const auto geometry = doorWorld.verticalGeometry();
            if (!geometry.containsY(ty) || !geometry.containsY(ty + 1) ||
                !doorWorld.generateChunkIfMissing(tx >> 4, tz >> 4))
                return true;
            if (!hasSturdyTopFace(doorWorld.getBlock(tx, ty - 1, tz)))
                return true;
            const gen::BlockDef* ddef = gen::blockByName(heldName);
            if (ddef && doorWorld.getBlock(tx, ty, tz) == 0 &&
                doorWorld.getBlock(tx, ty + 1, tz) == 0) {
                float yaw = self_->yaw;
                const char* facing = "north";
                if (yaw >= 45.f && yaw < 135.f) facing = "west";
                else if (yaw >= 135.f && yaw < 225.f) facing = "south";
                else if (yaw >= 225.f && yaw < 315.f) facing = "east";
                // hinge logic via solid faces and neighboring doors (vanilla DoorBlock per Yarn 1.21.4: isFullCube count + hitPos tie - strict B5)
                std::string hingeStr = "left";
                {
                    auto isFullCubeAt = [&](int nx,int ny,int nz)->bool{
                        uint16_t s2 = srv_.worldFor(self_->dimension).getBlock(nx,ny,nz);
                        if(s2==0) return false;
                        auto* bd2 = gen::blockByState(s2);
                        if(!bd2) return false;
                        std::string n(bd2->name);
                        if(n.find("_slab")!=std::string::npos){
                            for(auto& [k,v]: gen::propsOf(s2)) if(k=="type" && v!="double") return false;
                        }
                        if(n.find("stairs")!=std::string::npos) return false;
                        return !bd2->transparent;
                    };
                    auto isDoorLowerAt = [&](int nx,int ny,int nz)->bool{
                        uint16_t s2 = srv_.worldFor(self_->dimension).getBlock(nx,ny,nz);
                        if(s2==0) return false;
                        auto* bd2 = gen::blockByState(s2);
                        if(!bd2 || std::string(bd2->name).find("_door")==std::string::npos) return false;
                        for(auto& [k,v]: gen::propsOf(s2)) if(k=="half" && v=="lower") return true;
                        return false;
                    };
                    int dxL=0, dzL=0, dxR=0, dzR=0;
                    std::string fs(facing);
                    if(fs=="north"){ dxL=-1; dzL=0; dxR=1; dzR=0; }
                    else if(fs=="south"){ dxL=1; dzL=0; dxR=-1; dzR=0; }
                    else if(fs=="west"){ dxL=0; dzL=1; dxR=0; dzR=-1; }
                    else if(fs=="east"){ dxL=0; dzL=-1; dxR=0; dzR=1; }
                    else { dxL=-1; dzL=0; dxR=1; dzR=0; }
                    int i = 0;
                    if(isFullCubeAt(tx+dxL, ty, tz+dzL)) i += -1;
                    if(isFullCubeAt(tx+dxL, ty+1, tz+dzL)) i += -1;
                    if(isFullCubeAt(tx+dxR, ty, tz+dzR)) i += 1;
                    if(isFullCubeAt(tx+dxR, ty+1, tz+dzR)) i += 1;
                    bool leftDoor = isDoorLowerAt(tx+dxL, ty, tz+dzL);
                    bool rightDoor = isDoorLowerAt(tx+dxR, ty, tz+dzR);
                    if((!leftDoor || rightDoor) && i <= 0){
                        if((!rightDoor || leftDoor) && i >= 0){
                            int j = (fs=="east"?1: fs=="west"?-1:0);
                            int k = (fs=="south"?1: fs=="north"?-1:0);
                            const double cursorX = ctx.cursor.x;
                            const double cursorZ = ctx.cursor.z;
                            const bool chooseLeft =
                                (j >= 0 || !(cursorZ < 0.5)) &&
                                (j <= 0 || !(cursorZ > 0.5)) &&
                                (k >= 0 || !(cursorX > 0.5)) &&
                                (k <= 0 || !(cursorX < 0.5));
                            hingeStr = chooseLeft ? "left" : "right";
                        } else {
                            hingeStr = "left";
                        }
                    } else {
                        hingeStr = "right";
                    }
                }
                bool powered = false;
                powered = srv_.redstoneFor(self_->dimension).isPoweredHere(tx,ty,tz) ||
                          srv_.redstoneFor(self_->dimension).isPoweredHere(tx,ty+1,tz);
                std::string openStr = powered ? "true" : "false";
                std::string poweredStr = powered ? "true" : "false";
                const auto lower =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","lower"},{"facing",facing},{"open",openStr},{"hinge",hingeStr},{"powered",poweredStr}}));
                const auto upper =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","upper"},{"facing",facing},{"open",openStr},{"hinge",hingeStr},{"powered",poweredStr}}));
                if (!blockEventDispatcher().onBlockPlace(tx, ty, tz, 0, lower, self_.get()) ||
                    !blockEventDispatcher().onBlockPlace(tx, ty + 1, tz, 0, upper, self_.get()))
                    return true;
                if (!session_detail::interactionContextUnchanged(
                        *self_, request.playerState, request.heldItem))
                    return true;
                api::BlockPlaceEvent lowerEvent;
                lowerEvent.player = self_.get(); lowerEvent.x = tx; lowerEvent.y = ty;
                lowerEvent.z = tz; lowerEvent.newState = lower;
                api::BlockPlaceEvent upperEvent;
                upperEvent.player = self_.get(); upperEvent.x = tx; upperEvent.y = ty + 1;
                upperEvent.z = tz; upperEvent.newState = upper;
                if (!srv_.events().blockPlace.fire(lowerEvent) ||
                    !srv_.events().blockPlace.fire(upperEvent)) return true;
                if (!session_detail::interactionContextUnchanged(
                        *self_, request.playerState, request.heldItem))
                    return true;
                if (srv_.jvmRuntime() &&
                    (!srv_.jvmRuntime()->onBlockPlace(*self_, tx, ty, tz, lower) ||
                     !srv_.jvmRuntime()->onBlockPlace(*self_, tx, ty + 1, tz, upper)))
                    return true;
                if (!session_detail::interactionContextUnchanged(
                        *self_, request.playerState, request.heldItem))
                    return true;
                if (doorWorld.getBlock(tx, ty, tz) != 0 ||
                    doorWorld.getBlock(tx, ty + 1, tz) != 0) return true;
                const std::array doorUpdates{
                    World::BlockUpdate{tx, ty, tz, lower},
                    World::BlockUpdate{tx, ty + 1, tz, upper},
                };
                if (!doorWorld.setBlockBatch(doorUpdates)) return true;
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, lower);
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty + 1, tz, upper);
                if (survival) {
                    auto mh = &self_->inv[36 + self_->heldSlot];
                    if (ItemStack::maxDamageFor(mh->itemId) > 0) {
                        if (mh->applyDamage(1)) *mh = ItemStack::air();
                        srv_.resendInventory(*self_);
                    } else {
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
                }
                return true;
            }
        }
    }

    if (!heldItem.empty()) {
        std::string hName = heldItem.name();
        if (hName.find("_slab") != std::string::npos) {
            uint16_t existing = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            const gen::BlockDef* ed = gen::blockByState(existing);
            if (ed && std::string(ed->name) == hName) {
                std::string curType = getPropStr(existing, "type");
                if (curType != "double") {
                    bool isBottom = curType=="bottom";
                    bool hittingOpposite = false;
                    if(isBottom) hittingOpposite = (d==1 || ctx.cursor.y > 0.5);
                    else /* top */ hittingOpposite = (d==0 || ctx.cursor.y < 0.5);
                    if(hittingOpposite){
                        std::vector<std::pair<std::string_view,std::string_view>> p;
                        for(auto&[k,v]: gen::propsOf(existing)) if(k!="type" && k!="waterlogged") p.emplace_back(k,v);
                        p.emplace_back("type","double");
                        // double slab must be waterlogged false
                        bool hasWl=false; for(int i=0;i<ed->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[ed->propsOff+i]]; if(pd.name=="waterlogged") hasWl=true; }
                        if(hasWl) p.emplace_back("waterlogged","false");
                        uint16_t dbl = static_cast<uint16_t>(gen::stateWithProps(*ed, p));
                        if (!blockEventDispatcher().onBlockPlace(tx, ty, tz, existing, dbl, self_.get())) return true;
                        if (!session_detail::interactionContextUnchanged(
                                *self_, request.playerState,
                                request.heldItem)) return true;
                        api::BlockPlaceEvent ev2; ev2.player=self_.get(); ev2.x=tx; ev2.y=ty; ev2.z=tz; ev2.newState=dbl;
                        if (!srv_.events().blockPlace.fire(ev2)) return true;
                        if (!session_detail::interactionContextUnchanged(
                                *self_, request.playerState,
                                request.heldItem)) return true;
                        if (!srv_.jvmRuntime() ||
                            srv_.jvmRuntime()->onBlockPlace(
                                *self_, tx, ty, tz, dbl)) {
                            if (!session_detail::interactionContextUnchanged(
                                    *self_, request.playerState,
                                    request.heldItem)) return true;
                            if (srv_.worldFor(self_->dimension).getBlock(tx, ty, tz) != existing)
                                return true;
                            if (!srv_.worldFor(self_->dimension).setBlock(
                                    tx, ty, tz, dbl))
                                return true;
                            srv_.broadcastBlockChangeFor(self_->dimension, tx,ty,tz,dbl);
                            if (survival) {
                                auto* mh=&self_->inv[36 + self_->heldSlot];
                                if(--mh->count<=0) *mh=ItemStack::air();
                                srv_.resendInventory(*self_);
                            }
                            return true;
                        }
                    } else {
                        // same half → try adjacent placement (vanilla places single slab at offset)
                        const int adjX = tx + kBlockFaceOffsetX[d];
                        const int adjY = ty + kBlockFaceOffsetY[d];
                        const int adjZ = tz + kBlockFaceOffsetZ[d];
                        if (srv_.worldFor(self_->dimension).getBlock(adjX,adjY,adjZ)==0) {
                            const gen::BlockDef* sdef = gen::blockByName(hName);
                            if(sdef){
                                const char* newType;
                                if(d==1) newType="bottom";
                                else if(d==0) newType="top";
                                else newType = (ctx.cursor.y > 0.5 ? "top" : "bottom");
                                auto adjFs = FluidSim::getFluidState(srv_.worldFor(self_->dimension), adjX, adjY, adjZ);
                                bool wl = adjFs.isStillWater();
                                bool hasWlAdj=false; for(int i=0;i<sdef->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[sdef->propsOff+i]]; if(pd.name=="waterlogged") hasWlAdj=true; }
                                std::vector<std::pair<std::string_view,std::string_view>> ap;
                                ap.emplace_back("type", newType);
                                if(hasWlAdj) ap.emplace_back("waterlogged", wl?"true":"false");
                                uint16_t adjSt = static_cast<uint16_t>(gen::stateWithProps(*sdef, ap));
                                if (!blockEventDispatcher().onBlockPlace(adjX, adjY, adjZ, 0, adjSt, self_.get())) return true;
                                if (!session_detail::interactionContextUnchanged(
                                        *self_, request.playerState,
                                        request.heldItem)) return true;
                                api::BlockPlaceEvent evA; evA.player=self_.get(); evA.x=adjX; evA.y=adjY; evA.z=adjZ; evA.newState=adjSt;
                                if (!srv_.events().blockPlace.fire(evA))
                                    return true;
                                if (!session_detail::interactionContextUnchanged(
                                        *self_, request.playerState,
                                        request.heldItem)) return true;
                                if (!srv_.jvmRuntime() ||
                                    srv_.jvmRuntime()->onBlockPlace(
                                        *self_, adjX, adjY, adjZ, adjSt)) {
                                    if (!session_detail::interactionContextUnchanged(
                                            *self_, request.playerState,
                                            request.heldItem)) return true;
                                    if (srv_.worldFor(self_->dimension).getBlock(adjX, adjY, adjZ) != 0)
                                        return true;
                                    if (!srv_.worldFor(self_->dimension).setBlock(
                                            adjX, adjY, adjZ, adjSt))
                                        return true;
                                    srv_.broadcastBlockChangeFor(self_->dimension, adjX,adjY,adjZ,adjSt);
                                    if(wl) srv_.fluidsFor(self_->dimension).touch(adjX,adjY,adjZ);
                                    if(survival){
                                        auto* mh=&self_->inv[36 + self_->heldSlot];
                                        if(--mh->count<=0) *mh=ItemStack::air();
                                        srv_.resendInventory(*self_);
                                    }
                                    return true;
                                }
                            }
                        }
                        // same half but adjacent blocked → do not make double, fall through to normal handling (will ack without placing)
                        return true;
                    }
                }
            }
        }
        // also check placing slab onto existing slab at click position? vanilla allows placing slab on top of clicked slab to make double.
        // If tx is offset, also check clicked pos if it is slab and face is up/down
        if (hName.find("_slab") != std::string::npos) {
            uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x,y,z);
            const gen::BlockDef* cd = gen::blockByState(clickedSt);
            if (cd && std::string(cd->name) == hName) {
                std::string curType = getPropStr(clickedSt, "type");
                if (curType != "double") {
                    // Only double if clicking top of bottom slab or bottom of top slab
                    std::string chalf = getPropStr(clickedSt, "type");
                    bool canDouble = false;
                    if (chalf=="bottom" && dir==1) canDouble=true;
                    if (chalf=="top" && dir==0) canDouble=true;
                    if (canDouble) {
                        std::vector<std::pair<std::string_view,std::string_view>> p;
                        for(auto&[k,v]: gen::propsOf(clickedSt)) if(k!="type" && k!="waterlogged") p.emplace_back(k,v);
                        p.emplace_back("type","double");
                        bool hasWl=false; for(int i=0;i<cd->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[cd->propsOff+i]]; if(pd.name=="waterlogged") hasWl=true; }
                        if(hasWl) p.emplace_back("waterlogged","false");
                        uint16_t dbl = static_cast<uint16_t>(gen::stateWithProps(*cd, p));
                        if (!srv_.worldFor(self_->dimension).setBlock(
                                x, y, z, dbl))
                            return true;
                        srv_.broadcastBlockChangeFor(self_->dimension, x,y,z,dbl);
                        if (survival) {
                            auto* mh=&self_->inv[36 + self_->heldSlot];
                            if(--mh->count<=0) *mh=ItemStack::air();
                            srv_.resendInventory(*self_);
                        }
                        return true;
                    }
                }
            }
        }
    }

    return false;
}
bool Session::handleUseItemOnOccupied(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    if (srv_.worldFor(self_->dimension).getBlock(tx, ty, tz) != 0 || heldItem.empty()) {
        const std::uint16_t clickedState = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        const gen::BlockDef* cdef = gen::blockByState(clickedState);
        if (cdef && cdef->name.size() > 5 &&
            cdef->name.rfind("_door", cdef->name.size() - 5) != std::string::npos) {
            bool isIron = std::string(cdef->name) == "minecraft:iron_door";
            if(isIron){
                return true;
            }
            bool open = false, upperHalf = false;
            bool powered = false;
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "open") open = v == "true";
                if (k == "half") upperHalf = v == "upper";
                if (k == "powered") powered = v == "true";
            }
            std::string facing;
            std::string hinge = "left";
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "facing") facing = std::string(v);
                if (k == "hinge") hinge = std::string(v);
            }
            const std::uint16_t st1 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "upper" : "lower"},
                     {"facing", facing}, {"hinge", hinge}, {"powered", powered?"true":"false"}}));
            const std::int32_t oy = upperHalf ? y - 1 : y + 1;
            const std::uint16_t st2 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "lower" : "upper"},
                     {"facing", facing}, {"hinge", hinge}, {"powered", powered?"true":"false"}}));
            auto& doorWorld = srv_.worldFor(self_->dimension);
            const std::array doorUpdates{
                World::BlockUpdate{x, y, z, st1},
                World::BlockUpdate{x, oy, z, st2},
            };
            if (!doorWorld.setBlockBatch(doorUpdates)) return true;
            srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, st1);
            srv_.broadcastBlockChangeFor(self_->dimension, x, oy, z, st2);
            srv_.broadcastSoundFor(self_->dimension,
                                   "minecraft:block.wooden_door.toggle",
                                   x + .5, y + .5, z + .5, 1.f,
                                   open ? 0.7f : 0.9f);
        }
        return true;
    }
    return false;
}
bool Session::handleUseItemOnEntityItems(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto survival = request.survival;
    // item id -> block name (block items share the name)
    std::string itemName = heldItem.name();
    {
        BlockPos hitPos{x, y, z};
        if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
            ItemStack& stk = self_->inv[36 + self_->heldSlot];
            if (!stk.empty() && stk.name().ends_with("_spawn_egg")) {
                if (srv_.trySpawnEgg(*self_, stk, hitPos, d)) return true;
            }
        }
        // keep itemName endsWith check for tooling/grep
        if (itemName.ends_with("_spawn_egg")) {
            // handled via trySpawnEgg above
        }
    }
    {
        if (!heldItem.empty()) {
            std::string hName = heldItem.name();
            bool isBoatItem = hName.ends_with("_boat") || hName.ends_with("_raft");
            if (isBoatItem) {
                auto kindOpt = [&]() -> std::optional<MobKind> {
                    if (hName=="minecraft:oak_boat") return MobKind::OakBoat;
                    if (hName=="minecraft:spruce_boat") return MobKind::SpruceBoat;
                    if (hName=="minecraft:birch_boat") return MobKind::BirchBoat;
                    if (hName=="minecraft:jungle_boat") return MobKind::JungleBoat;
                    if (hName=="minecraft:acacia_boat") return MobKind::AcaciaBoat;
                    if (hName=="minecraft:dark_oak_boat") return MobKind::DarkOakBoat;
                    if (hName=="minecraft:mangrove_boat") return MobKind::MangroveBoat;
                    if (hName=="minecraft:cherry_boat") return MobKind::CherryBoat;
                    if (hName=="minecraft:pale_oak_boat") return MobKind::PaleOakBoat;
                    if (hName=="minecraft:bamboo_raft") return MobKind::BambooRaft;
                    if (hName=="minecraft:oak_chest_boat") return MobKind::OakChestBoat;
                    if (hName=="minecraft:spruce_chest_boat") return MobKind::SpruceChestBoat;
                    if (hName=="minecraft:birch_chest_boat") return MobKind::BirchChestBoat;
                    if (hName=="minecraft:jungle_chest_boat") return MobKind::JungleChestBoat;
                    if (hName=="minecraft:acacia_chest_boat") return MobKind::AcaciaChestBoat;
                    if (hName=="minecraft:dark_oak_chest_boat") return MobKind::DarkOakChestBoat;
                    if (hName=="minecraft:mangrove_chest_boat") return MobKind::MangroveChestBoat;
                    if (hName=="minecraft:cherry_chest_boat") return MobKind::CherryChestBoat;
                    if (hName=="minecraft:pale_oak_chest_boat") return MobKind::PaleOakChestBoat;
                    if (hName=="minecraft:bamboo_chest_raft") return MobKind::BambooChestRaft;
                    return std::nullopt;
                }();
                if (kindOpt) {
                    double sx = tx + 0.5, sy = ty + 0.1, sz = tz + 0.5;
                    srv_.spawnMobFor(self_->dimension, *kindOpt, sx, sy, sz);
                    if (survival) {
                        auto* mh = &self_->inv[36 + self_->heldSlot];
                        if (--mh->count <= 0) *mh = ItemStack::air();
                        srv_.resendInventory(*self_);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}
void Session::placeUseItemOnBlock(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto survival = request.survival;
    const auto& ctx = request.context;
    const std::string itemName = heldItem.name();
    std::uint16_t newState = 0;
    const gen::BlockDef* bdef2 = gen::blockByName(itemName);
    if (!bdef2) {                                          // not a placeable block
        // special items handled elsewhere (food via UseItem); nothing to do

        return;
    }
    World& placementWorld = srv_.worldFor(self_->dimension);
    const auto geometry = placementWorld.verticalGeometry();
    if (!geometry.containsY(ty)) return;
    const std::string_view itemBlockName(bdef2->name);
    const bool isDoor = itemBlockName.ends_with("_door");
    if (isDoor && !geometry.containsY(ty + 1)) return;
    std::vector<std::pair<std::string_view, std::string_view>> props;
    {
        float yaw = ctx.yaw;
        const char* facing = "north";
        if (yaw >= 45.f && yaw < 135.f) facing = "east";
        else if (yaw >= 135.f && yaw < 225.f) facing = "south";
        else if (yaw >= 225.f && yaw < 315.f) facing = "west";
        bool hasFacing = false;
        bool hasHalf = false, hasShape = false, hasSnowy = false, hasWaterlogged = false, hasAxis = false;
        bool hasOrientation = false;
        for (int i = 0; i < bdef2->propCount; ++i) {
            const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[bdef2->propsOff + i]];
            if (pd.name == "facing") hasFacing = true;
            if (pd.name == "half") hasHalf = true;
            if (pd.name == "shape") hasShape = true;
            if (pd.name == "snowy") hasSnowy = true;
            if (pd.name == "waterlogged") hasWaterlogged = true;
            if (pd.name == "axis") hasAxis = true;
            if (pd.name == "orientation") hasOrientation = true;
        }
        if (hasFacing) props.emplace_back("facing", facing);
        if (hasHalf) {
            const char* half = "bottom";
            if (ctx.face == 0) half = "top";
            else if (ctx.face == 1) half = "bottom";
            else {
                half = (ctx.cursor.y > 0.5 ? "top" : "bottom");
            }
            props.emplace_back("half", half);
        }
        if (hasShape) {
            std::string facingStr = std::string(facing);
            std::string halfStr = "bottom";
            for(auto& pr: props) if(pr.first=="half") halfStr=std::string(pr.second);
            std::string shape = computeStairsShape(*ctx.world, ctx.placePos.x, ctx.placePos.y, ctx.placePos.z, facingStr, halfStr);
            props.emplace_back("shape", shape);
        }
        if (hasWaterlogged) {
            bool waterlogged = false;
            auto fluid = FluidSim::getFluidState(*ctx.world, ctx.placePos.x, ctx.placePos.y, ctx.placePos.z);
            if (fluid.isStillWater()) waterlogged = true;
            // For double slab, force false (handled earlier)
            bool isDoubleSlab = false;
            for(auto& pr: props) if(pr.first=="type" && pr.second=="double") isDoubleSlab=true;
            if(isDoubleSlab) waterlogged=false;
            props.emplace_back("waterlogged", waterlogged ? "true" : "false");
        }
        if (hasSnowy) {
            bool snowy = false;
            std::uint16_t above = ctx.world->getBlock(ctx.placePos.x, ctx.placePos.y + 1, ctx.placePos.z);
            const gen::BlockDef* ad = gen::blockByState(above);
            if (ad && (std::string(ad->name) == "minecraft:snow" || std::string(ad->name) == "minecraft:snow_block" || std::string(ad->name) == "minecraft:powder_snow")) snowy = true;
            props.emplace_back("snowy", snowy ? "true" : "false");
        }
        if (hasAxis) {
            const char* axis = "y";
            if (ctx.face == 4 || ctx.face == 5) axis = "x";
            else if (ctx.face == 2 || ctx.face == 3) axis = "z";
            props.emplace_back("axis", axis);
        }
        if (hasOrientation) {
            // Crafter's `orientation` is the ordered pair front_top.  A
            // vertical placement uses the player's horizontal direction for
            // the top; a horizontal placement keeps the top upright.  The
            // first token is also the ejection/front direction used by the
            // redstone craft path.
            std::string orientation;
            if (ctx.face == 0 || ctx.face == 1) {
                orientation = (ctx.face == 0 ? "down_" : "up_");
                orientation += facing;
            } else {
                orientation = facing;
                orientation += "_up";
            }
            props.emplace_back("orientation", orientation);
        }
        // slab type handling: reuse half logic as type
        bool hasTypeSlab = false;
        for (int i = 0; i < bdef2->propCount; ++i) {
            const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[bdef2->propsOff + i]];
            if (pd.name == "type") { hasTypeSlab = true; break; }
        }
        if (hasTypeSlab && std::string(bdef2->name).find("_slab") != std::string::npos) {
            const char* type = "bottom";
            if (ctx.face == 0) type = "top";
            else if (ctx.face == 1) type = "bottom";
            else type = (ctx.cursor.y > 0.5 ? "top" : "bottom");
            // remove previous if any, then add
            props.emplace_back("type", type);
        }
        newState = static_cast<std::uint16_t>(gen::stateWithProps(*bdef2, props));
        if (std::string(bdef2->name)=="minecraft:creaking_heart") {
            std::string axis="y"; for(auto& pr: props) if(pr.first=="axis") axis=std::string(pr.second);
            std::vector<std::pair<std::string_view,std::string_view>> cprops;
            cprops.emplace_back("axis", axis);
            cprops.emplace_back("natural", "false");
            cprops.emplace_back("active", "false");
            newState = static_cast<std::uint16_t>(gen::stateWithProps(*bdef2, cprops));
        }
    }

    const auto oldState = placementWorld.getBlock(tx, ty, tz);
    api::BlockPlaceEvent ev;
    ev.player = self_.get();
    ev.x = tx; ev.y = ty; ev.z = tz;
    ev.newState = newState;
    if (!srv_.events().blockPlace.fire(ev)) {  return; }
    if (!session_detail::interactionContextUnchanged(
            *self_, request.playerState, request.heldItem)) return;
    if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onBlockPlace(*self_, tx, ty, tz, newState)) return;
    if (!session_detail::interactionContextUnchanged(
            *self_, request.playerState, request.heldItem)) return;
    if (!blockEventDispatcher().onBlockPlace(tx, ty, tz, oldState, newState,
                                             self_.get())) return;
    if (!session_detail::interactionContextUnchanged(
            *self_, request.playerState, request.heldItem)) return;
    // Keep a stable world/block-entity transaction across the placement and
    // block-entity admission. In particular, persistence snapshots must not
    // consume the pre-admission dirty generation before the new crafter owner
    // is published.
    auto placementMutationGate = placementWorld.lockMutationGate();
    if (placementWorld.getBlock(tx, ty, tz) != oldState) return;

    const bool placesCrafter = bdef2->name == "minecraft:crafter";
    const bool placesShulker =
        itemBlockName == "minecraft:shulker_box" ||
        itemBlockName.ends_with("_shulker_box");
    const auto crafterKey = posKey(tx, ty, tz);
    if (placesCrafter &&
        !srv_.blockEntitiesFor(self_->dimension).canCreateAt(crafterKey)) {
        sendSystemText(msg::kRed +
            "This server cannot safely create another block entity right now.");
        return;
    }

    std::optional<nbt::Value> placedShulkerNbt;
    std::vector<World::BlockUpdate> shulkerBlockProjection;
    std::vector<BlockEntityStore::NbtReplacement> shulkerEntityProjection;
    auto& placementBlockEntities =
        srv_.blockEntitiesFor(self_->dimension);
    if (placesShulker) {
        const auto itemData = shulkerBoxDataFromItem(heldItem);
        if (!itemData || placementBlockEntities.containsAt(tx, ty, tz)) {
            sendSystemText(msg::kRed +
                "This shulker box contains data this server cannot safely place here.");
            return;
        }
        placedShulkerNbt = shulkerBoxBlockEntityNbt(*itemData, tx, ty, tz);
        shulkerBlockProjection.push_back({tx, ty, tz, newState});
        shulkerEntityProjection.push_back(
            {tx, ty, tz, *placedShulkerNbt});
        if (!placementBlockEntities.canReplaceNbtBatch(
                shulkerEntityProjection) ||
            !srv_.blockMutationCandidatesFitAnvilEntries(
                self_->dimension, shulkerBlockProjection,
                shulkerEntityProjection)) {
            sendSystemText(msg::kRed +
                "This shulker box cannot be safely saved at this location.");
            return;
        }
    }

    if (!placementWorld.setBlock(tx, ty, tz, newState)) return;
    if (placedShulkerNbt) {
        bool installed = false;
        try {
            installed = placementBlockEntities.replaceNbtAtIfAdmitted(
                tx, ty, tz, std::move(placedShulkerNbt),
                [this, &placementBlockEntities, &shulkerBlockProjection](
                    const BlockEntityStore::NbtReplacement& replacement) {
                    const std::vector<BlockEntityStore::NbtReplacement>
                        projectedEntities{replacement};
                    return placementBlockEntities.canReplaceNbtBatch(
                               projectedEntities) &&
                           srv_.blockMutationCandidatesFitAnvilEntries(
                               self_->dimension, shulkerBlockProjection,
                               projectedEntities);
                });
        } catch (...) {
            installed = false;
        }
        if (!installed) {
            (void)placementWorld.setBlockIfState(
                tx, ty, tz, newState, oldState);
            sendSystemText(msg::kRed +
                "This shulker box could not be safely saved; placement was cancelled.");
            return;
        }
    }
    if (placesCrafter) {
        auto& store = srv_.blockEntitiesFor(self_->dimension);
        auto owner = store.getShared(crafterKey);
        const bool isCrafter = owner && [&] {
            std::lock_guard lock(*owner->stateMtx);
            return owner->kind == BlockEntity::Kind::Crafter;
        }();
        if (!isCrafter) store.createShared(crafterKey, BlockEntity::Kind::Crafter);
    }
    srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, newState);
    if (std::string(bdef2->name)=="minecraft:bamboo") {
        auto* bambooDef = gen::blockByName("minecraft:bamboo");
        if (bambooDef) {
            bool needsFix=false;
            std::string curLeaves, curStage, curAge;
            for(auto&[k,v]: gen::propsOf(newState)) {
                if(k=="leaves") curLeaves=std::string(v);
                if(k=="stage") curStage=std::string(v);
                if(k=="age") curAge=std::string(v);
            }
            if(curLeaves!="none" || curStage!="0" || curAge!="0") needsFix=true;
            if (needsFix) {
                std::vector<std::pair<std::string_view,std::string_view>> bambooProps;
                bambooProps.emplace_back("leaves","none");
                bambooProps.emplace_back("stage","0");
                bambooProps.emplace_back("age","0");
                std::uint16_t fixed = static_cast<std::uint16_t>(
                    gen::stateWithProps(*bambooDef, bambooProps));
                srv_.worldFor(self_->dimension).setBlock(tx,ty,tz, fixed);
                srv_.broadcastBlockChangeFor(self_->dimension, tx,ty,tz, fixed);
                newState = fixed;
            }
            World& bambooWorld = srv_.worldFor(self_->dimension);
            int by = ty;
            while (by > bambooWorld.minY()) {
                std::uint16_t bs = srv_.worldFor(self_->dimension).getBlock(tx, by-1, tz);
                if (bs==0) break;
                auto* bd = gen::blockByState(bs);
                if(!bd || std::string(bd->name)!="minecraft:bamboo") break;
                --by;
            }
            int h=0;
            for(int yy=by; yy<bambooWorld.maxY(); ++yy) {
                std::uint16_t bs = srv_.worldFor(self_->dimension).getBlock(tx, yy, tz);
                if (bs==0) break;
                auto* bd = gen::blockByState(bs);
                if(!bd || std::string(bd->name)!="minecraft:bamboo") break;
                ++h;
            }
            auto bambooLeavesFor = [](int height, int dist)->std::string {
                if(dist==0) { if(height==1) return "none"; if(height==2) return "small"; return "large"; }
                if(dist==1) { if(height==2) return "none"; if(height==3) return "small"; if(height>=4) return "large"; return "none"; }
                if(dist==2) { if(height>=5) return "small"; return "none"; }
                return "none";
            };
            bool thick = h>=4;
            for(int i=0;i<h;++i){
                int yy = by + i;
                int dist = h-1 - i;
                std::string want = bambooLeavesFor(h, dist);
                std::uint16_t st = srv_.worldFor(self_->dimension).getBlock(tx, yy, tz);
                auto* d = gen::blockByState(st);
                if(!d) continue;
                std::string curL;
                for(auto&[k,v]: gen::propsOf(st)) if(k=="leaves") curL=std::string(v);
                int curA=0;
                for(auto&[k,v]: gen::propsOf(st)) if(k=="age") curA=std::atoi(std::string(v).c_str());
                int wantA = thick?1:curA;
                if(curL!=want || (thick && curA!=1)) {
                    std::vector<std::pair<std::string_view,std::string_view>> bambooGrowthProps;
                    for(auto&[k,v]: gen::propsOf(st)) if(k!="leaves" && k!="age") bambooGrowthProps.emplace_back(k,v);
                    bambooGrowthProps.emplace_back("leaves", want);
                    const std::string ageString = std::to_string(wantA);
                    bambooGrowthProps.emplace_back("age", ageString);
                    std::uint16_t ns = static_cast<std::uint16_t>(
                        gen::stateWithProps(*d, bambooGrowthProps));
                    srv_.worldFor(self_->dimension).setBlock(tx, yy, tz, ns);
                    srv_.broadcastBlockChangeFor(self_->dimension, tx, yy, tz, ns);
                }
            }
        }
    }
    srv_.worldFor(self_->dimension).scheduleNeighborUpdates(tx, ty, tz);
    if (std::string(bdef2->name).find("_stairs") != std::string::npos) {
        auto& world = srv_.worldFor(self_->dimension);
        updateNeighborStairsShapes(
            world, tx, ty, tz,
            [this](std::int8_t dimension, int x, int y, int z,
                   std::uint16_t state) {
                srv_.broadcastBlockChangeFor(dimension, x, y, z, state);
            });
        // schedule fluid tick if waterlogged
        std::string wl = getPropStr(newState, "waterlogged");
        if (wl=="true") {
            srv_.fluidsFor(self_->dimension).touch(tx,ty,tz);
        }
    } else {
        std::string wl = getPropStr(newState, "waterlogged");
        if (wl=="true") srv_.fluidsFor(self_->dimension).touch(tx,ty,tz);
    }
    srv_.onPlacedBlock(*self_, tx, ty, tz, newState);
    if (survival) {
        auto mutableHeld = &self_->inv[36 + self_->heldSlot];
        if (ItemStack::maxDamageFor(mutableHeld->itemId) > 0) {
            if (mutableHeld->applyDamage(1)) *mutableHeld = ItemStack::air();
        } else {
            if (--mutableHeld->count <= 0) *mutableHeld = ItemStack::air();
        }
        srv_.resendInventory(*self_);
    }
}
void Session::onUseItemOn(ReadBuffer& in) {
    (void)in.varint();                                  // hand
    std::int32_t x, y, z;
    in.position(x, y, z);
    const std::int32_t dir = in.varint();
    const float cursorX = in.f32();
    const float cursorY = in.f32();
    const float cursorZ = in.f32();
    (void)cursorX; (void)cursorZ;
    (void)in.boolean();                                 // inside block
    (void)in.boolean();                                 // world border hit
    const std::int32_t sequence = in.varint();

    const session_detail::SessionPlayerSnapshot playerState = session_detail::snapshotPlayerForSession(*self_);
    if (!withinBlockInteractionRange(playerState, x, y, z)) {
        // The sequence still needs an acknowledgement, but no interaction,
        // callback, item consumption, or placement may be driven by a block
        // outside the server-side interaction range.
        ack(sequence);
        return;
    }

    const int d = (dir >= 0 && dir < 6) ? dir : 0;
    const std::int32_t tx = x + kBlockFaceOffsetX[d], ty = y + kBlockFaceOffsetY[d], tz = z + kBlockFaceOffsetZ[d];

    ItemUseContext ctx;
    ctx.player = self_.get();
    ctx.world = &srv_.worldFor(playerState.dimension);
    ctx.hitPos = {x, y, z};
    ctx.placePos = {tx, ty, tz};
    ctx.face = d;
    ctx.cursor = {static_cast<double>(cursorX), static_cast<double>(cursorY), static_cast<double>(cursorZ)};
    ctx.yaw = playerState.yaw;
    ctx.isSneaking = playerState.sneaking;

    const InvSlot heldItem =
        (playerState.heldSlot >= 0 && playerState.heldSlot < 9)
            ? [&]() {
                  std::lock_guard playerLock(self_->stateMtx);
                  return self_->inv[36 + playerState.heldSlot];
              }()
            : InvSlot::air();
    const UseItemOnRequest request{x, y, z, tx, ty, tz, d,
                                  playerState.gamemode == 0, ctx,
                                  playerState, heldItem};
    if (handleUseItemOnInteractions(request)) {
        ack(sequence);
        return;
    }
    if (!session_detail::interactionContextUnchanged(
            *self_, playerState, heldItem)) {
        ack(sequence);
        return;
    }
    if (!heldItem.empty()) {
        srv_.onItemUsedOnBlock(self_.get(), x, y, z, heldItem);
    }
    if (!session_detail::interactionContextUnchanged(
            *self_, playerState, heldItem)) {
        ack(sequence);
        return;
    }
    if (handleUseItemOnToolActions(request, heldItem) ||
        handleUseItemOnDoorAndSlab(request, heldItem) ||
        handleUseItemOnOccupied(request, heldItem) ||
        handleUseItemOnEntityItems(request, heldItem)) {
        ack(sequence);
        return;
    }

    placeUseItemOnBlock(request, heldItem);
    ack(sequence);
}
void Session::onUseItem(ReadBuffer& in) {
    const std::int32_t hand = in.varint();
    const std::int32_t sequence = in.varint();
    (void)in.f32(); (void)in.f32();
    if ((hand != 0 && hand != 1) || in.remaining() != 0 ||
        (hand == 0 && (self_->heldSlot < 0 || self_->heldSlot >= 9))) {
        ack(sequence);
        return;
    }

    const std::size_t inventorySlot = hand == 1
        ? 45u : static_cast<std::size_t>(36 + self_->heldSlot);
    auto& useStack = self_->inv[inventorySlot];
    auto clearUseAnimation = [&] {
        std::lock_guard playerLock(self_->stateMtx);
        self_->isEating = false;
        self_->eatTicks = 0;
    };

    if (consumableUse_.activeSlot()) {
        if (consumableUse_.matchesActiveStack(inventorySlot, useStack)) {
            // Repeated UseItem packets from the same hand must not restart
            // the vanilla consume timer.
            ack(sequence);
            return;
        }
        consumableUse_.cancel();
        clearUseAnimation();
    }

    bool malformedFood = false;
    const auto food = foodUseProperties(useStack, malformedFood);
    bool malformedConsumable = false;
    const auto consumeTicks = malformedFood
        ? std::optional<std::uint32_t>{}
        : configuredConsumeTicks(useStack, food.has_value(),
                                 malformedConsumable);
    if (malformedFood || malformedConsumable) {
        ack(sequence);
        return;
    }
    if (consumeTicks) {
        if (food && !food->canAlwaysEat && self_->food >= 20 &&
            self_->gamemode != 1) {
            ack(sequence);
            return;
        }
        if (consumableUse_.startWithTicks(true, inventorySlot,
                                          *consumeTicks, useStack)) {
            std::lock_guard playerLock(self_->stateMtx);
            self_->isEating = true;
            self_->eatTicks = 0;
        }
        ack(sequence);
        return;
    }

    if (hand != 0) {
        ack(sequence);
        return;
    }

    if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
        auto& sl = self_->inv[36 + self_->heldSlot];
        {
            bool shieldHeld = (!sl.empty() && sl.name().find("shield") != std::string::npos) ||
                              (!self_->inv[45].empty() && self_->inv[45].name().find("shield") != std::string::npos);
            if (shieldHeld && self_->shieldDisableTicks <= 0) self_->isBlocking = true;
        }
        if (!sl.empty() && sl.name().find("trident")!=std::string::npos) {
            int riptideLvl = EnchantmentHelper::getRiptide(sl);
            bool isInWater = false;
            {
                // check block at feet is water
                uint16_t bst = srv_.worldFor(self_->dimension).getBlock((int)std::floor(self_->x), (int)std::floor(self_->y), (int)std::floor(self_->z));
                if (auto* bd = gen::blockByState(bst)) {
                    std::string bn(bd->name);
                    if (bn.find("water")!=std::string::npos) isInWater = true;
                }
                // also check block at eye height
                if (!isInWater) {
                    uint16_t bst2 = srv_.worldFor(self_->dimension).getBlock((int)std::floor(self_->x), (int)std::floor(self_->y+1), (int)std::floor(self_->z));
                    if (auto* bd2 = gen::blockByState(bst2)) {
                        std::string bn2(bd2->name);
                        if (bn2.find("water")!=std::string::npos) isInWater = true;
                    }
                }
            }
            bool raining = srv_.raining() || srv_.thundering();
            if (riptideLvl>0 && (raining || isInWater)) {
                // riptide: propel player in look direction, damage trident, no projectile
                double yawRad = self_->yaw * 3.14159265/180.0;
                double pitchRad = self_->pitch * 3.14159265/180.0;
                const double riptideForce = 1.2 * static_cast<double>(riptideLvl);
                double vx = -std::sin(yawRad)*std::cos(pitchRad)*riptideForce;
                double vy = -std::sin(pitchRad)*riptideForce;
                double vz =  std::cos(yawRad)*std::cos(pitchRad)*riptideForce;
                // apply velocity via EntityVelocity packet
                WriteBuffer vel;
                vel.varint(self_->entityId);
                vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(vy*8000)); vel.i16((int16_t)(vz*8000));
                self_->conn->trySendPacket(proto::pl::sc::EntityVelocity, vel);
                // also broadcast to others
                srv_.broadcastPacketExceptInDimension(self_->dimension,
                                                      self_.get(),
                                                      proto::pl::sc::EntityVelocity, vel);
                if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                    if (sl.applyDamage(1)) sl = ItemStack::air();
                    srv_.resendInventory(*self_);
                }
                ack(sequence);
                return;
            }
            // normal trident throw (channeling handled on hit in GameServer_items.cpp)
            double yawRad = self_->yaw * 3.14159265/180.0;
            double pitchRad = self_->pitch * 3.14159265/180.0;
            double vx = -std::sin(yawRad)*std::cos(pitchRad)*1.5;
            double vy = -std::sin(pitchRad)*1.5;
            double vz =  std::cos(yawRad)*std::cos(pitchRad)*1.5;
            auto trident = srv_.spawnProjectileFor(self_->dimension,
                                                   ProjectileKind::Trident,
                                                   self_->x, self_->y+1.6,
                                                   self_->z, vx, vy, vz,
                                                   self_->entityId, true);
            if (trident) trident->loyaltyLevel = EnchantmentHelper::getLoyalty(sl); // plan44 G-09: loyalty return
            if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                if (sl.applyDamage(1)) sl = ItemStack::air();
                srv_.resendInventory(*self_);
            }
            ack(sequence);
            return;
        }
        if (!sl.empty() && sl.name()=="minecraft:bow") {
            bool hasInfinity = EnchantmentHelper::hasInfinity(sl);
            bool isCreative = self_->gamemode==1;
            // find arrow in inventory (any arrow type)
            int arrowSlot = -1;
            for(int i=9;i<=44;++i){
                if(self_->inv[i].empty()) continue;
                std::string an = self_->inv[i].name();
                if(an.find("arrow")!=std::string::npos){ arrowSlot=i; break; }
            }
            // also check offhand
            if(arrowSlot==-1 && !self_->inv[45].empty() && self_->inv[45].name().find("arrow")!=std::string::npos) arrowSlot=45;
            bool hasArrow = arrowSlot!=-1 && self_->inv[arrowSlot].count>0;
            if(!hasArrow && !isCreative && !hasInfinity){
                ack(sequence); return;
            }
            // spawn arrow projectile
            double yawRad = self_->yaw * 3.14159265/180.0;
            double pitchRad = self_->pitch * 3.14159265/180.0;
            double vx = -std::sin(yawRad)*std::cos(pitchRad)*2.0;
            double vy = -std::sin(pitchRad)*2.0 + 0.15;
            double vz =  std::cos(yawRad)*std::cos(pitchRad)*2.0;
            srv_.spawnProjectileFor(self_->dimension, ProjectileKind::Arrow,
                                    self_->x, self_->y+1.6, self_->z, vx, vy,
                                    vz, self_->entityId, true);
            // damage bow
            if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                if (sl.applyDamage(1)) sl = ItemStack::air();
            }
            // consume arrow unless infinity or creative
            if(!isCreative){
                if(hasInfinity && hasArrow){
                    // infinity: consume 0 (require at least 1 arrow stays)
                } else if(hasArrow){
                    auto &arr = self_->inv[arrowSlot];
                    if(--arr.count<=0) arr = ItemStack::air();
                }
            }
            srv_.resendInventory(*self_);
            ack(sequence);
            return;
        }
        // durability cost of 1 — quickcharge has no live load timer here, see quickChargeLoadTime)
        if (!sl.empty() && sl.name()=="minecraft:crossbow") {
            bool isCreative = self_->gamemode==1;
            int arrowSlot = -1;
            for(int i=9;i<=44;++i){
                if(self_->inv[i].empty()) continue;
                std::string an = self_->inv[i].name();
                if(an.find("arrow")!=std::string::npos){ arrowSlot=i; break; }
            }
            if(arrowSlot==-1 && !self_->inv[45].empty() && self_->inv[45].name().find("arrow")!=std::string::npos) arrowSlot=45;
            bool hasArrow = arrowSlot!=-1 && self_->inv[arrowSlot].count>0;
            if(!hasArrow && !isCreative){
                ack(sequence); return;
            }
            int shots = EnchantmentHelper::multishotShots(sl);
            int pierce = EnchantmentHelper::getPiercing(sl);
            double yawRad = self_->yaw * 3.14159265/180.0;
            double pitchRad = self_->pitch * 3.14159265/180.0;
            for (int s = 0; s < shots; ++s) {
                double spread = (shots == 1) ? 0.0 : (s == 0 ? 0.0 : (s == 1 ? 0.17 : -0.17)); // ±10deg
                double yr = yawRad + spread;
                double vx = -std::sin(yr)*std::cos(pitchRad)*2.0;
                double vy = -std::sin(pitchRad)*2.0 + 0.15;
                double vz =  std::cos(yr)*std::cos(pitchRad)*2.0;
                auto pr = srv_.spawnProjectileFor(self_->dimension,
                                                  ProjectileKind::Arrow,
                                                  self_->x, self_->y+1.6,
                                                  self_->z, vx, vy, vz,
                                                  self_->entityId, true);
                if (pr) pr->piercingLevel = pierce;
            }
            if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                if (DamageComponent::applyDamage(sl, 1)) sl = ItemStack::air(); // multishot costs 1 durability
            }
            if(!isCreative && hasArrow){
                auto &arr = self_->inv[arrowSlot];
                if(--arr.count<=0) arr = ItemStack::air();
            }
            srv_.resendInventory(*self_);
            ack(sequence);
            return;
        }
        if (!sl.empty()) {
            std::string n = sl.name();
            bool isPearl = n.find("ender_pearl")!=std::string::npos;
            bool isSnow  = n.find("snowball")!=std::string::npos;
            bool isEgg   = n=="minecraft:egg";
            if (isPearl || isSnow || isEgg) {
                if (isPearl && srv_.tickNow() - self_->lastEnderPearlTick < 20) {
                    // still on cooldown — notify
                    auto pidIt = gen::itemIdByName().find("minecraft:ender_pearl");
                    if (pidIt!=gen::itemIdByName().end() && self_->conn) {
                        WriteBuffer cd; cd.varint((int32_t)pidIt->second); cd.varint(20 - (int)(srv_.tickNow() - self_->lastEnderPearlTick));
                        self_->conn->trySendPacket(proto::pl::sc::SetCooldown, cd);
                    }
                    ack(sequence); return;
                }
                double yawRad = self_->yaw * 3.14159265/180.0;
                double pitchRad = self_->pitch * 3.14159265/180.0;
                double vx = -std::sin(yawRad)*std::cos(pitchRad)*1.5;
                double vy = -std::sin(pitchRad)*1.5;
                double vz =  std::cos(yawRad)*std::cos(pitchRad)*1.5;
                ProjectileKind pk = isPearl? ProjectileKind::EnderPearl : (isSnow? ProjectileKind::Snowball : ProjectileKind::Egg);
                srv_.spawnProjectileFor(self_->dimension, pk, self_->x,
                                        self_->y+1.6, self_->z, vx, vy, vz,
                                        self_->entityId, true);
                if (isPearl) {
                    self_->lastEnderPearlTick = srv_.tickNow();
                    if (self_->conn) {
                        auto pidIt = gen::itemIdByName().find("minecraft:ender_pearl");
                        if (pidIt!=gen::itemIdByName().end()){
                            WriteBuffer cd; cd.varint((int32_t)pidIt->second); cd.varint(20);
                            self_->conn->trySendPacket(proto::pl::sc::SetCooldown, cd);
                        }
                    }
                }
                if (self_->gamemode!=1) {
                    if (--sl.count <=0) sl = ItemStack::air();
                    srv_.resendInventory(*self_);
                }
                ack(sequence); return;
            }
        }
    }
    ack(sequence);
}

} // namespace cppfm
