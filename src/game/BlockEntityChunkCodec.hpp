// Projects an immutable block-entity chunk snapshot into vanilla chunk NBT.
// Store locking/indexing lives in BlockEntityStore; this module owns the
// projection, per-chunk limits, and transient initial-packet representation.
#pragma once

#include "BlockEntityData.hpp"
#include "BlockEntityNbtCodec.hpp"
#include "BlockEntityPosition.hpp"
#include "../core/NBTValue.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../generated/BlockStates.hpp"

namespace cppfm {

class BlockEntityChunkCodec final {
public:
    using Owner = BlockEntityOwner;

    struct SnapshotOverride {
        std::int64_t key = 0;
        BlockEntity entity;
    };

    struct TypedRecord {
        std::int64_t key = 0;
        BlockEntityState entity;
    };

    struct Snapshot {
        std::vector<TypedRecord> typedRecords;
        std::unordered_map<std::int64_t, nbt::Value> rawTemplates;
        std::unordered_set<std::int64_t> typedKeys;
    };

    static bool writeChunk(
        std::int32_t chunkX, std::int32_t chunkZ, nbt::Value& outList,
        const Snapshot& snapshot,
        const std::vector<SnapshotOverride>& overrides,
        std::int32_t minY, std::int32_t maxYExclusive,
        std::size_t maxRecords, std::size_t maxNbtFootprint) {
        struct PreparedOverride {
            std::int64_t key = 0;
            BlockEntityState entity;
            bool emitted = false;
        };
        std::vector<PreparedOverride> preparedOverrides;
        preparedOverrides.reserve(overrides.size());
        std::unordered_map<std::int64_t, std::size_t> overrideByKey;
        overrideByKey.reserve(overrides.size());

        for (const auto& replacement : overrides) {
            const auto key = replacement.key;
            if (!containsWorldY(posKeyUnpackY(key), minY, maxYExclusive))
                throw std::invalid_argument(
                    "block-entity snapshot override has invalid height");
            if ((posKeyUnpackX(key) >> 4) != chunkX ||
                (posKeyUnpackZ(key) >> 4) != chunkZ)
                continue;

            BlockEntityState detached = replacement.entity;
            const auto [existing, inserted] = overrideByKey.try_emplace(
                key, preparedOverrides.size());
            if (inserted)
                preparedOverrides.push_back(
                    PreparedOverride{key, std::move(detached), false});
            else
                preparedOverrides[existing->second].entity.swapState(
                    detached);
        }

        std::size_t recordCount = 0;
        std::size_t nbtFootprint = 0;
        auto staged = nbt::Value::makeList(nbt::Compound);
        for (const auto& typed : snapshot.typedRecords) {
            const auto key = typed.key;
            const auto x = posKeyUnpackX(key);
            const auto y = posKeyUnpackY(key);
            const auto z = posKeyUnpackZ(key);
            if ((x >> 4) != chunkX || (z >> 4) != chunkZ ||
                !containsWorldY(y, minY, maxYExclusive))
                continue;

            const BlockEntityState* entity = &typed.entity;
            if (const auto replacement = overrideByKey.find(key);
                replacement != overrideByKey.end()) {
                auto& prepared = preparedOverrides[replacement->second];
                entity = &prepared.entity;
                prepared.emitted = true;
            }
            nbt::Value record = nbt::Value::makeCompound();
            if (const auto raw = snapshot.rawTemplates.find(key);
                raw != snapshot.rawTemplates.end())
                record = raw->second;
            if (!BlockEntityNbtCodec::encodeRecord(
                    record, *entity, x, y, z))
                continue;
            if (recordCount == maxRecords)
                throw std::length_error(
                    "too many block entities in one chunk while saving");
            ++recordCount;
            accountNbtFootprint(record, nbtFootprint, maxNbtFootprint);
            staged.list.push_back(std::move(record));
        }

        for (auto& replacement : preparedOverrides) {
            if (replacement.emitted) continue;
            const auto x = posKeyUnpackX(replacement.key);
            const auto y = posKeyUnpackY(replacement.key);
            const auto z = posKeyUnpackZ(replacement.key);
            nbt::Value record = nbt::Value::makeCompound();
            if (const auto raw = snapshot.rawTemplates.find(replacement.key);
                raw != snapshot.rawTemplates.end())
                record = raw->second;
            if (!BlockEntityNbtCodec::encodeRecord(
                    record, replacement.entity, x, y, z))
                continue;
            if (recordCount == maxRecords)
                throw std::length_error(
                    "too many block entities in one chunk while saving");
            ++recordCount;
            accountNbtFootprint(record, nbtFootprint, maxNbtFootprint);
            staged.list.push_back(std::move(record));
        }

        // Unknown and mod-owned records remain opaque. Typed entries above
        // start from the same raw template to preserve fields we do not model.
        for (auto& [key, record] : snapshot.rawTemplates) {
            if (snapshot.typedKeys.count(key) || overrideByKey.contains(key))
                continue;
            if ((posKeyUnpackX(key) >> 4) != chunkX ||
                (posKeyUnpackZ(key) >> 4) != chunkZ ||
                !containsWorldY(posKeyUnpackY(key), minY, maxYExclusive))
                continue;
            if (recordCount == maxRecords)
                throw std::length_error(
                    "too many block entities in one chunk while saving");
            ++recordCount;
            accountNbtFootprint(record, nbtFootprint, maxNbtFootprint);
            staged.list.push_back(std::move(record));
        }
        appendList(outList, std::move(staged));
        return true;
    }

