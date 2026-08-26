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
#include "../proto/Ids.hpp"

namespace cppfm {

class Scoreboard {
public:
    struct Objective {
        std::string name;
        std::string criteria = "dummy";
        std::string displayName;                 // legacy text (also NBT sent)
        std::uint8_t type = 0;                   // 0 integer 1 hearts
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
        objectives.push_back({name, criteria,
                              display.empty() ? name : display, 0});
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

    // -------------------------------------------------------------- packets
    void writeObjectivePacket(WriteBuffer& b, const Objective& o,
                              std::int8_t method) const {
        b.string(o.name);
        b.i8(method);
        if (method == 0 || method == 2) {
            nbt::writeTextComponent(b, o.displayName);
            b.varint(o.type);
            b.boolean(false);                    // no number format
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
        b.boolean(false);                        // display name absent
        b.boolean(false);                        // number format absent
    }
    void writeResetScorePacket(WriteBuffer& b, const std::string& holder,
                               const std::string* obj) const {
        b.string(holder);
        b.boolean(obj != nullptr);
        if (obj) b.string(*obj);
    }
};

} // namespace cppfm
