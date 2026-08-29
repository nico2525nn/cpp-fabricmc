// Teams.hpp: vanilla Teams 0x67 - minimal create/remove/join/leave (plan10 §6).
// Implements subset of 1.21.4 Teams packet needed for smoke80 #79.
// Packet layout verified via minecraft-data 1.21.4 & Yarn Team.java.
// Methods: 0 create, 1 remove, 2 update info, 3 add entities, 4 remove entities.
// For minimal parity we implement create/remove/join/leave + update via method 2.
#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "../core/ByteBuffer.hpp"
#include "../proto/Ids.hpp"
#include "../core/NBT.hpp"

namespace cppfm {

struct Team {
    std::string name;
    std::string displayName;
    std::string prefix;
    std::string suffix;
    int color = 21; // Formatting.RESET 1.21.4 (0-15 dye, 21 reset) — was 15 white
    std::string nameTagVisibility = "always";
    std::string collisionRule = "always";
    uint8_t friendlyFlags = 0; // 0x01 allowFriendlyFire 0x02 seeFriendlyInvisibles
    std::unordered_set<std::string> members;
};

class TeamsManager {
public:
    std::unordered_map<std::string, Team> teams;

    Team* find(const std::string& name) {
        auto it = teams.find(name);
        return it == teams.end() ? nullptr : &it->second;
    }
    bool create(const std::string& name) {
        if (teams.count(name)) return false;
        Team t; t.name = name; t.displayName = name;
        teams.emplace(name, std::move(t));
        return true;
    }
    bool remove(const std::string& name) {
        return teams.erase(name) > 0;
    }
    bool addMember(const std::string& team, const std::string& member) {
        auto* t = find(team); if (!t) return false;
        t->members.insert(member);
        return true;
    }
    bool removeMember(const std::string& team, const std::string& member) {
        auto* t = find(team); if (!t) return false;
        return t->members.erase(member) > 0;
    }
    // Packet builders
    static void writeCreate(WriteBuffer& b, const Team& t) {
        b.string(t.name);
        b.i8(0); // create
        nbt::writeTextComponent(b, t.displayName);
        b.u8(t.friendlyFlags);
        b.string(t.nameTagVisibility);
        b.string(t.collisionRule);
        b.varint(t.color);
        nbt::writeTextComponent(b, t.prefix);
        nbt::writeTextComponent(b, t.suffix);
        b.varint((int32_t)t.members.size());
        for (auto& m : t.members) b.string(m);
    }
    static void writeRemove(WriteBuffer& b, const std::string& name) {
        b.string(name);
        b.i8(1); // remove
    }
    static void writeAddMembers(WriteBuffer& b, const std::string& name, const std::vector<std::string>& members) {
        b.string(name);
        b.i8(3); // add entities
        b.varint((int32_t)members.size());
        for (auto& m : members) b.string(m);
    }
    static void writeRemoveMembers(WriteBuffer& b, const std::string& name, const std::vector<std::string>& members) {
        b.string(name);
        b.i8(4);
        b.varint((int32_t)members.size());
        for (auto& m : members) b.string(m);
    }
    static void writeUpdate(WriteBuffer& b, const Team& t) {
        b.string(t.name);
        b.i8(2); // update info
        nbt::writeTextComponent(b, t.displayName);
        b.u8(t.friendlyFlags);
        b.string(t.nameTagVisibility);
        b.string(t.collisionRule);
        b.varint(t.color);
        nbt::writeTextComponent(b, t.prefix);
        nbt::writeTextComponent(b, t.suffix);
    }
};

} // namespace cppfm
