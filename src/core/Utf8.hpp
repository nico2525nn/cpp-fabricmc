// Strict UTF-8 validation and Java-compatible UTF-16 string length helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cppfm::utf8 {

namespace detail {

inline bool decodeNext(std::string_view value, std::size_t offset,
                      std::uint32_t& codePoint,
                      std::size_t& width) noexcept {
    if (offset >= value.size()) return false;
    const auto first = static_cast<std::uint8_t>(value[offset]);
    if (first <= 0x7fU) {
        codePoint = first;
        width = 1;
        return true;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
        codePoint = first & 0x1fU;
        width = 2;
    } else if (first >= 0xe0U && first <= 0xefU) {
        codePoint = first & 0x0fU;
        width = 3;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        codePoint = first & 0x07U;
        width = 4;
    } else {
        return false;
    }

    if (width > value.size() - offset) return false;
    const auto second = static_cast<std::uint8_t>(value[offset + 1]);
    if ((first == 0xe0U && second < 0xa0U) ||
        (first == 0xedU && second > 0x9fU) ||
        (first == 0xf0U && second < 0x90U) ||
        (first == 0xf4U && second > 0x8fU))
        return false;

    for (std::size_t index = 1; index < width; ++index) {
        const auto continuation =
            static_cast<std::uint8_t>(value[offset + index]);
        if (continuation < 0x80U || continuation > 0xbfU) return false;
        codePoint = (codePoint << 6U) | (continuation & 0x3fU);
    }
    return true;
}

inline void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

} // namespace detail

// Java's String.length() counts UTF-16 code units, not UTF-8 bytes or Unicode
// scalar values. Reject malformed, overlong, surrogate, and out-of-range UTF-8
// so protocol length checks use the same valid-string domain as Minecraft.
inline std::optional<std::size_t> utf16Length(
    std::string_view value) noexcept {
    std::size_t units = 0;
    for (std::size_t offset = 0; offset < value.size();) {
        std::uint32_t codePoint = 0;
        std::size_t width = 0;
        if (!detail::decodeNext(value, offset, codePoint, width))
            return std::nullopt;
        const std::size_t increment = codePoint > 0xffffU ? 2U : 1U;
        if (units > std::numeric_limits<std::size_t>::max() - increment)
            return std::nullopt;
        units += increment;
        offset += width;
    }
    return units;
}

// Convert valid standard UTF-8 to Java's UTF-16 code-unit representation.
// Output changes only after the complete input has been validated.
inline bool toUtf16(std::string_view value, std::u16string& output) {
    const auto length = utf16Length(value);
    if (!length) return false;

    std::u16string converted;
    converted.reserve(*length);
    for (std::size_t offset = 0; offset < value.size();) {
        std::uint32_t codePoint = 0;
        std::size_t width = 0;
        if (!detail::decodeNext(value, offset, codePoint, width))
            return false; // Defensive: utf16Length already validated it.
        if (codePoint <= 0xffffU) {
            converted.push_back(static_cast<char16_t>(codePoint));
        } else {
            codePoint -= 0x10000U;
            converted.push_back(static_cast<char16_t>(
                0xd800U + (codePoint >> 10U)));
            converted.push_back(static_cast<char16_t>(
                0xdc00U + (codePoint & 0x3ffU)));
        }
        offset += width;
    }
    output = std::move(converted);
    return true;
}

// Convert Java UTF-16 code units to standard UTF-8. Java strings may contain
// unpaired surrogates; callers can either reject them or replace each one with
// U+FFFD when crossing into UTF-8-only native APIs.
inline bool fromUtf16(std::u16string_view value, std::string& output,
                      bool replaceUnpairedSurrogates = false) {
    std::string converted;
    if (value.size() <= std::numeric_limits<std::size_t>::max() / 3U)
        converted.reserve(value.size() * 3U);

    for (std::size_t index = 0; index < value.size(); ++index) {
        std::uint32_t codePoint = value[index];
        if (codePoint >= 0xd800U && codePoint <= 0xdbffU) {
            if (index + 1 < value.size() &&
                value[index + 1] >= 0xdc00U && value[index + 1] <= 0xdfffU) {
                codePoint = 0x10000U +
                    ((codePoint - 0xd800U) << 10U) +
                    (static_cast<std::uint32_t>(value[++index]) - 0xdc00U);
            } else if (replaceUnpairedSurrogates) {
                codePoint = 0xfffdU;
            } else {
                return false;
            }
        } else if (codePoint >= 0xdc00U && codePoint <= 0xdfffU) {
            if (!replaceUnpairedSurrogates) return false;
            codePoint = 0xfffdU;
        }
        detail::appendUtf8(converted, codePoint);
    }
    output = std::move(converted);
    return true;
}

inline bool isValid(std::string_view value) noexcept {
    return utf16Length(value).has_value();
}

// Interpret a UTF-8 byte string as a filesystem path without passing through
// the process locale/code page. Use this only for APIs whose string contract is
// explicitly UTF-8; legacy narrow-path APIs should keep their prior semantics.
inline std::filesystem::path pathFromUtf8(std::string_view value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value)
        encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(encoded));
}

inline std::string pathToUtf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t byte : encoded)
        result.push_back(static_cast<char>(byte));
    return result;
}

inline std::string genericPathToUtf8(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t byte : encoded)
        result.push_back(static_cast<char>(byte));
    return result;
}

} // namespace cppfm::utf8
