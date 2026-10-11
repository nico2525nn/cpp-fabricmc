#include "game/EntitySupport.hpp"
#include "game/BlockStateCollision.hpp"
#include "game/MobGrounding.hpp"
#include "game/PlayerCollision.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

int failures = 0;
int assertions = 0;

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

std::uint32_t state(
    std::string_view name,
    std::initializer_list<std::pair<std::string_view, std::string_view>> props = {}) {
    const auto* block = cppfm::gen::blockByName(name);
    if (!block) return 0xffffffffu;
    return cppfm::gen::stateWithProps(*block, std::vector(props));
}

void checkHeight(std::uint32_t blockState, double expected,
                 const char* description) {
    const auto height = cppfm::entity_support::topCollisionSurfaceHeight(
        blockState);
    check(height && std::abs(*height - expected) < 1e-12, description);
}

void checkNoSupport(std::uint32_t blockState, const char* description) {
    check(!cppfm::entity_support::topCollisionSurfaceHeight(blockState),
          description);
}

void testFullCubesAndEmptyShapes() {
    checkHeight(state("minecraft:stone"), 1.0,
                "stone has a full-height support surface");
    checkHeight(state("minecraft:glass"), 1.0,
                "common transparent glass is a full-height support surface");
    checkHeight(state("minecraft:oak_leaves"), 1.0,
                "leaf collision is approximated as a full-height support surface");
    checkNoSupport(state("minecraft:air"), "air has no support surface");
    checkNoSupport(state("minecraft:water"), "water has no support surface");
    checkNoSupport(state("minecraft:lava"), "lava has no support surface");
    checkNoSupport(state("minecraft:fire"), "fire has no support surface");
    checkNoSupport(0xffffffffu, "unknown state ids have no support surface");
}

void testSlabSurfaces() {
    checkHeight(state("minecraft:oak_slab", {{"type", "bottom"}}), 0.5,
                "bottom slab support is one half block high");
    checkHeight(state("minecraft:oak_slab", {{"type", "top"}}), 1.0,
                "top slab support reaches block height");
    checkHeight(state("minecraft:oak_slab", {{"type", "double"}}), 1.0,
                "double slab support reaches block height");
}

void testThinAndRaisedSupports() {
    checkHeight(state("minecraft:snow", {{"layers", "1"}}), 1.0 / 8.0,
                "one snow layer is one eighth block high");
    checkHeight(state("minecraft:snow", {{"layers", "4"}}), 0.5,
                "four snow layers are half a block high");
    checkHeight(state("minecraft:snow", {{"layers", "8"}}), 1.0,
                "eight snow layers reach block height");
    checkHeight(state("minecraft:white_carpet"), 1.0 / 16.0,
                "carpet support is one sixteenth block high");
    checkHeight(state("minecraft:farmland"), 15.0 / 16.0,
                "farmland support is fifteen sixteenths high");
    checkHeight(state("minecraft:dirt_path"), 15.0 / 16.0,
                "dirt-path support is fifteen sixteenths high");
}

void testFenceAndWallShapeMaximums() {
    checkHeight(state("minecraft:oak_fence"), 1.5,
                "fence support maximum is one and a half blocks");
    checkHeight(state("minecraft:stone_brick_wall"), 1.5,
                "wall support maximum is one and a half blocks");
    checkHeight(state("minecraft:oak_fence_gate", {{"open", "false"}}), 1.5,
                "closed fence gate support maximum is one and a half blocks");
    checkNoSupport(state("minecraft:oak_fence_gate", {{"open", "true"}}),
                   "open fence gate has no collision support");
}

void testOpenFenceGateCollisionClassification() {
    const auto openGate = state("minecraft:oak_fence_gate", {{"open", "true"}});
    const auto closedGate = state("minecraft:oak_fence_gate", {{"open", "false"}});
    check(cppfm::block_state_collision::hasKnownEmptyCollisionShape(openGate),
          "open fence gate is classified as a known empty collision shape");
    check(!cppfm::block_state_collision::hasKnownEmptyCollisionShape(closedGate),
          "closed fence gate is not classified as empty");
    check(!cppfm::player_collision::solidBlock(openGate) &&
              cppfm::player_collision::solidBlock(closedGate),
          "generated gate voxel shapes distinguish open and closed states");
    check(cppfm::player_collision::solidBlock(state("minecraft:stone")) &&
              cppfm::player_collision::solidBlock(state("minecraft:oak_stairs")),
          "full cubes and partial stairs both retain collision shapes");
}

