#include "game/EntitySupport.hpp"
#include "game/PlayerCollision.hpp"

#include <cmath>
#include <cstdio>
#include <initializer_list>
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

void testPlayerCollisionStateClassification() {
    const auto openGate = state("minecraft:oak_fence_gate", {{"open", "true"}});
    const auto closedGate = state("minecraft:oak_fence_gate", {{"open", "false"}});
    const auto oakFence = state("minecraft:oak_fence");
    const auto stairs = state("minecraft:oak_stairs");

    check(cppfm::block_state_collision::hasKnownEmptyCollisionShape(openGate),
          "open vanilla fence gate is classified as a known empty collision shape");
    check(!cppfm::block_state_collision::hasKnownEmptyCollisionShape(closedGate),
          "closed vanilla fence gate is not classified as empty");
    check(!cppfm::player_collision::solidBlock(openGate) &&
              cppfm::player_collision::solidBlock(closedGate),
          "open gates allow player movement while closed gates remain blocking");
    check(cppfm::player_collision::solidBlock(state("minecraft:stone")) &&
              cppfm::player_collision::solidBlock(oakFence) &&
              cppfm::player_collision::solidBlock(stairs),
          "unrelated conservative block and partial-shape collision behavior is preserved");
}

void testUnimplementedPartialShapesFailClosed() {
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
    testPlayerCollisionStateClassification();
    testUnimplementedPartialShapesFailClosed();

    if (failures != 0) {
        std::fprintf(stderr, "%d/%d assertions failed\n", failures,
                     assertions);
        return 1;
    }
    std::printf("PASS: %d entity-support assertions\n", assertions);
    return 0;
}
