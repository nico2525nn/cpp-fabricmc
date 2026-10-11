#include "game/BlockEntityChunkCodec.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

int assertions = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

cppfm::nbt::Value makeChestRecord(std::int32_t x, std::int32_t y,
                                  std::int32_t z) {
    using namespace cppfm;
    auto record = nbt::Value::makeCompound();
    record.set("id", nbt::Value::makeString("minecraft:chest"));
    record.set("x", nbt::Value::makeInt(x));
    record.set("y", nbt::Value::makeInt(y));
    record.set("z", nbt::Value::makeInt(z));
    return record;
}

cppfm::BlockEntityChunkCodec::Snapshot chestSnapshot(
    std::int64_t key, const cppfm::BlockEntityOwner& owner) {
    cppfm::BlockEntityChunkCodec::Snapshot snapshot;
    cppfm::BlockEntityState detached = *owner;
    snapshot.typedRecords.push_back({key, std::move(detached)});
    snapshot.typedKeys.insert(key);
    return snapshot;
}

void testTypedProjectionPreservesUnknownTemplateFields() {
    using namespace cppfm;
    const auto key = posKey(2, 70, 3);
    auto owner = std::make_shared<BlockEntity>();
    owner->kind = BlockEntity::Kind::Chest;

    auto snapshot = chestSnapshot(key, owner);
    auto templateRecord = makeChestRecord(2, 70, 3);
    templateRecord.set("modded_field", nbt::Value::makeString("preserve-me"));
    snapshot.rawTemplates.emplace(key, std::move(templateRecord));

    auto output = nbt::Value::makeList(nbt::Compound);
    check(BlockEntityChunkCodec::writeChunk(
              0, 0, output, snapshot, {}, -64, 320, 64,
              1024 * 1024),
          "valid typed chunk snapshot encodes");
    check(output.list.size() == 1,
          "typed chunk snapshot emits one record");
    if (output.list.size() == 1) {
        const auto* modded = output.list[0].get("modded_field");
        check(modded && modded->tag == nbt::String &&
                  modded->str == "preserve-me",
              "typed projection retains unknown template fields");
        const auto* id = output.list[0].get("id");
        check(id && id->tag == nbt::String && id->str == "minecraft:chest",
              "typed projection writes the canonical chest identifier");
    }
}

void testOpaqueRecordsAndRecordLimit() {
    using namespace cppfm;
    const auto key = posKey(1, 64, 1);
    BlockEntityChunkCodec::Snapshot opaque;
    auto record = nbt::Value::makeCompound();
    record.set("id", nbt::Value::makeString("example:modded_machine"));
    record.set("x", nbt::Value::makeInt(1));
    record.set("y", nbt::Value::makeInt(64));
    record.set("z", nbt::Value::makeInt(1));
    opaque.rawTemplates.emplace(key, std::move(record));

    auto output = nbt::Value::makeList(nbt::Compound);
    check(BlockEntityChunkCodec::writeChunk(
              0, 0, output, opaque, {}, -64, 320, 8, 1024 * 1024) &&
              output.list.size() == 1,
          "unknown mod-owned block-entity record is emitted unchanged");

    bool rejected = false;
    try {
        (void)BlockEntityChunkCodec::writeChunk(
            0, 0, output, chestSnapshot(key, std::make_shared<BlockEntity>()),
            {}, -64, 320, 0, 1024 * 1024);
    } catch (const std::length_error&) {
        rejected = true;
    }
    check(rejected,
          "chunk projection enforces its record count limit");
}

void testProjectionFailureDoesNotPublishPartialRecords() {
    using namespace cppfm;
    const auto key = posKey(0, 64, 0);
    auto owner = std::make_shared<BlockEntity>();
    owner->kind = BlockEntity::Kind::Chest;
    auto output = nbt::Value::makeList(nbt::Compound);
    output.list.push_back(makeChestRecord(9, 64, 9));
    const auto originalSize = output.list.size();
    auto snapshot = chestSnapshot(key, owner);
    bool rejected = false;
    try {
        (void)BlockEntityChunkCodec::writeChunk(
            0, 0, output, snapshot, {}, -64, 320, 0, 1024 * 1024);
    } catch (const std::length_error&) {
        rejected = true;
    }
    check(rejected, "record limit rejects an oversized projection");
    check(output.list.size() == originalSize &&
              output.list.front().get("x") &&
              output.list.front().get("x")->i == 9,
          "failed projection leaves previously published output unchanged");
}

void testInitialProjectionBoundsMovingPistonOutput() {
    using namespace cppfm;
    const auto* piston = gen::blockByName("minecraft:piston");
    check(piston != nullptr, "generated registry contains the piston state");
    if (!piston) return;

    BlockEntityChunkCodec::Snapshot snapshot;
    BlockEntityState inside;
    inside.kind = BlockEntity::Kind::MovingPiston;
    inside.movingPiston.state =
        static_cast<std::uint16_t>(piston->defaultState);
    snapshot.typedRecords.push_back(
        {posKey(1, 64, 1), std::move(inside)});

    BlockEntityState otherChunk;
    otherChunk.kind = BlockEntity::Kind::MovingPiston;
    otherChunk.movingPiston.state =
        static_cast<std::uint16_t>(piston->defaultState);
    snapshot.typedRecords.push_back(
        {posKey(16, 64, 1), std::move(otherChunk)});

    BlockEntityState outsideHeight;
    outsideHeight.kind = BlockEntity::Kind::MovingPiston;
    outsideHeight.movingPiston.state =
        static_cast<std::uint16_t>(piston->defaultState);
    snapshot.typedRecords.push_back(
        {posKey(2, 400, 2), std::move(outsideHeight)});

    auto output = nbt::Value::makeList(nbt::Compound);
    check(BlockEntityChunkCodec::writeInitialChunk(
              0, 0, output, snapshot, -64, 320, 64, 1024 * 1024) &&
              output.list.size() == 1,
          "initial piston projection filters records outside its chunk and height");
    if (output.list.size() == 1) {
        check(output.list.front().get("x") &&
                  output.list.front().get("x")->i == 1,
              "only the piston belonging to the requested chunk is emitted");
    }

    auto boundedOutput = nbt::Value::makeList(nbt::Compound);
    auto sentinel = nbt::Value::makeCompound();
    sentinel.set("x", nbt::Value::makeInt(99));
    boundedOutput.list.push_back(std::move(sentinel));
    bool rejected = false;
    try {
        (void)BlockEntityChunkCodec::writeInitialChunk(
            0, 0, boundedOutput, snapshot, -64, 320, 64, 1);
    } catch (const std::length_error&) {
        rejected = true;
    }
    check(rejected && boundedOutput.list.size() == 1 &&
              boundedOutput.list.front().get("x") &&
              boundedOutput.list.front().get("x")->i == 99,
          "moving piston NBT obeys the footprint limit without partial output");
}

} // namespace

int main() {
    testTypedProjectionPreservesUnknownTemplateFields();
    testOpaqueRecordsAndRecordLimit();
    testProjectionFailureDoesNotPublishPartialRecords();
    testInitialProjectionBoundsMovingPistonOutput();
    std::printf("BlockEntityChunkCodec: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