void testVoxelShapeIntersections() {
    using cppfm::player_collision::Aabb;
    const auto bottomStair = state("minecraft:oak_stairs", {
        {"facing", "north"}, {"half", "bottom"}, {"shape", "straight"},
        {"waterlogged", "false"},
    });
    const Aabb playerOverTheOpenStep{
        0.2, 0.5, 0.51, 0.8, 2.3, 1.11};
    const Aabb playerOverTheLowerStep{
        0.2, 0.5, 0.49, 0.8, 2.3, 1.09};
    check(!cppfm::player_collision::intersectsBlockShape(
              bottomStair, 0, 0, 0, playerOverTheOpenStep),
          "player AABB passes above the lower half of a bottom stair's open step");
    check(cppfm::player_collision::intersectsBlockShape(
              bottomStair, 0, 0, 0, playerOverTheLowerStep),
          "the same stair blocks where its upper voxel-shape box exists");

    const auto bottomSlab = state("minecraft:oak_slab", {
        {"type", "bottom"}, {"waterlogged", "false"},
    });
    const Aabb aboveBottomSlab{0.2, 0.5, 0.2, 0.8, 2.3, 0.8};
    const Aabb insideBottomSlab{0.2, 0.49, 0.2, 0.8, 2.29, 0.8};
    check(!cppfm::player_collision::intersectsBlockShape(
              bottomSlab, 0, 0, 0, aboveBottomSlab),
          "bottom slab does not occupy the upper half of its block");
    check(cppfm::player_collision::intersectsBlockShape(
              bottomSlab, 0, 0, 0, insideBottomSlab),
          "bottom slab collides with an AABB entering its lower half");

    const auto unknownState = std::numeric_limits<std::uint32_t>::max();
    check(cppfm::player_collision::intersectsBlockShape(
              unknownState, 0, 0, 0,
              Aabb{0.2, 0.2, 0.2, 0.8, 0.8, 0.8}),
          "unknown block states fail closed as a full cube");
}

void testSweptMovementAndPoseCollision() {
    const auto stone = state("minecraft:stone");
    const auto wall = [stone](int blockX, int blockY, int blockZ) {
        return blockX == 1 && blockY == 0 && blockZ == 0 ? stone : 0U;
    };
    check(!cppfm::player_collision::intersectsAt(wall, 0.5, 0.0, 0.5) &&
              !cppfm::player_collision::intersectsAt(wall, 2.5, 0.0, 0.5),
          "both endpoints of the wall-crossing movement are individually clear");
    check(cppfm::player_collision::intersectsSwept(
              wall, 0.5, 0.0, 0.5, 2.5, 0.0, 0.5),
          "continuous movement collision prevents tunneling through a wall");
    check(cppfm::player_collision::intersectsSwept(
              wall, 0.5, 0.0, 0.5, 0.9, 0.0, 0.5),
          "continuous movement collision rejects an endpoint inside a wall");
    check(!cppfm::player_collision::sweptAabbOverlaps(
              cppfm::player_collision::playerBox(0.5, 0.0, 1.5, 1.8),
              cppfm::player_collision::Aabb{1.0, 0.0, 0.0,
                                             2.0, 1.0, 1.0},
              2.0, 0.0, 0.0),
          "swept collision permits a path parallel to and clear of a wall");

    const double embeddedX = 0.95;
    check(cppfm::player_collision::intersectsAt(
              wall, embeddedX, 0.0, 0.5),
          "fixture starts with the player overlapping the wall shape");
    check(cppfm::player_collision::intersectsSwept(
              wall, embeddedX, 0.0, 0.5, 2.5, 0.0, 0.5),
          "an embedded player cannot cross the wall to its far side within the movement limit");
    check(!cppfm::player_collision::intersectsSwept(
              wall, embeddedX, 0.0, 0.5, 0.8, 0.0, 0.5) &&
              cppfm::player_collision::intersectsAt(wall, 0.8, 0.0, 0.5),
          "monotonic movement toward the nearest face may make progress while still overlapping");
    check(!cppfm::player_collision::intersectsSwept(
              wall, 0.8, 0.0, 0.5, 0.6, 0.0, 0.5) &&
              !cppfm::player_collision::intersectsAt(wall, 0.6, 0.0, 0.5),
          "the player can complete the nearest-face escape in a later movement");
    check(cppfm::player_collision::intersectsSwept(
              wall, embeddedX, 0.0, 0.5, 1.05, 0.0, 0.5),
          "an overlapping endpoint that increases penetration is rejected");
    check(cppfm::player_collision::intersectsSwept(
              wall, 1.5, 0.0, 0.5, 2.5, 0.0, 0.5) &&
              !cppfm::player_collision::intersectsSwept(
                  wall, 1.5, 0.0, 0.5, 0.5, 0.0, 0.5),
          "equidistant embedded positions use the deterministic negative-X escape face");

    const auto adjacentWalls = [stone](int blockX, int blockY, int blockZ) {
        return (blockX == 0 || blockX == 1) && blockY == 0 && blockZ == 0
            ? stone : 0U;
    };
    check(cppfm::player_collision::intersectsAt(
              adjacentWalls, 1.0, 0.0, 0.5),
          "shared-boundary fixture overlaps both adjacent block shapes");
    check(!cppfm::player_collision::intersectsSwept(
              adjacentWalls, 1.0, 0.0, 0.5, 1.0, 0.0, -0.3) &&
              !cppfm::player_collision::intersectsAt(
                  adjacentWalls, 1.0, 0.0, -0.3),
          "an embedded player can leave adjacent blocks whose local escape faces oppose each other");
    check(cppfm::player_collision::intersectsSwept(
              adjacentWalls, 1.0, 0.0, 0.5, 1.0, 0.0, 1.5),
          "shared-overlap recovery still rejects movement through the combined obstacle union");

    const auto topSlab = state("minecraft:oak_slab", {
        {"type", "top"}, {"waterlogged", "false"},
    });
    const auto lowCeiling = [topSlab](int blockX, int blockY, int blockZ) {
        return blockX == 0 && blockY == 1 && blockZ == 0 ? topSlab : 0U;
    };
    check(cppfm::player_collision::intersectsAt(
              lowCeiling, 0.5, 0.0, 0.5, 1.8) &&
              !cppfm::player_collision::intersectsAt(
                  lowCeiling, 0.5, 0.0, 0.5, 1.5) &&
              !cppfm::player_collision::intersectsSwept(
                  lowCeiling, 0.5, 0.0, 0.5, 0.6, 0.0, 0.5, 1.5),
          "sneaking-height collision permits movement under a top-slab ceiling");

    const auto fence = state("minecraft:oak_fence");
    const auto raisedFence = [fence](int blockX, int blockY, int blockZ) {
        return blockX == 0 && blockY == 0 && blockZ == 0 ? fence : 0U;
    };
    check(cppfm::player_collision::intersectsAt(
              raisedFence, 0.5, 1.25, 0.5) &&
              !cppfm::player_collision::intersectsAt(
                  raisedFence, 0.5, 1.5, 0.5),
          "world collision broad phase includes a fence's raised shape and honors its top boundary");
}

