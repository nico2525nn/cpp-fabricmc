#pragma once
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cppfm {

class ServerProperties {
public:
    std::map<std::string, std::string> props;

    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::string bytes;
        char buffer[8192];
        while (f) {
            f.read(buffer, sizeof(buffer));
            bytes.append(buffer, static_cast<std::size_t>(f.gcount()));
        }
        if (f.bad()) return false;
        return loadBytes(bytes);
    }

    bool loadText(std::string_view text) {
        return loadBytes(text);
    }

    const std::vector<std::pair<std::string, std::string>>& parsedEntries() const {
        return parsedEntries_;
    }

private:
    bool loadBytes(std::string_view bytes) {
        const std::string text = decodeInput(bytes);
        std::map<std::string, std::string> loaded;
        std::vector<std::pair<std::string, std::string>> ordered;
        std::string logicalLine;
        bool continuing = false;
        std::size_t offset = 0;
        while (offset < text.size()) {
            const std::size_t lineStart = offset;
            while (offset < text.size() && text[offset] != '\n' && text[offset] != '\r')
                ++offset;
            const std::string_view physicalLine(text.data() + lineStart, offset - lineStart);
            if (offset < text.size() && text[offset] == '\r') ++offset;
            if (offset < text.size() && text[offset] == '\n') ++offset;

            if (continuing) {
                logicalLine.append(physicalLine.substr(skipPropertyWhitespace(physicalLine)));
            } else {
                const std::size_t first = skipPropertyWhitespace(physicalLine);
                if (first == physicalLine.size() || physicalLine[first] == '#' ||
                    physicalLine[first] == '!')
                    continue;
                logicalLine.assign(physicalLine);
            }

            if (hasContinuationMarker(logicalLine)) {
                logicalLine.pop_back();
                continuing = true;
                continue;
            }

            addPropertyLine(logicalLine, loaded, ordered);
            logicalLine.clear();
            continuing = false;
        }

        // A final continuation marker is removed just as it is for any other
        // physical line; Java's reader then reaches EOF with the accumulated
        // logical property still available.
        if (continuing) addPropertyLine(logicalLine, loaded, ordered);

        props = std::move(loaded);
        parsedEntries_ = std::move(ordered);
        return true;
    }

    static bool isValidUtf8(std::string_view input) {
        std::size_t i = 0;
        while (i < input.size()) {
            const auto first = static_cast<unsigned char>(input[i]);
            if (first <= 0x7f) {
                ++i;
                continue;
            }

            std::size_t length = 0;
            unsigned char secondMin = 0x80;
            unsigned char secondMax = 0xbf;
            if (first >= 0xc2 && first <= 0xdf) {
                length = 2;
            } else if (first >= 0xe0 && first <= 0xef) {
                length = 3;
                if (first == 0xe0) secondMin = 0xa0;
                if (first == 0xed) secondMax = 0x9f;
            } else if (first >= 0xf0 && first <= 0xf4) {
                length = 4;
                if (first == 0xf0) secondMin = 0x90;
                if (first == 0xf4) secondMax = 0x8f;
            } else {
                return false;
            }
            if (i + length > input.size()) return false;
            const auto second = static_cast<unsigned char>(input[i + 1]);
            if (second < secondMin || second > secondMax) return false;
            for (std::size_t j = 2; j < length; ++j) {
                const auto continuation = static_cast<unsigned char>(input[i + j]);
                if (continuation < 0x80 || continuation > 0xbf) return false;
            }
            i += length;
        }
        return true;
    }

    static void appendUtf8(std::string& output, std::uint32_t codePoint) {
        if (codePoint <= 0x7f) {
            output.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else if (codePoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }

    static std::string decodeInput(std::string_view bytes) {
        if (isValidUtf8(bytes)) return std::string(bytes);

        // Dedicated-server properties loading first uses a strict UTF-8
        // decoder, then retries the complete file as ISO-8859-1 on failure.
        std::string decoded;
        decoded.reserve(bytes.size() * 2);
        for (const unsigned char byte : bytes) appendUtf8(decoded, byte);
        return decoded;
    }

    static bool isPropertyWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\f';
    }

    static std::size_t skipPropertyWhitespace(std::string_view text) {
        std::size_t i = 0;
        while (i < text.size() && isPropertyWhitespace(text[i])) ++i;
        return i;
    }

    static bool hasContinuationMarker(std::string_view line) {
        std::size_t backslashes = 0;
        for (std::size_t i = line.size(); i > 0 && line[i - 1] == '\\'; --i)
            ++backslashes;
        return (backslashes & 1U) != 0;
    }

    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static std::uint16_t parseUnicodeEscape(std::string_view text, std::size_t slash) {
        if (slash + 6 > text.size() || text[slash] != '\\' || text[slash + 1] != 'u')
            throw std::invalid_argument("Malformed \\uxxxx encoding in properties");
        std::uint16_t codeUnit = 0;
        for (std::size_t i = slash + 2; i < slash + 6; ++i) {
            const int digit = hexValue(text[i]);
            if (digit < 0)
                throw std::invalid_argument("Malformed \\uxxxx encoding in properties");
            codeUnit = static_cast<std::uint16_t>((codeUnit << 4) | digit);
        }
        return codeUnit;
    }

    static std::string decodeEscapes(std::string_view input) {
        std::string output;
        output.reserve(input.size());
        for (std::size_t i = 0; i < input.size();) {
            if (input[i] != '\\') {
                output.push_back(input[i++]);
                continue;
            }
            if (i + 1 == input.size()) {
                output.push_back('\\');
                ++i;
                continue;
            }

            const char escaped = input[i + 1];
            if (escaped == 'u') {
                const std::uint16_t first = parseUnicodeEscape(input, i);
                i += 6;
                if (first >= 0xd800 && first <= 0xdbff) {
                    if (i + 6 <= input.size() && input[i] == '\\' && input[i + 1] == 'u') {
                        const std::uint16_t second = parseUnicodeEscape(input, i);
                        if (second >= 0xdc00 && second <= 0xdfff) {
                            const std::uint32_t codePoint = 0x10000U +
                                ((static_cast<std::uint32_t>(first) - 0xd800U) << 10) +
                                (static_cast<std::uint32_t>(second) - 0xdc00U);
                            appendUtf8(output, codePoint);
                            i += 6;
                            continue;
                        }
                    }
                    // Java strings can retain isolated UTF-16 surrogates;
                    // this API stores UTF-8, so represent them with U+FFFD.
                    appendUtf8(output, 0xfffd);
                } else if (first >= 0xdc00 && first <= 0xdfff) {
                    appendUtf8(output, 0xfffd);
                } else {
                    appendUtf8(output, first);
                }
                continue;
            }

            switch (escaped) {
                case 't': output.push_back('\t'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 'f': output.push_back('\f'); break;
                default: output.push_back(escaped); break;
            }
            i += 2;
        }
        return output;
    }

    static void addPropertyLine(const std::string& line,
                                std::map<std::string, std::string>& loaded,
                                std::vector<std::pair<std::string, std::string>>& ordered) {
        std::size_t keyStart = skipPropertyWhitespace(line);
        if (keyStart == line.size() || line[keyStart] == '#' || line[keyStart] == '!') return;

        std::size_t keyEnd = keyStart;
        bool escaped = false;
        for (; keyEnd < line.size(); ++keyEnd) {
            const char c = line[keyEnd];
            if (c == '\\') {
                escaped = !escaped;
                continue;
            }
            if (!escaped && (isPropertyWhitespace(c) || c == '=' || c == ':')) break;
            escaped = false;
        }

        std::size_t valueStart = keyEnd;
        if (valueStart < line.size()) {
            if (isPropertyWhitespace(line[valueStart])) {
                valueStart = skipPropertyWhitespace(std::string_view(line).substr(valueStart)) +
                             valueStart;
                if (valueStart < line.size() && (line[valueStart] == '=' || line[valueStart] == ':'))
                    ++valueStart;
            } else {
                ++valueStart;
            }
            valueStart += skipPropertyWhitespace(std::string_view(line).substr(valueStart));
        }

        const std::string key = decodeEscapes(
            std::string_view(line).substr(keyStart, keyEnd - keyStart));
        const std::string value = decodeEscapes(std::string_view(line).substr(valueStart));
        loaded[key] = value;
        for (auto it = ordered.begin(); it != ordered.end();) {
            if (it->first == key) it = ordered.erase(it);
            else ++it;
        }
        ordered.emplace_back(key, value);
    }

public:
    bool save(const std::string& path) const {
        // This legacy helper writes a simple key=value snapshot. It does not
        // implement java.util.Properties.store escaping, comments, or encoding.
        std::ofstream f(path);
        if (!f) return false;
        for (auto& [k,v] : props) f << k << "=" << v << "\n";
        f.flush();
        return static_cast<bool>(f);
    }

    bool has(const std::string& key) const {
        return findProperty(key) != props.end();
    }

    std::string getString(const std::string& key, const std::string& def="") const {
        const auto it = findProperty(key);
        return it == props.end() ? def : it->second;
    }

    template<typename T>
    T get(const std::string& key, T def = T{}) const {
        const auto it = findProperty(key);
        if (it == props.end()) return def;
        const std::string& v = it->second;

        if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            T parsed{};
            return parseInteger(v, parsed) ? parsed : def;
        } else if constexpr (std::is_same_v<T, bool>) {
            const std::string low = asciiLower(v);
            return low == "true";
        } else if constexpr (std::is_same_v<T, double>) {
            double parsed{};
            return parseFloating(v, parsed) ? parsed : def;
        } else if constexpr (std::is_same_v<T, float>) {
            float parsed{};
            return parseFloating(v, parsed) ? parsed : def;
        } else {
            return def;
        }
    }

    template<typename T>
    void set(const std::string& key, T value) {
        parsedEntries_.clear();
        if constexpr (std::is_same_v<T, std::string>) props[key]=value;
        else if constexpr (std::is_same_v<T, bool>) props[key]= value ? "true":"false";
        else props[key]= std::to_string(value);
    }

    // Convenience typed getters matching spec: get<int>(key), get<bool>(key) Usage: props.get<int>("viewDistance") etc.

    // Apply to ServerConfig helpers
    // Legacy convenience accessor; ServerConfig remains the canonical startup path.
    int viewDistance() const { return get<int>("view-distance", get<int>("viewDistance", 10)); }
    int simulationDistance() const { return get<int>("simulation-distance", get<int>("simulationDistance", 10)); }
    int spawnProtection() const { return get<int>("spawn-protection", 16); }
    // Chebyshev-sorted + burst 16/tick with forced/spawn ticket protection — NOT a simple clear().
    int maxLoadedChunks(int viewDist) const {
        int configured = get<int>("max-loaded-chunks", get<int>("maxLoadedChunks", -1));
        if (configured >= 0) return std::max(0, configured);
        int autoCap = std::max(8192, viewDist * viewDist * 4);
        return autoCap;
    }

private:
    using PropertyIterator = std::map<std::string, std::string>::const_iterator;
    std::vector<std::pair<std::string, std::string>> parsedEntries_;

    static std::string asciiLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        });
        return value;
    }

    PropertyIterator findProperty(const std::string& key) const {
        return props.find(key);
    }

    template<typename T>
    static bool parseInteger(std::string_view text, T& value) {
        if (text.empty()) return false;
        if (text.front() == '+') {
            text.remove_prefix(1);
            if (text.empty()) return false;
        }
        const char* first = text.data();
        const char* last = first + text.size();
        const auto result = std::from_chars(first, last, value, 10);
        return result.ec == std::errc{} && result.ptr == last;
    }

    template<typename T>
    static bool parseFloating(const std::string& text, T& value) {
        if (text.empty()) return false;
        char* end = nullptr;
        errno = 0;
        const char* start = text.c_str();
        if constexpr (std::is_same_v<T, double>) {
            value = std::strtod(start, &end);
        } else {
            value = std::strtof(start, &end);
        }
        return end == start + text.size() && errno != ERANGE && std::isfinite(value);
    }
};

} // namespace cppfm
