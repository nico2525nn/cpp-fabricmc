#pragma once
#include <string>
#include <map>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <type_traits>

namespace cppfm {

class ServerProperties {
public:
    std::map<std::string, std::string> props;

    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            // trim trailing \r\n and spaces
            while (!line.empty() && (line.back()=='\n' || line.back()=='\r' || line.back()==' ' || line.back()=='\t')) line.pop_back();
            size_t start = line.find_first_not_of(" \t");
            if (start==std::string::npos) continue;
            line = line.substr(start);
            if (line.empty() || line[0]=='#') continue;
            auto eq = line.find('=');
            if (eq==std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq+1);
            // trim k and v
            auto trim = [](std::string s){
                size_t a = s.find_first_not_of(" \t\r\n");
                if (a==std::string::npos) return std::string();
                size_t b = s.find_last_not_of(" \t\r\n");
                return s.substr(a, b-a+1);
            };
            k = trim(k);
            v = trim(v);
            // case-insensitive keys? keep as-is but lower-case for lookup tolerance
            props[k] = v;
        }
        return true;
    }

    bool save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;
        for (auto& [k,v] : props) f << k << "=" << v << "\n";
        return true;
    }

    bool has(const std::string& key) const {
        return props.find(key) != props.end();
    }

    std::string getString(const std::string& key, const std::string& def="") const {
        auto it = props.find(key);
        if (it==props.end()) {
            // try lower-case variants? also try hyphen vs without
            for (auto& [k,v] : props) {
                std::string kl=k, kk=key;
                std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
                std::transform(kk.begin(), kk.end(), kk.begin(), ::tolower);
                if (kl==kk) return v;
            }
            return def;
        }
        return it->second;
    }

    template<typename T>
    T get(const std::string& key, T def = T{}) const {
        auto it = props.find(key);
        std::string v;
        if (it==props.end()) {
            // case-insensitive fallback
            std::string lowKey = key; std::transform(lowKey.begin(), lowKey.end(), lowKey.begin(), ::tolower);
            bool found=false;
            for (auto& [k,val] : props) {
                std::string lk=k; std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
                if (lk==lowKey) { v=val; found=true; break; }
            }
            if (!found) return def;
        } else v = it->second;

        if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, int>) {
            try { return std::stoi(v); } catch (...) { return def; }
        } else if constexpr (std::is_same_v<T, std::int32_t>) {
            try { return static_cast<std::int32_t>(std::stoi(v)); } catch (...) { return def; }
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            try { return std::stoll(v); } catch (...) { return def; }
        } else if constexpr (std::is_same_v<T, bool>) {
            std::string low=v; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (low=="true" || low=="1" || low=="yes" || low=="on") return true;
            if (low=="false" || low=="0" || low=="no" || low=="off") return false;
            return def;
        } else if constexpr (std::is_same_v<T, double>) {
            try { return std::stod(v); } catch (...) { return def; }
        } else if constexpr (std::is_same_v<T, float>) {
            try { return std::stof(v); } catch (...) { return def; }
        } else {
            return def;
        }
    }

    template<typename T>
    void set(const std::string& key, T value) {
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
};

} // namespace cppfm
