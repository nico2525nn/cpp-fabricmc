#pragma once
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
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
        return loadStream(f);
    }

    bool loadText(std::string_view text) {
        std::istringstream stream{std::string(text)};
        return loadStream(stream);
    }

    const std::vector<std::pair<std::string, std::string>>& parsedEntries() const {
        return parsedEntries_;
    }

private:
    bool loadStream(std::istream& f) {
        std::map<std::string, std::string> loaded;
        std::vector<std::pair<std::string, std::string>> ordered;
        std::string line;
        while (std::getline(f, line)) {
            const std::string trimmedLine = trim(line);
            if (trimmedLine.empty() || trimmedLine.front() == '#') continue;
            const std::size_t eq = trimmedLine.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(trimmedLine.substr(0, eq));
            if (key.empty()) continue;
            const std::string value = trim(trimmedLine.substr(eq + 1));

            // Java Properties treats a later duplicate as the effective
            // value.  Apply the same rule for case variants as well, while
            // retaining the final spelling for deterministic save output.
            for (auto it = loaded.begin(); it != loaded.end();) {
                if (asciiLower(it->first) == asciiLower(key)) it = loaded.erase(it);
                else ++it;
            }
            loaded[key] = value;
            for (auto it = ordered.begin(); it != ordered.end();) {
                if (asciiLower(it->first) == asciiLower(key)) it = ordered.erase(it);
                else ++it;
            }
            ordered.emplace_back(key, value);
        }
        if (f.bad()) return false;
        props = std::move(loaded);
        parsedEntries_ = std::move(ordered);
        return true;
    }

    static std::string trim(const std::string& value) {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

public:
    bool save(const std::string& path) const {
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
            if (low=="true" || low=="1" || low=="yes" || low=="on") return true;
            if (low=="false" || low=="0" || low=="no" || low=="off") return false;
            return def;
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
    int viewDistance() const { return get<int>("view-distance", get<int>("viewDistance", 6)); }
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
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    PropertyIterator findProperty(const std::string& key) const {
        auto it = props.find(key);
        if (it != props.end()) return it;
        const std::string loweredKey = asciiLower(key);
        for (auto candidate = props.begin(); candidate != props.end(); ++candidate) {
            if (asciiLower(candidate->first) == loweredKey) return candidate;
        }
        return props.end();
    }

    template<typename T>
    static bool parseInteger(const std::string& text, T& value) {
        if (text.empty()) return false;
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
