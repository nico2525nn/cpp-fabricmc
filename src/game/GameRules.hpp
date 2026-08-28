// GameRules: vanilla-style rule storage persisted inside level.dat
// (plan3.md 永続化拡張). Values kept as strings like vanilla NBT.
#pragma once
#include <string>
#include <unordered_map>

namespace cppfm {

class GameRuleManager {
public:
    GameRuleManager() {
        // vanilla defaults — 37 rules (strict audit MEDIUM)
        set("doFireTick", "true", false);
        set("mobGriefing", "true", false);
        set("keepInventory", "false", false);
        set("doMobSpawning", "true", false);
        set("doDaylightCycle", "true", false);
        set("randomTickSpeed", "3", false);
        set("doWeatherCycle", "true", false);
        set("announceAdvancements", "true", false);
        set("naturalRegeneration", "true", false);
        set("doImmediateRespawn", "false", false);
        set("drowningDamage", "true", false);
        set("fallDamage", "true", false);
        set("fireDamage", "true", false);
        set("freezeDamage", "true", false);
        set("doTileDrops", "true", false);
        set("doMobLoot", "true", false);
        set("doEntityDrops", "true", false);
        set("commandBlockOutput", "true", false);
        set("logAdminCommands", "true", false);
        set("showDeathMessages", "true", false);
        set("sendCommandFeedback", "true", false);
        set("reducedDebugInfo", "false", false);
        set("spectatorsGenerateChunks", "true", false);
        set("spawnRadius", "10", false);
        set("maxEntityCramming", "24", false);
        set("doLimitedCrafting", "false", false);
        set("maxCommandChainLength", "65536", false);
        set("disableElytraMovementCheck", "false", false);
        set("disableRaids", "false", false);
        set("doInsomnia", "true", false);
        set("doPatrolSpawning", "true", false);
        set("doTraderSpawning", "true", false);
        set("doWardenSpawning", "true", false);
        set("forgiveDeadPlayers", "true", false);
        set("universalAnger", "false", false);
        set("playersSleepingPercentage", "100", false);
        set("blockExplosionDropDecay", "true", false);
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
