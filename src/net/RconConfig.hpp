#pragma once

#include <cstdint>
#include <string>

namespace cppfm {

struct RconConfig {
    bool enabled = false;
    std::uint16_t port = 25575;
    std::string password;
};

} // namespace cppfm
