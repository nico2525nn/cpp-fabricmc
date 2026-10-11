#include "game/EntitySupport.hpp"
#include "game/BlockStateCollision.hpp"
#include "game/PlayerCollision.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>
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

    if (failures != 0) {
        std::fprintf(stderr, "%d/%d assertions failed\n", failures,
                     assertions);
        return 1;
    }
    std::printf("PASS: %d entity-support assertions\n", assertions);
    return 0;
}