void testUncertainSupportFootprintsRemainConservative() {
    checkHeight(state("minecraft:oak_stairs"), 1.0,
                "stair collision maximum prevents dropped items from falling through");
    checkNoSupport(state("minecraft:oak_sapling"),
                   "plant shape is not treated as a support surface");
    checkNoSupport(state("minecraft:glass_pane"),
                   "narrow transparent pane is not treated as a full cube");
    checkNoSupport(state("minecraft:piston", {{"extended", "true"}}),
                   "state-dependent extended piston shape fails closed");
}

void testMobGroundContact() {
    using BlockPosition = std::tuple<int, int, int>;
    std::map<BlockPosition, std::uint32_t> blocks;
    const auto getBlock = [&blocks](const int x, const int y, const int z) {
        const auto found = blocks.find({x, y, z});
        return found == blocks.end() ? std::uint32_t{0} : found->second;
    };
    const auto stone = state("minecraft:stone");
    const auto bottomSlab = state(
        "minecraft:oak_slab", {{"type", "bottom"}});
    blocks[{0, 0, 0}] = stone;
    check(cppfm::mob_grounding::isGroundedAt(0.5, 1.0, 0.5, getBlock, 0.3),
          "full-cube contact marks an entity grounded");
    check(!cppfm::mob_grounding::isGroundedAt(0.5, 1.25, 0.5, getBlock, 0.3),
          "an entity hovering above support is not grounded");
    check(cppfm::mob_grounding::isGroundedAt(1.2, 1.0, 0.5, getBlock, 0.3) &&
              !cppfm::mob_grounding::isGroundedAt(1.31, 1.0, 0.5,
                                                   getBlock, 0.3),
          "support contact respects an explicitly supplied entity footprint");
    check(cppfm::mob_grounding::isGroundedAt(1.2, 1.0, 0.5, getBlock, 0.45) &&
              !cppfm::mob_grounding::isGroundedAt(1.46, 1.0, 0.5,
                                                   getBlock, 0.45),
          "wide-mob support uses its own width instead of a shared default");

    blocks.clear();
    blocks[{0, 0, 0}] = bottomSlab;
    check(cppfm::mob_grounding::isGroundedAt(0.5, 0.5, 0.5, getBlock, 0.3),
          "partial collision top is used for grounded contact");
    check(!cppfm::mob_grounding::isGroundedAt(0.5, 1.0, 0.5, getBlock, 0.3),
          "the empty upper half of a slab is not treated as support");

    check(!cppfm::mob_grounding::isGroundedAt(
              0.5, 0.5, 0.5,
              [](const int, const int, const int) {
                  return std::uint32_t{0};
              }, 0.3),
          "airborne Vex-like position with no collision support stays airborne");
    check(!cppfm::mob_grounding::isGroundedAt(
              std::numeric_limits<double>::infinity(), 1.0, 0.5, getBlock,
              0.3),
          "non-finite entity positions are never grounded");
}

