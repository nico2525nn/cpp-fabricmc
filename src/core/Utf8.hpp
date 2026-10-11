// Strict UTF-8 validation and Java-compatible UTF-16 string length helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace cppfm::utf8 {

// Java's String.length() counts UTF-16 code units, not UTF-8 bytes or Unicode
// scalar values. Reject malformed, overlong, surrogate, and out-of-range UTF-8
// so protocol length checks use the same valid-string domain as Minecraft.
inline std::optional<std::size_t> utf16Length(
    std::string_view value) noexcept {
    std::size_t units = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<std::uint8_t>(value[offset]);
        std::uint32_t codePoint = 0;
        std::size_t width = 0;
        if (first <= 0x7fU) {
            codePoint = first;
            width = 1;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            codePoint = first & 0x1fU;
            width = 2;
        } else if (first >= 0xe0U && first <= 0xefU) {
            codePoint = first & 0x0fU;
            width = 3;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            codePoint = first & 0x07U;
            width = 4;
        } else {
            return std::nullopt;
        }

        if (width > value.size() - offset) return std::nullopt;
        const std::uint8_t second = width > 1
            ? static_cast<std::uint8_t>(value[offset + 1])
            : std::uint8_t{0};
        if ((first == 0xe0U && second < 0xa0U) ||
            (first == 0xedU && second > 0x9fU) ||
            (first == 0xf0U && second < 0x90U) ||
            (first == 0xf4U && second > 0x8fU))
            return std::nullopt;

        for (std::size_t index = 1; index < width; ++index) {
            const auto continuation =
                static_cast<std::uint8_t>(value[offset + index]);
            if (continuation < 0x80U || continuation > 0xbfU)
                return std::nullopt;
            codePoint = (codePoint << 6U) | (continuation & 0x3fU);
        }
        const std::size_t increment = codePoint > 0xffffU ? 2U : 1U;
        if (units > std::numeric_limits<std::size_t>::max() - increment)
            return std::nullopt;
        units += increment;
        offset += width;
    }
    return units;
}

inline bool isValid(std::string_view value) noexcept {
    return utf16Length(value).has_value();
}

} // namespace cppfm::utf8
