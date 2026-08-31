// Stats & Advancements (plan3.md 永続化拡張).
//
// * StatsManager: per-player counters persisted to world/stats/<uuid>.json in
//   a vanilla-shaped JSON layout; surfaced via /stats.
// * AdvancementManager: clean-room "cppfm:*" advancement tree with display
//   data, criteria progress and per-player persistence; advertised via the
//   Update Advancements packet so clients show proper toasts.
#pragma once
#include <cstdint>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "../core/Json.hpp"
#include "../core/NBT.hpp"

namespace cppfm {

class StatsManager {
public:
    using Counters = std::unordered_map<std::string, std::int64_t>;

    void load(const std::string& uuidHex);
    void save(const std::string& uuidHex);

    Counters& counters() { return c_; }
    void add(const std::string& key, std::int64_t v = 1) {
        c_[key] += v;
        dirty_ = true;
    }
    std::int64_t get(const std::string& key) const {
        auto it = c_.find(key);
        return it != c_.end() ? it->second : 0;
    }
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    Counters c_;
    bool dirty_ = false;
};

struct AdvancementDef {
    const char* id;
    const char* parent;          // nullptr for root
    const char* title;
    const char* description;
    const char* iconItem;
    int frame;                   // 0 task, 1 challenge, 2 goal
    float x, y;
    int flags = 0;               // 0x01 background, 0x02 show_toast, 0x04 hidden
    const char* background = nullptr; // valid when flags & 0x01
};

inline const std::vector<AdvancementDef>& advancementDefs() {
    static const std::vector<AdvancementDef> defs = {
        {"cppfm:root", nullptr, "CppFabricMC",
         "Welcome to the C++ server", "minecraft:grass_block", 0, 0.f, 0.f,
         0x03, "minecraft:textures/gui/advancements/backgrounds/stone.png"},
        {"cppfm:wood", "cppfm:root", "Getting Wood",
         "Punch a tree until a block of wood pops out",
         "minecraft:oak_log", 0, -2.f, 1.f, 0x00, nullptr},
        {"cppfm:bench", "cppfm:wood", "Benchmarking",
         "Craft a crafting table", "minecraft:crafting_table", 0, -2.f, 2.f, 0x00, nullptr},
        {"cppfm:tools", "cppfm:bench", "Time to Mine!",
         "Craft a stone pickaxe", "minecraft:stone_pickaxe", 0, -3.f, 3.f, 0x00, nullptr},
        {"cppfm:iron", "cppfm:tools", "Acquire Hardware",
         "Smelt an iron ingot", "minecraft:iron_ingot", 1, -1.f, 4.f, 0x00, nullptr},
        {"cppfm:diamonds", "cppfm:iron", "DIAMONDS!",
         "Acquire diamonds", "minecraft:diamond", 1, -1.f, 5.5f, 0x00, nullptr},
        {"cppfm:hunter", "cppfm:root", "Monster Hunter",
         "Slay a hostile monster", "minecraft:iron_sword", 0, 2.f, 1.f, 0x00, nullptr},
        {"cppfm:husbandry", "cppfm:root", "The Parrots and the Bats",
         "Breed two animals", "minecraft:wheat", 0, 4.f, 1.f, 0x00, nullptr},
        {"cppfm:cook", "cppfm:bench", "Delicious Fish",
         "Cook something in a furnace", "minecraft:furnace", 0, 0.f, 3.f, 0x00, nullptr},
    };
    return defs;
}

class AdvancementManager {
public:
    explicit AdvancementManager(const std::string& uuidHex)
        : uuid_(uuidHex) {}

    void load();
    void save();

    bool has(const std::string& id) const {
        return unlocked_.count(id) != 0;
    }
    // Returns true when this call granted it (fresh).
    bool grant(const std::string& id) {
        if (has(id)) return false;
        unlocked_.insert(id);
        dirty_ = true;
        return true;
    }
    // plan32 combat: revoke advancement (Yarn AdvancementCommand revoke)
    bool revoke(const std::string& id) {
        if (!has(id)) return false;
        unlocked_.erase(id);
        dirty_ = true;
        return true;
    }
    const std::unordered_set<std::string>& unlocked() const {
        return unlocked_;
    }

private:
    std::string uuid_;
    std::unordered_set<std::string> unlocked_;
    bool dirty_ = false;
};

// Wire helpers --------------------------------------------------------------
// Serializes the Update Advancements packet body advertising `defs` with the
// player's unlocked set and criterion timestamps.
void writeAdvancementsPacket(
    WriteBuffer& out, bool reset,
    const std::vector<AdvancementDef>& defs,
    const std::function<bool(const std::string&)>& isUnlocked,
    const std::vector<std::string>& removed = {});

} // namespace cppfm
