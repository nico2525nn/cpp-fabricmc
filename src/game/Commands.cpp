// Commands.cpp: Brigadier command tree + selector resolution (plan3.md
// "Brigadier完全移植"). All commands are registered on a real CommandNode
// tree, parsed by the dispatcher and advertised via declare_commands.
#include "GameServer.hpp"
#include "Messages.hpp"
#include "Particles.hpp"
#include "../generated/EntityIds.hpp"
#include "../generated/BlockStates.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <filesystem>
#include <unordered_set>
#include <fstream>

namespace cppfm {

using brigadier::CommandNode;
using brigadier::CommandContext;
namespace args = brigadier::args;

// plan38 B-13: helper to parse inline NBT {k:v,...} into map<string,string> without suffixes

// ---------------------------------------------------------------- selectors

brigadier::SelectorResult GameServer::resolveSelector(
    const std::string& raw, Player* source) {
    brigadier::SelectorResult out;
    if (raw.empty()) return out;
    if (raw[0] != '@') {
        out.playersOnly = true;
        out.playerNames.push_back(raw);
        return out;
    }
    const char kind = raw.size() > 1 ? raw[1] : 'a';
    std::unordered_map<std::string, std::string> kv;
    const auto bracket = raw.find('[');
    if (bracket != std::string::npos) {
        std::string body = raw.substr(bracket + 1,
                                      raw.find(']') - bracket - 1);
        size_t pos = 0;
        while (pos < body.size()) {
            const auto eq = body.find('=', pos);
            if (eq == std::string::npos) break;
            auto comma = body.find(',', eq);
            if (comma == std::string::npos) comma = body.size();
            kv[body.substr(pos, eq - pos)] = body.substr(eq + 1, comma - eq - 1);
            pos = comma + 1;
        }
    }

    struct Cand { double dist; Player* p; };
    std::vector<Cand> players;
    for (auto& pl : playersSnapshot()) {
        if (!pl->inPlay || pl->dead) continue;
        if (!source || pl.get() != source)
            players.push_back({std::pow(pl->x - (source ? source->x : 0), 2) +
                               std::pow(pl->z - (source ? source->z : 0), 2),
                               pl.get()});
        else players.push_back({0.0, pl.get()});
    }

    switch (kind) {
    case 'a':
        for (auto& c : players) out.playerNames.push_back(c.p->name);
        break;
    case 's':
        if (source && source->inPlay && !source->dead)
            out.playerNames.push_back(source->name);
        break;
    case 'p': {
        if (players.empty()) break;
        auto best = *std::min_element(players.begin(), players.end(),
            [](auto& a, auto& b) { return a.dist < b.dist; });
        out.playerNames.push_back(best.p->name);
        break;
    }
    case 'r': {
        if (players.empty()) break;
        out.playerNames.push_back(players[rand() % players.size()].p->name);
        break;
    }
    case 'e': {
        // entities (mobs); optional type= filter
        const auto typeIt = kv.find("type");
        const int limit = kv.count("limit") ? std::max(1, [&]{
            try { return std::stoi(kv["limit"]); } catch (...) { return 1; }}()) : 0;
        std::lock_guard lk(const_cast<std::mutex&>(entsMtx_));
        for (const auto& m : mobs_) {
            if (typeIt != kv.end()) {
                const std::string want =
                    typeIt->second.find(':') == std::string::npos
                        ? "minecraft:" + typeIt->second
                        : typeIt->second;
                if (MobEntity::kindName(m->kind) != want) continue;
            }
            out.entityIds.push_back(m->entityId);
            if (limit > 0 &&
                static_cast<int>(out.entityIds.size()) >= limit) break;
        }
        break;
    }
    default: break;
    }
    return out;
}

// -------------------------------------------------------------- helpers ----



// ------------------------------------------------------------ registration --

// Recipe-book UpdateRecipes SlotDisplay writer: varint presence (2 = item)
// + item id. Single truth (was a lambda inside the recipe block, used 10x).

void GameServer::initCommands() {
    // Dispatcher (cleanup P3): department registration lives in
    // commands_{chat,admin,world,player,data,misc,execute,scoreboard}.cpp
    // (was one 6076-line function).
    initChatCommands();
    initAdminCommands();
    initWorldCommands();
    initPlayerCommands();
    initDataCommands();
    initMiscCommands();
    initExecuteCommands();
    initScoreboardCommands();
}
} // namespace cppfm
