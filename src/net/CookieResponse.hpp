#pragma once

#include "../core/ByteBuffer.hpp"
#include "../game/Constants.hpp"

#include <cstddef>
#include <stdexcept>

namespace cppfm::net {

// Consume the common CookieResponse body without retaining client-owned data.
// A cookie is at most the remainder of its already frame-bounded packet, and
// skipping it avoids allocating a second copy while the simulation lock is held.
inline void consumeCookieResponseBody(ReadBuffer& input) {
    (void)input.string(constants::kMaxStringLength);
    if (!input.boolean()) return;

    const auto length = input.varint();
    if (length < 0)
        throw std::runtime_error("negative cookie response length");
    input.skip(static_cast<std::size_t>(length));
}

} // namespace cppfm::net
