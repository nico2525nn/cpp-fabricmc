// Stats & Advancements implementation.
#include "Stats.hpp"
#include "Items.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace cppfm {

// ---------------------------------------------------------------- stats io

void StatsManager::load(const std::string& uuidHex) {
    std::ifstream f("world/stats/" + uuidHex + ".json");
    if (!f) return;
    try {
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        const json::Value v = json::Value::parse(text);
        const json::Value& stats = v.at("stats");
        for (auto& [cat, entries] : stats.obj)
            for (auto& [k, n] : entries.obj)
                c_[cat + "|" + k] = static_cast<std::int64_t>(n.asI64());
        dirty_ = false;
    } catch (...) {}
}

void StatsManager::save(const std::string& uuidHex) {
    try {
        namespace fs = std::filesystem;
        fs::create_directories("world/stats");
        json::Value root = json::Value::object();
        json::Value stats = json::Value::object();
        for (auto& [key, val] : c_) {
            const auto bar = key.find('|');
            if (bar == std::string::npos) continue;
            const std::string cat = key.substr(0, bar);
            const std::string name = key.substr(bar + 1);
            json::Value* bucket = stats.find(cat);
            if (!bucket) { stats.set(cat, json::Value::object()); bucket = stats.find(cat); }
            bucket->set(name, json::Value::ofNumber(static_cast<double>(val)));
        }
        root.set("stats", stats);
        root.set("DataVersion", json::Value::ofNumber(4189));
        std::ofstream f("world/stats/" + uuidHex + ".json", std::ios::binary);
        f << root.dump();
        dirty_ = false;
    } catch (...) {}
}

// ---------------------------------------------------------- advancements io

void AdvancementManager::load() {
    std::ifstream f("world/advancements/" + uuid_ + ".json");
    if (!f) return;
    try {
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        const json::Value v = json::Value::parse(text);
        for (auto& [k, _] : v.obj) unlocked_.insert(k);
        dirty_ = false;
    } catch (...) {}
}

void AdvancementManager::save() {
    try {
        namespace fs = std::filesystem;
        fs::create_directories("world/advancements");
        json::Value root = json::Value::object();
        for (auto& id : unlocked_)
            root.set(id, json::Value::object());
        std::ofstream f2("world/advancements/" + uuid_ + ".json",
                         std::ios::binary);
        f2 << root.dump();
        dirty_ = false;
    } catch (...) {}
}

// ------------------------------------------------------------------ packet

void writeAdvancementsPacket(
    WriteBuffer& out, bool reset,
    const std::vector<AdvancementDef>& defs,
    const std::function<bool(const std::string&)>& isUnlocked,
    const std::vector<std::string>& removed) {
    out.boolean(reset);
    // advancementMapping: send every def; display only when unlocked
    out.varint(static_cast<std::int32_t>(defs.size()));
    for (const auto& d : defs) {
        out.string(d.id);
        const bool hasParent = d.parent != nullptr;
        out.boolean(hasParent);                       // parentId option
        if (hasParent) out.string(d.parent);
        const bool show = isUnlocked(d.id);
        out.boolean(show);                            // displayData option
        if (show) {
            // title / description as NBT text components
            nbt::writeTextComponent(out, d.title);
            nbt::writeTextComponent(out, d.description);
            ItemStack icon = ItemStack::ofName(d.iconItem, 1);
            icon.write(out);
            out.varint(d.frame);                      // frame type
            int flags = d.flags;
            if (reset) flags &= ~0x02;                // suppress toast on reset/relog (D23)
            out.varint(flags);
            if (flags & 0x01) {
                const char* bg = d.background ? d.background : "minecraft:textures/gui/advancements/backgrounds/stone.png";
                out.string(bg);
            }
            out.f32(d.x);
            out.f32(d.y);
        }
        // requirements: single criterion named "done"
        out.varint(1);                                // one requirement group
        out.varint(1);                                // one criterion in it
        out.string("done");
        out.boolean(false);                           // sendsTelemetryData
    }
    // identifiers (removed advancements)
    out.varint(static_cast<std::int32_t>(removed.size()));
    for (auto& r : removed) out.string(r);
    // progressMapping: mark unlocked ones complete
    std::vector<const AdvancementDef*> done;
    for (const auto& d : defs)
        if (isUnlocked(d.id)) done.push_back(&d);
    out.varint(static_cast<std::int32_t>(done.size()));
    for (const auto* d : done) {
        out.string(d->id);
        out.varint(1);
        out.string("done");
        out.boolean(true);
        out.i64(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
    }
}

} // namespace cppfm
