#include "game/BlockEntityData.hpp"
#include "game/BlockEntityPosition.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

int failures = 0;
int assertions = 0;

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

void testPackedBlockEntityPositionBoundaries() {
    constexpr std::int32_t kMinHorizontal = -(1 << 25);
    constexpr std::int32_t kMaxHorizontal = (1 << 25) - 1;
    constexpr std::int32_t kMinY = -(1 << 11);
    constexpr std::int32_t kMaxY = (1 << 11) - 1;
    constexpr std::array<std::array<std::int32_t, 3>, 6> positions{{
        {{0, 0, 0}},
        {{-1, -64, -1}},
        {{kMinHorizontal, kMinY, kMinHorizontal}},
        {{kMaxHorizontal, kMaxY, kMaxHorizontal}},
        {{kMinHorizontal, kMaxY, kMaxHorizontal}},
        {{kMaxHorizontal, kMinY, kMinHorizontal}},
    }};

    for (const auto& position : positions) {
        const auto key = cppfm::posKey(position[0], position[1], position[2]);
        check(cppfm::posKeyUnpackX(key) == position[0] &&
                  cppfm::posKeyUnpackY(key) == position[1] &&
                  cppfm::posKeyUnpackZ(key) == position[2],
              "packed position round-trips signed field boundaries");
    }

    const auto knownLayout = cppfm::posKey(1, 2, 3);
    check(static_cast<std::uint64_t>(knownLayout) ==
              ((std::uint64_t{1} << 38) | (std::uint64_t{3} << 12) | 2),
          "packed position keeps the vanilla x/z/y bit layout");
}

void testBlockEntityValueTypes() {
    using namespace cppfm;
    static_assert(std::is_nothrow_move_constructible_v<BlockEntity>);
    static_assert(std::is_same_v<BlockEntityOwner,
                                 std::shared_ptr<BlockEntity>>);

    BlockEntity entity;
    check(entity.kind == BlockEntity::Kind::Chest &&
              entity.chest.kSlots == 27,
          "block-entity value defaults to a chest with the vanilla slot count");

    entity.kind = BlockEntity::Kind::Crafter;
    entity.crafter.setSlotEnabled(0, false);
    entity.crafter.setSlotEnabled(8, false);
    check(entity.crafter.isSlotDisabled(0) &&
              entity.crafter.isSlotDisabled(8) &&
              !entity.crafter.isSlotDisabled(-1) &&
              !entity.crafter.isSlotDisabled(CrafterData::kSlots),
          "crafter value tracks valid disabled slots and rejects invalid indices");
    entity.crafter.setSlotEnabled(8, true);
    check(!entity.crafter.isSlotDisabled(8),
          "crafter value can re-enable an individual slot");

    entity.kind = BlockEntity::Kind::MovingPiston;
    entity.movingPiston.source = true;
    entity.movingPiston.progress = 0.5f;
    const BlockEntityOwner owner = std::make_shared<BlockEntity>(entity);
    check(owner && owner->kind == BlockEntity::Kind::MovingPiston &&
              owner->movingPiston.source &&
              owner->movingPiston.progress == 0.5f,
          "shared block-entity owner retains its value payload");

    entity.kind = BlockEntity::Kind::Sign;
    entity.sign.front[0] = "front";
    entity.sign.back[0] = "back";
    entity.sign.hasFront = true;
    entity.sign.hasBack = true;
    check(entity.sign.hasFront && entity.sign.hasBack &&
              entity.sign.front[0] == "front" && entity.sign.back[0] == "back",
          "sign value stores independent front and back text");
}

} // namespace

int main() {
    check(cppfm::isPackedBlockPosition(
              cppfm::kMinPackedBlockCoordinate, cppfm::kMinPackedBlockY,
              cppfm::kMaxPackedBlockCoordinate) &&
              !cppfm::isPackedBlockPosition(
                  cppfm::kMaxPackedBlockCoordinate + 1, 0, 0) &&
              !cppfm::isPackedBlockPosition(
                  0, cppfm::kMaxPackedBlockY + 1, 0),
          "packed block-position range accepts exact signed boundaries and rejects overflow");
    testPackedBlockEntityPositionBoundaries();
    testBlockEntityValueTypes();
    std::printf("block_entity_value_contract: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
