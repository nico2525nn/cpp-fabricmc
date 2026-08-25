// GameRules: vanilla-style rule storage persisted inside level.dat
// (plan3.md 永続化拡張). Values kept as strings like vanilla NBT.
#pragma once
#include <string>
#include <unordered_map>

namespace cppfm {

class GameRuleManager {
public:
    GameRuleManager() {
        // vanilla defaults
        set("doFireTick", "true", false);
        set("mobGriefing", "true", false);
        set("keepInventory", "false", false);
        set("doMobSpawning", "true", false);
        set("doDaylightCycle", "true", false);
        set("randomTickSpeed", "3", false);
        set("doWeatherCycle", "true", false);
        set("announceAdvancements", "true", false);
    }

    void set(const std::string& key, const std::string& value,
             bool markDirty = true) {
        rules_[key] = value;
        if (markDirty) dirty_ = true;
    }
    std::string get(const std::string& key) const {
        auto it = rules_.find(key);
        return it != rules_.end() ? it->second : std::string();
    }
    bool getBool(const std::string& key) const {
        const std::string v = get(key);
        return v == "true" || v == "1";
    }
    int getInt(const std::string& key, int def) const {
        const std::string v = get(key);
        if (v.empty()) return def;
        try { return std::stoi(v); } catch (...) { return def; }
    }
    bool contains(const std::string& key) const {
        return rules_.count(key) != 0;
    }
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    const std::unordered_map<std::string, std::string>& all() const {
        return rules_;
    }

private:
    std::unordered_map<std::string, std::string> rules_;
    bool dirty_ = false;
};

} // namespace cppfm
