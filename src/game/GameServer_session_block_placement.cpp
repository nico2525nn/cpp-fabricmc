#include "GameServer.hpp"
#include "Session.hpp"
#include "SessionInteractionSupport.hpp"
#include "SessionUseItemOnRequest.hpp"
#include "BlockEntityDrops.hpp"
#include "Messages.hpp"
#include "BlockEvent.hpp"
#include "../physics/Fluids.hpp"
#include "StairsHelper.hpp"
#include "../generated/BlockStates.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm {

namespace {

void normalizeBambooColumnAfterPlacement(
    GameServer& server, const std::int8_t dimension, const std::int32_t x,
    const std::int32_t placedY, const std::int32_t z,
    std::uint16_t& placedState) {
    const auto* bamboo = gen::blockByName("minecraft:bamboo");
    if (!bamboo) return;

    World& world = server.worldFor(dimension);
    const auto readProperty = [](const std::uint16_t state,
                                 const std::string_view property) {
        for (const auto& [name, value] : gen::propsOf(state))
            if (name == property) return value;
        return std::string_view{};
    };
    const auto setAndBroadcast = [&](const std::int32_t y,
                                     const std::uint16_t state) {
        (void)world.setBlock(x, y, z, state);
        server.broadcastBlockChangeFor(dimension, x, y, z, state);
    };

    const auto leaves = readProperty(placedState, "leaves");
    const auto stage = readProperty(placedState, "stage");
    const auto age = readProperty(placedState, "age");
    if (leaves != "none" || stage != "0" || age != "0") {
        const std::vector<std::pair<std::string_view, std::string_view>>
            defaultProperties{{"leaves", "none"}, {"stage", "0"},
                              {"age", "0"}};
        placedState = static_cast<std::uint16_t>(
            gen::stateWithProps(*bamboo, defaultProperties));
        setAndBroadcast(placedY, placedState);
    }

    std::int32_t bottomY = placedY;
    while (bottomY > world.minY()) {
        const auto state = world.getBlock(x, bottomY - 1, z);
        const auto* block = gen::blockByState(state);
        if (!block || std::string_view(block->name) != "minecraft:bamboo")
            break;
        --bottomY;
    }

    std::int32_t height = 0;
    for (std::int32_t y = bottomY; y < world.maxY(); ++y) {
        const auto state = world.getBlock(x, y, z);
        const auto* block = gen::blockByState(state);
        if (!block || std::string_view(block->name) != "minecraft:bamboo")
            break;
        ++height;
    }

    const auto leavesFor = [](const std::int32_t columnHeight,
                              const std::int32_t distanceFromTop)
        -> std::string_view {
        if (distanceFromTop == 0) {
            if (columnHeight == 1) return "none";
            if (columnHeight == 2) return "small";
            return "large";
        }
        if (distanceFromTop == 1) {
            if (columnHeight == 3) return "small";
            if (columnHeight >= 4) return "large";
            return "none";
        }
        if (distanceFromTop == 2 && columnHeight >= 5) return "small";
        return "none";
    };

    const bool thick = height >= 4;
    for (std::int32_t offset = 0; offset < height; ++offset) {
        const auto y = bottomY + offset;
        const auto state = world.getBlock(x, y, z);
        const auto* block = gen::blockByState(state);
        if (!block) continue;

        const auto currentLeaves = readProperty(state, "leaves");
        const auto currentAge = readProperty(state, "age");
        const auto desiredLeaves = leavesFor(height, height - 1 - offset);
        const std::string_view desiredAge = thick
            ? std::string_view{"1"}
            : (currentAge == "1" ? std::string_view{"1"}
                                  : std::string_view{"0"});
        if (currentLeaves == desiredLeaves && currentAge == desiredAge)
            continue;

        std::vector<std::pair<std::string_view, std::string_view>> properties;
        for (const auto& [name, value] : gen::propsOf(state))
            if (name != "leaves" && name != "age")
                properties.emplace_back(name, value);
        properties.emplace_back("leaves", desiredLeaves);
        properties.emplace_back("age", desiredAge);
        const auto nextState = static_cast<std::uint16_t>(
            gen::stateWithProps(*block, properties));
        setAndBroadcast(y, nextState);
    }
}

} // namespace

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
        if (itemBlockName == "minecraft:creaking_heart") {
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
            *self_, request.playerState, request.heldItem,
            request.inventorySlot)) return;
    if (auto* runtime = srv_.jvmRuntime(); runtime &&
        !request.simulationLock.withLockReleased([&] {
            return runtime->onBlockPlace(*self_, tx, ty, tz, newState);
        })) return;
    if (!session_detail::interactionContextUnchanged(
            *self_, request.playerState, request.heldItem,
            request.inventorySlot)) return;
    if (!blockEventDispatcher().onBlockPlace(tx, ty, tz, oldState, newState,
                                             self_.get())) return;
    if (!session_detail::interactionContextUnchanged(
            *self_, request.playerState, request.heldItem,
            request.inventorySlot)) return;
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
    if (itemBlockName == "minecraft:bamboo")
        normalizeBambooColumnAfterPlacement(
            srv_, self_->dimension, tx, ty, tz, newState);
    srv_.worldFor(self_->dimension).scheduleNeighborUpdates(tx, ty, tz);
    if (itemBlockName.ends_with("_stairs")) {
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
        auto mutableHeld = &self_->inv[request.inventorySlot];
        if (ItemStack::maxDamageFor(mutableHeld->itemId) > 0) {
            if (mutableHeld->applyDamage(1)) *mutableHeld = ItemStack::air();
        } else {
            if (--mutableHeld->count <= 0) *mutableHeld = ItemStack::air();
        }
        srv_.resendInventory(*self_);
    }
}

} // namespace cppfm
