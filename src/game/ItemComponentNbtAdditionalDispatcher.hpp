// Connects the bounded component codecs to ItemComponentNbt's main dispatch.
#pragma once

#include "ItemComponentNbtAdditionalFood.hpp"
#include "ItemComponentNbtAdditionalOther.hpp"

namespace cppfm::item_component_nbt {

inline bool consumeAdditionalProtocolComponentPayload(
    ReadBuffer& input, std::uint32_t id, std::size_t depth) {
    switch (id) {
    case 21:
    case 22:
    case 23:
    case 24:
    case 26:
    case 28:
    case 29:
    case 32:
        return additional_food::consume(input, id, depth);
    case 39:
    case 42:
    case 50:
    case 51:
    case 52:
    case 54:
    case 55:
    case 56:
    case 57:
    case 59:
    case 61:
    case 63:
    case 64:
        return additional_other::consume(input, id, depth);
    default:
        return false;
    }
}

inline std::optional<std::vector<std::uint8_t>>
additionalComponentNbtToProtocol(std::uint32_t id, const nbt::Value& saved,
                                 std::size_t depth) {
    switch (id) {
    case 21:
    case 22:
    case 23:
    case 24:
    case 26:
    case 28:
    case 29:
    case 32:
        return additional_food::fromNbt(id, saved, depth);
    case 39:
    case 42:
    case 50:
    case 51:
    case 52:
    case 54:
    case 55:
    case 56:
    case 57:
    case 59:
    case 61:
    case 63:
    case 64:
        return additional_other::fromNbt(id, saved, depth);
    default:
        return std::nullopt;
    }
}

inline std::optional<nbt::Value> additionalComponentProtocolToNbt(
    std::uint32_t id, const std::vector<std::uint8_t>& payload,
    std::size_t depth) {
    switch (id) {
    case 21:
    case 22:
    case 23:
    case 24:
    case 26:
    case 28:
    case 29:
    case 32:
        return additional_food::toNbt(id, payload, depth);
    case 39:
    case 42:
    case 50:
    case 51:
    case 52:
    case 54:
    case 55:
    case 56:
    case 57:
    case 59:
    case 61:
    case 63:
    case 64:
        return additional_other::toNbt(id, payload, depth);
    default:
        return std::nullopt;
    }
}

} // namespace cppfm::item_component_nbt
