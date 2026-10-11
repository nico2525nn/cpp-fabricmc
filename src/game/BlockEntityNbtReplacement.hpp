// Detached block-entity NBT replacement used by mutation preflight and clone.
#pragma once

#include "../core/NBTValue.hpp"

#include <cstdint>
#include <optional>

namespace cppfm {

struct BlockEntityNbtReplacement {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::optional<nbt::Value> entity;
};

} // namespace cppfm