    static bool appendMovingPistons(
        std::int32_t chunkX, std::int32_t chunkZ,
        const std::vector<TypedRecord>& movingRecords,
        nbt::Value& outList, std::int32_t minY,
        std::int32_t maxYExclusive, std::size_t maxRecords,
        std::size_t maxNbtFootprint) {
        if (outList.list.size() > maxRecords)
            throw std::length_error(
                "too many block entities in one initial chunk packet");

        std::size_t recordCount = outList.list.size();
        std::size_t nbtFootprint = 0;
        for (const auto& record : outList.list)
            accountNbtFootprint(record, nbtFootprint, maxNbtFootprint);

        auto staged = nbt::Value::makeList(nbt::Compound);
        for (const auto& recordSnapshot : movingRecords) {
            const auto key = recordSnapshot.key;
            const auto& owner = recordSnapshot.entity;
            if (owner.kind != BlockEntity::Kind::MovingPiston) continue;
            const auto x = posKeyUnpackX(key);
            const auto y = posKeyUnpackY(key);
            const auto z = posKeyUnpackZ(key);
            if ((x >> 4) != chunkX || (z >> 4) != chunkZ ||
                !containsWorldY(y, minY, maxYExclusive))
                continue;

            const auto* state = gen::blockByState(owner.movingPiston.state);
            if (!state) continue;
            if (recordCount == maxRecords)
                throw std::length_error(
                    "too many block entities in one initial chunk packet");
            nbt::Value blockState = nbt::Value::makeCompound();
            blockState.set("Name",
                           nbt::Value::makeString(std::string(state->name)));
            if (owner.movingPiston.state != state->defaultState) {
                nbt::Value properties = nbt::Value::makeCompound();
                for (const auto& [name, value] :
                     gen::propsOf(owner.movingPiston.state))
                    properties.set(std::string(name),
                                   nbt::Value::makeString(std::string(value)));
                blockState.set("Properties", std::move(properties));
            }

            nbt::Value record = nbt::Value::makeCompound();
            record.set("id", nbt::Value::makeString("minecraft:piston"));
            record.set("x", nbt::Value::makeInt(x));
            record.set("y", nbt::Value::makeInt(y));
            record.set("z", nbt::Value::makeInt(z));
            record.set("keepPacked", nbt::Value::makeByte(0));
            record.set("blockState", std::move(blockState));
            record.set("extending", nbt::Value::makeByte(
                owner.movingPiston.extending ? 1 : 0));
            record.set("facing", nbt::Value::makeInt(
                owner.movingPiston.facing));
            record.set("progress", nbt::Value::makeFloat(
                owner.movingPiston.progress));
            record.set("source", nbt::Value::makeByte(
                owner.movingPiston.source ? 1 : 0));
            accountNbtFootprint(record, nbtFootprint, maxNbtFootprint);
            ++recordCount;
            staged.list.push_back(std::move(record));
        }
        appendList(outList, std::move(staged));
        return true;
    }

    static bool writeInitialChunk(
        std::int32_t chunkX, std::int32_t chunkZ, nbt::Value& outList,
        const Snapshot& snapshot, std::int32_t minY,
        std::int32_t maxYExclusive, std::size_t maxRecords,
        std::size_t maxNbtFootprint) {
        auto staged = nbt::Value::makeList(nbt::Compound);
        (void)writeChunk(chunkX, chunkZ, staged, snapshot, {}, minY,
                         maxYExclusive, maxRecords, maxNbtFootprint);
        (void)appendMovingPistons(
            chunkX, chunkZ, snapshot.typedRecords, staged, minY,
            maxYExclusive, maxRecords, maxNbtFootprint);
        appendList(outList, std::move(staged));
        return true;
    }

    static void accountNbtFootprint(const nbt::Value& value,
                                    std::size_t& used,
                                    std::size_t budget) {
        const auto add = [&used, budget](std::size_t amount) {
            if (used > budget || amount > budget - used)
                throw std::length_error(
                    "block-entity NBT exceeds the per-chunk memory budget");
            used += amount;
        };
        const auto addRepeated = [&add, budget](std::size_t count,
                                                std::size_t elementSize) {
            if (elementSize != 0 && count > budget / elementSize)
                throw std::length_error(
                    "block-entity NBT exceeds the per-chunk memory budget");
            add(count * elementSize);
        };

        add(sizeof(nbt::Value));
        add(value.str.size());
        add(value.byteArray.size());
        addRepeated(value.intArray.size(), sizeof(std::int32_t));
        addRepeated(value.longArray.size(), sizeof(std::int64_t));
        addRepeated(value.list.size(), sizeof(nbt::Value));
        addRepeated(value.comp.size(),
                    sizeof(std::pair<std::string, nbt::Value>));
        for (const auto& [key, child] : value.comp) {
            add(key.size());
            accountNbtFootprint(child, used, budget);
        }
        for (const auto& child : value.list)
            accountNbtFootprint(child, used, budget);
    }

private:
    static void appendList(nbt::Value& destination, nbt::Value staged) {
        if (destination.tag != nbt::List ||
            destination.listElement != nbt::Compound)
            throw std::invalid_argument(
                "block-entity output is not a compound list");
        if (staged.list.size() >
            destination.list.max_size() - destination.list.size())
            throw std::length_error(
                "block-entity output record count exceeds container limits");
        destination.list.reserve(destination.list.size() + staged.list.size());
        for (auto& record : staged.list)
            destination.list.push_back(std::move(record));
    }

    static bool containsWorldY(std::int32_t y, std::int32_t minY,
                               std::int32_t maxYExclusive) noexcept {
        return y >= minY && y < maxYExclusive;
    }
};

} // namespace cppfm