void testPufferfishPuffStateFootprints() {
    using namespace cppfm;
    using BlockPosition = std::tuple<int, int, int>;
    std::map<BlockPosition, std::uint32_t> blocks;
    blocks[{0, 0, 0}] = state("minecraft:stone");
    const auto getBlock = [&blocks](const int x, const int y, const int z) {
        const auto found = blocks.find({x, y, z});
        return found == blocks.end() ? std::uint32_t{0} : found->second;
    };

    MobEntity pufferfish;
    pufferfish.kind = MobKind::Pufferfish;
    pufferfish.x = 1.2;
    pufferfish.y = 1.0;
    pufferfish.z = 0.5;
    pufferfish.pufferfishPuffState = 0;
    const double smallHalfWidth = mobGroundingHalfWidth(pufferfish);
    check(std::abs(smallHalfWidth - 0.175) < 1e-6,
          "small pufferfish width is base 0.7 scaled by 0.5");
    pufferfish.x = 1.0 + (smallHalfWidth + 0.245) * 0.5;
    check(!cppfm::mob_grounding::isMobGroundedAt(pufferfish, getBlock),
          "small pufferfish does not reach support at the small/mid boundary");

    pufferfish.pufferfishPuffState = 1;
    const double midHalfWidth = mobGroundingHalfWidth(pufferfish);
    check(std::abs(midHalfWidth - 0.245) < 1e-6,
          "mid pufferfish width is base 0.7 scaled by 0.7");
    pufferfish.x = 1.0 + (smallHalfWidth + midHalfWidth) * 0.5;
    check(cppfm::mob_grounding::isMobGroundedAt(pufferfish, getBlock),
          "mid pufferfish reaches support missed by the small state");

    const double fullHalfWidth = 0.35;
    pufferfish.x = 1.0 + (midHalfWidth + fullHalfWidth) * 0.5;
    check(!cppfm::mob_grounding::isMobGroundedAt(pufferfish, getBlock),
          "mid pufferfish does not reach support at the mid/full boundary");
    pufferfish.pufferfishPuffState = 2;
    const double measuredFullHalfWidth = mobGroundingHalfWidth(pufferfish);
    check(std::abs(measuredFullHalfWidth - fullHalfWidth) < 1e-6,
          "full pufferfish width is base 0.7 scaled by 1.0");
    check(cppfm::mob_grounding::isMobGroundedAt(pufferfish, getBlock),
          "full pufferfish reaches support missed by the mid state");
}

