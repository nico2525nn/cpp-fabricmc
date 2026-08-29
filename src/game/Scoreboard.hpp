// Scoreboard: objectives, scores, display slots and minimal teams
// (plan4 P1-D). Server-side model + packet builders; commands live in
// Commands.cpp.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "../core/NBT.hpp"
#include "../proto/Ids.hpp"

namespace cppfm {

class Scoreboard {
public:
    enum class NumberFormatType : std::int32_t { Blank = 0, Styled = 1, Fixed = 2 };
    struct NumberFormat {
        bool has = false;
        NumberFormatType type = NumberFormatType::Blank;
        std::string color;       // for Styled 1: e.g. "red"
        std::string fixedText;   // for Fixed 2: e.g. "♥" or JSON component
        // writes option varint + optional styling NBT (prismarine: option varint + switch styling)
        void write(WriteBuffer& b) const {
            if (!has) { b.boolean(false); return; }
            b.boolean(true);
            b.varint(static_cast<std::int32_t>(type));
            switch (type) {
                case NumberFormatType::Blank: break;
                case NumberFormatType::Styled: {
                    nbt::Writer w(b);
                    w.rootCompound();
                    if (!color.empty()) w.namedString("color", color);
                    else w.namedString("color", "red");
                    w.endCompound();
                    break;
                }
                case NumberFormatType::Fixed: {
                    nbt::writeTextComponent(b, fixedText.empty() ? std::string("\xE2\x99\xA5") : fixedText);
                    break;
                }
            }
        }
    };
    struct Objective {
        std::string name;
        std::string criteria = "dummy";
        std::string displayName;                 // legacy text (also NBT sent)
        std::uint8_t type = 0;                   // 0 integer 1 hearts
        NumberFormat numberFormat;               // §10 D25: blank/styled/fixed (hearts -> styled red)
    };

    // ---------------------------------------------------------------- model
    std::vector<Objective> objectives;
    // key = objective name; inner: holder -> score
    std::unordered_map<std::string, std::map<std::string, std::int32_t>> scores;
    std::int8_t displayedSlot = -1;              // sidebar slot(1); -1 none
    std::string displayedObjective;

    Objective* find(const std::string& name) {
        for (auto& o : objectives)
            if (o.name == name) return &o;
        return nullptr;
    }

    bool addObjective(const std::string& name, const std::string& criteria,
                      const std::string& display) {
        if (find(name)) return false;
        Objective o;
        o.name = name; o.criteria = criteria;
        o.displayName = display.empty() ? name : display;
        o.type = 0;
        // D25: hearts criterion should default to styled red so client renders hearts
        if (criteria == "health") {
            o.type = 1;
            o.numberFormat.has = true;
            o.numberFormat.type = NumberFormatType::Styled;
            o.numberFormat.color = "red";
        }
        objectives.push_back(std::move(o));
        return true;
    }
    bool removeObjectives(const std::string& name) {
        const auto n = std::remove_if(objectives.begin(), objectives.end(),
            [&](const Objective& o) { return o.name == name; });
        const bool removed = n != objectives.end();
        objectives.erase(n, objectives.end());
        scores.erase(name);
        return removed;
    }
    void setScore(const std::string& obj, const std::string& holder,
                  std::int32_t value) {
        scores[obj][holder] = value;
    }
    void addScore(const std::string& obj, const std::string& holder,
                  std::int32_t delta) {
        scores[obj][holder] += delta;
    }
    std::int32_t getScore(const std::string& obj,
                          const std::string& holder) const {
        auto it = scores.find(obj);
        if (it == scores.end()) return 0;
        auto it2 = it->second.find(holder);
        return it2 != it->second.end() ? it2->second : 0;
    }
    // D26 reset helpers — Prismarine packet_reset_score 0x49 (wildcard null vs specific)
    bool resetScore(const std::string& holder, const std::string& objective) {
        auto it = scores.find(objective);
        if (it == scores.end()) return false;
        return it->second.erase(holder) > 0;
    }
    std::vector<std::string> resetAllScores(const std::string& holder) {
        std::vector<std::string> affected;
        for (auto& [objName, map] : scores)
            if (map.erase(holder)) affected.push_back(objName);
        return affected;
    }
    bool removeObjectiveWithReset(const std::string& name, std::vector<std::string>& outHolders) {
        auto it = scores.find(name);
        if (it != scores.end()) {
            for (auto& [holder, _] : it->second) outHolders.push_back(holder);
            scores.erase(it);
        }
        return removeObjectives(name);
    }

    // -------------------------------------------------------------- packets
    void writeObjectivePacket(WriteBuffer& b, const Objective& o,
                              std::int8_t method) const {
        b.string(o.name);
        b.i8(method);
        if (method == 0 || method == 2) {
            nbt::writeTextComponent(b, o.displayName);
            b.varint(o.type);
            o.numberFormat.write(b);
        }
    }
    void writeDisplayPacket(WriteBuffer& b) const {
        b.varint(displayedSlot >= 0 ? displayedSlot : 0);
        if (displayedSlot < 0) b.string("");     // clear slot
        else b.string(displayedObjective);
    }
    void writeScorePacket(WriteBuffer& b, const std::string& obj,
                          const std::string& holder,
                          std::int32_t value) const {
        b.string(holder);                        // itemName
        b.string(obj);                           // objective name
        b.varint(value);
        b.boolean(false);                        // display name absent (option anonymousNbt)
        NumberFormat nf; nf.has = false;
        nf.write(b);                             // number format absent
    }
    void writeScorePacket(WriteBuffer& b, const std::string& obj,
                          const std::string& holder,
                          std::int32_t value,
                          const NumberFormat* fmt) const {
        b.string(holder);
        b.string(obj);
        b.varint(value);
        b.boolean(false);                        // display name absent
        if (fmt) fmt->write(b);
        else { NumberFormat nf; nf.has = false; nf.write(b); }
    }
    void writeResetScorePacket(WriteBuffer& b, const std::string& holder,
                               const std::string* obj) const {
        b.string(holder);
        b.boolean(obj != nullptr);
        if (obj) b.string(*obj);
    }
};

} // namespace cppfm
