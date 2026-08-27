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

    // -------------------------------------------------------------- teams (plan10 §6, Teams 0x67)
    struct Team {
        std::string name;
        std::string displayName;
        std::uint8_t flags = 0;
        std::string nametagVisibility = "always";
        std::string collisionRule = "always";
        int color = 21; // reset
        std::string prefix;
        std::string suffix;
        std::vector<std::string> members;
    };
    std::unordered_map<std::string, Team> teams;

    Team* findTeam(const std::string& name) {
        auto it = teams.find(name);
        return it == teams.end() ? nullptr : &it->second;
    }
    const Team* findTeam(const std::string& name) const {
        auto it = teams.find(name);
        return it == teams.end() ? nullptr : &it->second;
    }
    bool addTeam(const std::string& name, const std::string& display = "") {
        if (teams.count(name)) return false;
        Team t;
        t.name = name;
        t.displayName = display.empty() ? name : display;
        teams.emplace(name, std::move(t));
        return true;
    }
    bool removeTeam(const std::string& name) {
        return teams.erase(name) > 0;
    }
    bool addTeamMember(const std::string& team, const std::string& member) {
        auto* t = findTeam(team);
        if (!t) return false;
        if (std::find(t->members.begin(), t->members.end(), member) != t->members.end()) return false;
        t->members.push_back(member);
        return true;
    }
    bool removeTeamMember(const std::string& team, const std::string& member) {
        auto* t = findTeam(team);
        if (!t) return false;
        auto it = std::find(t->members.begin(), t->members.end(), member);
        if (it == t->members.end()) return false;
        t->members.erase(it);
        return true;
    }
    // removes member from whatever team they are in (vanilla auto-leave)
    bool leaveTeam(const std::string& member) {
        for (auto& kv : teams) {
            auto& memb = kv.second.members;
            auto it = std::find(memb.begin(), memb.end(), member);
            if (it != memb.end()) { memb.erase(it); return true; }
        }
        return false;
    }
    void writeTeamsPacket(WriteBuffer& b, const Team& t, std::int8_t mode) const {
        b.string(t.name);
        b.i8(mode);
        if (mode == 0 || mode == 2) {
            nbt::writeTextComponent(b, t.displayName);
            b.i8(t.flags);
            b.string(t.nametagVisibility);
            b.string(t.collisionRule);
            b.varint(t.color);
            nbt::writeTextComponent(b, t.prefix);
            nbt::writeTextComponent(b, t.suffix);
            if (mode == 0) {
                b.varint((std::int32_t)t.members.size());
                for (auto& m : t.members) b.string(m);
            }
        } else if (mode == 3 || mode == 4) {
            b.varint((std::int32_t)t.members.size());
            for (auto& m : t.members) b.string(m);
        }
    }
    void writeTeamsCreate(WriteBuffer& b, const Team& t) const {
        writeTeamsPacket(b, t, 0);
    }
    void writeTeamsRemove(WriteBuffer& b, const std::string& teamName) const {
        b.string(teamName);
        b.i8(1);
    }
    void writeTeamsAddPlayers(WriteBuffer& b, const std::string& teamName, const std::vector<std::string>& members) const {
        b.string(teamName);
        b.i8(3);
        b.varint((std::int32_t)members.size());
        for (auto& m : members) b.string(m);
    }
    void writeTeamsRemovePlayers(WriteBuffer& b, const std::string& teamName, const std::vector<std::string>& members) const {
        b.string(teamName);
        b.i8(4);
        b.varint((std::int32_t)members.size());
        for (auto& m : members) b.string(m);
    }
};

} // namespace cppfm
