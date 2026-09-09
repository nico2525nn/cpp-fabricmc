// Fallback used when CMake cannot run the resource packer.  Release builds
// with Python generate a replacement header in the build include directory.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cppfm::embedded {
inline constexpr std::uint8_t kPack[] = {0};
inline constexpr std::size_t kPackSize = 0;
inline constexpr bool kHasPack = false;
} // namespace cppfm::embedded
