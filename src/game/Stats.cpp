// Stats & Advancements implementation.
#include "Stats.hpp"
#include "Items.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <system_error>

namespace cppfm {

namespace {

constexpr std::size_t kMaxProgressFileBytes = 8u * 1024u * 1024u;
std::mutex progressIoMutex;

bool readProgressJson(const std::filesystem::path& path, json::Value& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    if (!sizeError && fileSize > kMaxProgressFileBytes)
        throw std::length_error("progress file exceeds size limit");

    std::string text;
    if (!sizeError) text.reserve(static_cast<std::size_t>(fileSize));
    char buffer[8192];
    while (file) {
        file.read(buffer, sizeof(buffer));
        const std::streamsize count = file.gcount();
        if (count <= 0) continue;
        if (text.size() > kMaxProgressFileBytes - static_cast<std::size_t>(count))
            throw std::length_error("progress file exceeds size limit");
        text.append(buffer, static_cast<std::size_t>(count));
    }
    if (!file.eof()) throw std::runtime_error("could not read progress file");
    out = json::Value::parse(text);
    return true;
}

void writeProgressJson(const std::filesystem::path& path, const json::Value& root) {
    const std::string encoded = root.dump();
    if (encoded.size() > kMaxProgressFileBytes)
        throw std::length_error("serialized progress file exceeds size limit");

    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path temporary = path.string() + ".new";
    try {
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("could not open progress temporary file");
            file.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
            file.flush();
            if (!file) throw std::runtime_error("could not write progress temporary file");
        }
        std::error_code renameError;
        std::filesystem::rename(temporary, path, renameError);
        if (renameError) throw std::system_error(renameError, "replace progress file");
    } catch (...) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        throw;
    }
}

bool parseCounter(const json::Value& value, std::int64_t& result) {
    if (!value.isNum() || !std::isfinite(value.number) ||
        std::trunc(value.number) != value.number ||
        value.number < -0x1p63 || value.number >= 0x1p63)
        return false;
    result = static_cast<std::int64_t>(value.number);
    return true;
}

} // namespace

// ---------------------------------------------------------------- stats io

void StatsManager::load(const std::string& uuidHex) {
    const std::filesystem::path path =
        std::filesystem::path(worldDir_) / "stats" / (uuidHex + ".json");
    std::lock_guard lock(progressIoMutex);
    c_.clear();
    dirty_ = false;
    try {
        json::Value root;
        if (!readProgressJson(path, root)) return;
        const auto* stats = root.find("stats");
        if (!root.isObj() || !stats || !stats->isObj())
            throw std::runtime_error("stats root has no object-valued stats field");

        Counters loaded;
        for (const auto& [category, entries] : stats->obj) {
            if (!entries.isObj())
                throw std::runtime_error("stats category is not an object");
            for (const auto& [name, value] : entries.obj) {
                std::int64_t count = 0;
                if (!parseCounter(value, count))
                    throw std::runtime_error("stats counter is not an integer");
                loaded[category + "|" + name] = count;
            }
        }
        c_ = std::move(loaded);
        dirty_ = false;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Stats] ignoring malformed %s: %s\n",
                     path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[Stats] ignoring malformed %s\n", path.c_str());
    }
}

void StatsManager::save(const std::string& uuidHex) {
    const std::filesystem::path path =
        std::filesystem::path(worldDir_) / "stats" / (uuidHex + ".json");
    std::lock_guard lock(progressIoMutex);
    try {
        json::Value root = json::Value::object();
        json::Value stats = json::Value::object();
        for (const auto& [key, val] : c_) {
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
        writeProgressJson(path, root);
        dirty_ = false;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Stats] save failed for %s: %s\n",
                     path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[Stats] save failed for %s\n", path.c_str());
    }
}

// ---------------------------------------------------------- advancements io

void AdvancementManager::load() {
    const std::filesystem::path path =
        std::filesystem::path(worldDir_) / "advancements" / (uuid_ + ".json");
    std::lock_guard lock(progressIoMutex);
    unlocked_.clear();
    dirty_ = false;
    try {
        json::Value root;
        if (!readProgressJson(path, root)) return;
        if (!root.isObj()) throw std::runtime_error("advancement root is not an object");
        std::unordered_set<std::string> loaded;
        loaded.reserve(root.obj.size());
        for (const auto& [id, _] : root.obj) loaded.insert(id);
        unlocked_ = std::move(loaded);
        dirty_ = false;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Advancement] ignoring malformed %s: %s\n",
                     path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[Advancement] ignoring malformed %s\n", path.c_str());
    }
}

void AdvancementManager::save() {
    const std::filesystem::path path =
        std::filesystem::path(worldDir_) / "advancements" / (uuid_ + ".json");
    std::lock_guard lock(progressIoMutex);
    try {
        json::Value root = json::Value::object();
        for (const auto& id : unlocked_)
            root.set(id, json::Value::object());
        writeProgressJson(path, root);
        dirty_ = false;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Advancement] save failed for %s: %s\n",
                     path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[Advancement] save failed for %s\n", path.c_str());
    }
}

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
            std::vector<std::string> criterionNames;
            if (auto* crit = v.find("criteria")) {
                if (crit->isObj()) {
                    criterionNames.reserve(crit->obj.size());
                    for (const auto& [k, cval] : crit->obj) {
                        if (!cval.isObj()) continue;
                        if (auto* tr = cval.find("trigger")) {
                            if (!tr->isStr()) continue;
                            AdvancementTriggerInfo ti;
                            ti.trigger = tr->asStr();
                            if (auto* cond = cval.find("conditions")) ti.conditions = *cond;
                            ex.triggers.push_back(std::move(ti));
                            criterionNames.push_back(k);
                        }
                    }
                }
            }
            // requirements
            if (auto* req = v.find("requirements")) {
                if (req->isArr()) {
                    for (const auto& grp : req->arr) if (grp.isArr()) {
                        std::vector<std::string> g;
                        for (const auto& s : grp.arr) if (s.isStr()) g.push_back(s.asStr());
                        if (!g.empty()) ex.requirements.push_back(std::move(g));
                    }
                }
            }
            if (ex.requirements.empty()) {
                if (!criterionNames.empty()) {
                    for (auto& criterion : criterionNames)
                        ex.requirements.push_back({std::move(criterion)});
                } else {
                    ex.requirements = {{"done"}};
                }
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