void testTeleportLandingSurfaceAndPacketBaseline() {
    using namespace cppfm;
    using BlockPosition = std::tuple<int, int, int>;
    std::map<BlockPosition, std::uint32_t> blocks;
    const auto getBlock = [&blocks](const int x, const int y, const int z) {
        const auto found = blocks.find({x, y, z});
        return found == blocks.end() ? std::uint32_t{0} : found->second;
    };

    MobEntity mob;
    mob.entityId = 812;
    mob.kind = MobKind::Enderman;
    mob.x = 0.5;
    mob.z = 0.5;

    const auto verifyTeleportAtY = [&](const double expectedY,
                                       const char* packetDescription,
                                       const char* baselineDescription) {
        mob.y = expectedY;
        const auto packets = mob.takeTeleportPacketsWithGroundState([&] {
            return cppfm::mob_grounding::isMobGroundedAt(mob, getBlock);
        });
        bool packetMatches = false;
        if (!packets.empty() &&
            packets.front().packetId == proto::pl::sc::EntityTeleport) {
            try {
                ReadBuffer body(packets.front().body.data);
                packetMatches = body.varint() == mob.entityId &&
                    body.f64() == mob.x && body.f64() == expectedY &&
                    body.f64() == mob.z && body.f64() == mob.velX &&
                    body.f64() == mob.velY && body.f64() == mob.velZ &&
                    body.f32() == mob.yaw && body.f32() == mob.pitch &&
                    body.i32() == 0 && body.boolean() && body.remaining() == 0;
            } catch (...) {
                packetMatches = false;
            }
        }
        check(packetMatches, packetDescription);
        check(mob.hasSent && mob.sentY == expectedY &&
                  mob.takePositionPacketsWithGroundState([&] {
                      return cppfm::mob_grounding::isMobGroundedAt(mob,
                                                                   getBlock);
                  }).empty(),
              baselineDescription);
    };

    // Enderman goal and BehaviorTree's normal candidate scan both use this
    // selector. Exercise full and partial CollisionShapes through each kind
    // of target surface before checking its packet/baseline contract.
    blocks[{0, 0, 0}] = state("minecraft:stone");
    const auto endermanFloorY =
        cppfm::mob_grounding::teleportLandingSurfaceY(
            mob, 0.5, 0.5, 1.0, getBlock);
    check(endermanFloorY && *endermanFloorY == 1.0,
          "Enderman teleport candidate resolves a full floor to y=1");
    if (endermanFloorY)
        verifyTeleportAtY(
            *endermanFloorY,
            "Enderman teleport packet carries exact floor Y and onGround=true",
            "Enderman teleport baseline prevents stale follow-up movement");

    blocks.clear();
    blocks[{0, 0, 0}] = state("minecraft:oak_slab", {{"type", "bottom"}});
    const auto behaviorTreeFloorY =
        cppfm::mob_grounding::teleportLandingSurfaceY(
            mob, 0.5, 0.5, 1.0, getBlock);
    check(behaviorTreeFloorY && *behaviorTreeFloorY == 0.5,
          "BehaviorTree normal candidate resolves a bottom slab to y=0.5");
    if (behaviorTreeFloorY)
        verifyTeleportAtY(
            *behaviorTreeFloorY,
            "BehaviorTree candidate packet carries exact slab Y and onGround=true",
            "BehaviorTree candidate teleport synchronizes its sent baseline");

    // The BehaviorTree column fallback selects the topmost CollisionShapes
    // surface directly; it must not add the former extra block of height.
    blocks.clear();
    blocks[{0, 0, 0}] = state("minecraft:stone");
    const auto fallbackFloorY =
        cppfm::mob_grounding::highestSupportSurfaceY(
            0.5, 0.5, -4.0, 8.0, getBlock,
            mobGroundingHalfWidth(mob));
    check(fallbackFloorY && *fallbackFloorY == 1.0,
          "BehaviorTree fallback resolves a full floor to y=1");
    if (fallbackFloorY)
        verifyTeleportAtY(
            *fallbackFloorY,
            "BehaviorTree fallback packet carries exact floor Y and onGround=true",
            "BehaviorTree fallback synchronizes its teleport baseline");

    blocks.clear();
    blocks[{0, 0, 0}] = state("minecraft:oak_slab", {{"type", "bottom"}});
    const auto fallbackLandingY =
        cppfm::mob_grounding::highestSupportSurfaceY(
            0.5, 0.5, -4.0, 8.0, getBlock,
            mobGroundingHalfWidth(mob));
    check(fallbackLandingY && *fallbackLandingY == 0.5,
          "BehaviorTree fallback resolves a bottom slab to y=0.5, with no double raise");
    if (fallbackLandingY)
        verifyTeleportAtY(
            *fallbackLandingY,
            "BehaviorTree fallback packet carries exact slab Y and onGround=true",
            "BehaviorTree fallback synchronizes its partial-block baseline");
}

} // namespace

int main() {
    testFullCubesAndEmptyShapes();
    testSlabSurfaces();
    testThinAndRaisedSupports();
    testFenceAndWallShapeMaximums();
    testOpenFenceGateCollisionClassification();
    testVoxelShapeIntersections();
    testSweptMovementAndPoseCollision();
    testUncertainSupportFootprintsRemainConservative();
    testMobGroundContact();
    testPufferfishPuffStateFootprints();
    testTeleportLandingSurfaceAndPacketBaseline();

    if (failures != 0) {
        std::fprintf(stderr, "%d/%d assertions failed\n", failures,
                     assertions);
        return 1;
    }
    std::printf("PASS: %d entity-support assertions\n", assertions);
    return 0;
}
