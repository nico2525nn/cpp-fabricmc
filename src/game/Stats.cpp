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

// plan35 §1: helpers for owned advancements
std::vector<AdvancementDefOwned> buildOwnedFromRaw(const std::unordered_map<std::string,std::string>& rawAdv) {
    std::vector<AdvancementDefOwned> out;
    out.reserve(rawAdv.size());
    for (auto& [id, raw] : rawAdv) {
        // filter to story/adventure for now but allow any minecraft: advancement
        if (id.rfind("minecraft:",0)!=0 && id.rfind("cppfm:",0)!=0) continue;
        try {
            json::Value v = json::Value::parse(raw);
            AdvancementDefOwned ex;
            ex.id = id;
            if (auto* par = v.find("parent")) ex.parent = par->asStr();
            std::string title = id, desc = "";
            if (auto* disp = v.find("display")) {
                if (auto* icon = disp->find("icon")) {
                    if (auto* it = icon->find("item")) ex.iconItem = it->asStr();
                    else if (icon->isStr()) ex.iconItem = icon->asStr();
                    else if (auto* nid = icon->find("id")) ex.iconItem = nid->asStr();
                }
                if (auto* ttl = disp->find("title")) {
                    if (ttl->isStr()) title = ttl->asStr();
                    else if (auto* tr = ttl->find("translate")) title = tr->asStr();
                    else title = ttl->dump();
                }
                if (auto* dsc = disp->find("description")) {
                    if (dsc->isStr()) desc = dsc->asStr();
                    else if (auto* tr = dsc->find("translate")) desc = tr->asStr();
                    else desc = dsc->dump();
                }
                if (auto* fr = disp->find("frame")) {
                    std::string fs = fr->asStr();
                    if (fs=="challenge") ex.frame = 1;
                    else if (fs=="goal") ex.frame = 2;
                    else ex.frame = 0;
                }
                // flags
                int flags = 0;
                if (disp->find("background")) { flags |= 0x01; ex.background = disp->at("background").asStr(); }
                if (auto* toast = disp->find("show_toast")) { if (toast->isBool() ? toast->boolean : toast->asStr()=="true") flags |= 0x02; else flags &= ~0x02; }
                else flags |= 0x02;
                if (auto* hidden = disp->find("hidden")) { if (hidden->isBool() ? hidden->boolean : hidden->asStr()=="true") flags |= 0x04; }
                if (auto* announce = disp->find("announce_to_chat")) { (void)announce; }
                ex.flags = flags;
                if (auto* bx = disp->find("x")) ex.x = bx->asFloat(0.f);
                if (auto* by = disp->find("y")) ex.y = by->asFloat(0.f);
            }
            ex.title = title;
            ex.description = desc.empty() ? title : desc;
            if (ex.iconItem.empty()) ex.iconItem = "minecraft:stone";
            // criteria -> triggers
            if (auto* crit = v.find("criteria")) {
                if (crit->isObj()) {
                    for (auto& [k, cval] : crit->obj) {
                        if (!cval.isObj()) continue;
                        if (auto* tr = cval.find("trigger")) {
                            AdvancementTriggerInfo ti;
                            ti.trigger = tr->asStr();
                            if (auto* cond = cval.find("conditions")) ti.conditions = *cond;
                            else if (auto* cond2 = cval.find("conditions")) ti.conditions = *cond2;
                            ex.triggers.push_back(std::move(ti));
                        }
                    }
                }
            }
            // requirements
            if (auto* req = v.find("requirements")) {
                if (req->isArr()) {
                    for (auto& grp : req->arr) if (grp.isArr()) {
                        std::vector<std::string> g;
                        for (auto& s : grp.arr) if (s.isStr()) g.push_back(s.asStr());
                        if (!g.empty()) ex.requirements.push_back(std::move(g));
                    }
                }
            }
            if (ex.requirements.empty()) {
                if (!ex.triggers.empty()) {
                    // fallback: single group with all criterion names? we stored triggers but names lost; use "done"
                    ex.requirements = {{"done"}};
                } else ex.requirements = {{"done"}};
            }
            out.push_back(std::move(ex));
        } catch (...) { continue; }
    }
    return out;
}
std::vector<AdvancementDefOwned> mergedAdvancements(const std::unordered_map<std::string,std::string>& rawAdv) {
    std::vector<AdvancementDefOwned> out;
    out.reserve(advancementDefs().size() + rawAdv.size());
    for (auto& d : advancementDefs()) out.push_back(AdvancementDefToOwned(d));
    auto owned = buildOwnedFromRaw(rawAdv);
    // deduplicate by id
    std::unordered_set<std::string> seen;
    for (auto& o : out) seen.insert(o.id);
    for (auto& o : owned) if (!seen.count(o.id)) out.push_back(std::move(o));
    return out;
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
void writeAdvancementsPacket(
    WriteBuffer& out, bool reset,
    const std::vector<AdvancementDefOwned>& defs,
    const std::function<bool(const std::string&)>& isUnlocked,
    const std::vector<std::string>& removed) {
    out.boolean(reset);
    out.varint(static_cast<std::int32_t>(defs.size()));
    for (const auto& d : defs) {
        out.string(d.id);
        const bool hasParent = !d.parent.empty();
        out.boolean(hasParent);
        if (hasParent) out.string(d.parent);
        const bool show = isUnlocked(d.id);
        out.boolean(show);
        if (show) {
            nbt::writeTextComponent(out, d.title.c_str());
            nbt::writeTextComponent(out, d.description.c_str());
            ItemStack icon = ItemStack::ofName(d.iconItem.c_str(), 1);
            icon.write(out);
            out.varint(d.frame);
            int flags = d.flags;
            if (reset) flags &= ~0x02;
            out.varint(flags);
            if (flags & 0x01) {
                std::string bg = d.background.empty() ? std::string("minecraft:textures/gui/advancements/backgrounds/stone.png") : d.background;
                out.string(bg);
            }
            out.f32(d.x);
            out.f32(d.y);
        }
        // requirements: use owned requirements if present else single done
        if (!d.requirements.empty()) {
            out.varint(static_cast<std::int32_t>(d.requirements.size()));
            for (auto& grp : d.requirements) {
                out.varint(static_cast<std::int32_t>(grp.size()));
                for (auto& cr : grp) out.string(cr);
            }
        } else {
            out.varint(1);
            out.varint(1);
            out.string("done");
        }
        out.boolean(false);
    }
    out.varint(static_cast<std::int32_t>(removed.size()));
    for (auto& r : removed) out.string(r);
    std::vector<const AdvancementDefOwned*> done;
    for (const auto& d : defs) if (isUnlocked(d.id)) done.push_back(&d);
    out.varint(static_cast<std::int32_t>(done.size()));
    for (const auto* d : done) {
        out.string(d->id);
        // if requirements defined, use first criterion else done
        std::string crit = "done";
        if (!d->requirements.empty() && !d->requirements[0].empty()) crit = d->requirements[0][0];
        out.varint(1);
        out.string(crit);
        out.boolean(true);
        out.i64(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
    }
}

} // namespace cppfm
