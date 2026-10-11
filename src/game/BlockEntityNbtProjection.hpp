#pragma once

#include "BlockEntityNbtReplacement.hpp"
#include "BlockEntityPosition.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppfm {

// Apply detached raw-NBT replacements to a serialized chunk snapshot. Unknown
// fields remain opaque and byte-relevant to the Anvil size admission check.
// Positions are indexed once so dense block-entity chunks stay linear in the
// number of records and replacements, including clone preflight.
inline void projectBlockEntityNbtReplacements(
    nbt::Value& entities,
    const std::vector<const BlockEntityNbtReplacement*>& replacements) {
    if (entities.tag != nbt::List || entities.listElement != nbt::Compound)
        throw std::runtime_error(
            "block-entity projection requires a compound list");

    struct PreparedReplacement {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::optional<nbt::Value> entity;
        bool encountered = false;
    };

    std::vector<PreparedReplacement> prepared;
    prepared.reserve(replacements.size());
    std::unordered_map<std::int64_t, std::size_t> indexByPosition;
    indexByPosition.reserve(replacements.size());
    for (const auto* replacement : replacements) {
        if (!replacement)
            throw std::runtime_error(
                "block-entity projection has a null candidate");

        std::optional<nbt::Value> candidate = replacement->entity;
        if (candidate) {
            if (candidate->tag != nbt::Compound)
                throw std::runtime_error(
                    "block-entity projection candidate is malformed");
            candidate->set("x", nbt::Value::makeInt(replacement->x));
            candidate->set("y", nbt::Value::makeInt(replacement->y));
            candidate->set("z", nbt::Value::makeInt(replacement->z));
        }

        const auto key = posKey(replacement->x, replacement->y,
                                replacement->z);
        const auto found = indexByPosition.find(key);
        if (found == indexByPosition.end()) {
            const auto index = prepared.size();
            indexByPosition.emplace(key, index);
            prepared.push_back(PreparedReplacement{
                replacement->x, replacement->y, replacement->z,
                std::move(candidate), false});
        } else {
            // Preserve the old sequential semantics: the last candidate for a
            // coordinate wins, while the first occurrence defines append order.
            prepared[found->second].entity = std::move(candidate);
        }
    }

    std::vector<nbt::Value> projected;
    projected.reserve(entities.list.size() + prepared.size());
    for (const auto& entry : entities.list) {
        if (entry.tag != nbt::Compound)
            throw std::runtime_error(
                "block-entity projection source is malformed");

        const auto* x = entry.get("x");
        const auto* y = entry.get("y");
        const auto* z = entry.get("z");
        if (!x || x->tag != nbt::Int || !y || y->tag != nbt::Int ||
            !z || z->tag != nbt::Int) {
            projected.push_back(entry);
            continue;
        }

        const auto found = indexByPosition.find(posKey(x->i, y->i, z->i));
        if (found == indexByPosition.end()) {
            projected.push_back(entry);
            continue;
        }

        auto& replacement = prepared[found->second];
        if (replacement.encountered) continue;
        replacement.encountered = true;
        if (replacement.entity)
            projected.push_back(std::move(*replacement.entity));
    }

    for (auto& replacement : prepared) {
        if (!replacement.encountered && replacement.entity)
            projected.push_back(std::move(*replacement.entity));
    }
    entities.list = std::move(projected);
}

} // namespace cppfm
