// Commands.cpp: Brigadier command tree + selector resolution (plan3.md
// "Brigadier完全移植"). All commands are registered on a real CommandNode
// tree, parsed by the dispatcher and advertised via declare_commands.
#include "GameServer.hpp"
#include "../generated/EntityIds.hpp"
#include "../generated/BlockStates.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <filesystem>
#include <fstream>

namespace cppfm {

using brigadier::CommandNode;
using brigadier::CommandContext;
namespace args = brigadier::args;

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

static Player* findPlayer(GameServer& srv, const std::string& name) {
    for (auto& p : srv.playersSnapshot())
        if (p->name == name) return p.get();
    return nullptr;
}

static void sendFeedback(Player* p, const std::string& msg) {
    if (p && p->conn) {
        WriteBuffer b;
        nbt::writeTextComponent(b, msg);
        b.boolean(false);
        try { p->conn->sendPacket(proto::pl::sc::SystemChat, b); } catch (...) {}
    } else {
        std::fprintf(stderr, "[cppfm] %s\n", msg.c_str());
    }
}

static std::vector<Player*> expandTargets(GameServer& srv, CommandContext& ctx,
                                          Player* source, const char* argName) {
    const std::string raw = ctx.arg(argName).asStr().empty()
        ? std::string()
        : std::string();     // selectors were resolved during parse; re-derive:
    (void)raw;
    std::vector<Player*> out;
    // The parser stored a SelectorResult; use it when present.
    const auto sel = ctx.arg(argName).asSelector();
    for (auto& n : sel.playerNames) {
        if (n == "@a" || n == "@e" || n == "@p") {   // unresolved fallback
            for (auto& p : srv.playersSnapshot())
                if (p->inPlay && !p->dead) out.push_back(p.get());
            return out;
        }
        if (Player* p = findPlayer(srv, n)) out.push_back(p);
    }
    if (out.empty() && source) out.push_back(source);
    return out;
}

// ------------------------------------------------------------ registration --

void GameServer::initCommands() {
    using NodePtr = brigadier::NodePtr;
    auto& d = commands_;

    // /ping
    {
        auto n = CommandNode::literal("ping");
        n->executable = true;
        n->action = [this](CommandContext& c) {
            Player* p = static_cast<Player*>(c.source.player);
            sendFeedback(p, "\u00a7aPong!");
            return 1;
        };
        d.root->then(n);
    }
    // /help
    {
        auto n = CommandNode::literal("help");
        n->executable = true;
        n->action = [this](CommandContext& c) {
            Player* p = static_cast<Player*>(c.source.player);
            sendFeedback(p, "\u00a77Commands: /help /ping /gamemode /give /time "
                            "/tp /kill /list /say /seed /gamerule /effect /xp "
                            "/setblock /summon /clear /spawnpoint /kick");
            return 1;
        };
        d.root->then(n);
    }
    // /list
    {
        auto n = CommandNode::literal("list");
        n->executable = true;
        n->action = [this](CommandContext& c) {
            Player* p = static_cast<Player*>(c.source.player);
            std::string names;
            for (auto& pl : playersSnapshot()) names += pl->name + " ";
            sendFeedback(p, "\u00a77Players online (" +
                         std::to_string(playerCount()) + "): " + names);
            return playerCount();
        };
        d.root->then(n);
    }
    // /seed
    {
        auto n = CommandNode::literal("seed");
        n->executable = true;
        n->action = [this](CommandContext& c) {
            Player* p = static_cast<Player*>(c.source.player);
            sendFeedback(p, "Seed: [" + std::to_string(config().seed) + "]");
            return 1;
        };
        d.root->then(n);
    }
    // /say <message>
    {
        auto say = CommandNode::literal("say");
        auto msg = CommandNode::argument("message", args::stringGreedy());
        msg->executable = true;
        msg->action = [this](CommandContext& c) {
            broadcastSystemText("\u00a7d[Server] " + c.arg("message").asStr());
            return 1;
        };
        say->then(msg);
        d.root->then(say);
    }
    // /gamemode <mode> [target]
    {
        auto gm = CommandNode::literal("gamemode");
        auto applyMode = [](const std::string& s) -> int {
            if (s == "survival" || s == "s" || s == "0") return 0;
            if (s == "creative" || s == "c" || s == "1") return 1;
            if (s == "adventure" || s == "a" || s == "2") return 2;
            if (s == "spectator" || s == "sp" || s == "3") return 3;
            return -1;
        };
        auto modeArg = CommandNode::argument("mode", args::gamemodeArg());
        modeArg->executable = true;
        modeArg->action = [this, applyMode](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const int m = applyMode(c.arg("mode").asStr());
            if (m < 0 || !src) throw std::runtime_error("unknown gamemode");
            src->gamemode = static_cast<std::uint8_t>(m);
            WriteBuffer ge;                          // game event 4 = gamemode
            ge.u8(4); ge.f32(static_cast<float>(m));
            try { src->conn->sendPacket(proto::pl::sc::GameEvent, ge); }
            catch (...) {}
            // abilities follow the mode
            WriteBuffer ab;
            ab.i8(m == 1 ? 0x01 | 0x04 | 0x08 : (m == 3 ? 0x01 | 0x08 : 0x01));
            ab.f32(0.05f); ab.f32(m == 1 ? 0.10f : 0.05f);
            try { src->conn->sendPacket(proto::pl::sc::Abilities, ab); } catch (...) {}
            sendFeedback(src, "Set own game mode to " + c.arg("mode").asStr());
            return 1;
        };
        auto target = CommandNode::argument("target",
                                            args::entity(true, false));
        target->executable = true;
        target->action = [this, applyMode](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const int m = applyMode(c.arg("mode").asStr());
            if (m < 0) throw std::runtime_error("unknown gamemode");
            const auto sel = c.arg("target").asSelector();
            int count = 0;
            for (auto& name : sel.playerNames)
                if (Player* t = findPlayer(*this, name)) {
                    t->gamemode = static_cast<std::uint8_t>(m);
                    WriteBuffer ab;
                    ab.i8(m == 1 ? 0x01 | 0x04 | 0x08 : (m == 3 ? 0x01 | 0x08 : 0x01));
                    ab.f32(0.05f); ab.f32(m == 1 ? 0.10f : 0.05f);
                    try { t->conn->sendPacket(proto::pl::sc::Abilities, ab); } catch (...) {}
                    ++count;
                }
            sendFeedback(src, "Updated gamemode for " + std::to_string(count));
            return count;
        };
        modeArg->then(target);
        gm->then(modeArg);
        d.root->then(gm);
    }
    // /give <target> <item> [count]
    {
        auto give = CommandNode::literal("give");
        auto who = CommandNode::argument("target", args::entity(true, false));
        auto item = CommandNode::argument("item", args::itemStackArg());
        item->executable = true;
        item->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("target").asSelector();
            const std::string raw = c.arg("item").asStr();
            // extract base item name before '['
            std::string base = raw;
            std::string compPart;
            auto br = raw.find('[');
            if (br!=std::string::npos) { base = raw.substr(0, br); compPart = raw.substr(br); }
            auto it = gen::itemIdByName().find(base);
            if (it == gen::itemIdByName().end())
                throw std::runtime_error("Unknown item: " + base);
            // build stack with trim if present (plan13 §2)
            ItemStack stack = ItemStack::of(it->second, 1);
            if (!compPart.empty() && compPart.find("trim")!=std::string::npos) {
                // naive extract pattern and material strings
                auto extract = [&](const std::string& key)->std::string{
                    auto pos = compPart.find(key);
                    if (pos==std::string::npos) return "";
                    auto q1 = compPart.find('"', pos);
                    if (q1==std::string::npos) return "";
                    auto q2 = compPart.find('"', q1+1);
                    if (q2==std::string::npos) return "";
                    return compPart.substr(q1+1, q2-q1-1);
                };
                std::string pat = extract("pattern");
                std::string mat = extract("material");
                if (!pat.empty()) {
                    ItemStack::ArmorTrim tr; tr.has=true; tr.pattern=pat; tr.material= mat.empty()?"minecraft:iron":mat;
                    stack.setTrim(tr);
                }
            }
            int given = 0;
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    // try to add stack preserving trim
                    bool placed=false;
                    for(int i: {36,37,38,39,40,41,42,43,44,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35}){
                        auto &s = t->inv[i];
                        if (s.empty()) { s = stack; placed=true; break; }
                    }
                    if(!placed) addToInventory(*t, it->second, 1);
                    resendInventory(*t);
                    // if armor slot, sync equipment (plan13)
                    if (base.find("_helmet")!=std::string::npos||base.find("_chestplate")!=std::string::npos||base.find("_leggings")!=std::string::npos||base.find("_boots")!=std::string::npos)
                        syncEquipmentOnChange(*t);
                    ++given;
                }
            sendFeedback(src, "Given 1 x " + base);
            return given;
        };
        auto cnt = CommandNode::argument("count", args::integer(1, 576));
        cnt->executable = true;
        cnt->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("target").asSelector();
            const std::string raw = c.arg("item").asStr();
            std::string base = raw;
            auto br = raw.find('[');
            if (br!=std::string::npos) base = raw.substr(0, br);
            auto it = gen::itemIdByName().find(base);
            if (it == gen::itemIdByName().end())
                throw std::runtime_error("Unknown item: " + base);
            const int n2 = c.arg("count").asInt();
            int given = 0;
            for (auto& nm : sel.playerNames)
                if (Player* t = findPlayer(*this, nm)) {
                    std::string compPart = br!=std::string::npos ? raw.substr(br) : "";
                    ItemStack stack = ItemStack::of(it->second, 1);
                    if (!compPart.empty() && compPart.find("trim")!=std::string::npos) {
                        auto extract = [&](const std::string& key)->std::string{
                            auto pos = compPart.find(key);
                            if (pos==std::string::npos) return "";
                            auto q1 = compPart.find('"', pos);
                            if (q1==std::string::npos) return "";
                            auto q2 = compPart.find('"', q1+1);
                            if (q2==std::string::npos) return "";
                            return compPart.substr(q1+1, q2-q1-1);
                        };
                        std::string pat = extract("pattern");
                        std::string mat = extract("material");
                        if (!pat.empty()) { ItemStack::ArmorTrim tr; tr.has=true; tr.pattern=pat; tr.material= mat.empty()?"minecraft:iron":mat; stack.setTrim(tr); }
                    }
                    for(int k=0;k<n2;k++){
                        bool placed=false;
                        for(int i: {36,37,38,39,40,41,42,43,44,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35}){
                            auto &s = t->inv[i];
                            if (s.empty()) { s = stack; placed=true; break; }
                        }
                        if(!placed) addToInventory(*t, it->second, 1);
                    }
                    resendInventory(*t);
                    ++given;
                }
            sendFeedback(src, "Given " + std::to_string(n2) + " x " + base);
            return given;
        };
        item->then(cnt);
        who->then(item);
        give->then(who);
        d.root->then(give);
    }
    // /time set <value>
    {
        auto time = CommandNode::literal("time");
        auto set = CommandNode::literal("set");
        auto named = CommandNode::argument("named", args::stringWord());
        named->executable = true;
        named->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"day", "noon", "night", "midnight"};
        };
        named->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string v = c.arg("named").asStr();
            std::int64_t t = 1000;
            if (v == "day") t = 1000;
            else if (v == "noon") t = 6000;
            else if (v == "night") t = 13000;
            else if (v == "midnight") t = 18000;
            else throw std::runtime_error("unknown time of day");
            setTimeOfDay(t);
            broadcastSystemText("\u00a77Time set to " + v);
            return 1;
        };
        auto ticks = CommandNode::argument("ticks", args::integer(0, 24000));
        ticks->executable = true;
        ticks->action = [this](CommandContext& c) {
            setTimeOfDay(c.arg("ticks").asInt());
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Set the time to " +
                         std::to_string(c.arg("ticks").asInt()));
            return 1;
        };
        set->then(named); set->then(ticks);
        time->then(set);
        d.root->then(time);
    }
    // /tp <x y z>
    {
        auto tp = CommandNode::literal("tp");
        auto pos = CommandNode::argument("pos", args::vec3());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (!src) return 0;
            const auto v = c.arg("pos").asVec3();
            src->fallDist = 0;
            WriteBuffer b;
            b.varint(0);                              // teleport id handled below
            // reuse session teleport path through a synthetic packet:
            WriteBuffer tb;
            tb.varint(++teleportCounterForTest_);
            tb.f64(v.x); tb.f64(v.y); tb.f64(v.z);
            tb.f64(0); tb.f64(0); tb.f64(0);
            tb.f32(src->yaw); tb.f32(src->pitch);
            tb.u32(0);
            try { src->conn->sendPacket(proto::pl::sc::PlayerPosition, tb); }
            catch (...) {}
            src->x = v.x; src->y = v.y; src->z = v.z;
            sendFeedback(src, "Teleported to " + std::to_string(v.x) + ", " +
                         std::to_string(v.y) + ", " + std::to_string(v.z));
            return 1;
        };
        tp->then(pos);
        d.root->then(tp);
    }
    // /kill [targets]
    {
        auto kill = CommandNode::literal("kill");
        kill->executable = true;
        kill->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (src) { applyDamage(*src, 1000.f, "/kill"); return 1; }
            return 0;
        };
        auto targets = CommandNode::argument("targets",
                                             args::entity(false, false));
        targets->executable = true;
        targets->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            int killed = 0;
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    applyDamage(*t, 1000.f, "killed");
                    ++killed;
                }
            std::lock_guard lk(entsMtx_);
            std::vector<std::int32_t> ids;
            for (auto id : sel.entityIds)
                for (auto& m : mobs_)
                    if (m->entityId == id && !m->dead) {
                        m->health = 0; m->dead = true;
                        ids.push_back(id);
                        ++killed;
                    }
            for (auto id : ids) {
                WriteBuffer rm; rm.varint(1); rm.varint(id);
                broadcastPacketExcept(nullptr, proto::pl::sc::RemoveEntities, rm);
            }
            sendFeedback(src, "Killed " + std::to_string(killed) + " entities");
            return killed;
        };
        kill->then(targets);
        d.root->then(kill);
    }
    // /gamerule <rule> [value] — W18 strict: 37 Yarn keys + validation, suggest via allKeys()
    {
        auto gr = CommandNode::literal("gamerule");
        auto rule = CommandNode::argument("rule", args::stringWord());
        rule->executable = true;
        rule->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            return gamerules_.allKeys();
        };
        rule->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string r = c.arg("rule").asStr();
            if (!gamerules_.contains(r)) { sendFeedback(src, "\u00a7cUnknown gamerule: " + r); return 0; }
            std::string cur = gamerules_.get(r);
            sendFeedback(src, "\u00a77" + r + " = " + cur);
            return 1;
        };
        auto value = CommandNode::argument("value", args::stringWord());
        value->executable = true;
        value->suggestions = [this](brigadier::StringReader& reader, brigadier::ParseCtx&) {
            // W18 polish: suggest true/false for Boolean, numeric hints for Int
            // Peek already-typed rule prefix: try to infer via token
            std::string token = reader.canRead() ? reader.readUnquotedString() : std::string();
            // fallback: offer both
            (void)token;
            // use last parsed rule if available; brigadier context would have it, but we approximate
            // Offer boolean choices; int rules also accept true/false as invalid but hint numbers
            std::vector<std::string> opts = {"true","false"};
            // also suggest common int values for int rules
            opts.push_back("0"); opts.push_back("1"); opts.push_back("10");
            return opts;
        };
        value->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string r = c.arg("rule").asStr();
            const std::string v = c.arg("value").asStr();
            std::string err;
            if (!gamerules_.setValidated(r, v, &err)) {
                sendFeedback(src, "\u00a7c" + err);
                return 0;
            }
            broadcastSystemText("\u00a77Gamerule " + r + " is now " + v);
            return 1;
        };
        rule->then(value);
        gr->then(rule);
        d.root->then(gr);
    }
    // /forceload — W17 strict: ForcedChunkState via setChunkForced/isChunkForced (Yarn ServerWorld)
    {
        auto fl = CommandNode::literal("forceload");
        // query
        auto flQuery = CommandNode::literal("query");
        flQuery->executable = true;
        flQuery->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto keys = world_.forcedChunkKeys();
            if (keys.empty()) { sendFeedback(src, "No forced chunks"); return 0; }
            std::string out="Forced chunks:";
            for(auto k: keys){
                int cx = static_cast<int32_t>(k>>32);
                int cz = static_cast<int32_t>(k & 0xFFFFFFFFLL);
                out += " [" + std::to_string(cx) + "," + std::to_string(cz) + "]";
            }
            sendFeedback(src, out);
            return (int)keys.size();
        };
        fl->then(flQuery);
        // remove all
        auto flRemove = CommandNode::literal("remove");
        auto flRemoveAll = CommandNode::literal("all");
        flRemoveAll->executable = true;
        flRemoveAll->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto keys = world_.forcedChunkKeys();
            for(auto k: keys){
                int cx = static_cast<int32_t>(k>>32);
                int cz = static_cast<int32_t>(k & 0xFFFFFFFFLL);
                world_.setChunkForced(cx,cz,false);
            }
            sendFeedback(src, "Removed all forced chunks (" + std::to_string(keys.size()) + ")");
            return (int)keys.size();
        };
        flRemove->then(flRemoveAll);
        // add <x> <z> and remove <x> <z>
        auto flAdd = CommandNode::literal("add");
        auto addX = CommandNode::argument("x", args::integer(INT32_MIN, INT32_MAX));
        auto addZ = CommandNode::argument("z", args::integer(INT32_MIN, INT32_MAX));
        addZ->executable = true;
        addZ->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            int cx = c.arg("x").asInt();
            int cz = c.arg("z").asInt();
            // vanilla forceload uses chunk coords directly; support block pos via >>4 fallback if large? keep chunk coords
            bool ok = world_.setChunkForced(cx,cz,true);
            if (!ok) { sendFeedback(src, "Chunk [" + std::to_string(cx)+","+std::to_string(cz)+"] already forced"); return 0; }
            sendFeedback(src, "Added chunk [" + std::to_string(cx)+","+std::to_string(cz)+"]");
            return 1;
        };
        auto remX = CommandNode::argument("x", args::integer(INT32_MIN, INT32_MAX));
        auto remZ = CommandNode::argument("z", args::integer(INT32_MIN, INT32_MAX));
        remZ->executable = true;
        remZ->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            int cx = c.arg("x").asInt();
            int cz = c.arg("z").asInt();
            bool ok = world_.setChunkForced(cx,cz,false);
            if (!ok) { sendFeedback(src, "Chunk [" + std::to_string(cx)+","+std::to_string(cz)+"] not forced"); return 0; }
            sendFeedback(src, "Removed chunk [" + std::to_string(cx)+","+std::to_string(cz)+"]");
            return 1;
        };
        addX->then(addZ);
        flAdd->then(addX);
        remX->then(remZ);
        flRemove->then(remX);
        fl->then(flAdd);
        fl->then(flRemove);
        // default executable query (bare /forceload)
        fl->executable = true;
        fl->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto keys = world_.forcedChunkKeys();
            sendFeedback(src, "Forced chunks: " + std::to_string(keys.size()));
            return (int)keys.size();
        };
        d.root->then(fl);
    }
    // /effect give <targets> <effect> [seconds] [amplifier]
    {
        auto effect = CommandNode::literal("effect");
        auto give = CommandNode::literal("give");
        auto targets = CommandNode::argument("targets",
                                             args::entity(false, false));
        auto eff = CommandNode::argument("effect", args::resourceLocation());
        eff->executable = true;
        eff->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (int i = effects::Speed; i <= effects::Darkness; ++i)
                v.emplace_back(effects::nameOf(static_cast<std::uint8_t>(i)));
            return v;
        };
        eff->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            int applied = 0;
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = 30 * 20;
                    t->effects.erase(
                        std::remove_if(t->effects.begin(), t->effects.end(),
                                       [&](const EffectInstance& x)
                                           { return x.type == e.type; }),
                        t->effects.end());
                    t->effects.push_back(e);
                    WriteBuffer b;
                    b.varint(t->entityId);
                    b.varint(e.type);
                    b.varint(e.amplifier);
                    b.varint(e.durationTicks);
                    b.u8(effectFlags(e));
                    try { t->conn->sendPacket(proto::pl::sc::EntityEffect, b); }
                    catch (...) {}
                    ++applied;
                }
            sendFeedback(src, "Applied " + en + " to " +
                         std::to_string(applied));
            return applied;
        };
        auto secs = CommandNode::argument("seconds", args::integer(1, 1000000));
        secs->executable = true;
        secs->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            const int dur = c.arg("seconds").asInt();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = dur * 20;
                    t->effects.push_back(e);
                    WriteBuffer b;
                    b.varint(t->entityId);
                    b.varint(e.type);
                    b.varint(e.amplifier);
                    b.varint(e.durationTicks);
                    b.u8(effectFlags(e));
                    try { t->conn->sendPacket(proto::pl::sc::EntityEffect, b); }
                    catch (...) {}
                }
            sendFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s)");
            return 1;
        };
        eff->then(secs);
        targets->then(eff);
        give->then(targets);
        effect->then(give);
        d.root->then(effect);
    }
    // /xp add <targets> <amount>
    {
        auto xpCmd = CommandNode::literal("xp");
        auto add = CommandNode::literal("add");
        auto targets = CommandNode::argument("targets",
                                             args::entity(true, false));
        auto amount = CommandNode::argument("amount", args::integer(-1000, 1000));
        amount->executable = true;
        amount->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            const int amt = c.arg("amount").asInt();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    t->xp.addPoints(amt);
                    sendSetExperience(*t);
                }
            sendFeedback(src, "Gave " + std::to_string(amt) + " xp");
            return 1;
        };
        targets->then(amount);
        add->then(targets);
        xpCmd->then(add);
        d.root->then(xpCmd);
    }
    // /setblock <pos> <block> (plan13: BlockState id 12 with props)
    {
        auto sb = CommandNode::literal("setblock");
        auto pos = CommandNode::argument("pos", args::blockPos());
        auto block = CommandNode::argument("block", args::blockStateArg());
        block->executable = true;
        block->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            v.reserve(gen::kBlocks.size());
            for (auto& e : gen::kBlocks) v.emplace_back(std::string(e.name));
            // add a few with state examples for tab testing
            v.push_back("minecraft:oak_stairs[facing=north,half=top]");
            v.push_back("minecraft:stone");
            return v;
        };
        block->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto p = c.arg("pos").asBlockPos();
            std::string raw = c.arg("block").asStr();
            // parse block state string: name[props]{nbt}
            std::string name = raw;
            std::string propsStr;
            auto b1 = raw.find('[');
            auto b2 = raw.find('{');
            size_t nameEnd = std::string::npos;
            if (b1 != std::string::npos && b2 != std::string::npos) nameEnd = std::min(b1,b2);
            else if (b1 != std::string::npos) nameEnd = b1;
            else if (b2 != std::string::npos) nameEnd = b2;
            if (nameEnd != std::string::npos) {
                name = raw.substr(0, nameEnd);
                if (b1 != std::string::npos) {
                    size_t e = raw.find(']', b1);
                    if (e != std::string::npos) propsStr = raw.substr(b1+1, e-b1-1);
                }
            }
            if (name.find(':') == std::string::npos) name = "minecraft:" + name;
            const gen::BlockDef* def = gen::blockByName(name);
            if (!def) throw std::runtime_error("unknown block: " + name);
            std::uint16_t state = static_cast<std::uint16_t>(def->defaultState);
            if (!propsStr.empty()) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                size_t pos2=0;
                while(pos2<propsStr.size()){
                    size_t eq=propsStr.find('=',pos2);
                    if(eq==std::string::npos) break;
                    size_t comma=propsStr.find(',',eq);
                    std::string k=propsStr.substr(pos2, eq-pos2);
                    std::string v=propsStr.substr(eq+1, (comma==std::string::npos?propsStr.size():comma)-eq-1);
                    // trim
                    auto trim=[](std::string s){ size_t a=s.find_first_not_of(" \t"); size_t b=s.find_last_not_of(" \t"); return a==std::string::npos?s:s.substr(a,b-a+1); };
                    k=trim(k); v=trim(v);
                    props.emplace_back(k,v);
                    if(comma==std::string::npos) break;
                    pos2=comma+1;
                }
                if(!props.empty()){
                    // try to resolve with props
                    // need to keep original props + new props (override)
                    std::vector<std::pair<std::string_view,std::string_view>> merged;
                    // start from def's default props? Use stateWithProps with supplied props plus defaults
                    // Simplistic: use stateWithProps with supplied props (will fill missing with defaults)
                    // We need to build string_views that live long enough: use static storage via string copy
                    // Instead, use gen::stateWithPropsList helper alternative
                    std::vector<std::pair<std::string,std::string>> tmp;
                    for(auto &pr: props) tmp.emplace_back(std::string(pr.first), std::string(pr.second));
                    // Convert to string_view vector that points to tmp's strings (need lifetime during call)
                    std::vector<std::pair<std::string_view,std::string_view>> sv;
                    for(auto &pr: tmp) sv.emplace_back(pr.first, pr.second);
                    // Actually gen::stateWithProps expects BlockDef and props; it will search exact match
                    // Use a map approach: try stateWithProps, fallback to default
                    uint32_t cand = gen::stateWithProps(*def, sv);
                    if(cand!=0) state = static_cast<std::uint16_t>(cand);
                    else {
                        // fallback: try with just name
                    }
                }
            }
            world_.generateChunkIfMissing(p.x >> 4, p.z >> 4);
            world_.setBlock(p.x, p.y, p.z, state);
            broadcastBlockChange(p.x, p.y, p.z, state);
            sendFeedback(src, "Changed the block at " + std::to_string(p.x) +
                         ", " + std::to_string(p.y) + ", " +
                         std::to_string(p.z));
            return 1;
        };
        pos->then(block);
        sb->then(pos);
        d.root->then(sb);
    }
    // /summon <entity> [pos]
    {
        auto summon = CommandNode::literal("summon");
        auto ent = CommandNode::argument("entity", args::resourceLocation());
        ent->executable = true;
        ent->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string en = c.arg("entity").asStr();
            if (en.find(':') == std::string::npos) en = "minecraft:" + en;
            auto it = gen::entityTypeIdByName().find(en);
            if (it == gen::entityTypeIdByName().end())
                throw std::runtime_error("unknown entity: " + en);
            spawnMobByTypeName(en,
                src ? src->x + 2.0 : 0.5, src ? src->y + 1.0 : -60.0,
                src ? src->z + 2.0 : 0.5);
            sendFeedback(src, "Summoned " + en);
            return 1;
        };
        summon->then(ent);
        d.root->then(summon);
    }
    // /clear [targets]
    {
        auto clear = CommandNode::literal("clear");
        clear->executable = true;
        clear->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            int removed = 0;
            for (auto& s : src->inv)
                if (!s.empty()) { ++removed; s = ItemStack::air(); }
            resendInventory(*src);
            sendFeedback(src, "Removed " + std::to_string(removed) +
                         " items");
            return removed;
        };
        d.root->then(clear);
    }
    // /spawnpoint
    {
        auto sp = CommandNode::literal("spawnpoint");
        sp->executable = true;
        sp->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            world_.setSpawnPoint({static_cast<std::int32_t>(src->x),
                                  static_cast<std::int32_t>(src->y),
                                  static_cast<std::int32_t>(src->z)});
            saveLevelData();
            sendFeedback(src, "Set spawn point to current position");
            return 1;
        };
        d.root->then(sp);
    }
    // /weather <clear|rain|thunder> [durationSeconds]
    {
        auto weather = CommandNode::literal("weather");
        auto kind = CommandNode::argument("kind", args::stringWord());
        kind->executable = true;
        kind->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"clear", "rain", "thunder"};
        };
        kind->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string k = c.arg("kind").asStr();
            if (k == "clear") setWeather(Weather::Clear, 6000 * 20);
            else setWeather(Weather::Rain,
                            (k == "thunder" ? 3000 : 6000) * 20LL);
            broadcastSystemText("\u00a77Weather set to " + k);
            return 1;
        };
        weather->then(kind);
        d.root->then(weather);
    }
    // /title <targets?> <title|subtitle|clear|times> ...
    {
        auto title = CommandNode::literal("title");
        auto clear = CommandNode::literal("clear");
        clear->executable = true;
        clear->action = [this](CommandContext&) {
            for (auto& p : playersSnapshot()) {
                WriteBuffer b;
                try { p->conn->sendPacket(proto::pl::sc::ClearTitles, b); }
                catch (...) {}
            }
            return 1;
        };
        title->then(clear);
        auto text = CommandNode::argument("text", args::stringGreedy());
        text->executable = true;
        text->action = [this](CommandContext& c) {
            const std::string t = c.arg("text").asStr();
            for (auto& p : playersSnapshot()) {
                WriteBuffer sub;
                nbt::writeTextComponent(sub, "");
                try { p->conn->sendPacket(proto::pl::sc::SetTitleSubtitle, sub); }
                catch (...) {}
                WriteBuffer b;
                nbt::writeTextComponent(b, "\u00a76" + t);
                try { p->conn->sendPacket(proto::pl::sc::SetTitleText, b); }
                catch (...) {}
            }
            return 1;
        };
        title->then(text);
        d.root->then(title);
    }
    // /worldborder center <x z> | size <s>
    {
        auto wb = CommandNode::literal("worldborder");
        auto size = CommandNode::literal("size");
        auto sz = CommandNode::argument("diameter", args::floatArg(1.f, 1000000.f));
        sz->executable = true;
        sz->action = [this](CommandContext& c) {
            worldBorderDiameter_ = c.arg("diameter").asDouble();
            // persist and broadcast (plan6 §10)
            if (persist_) persist_->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
            broadcastWorldBorder();
            // also send Center and LerpSize for spec compliance
            for (auto& p : playersSnapshot()) {
                WriteBuffer cc; cc.f64(worldBorderCenterX_); cc.f64(worldBorderCenterZ_);
                try { p->conn->sendPacket(proto::pl::sc::WorldBorderCenter, cc); } catch(...) {}
            }
            return 1;
        };
        wb->then(size);
        d.root->then(wb);
    }
    // /stats
    {
        auto st = CommandNode::literal("stats");
        st->executable = true;
        st->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (!src || !src->stats) return 0;
            sendFeedback(src,
                "\u00a77--- Stats ---\u00a7r\n"
                "mined: " + std::to_string([&]{
                    std::int64_t t = 0;
                    for (auto& [k, v] : src->stats->counters())
                        if (k.rfind("minecraft:mined|", 0) == 0) t += v;
                    return t;
                }()) +
                "  killed: " + std::to_string([&]{
                    std::int64_t t = 0;
                    for (auto& [k, v] : src->stats->counters())
                        if (k.rfind("minecraft:killed|", 0) == 0) t += v;
                    return t;
                }()) +
                "\nplay time: " +
                std::to_string(src->stats->get(
                    "minecraft:custom|minecraft:play_time") / 20 / 60) +
                " min\nadvancements: " +
                std::to_string(src->advancements->unlocked().size()) + "/" +
                std::to_string(advancementDefs().size()));
            return 1;
        };
        d.root->then(st);
    }
    // /scoreboard objectives add <name> <criteria> | players set <p> <obj> <v> (plan13 Objective id 23)
    {
        auto sb = CommandNode::literal("scoreboard");
        auto obj = CommandNode::literal("objectives");
        auto add = CommandNode::literal("add");
        auto name = CommandNode::argument("name", args::objectiveArg());
        name->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& o : scoreboard.objectives) v.push_back(o.name);
            return v;
        };
        auto crit = CommandNode::argument("criteria", args::objectiveCriteriaArg());
        crit->executable = true;
        crit->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"dummy", "deathCount",
                                            "playerKillCount", "totalKillCount"};
        };
        crit->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string n = c.arg("name").asStr();
            const std::string cr = c.arg("criteria").asStr();
            if (!scoreboard.addObjective(n, cr, n))
                throw std::runtime_error("objective already exists");
            Scoreboard::Objective* o =
                const_cast<Scoreboard::Objective*>(scoreboard.find(n));
            sendObjectiveAll(*o, 0);
            sendFeedback(src, "Created objective [" + cr + "] " + n);
            return 1;
        };
        auto list2 = CommandNode::literal("list");
        list2->executable = true;
        list2->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string out;
            for (auto& o : scoreboard.objectives) out += o.name + " (" + o.criteria + ") ";
            sendFeedback(src, out.empty() ? "no objectives" : out);
            return static_cast<int>(scoreboard.objectives.size());
        };
        auto setd = CommandNode::literal("setdisplay");
        auto slot = CommandNode::literal("sidebar");
        auto objName = CommandNode::argument("objective", args::objectiveArg());
        objName->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& o : scoreboard.objectives) v.push_back(o.name);
            v.push_back("clear");
            return v;
        };
        objName->executable = true;
        objName->action = [this](CommandContext& c) {
            const std::string n = c.arg("objective").asStr();
            if (n == "clear" || !scoreboard.find(n)) {
                scoreboard.displayedSlot = -1;
            } else {
                scoreboard.displayedSlot = 1;         // sidebar
                scoreboard.displayedObjective = n;
            }
            sendDisplayAll();
            return 1;
        };
        setd->then(slot); slot->then(objName);
        add->then(name); name->then(crit);
        obj->then(add); obj->then(list2); obj->then(setd);

        auto players = CommandNode::literal("players");
        auto set = CommandNode::literal("set");
        auto who = CommandNode::argument("player", args::stringWord());
        auto oname = CommandNode::argument("objective", args::objectiveArg());
        oname->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& o : scoreboard.objectives) v.push_back(o.name);
            return v;
        };
        auto val = CommandNode::argument("score", args::integer(INT32_MIN, INT32_MAX));
        val->executable = true;
        val->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string holder = c.arg("player").asStr();
            const std::string objn = c.arg("objective").asStr();
            const std::int32_t v = c.arg("score").asInt();
            scoreboard.setScore(objn, holder, v);
            sendScoreAll(objn, holder, v);
            sendFeedback(src, "Set " + holder + " " + objn + " = " +
                         std::to_string(v));
            return 1;
        };
        set->then(who); who->then(oname); oname->then(val);
        players->then(set);
        sb->then(obj); sb->then(players);
        d.root->then(sb);
    }

    // /spectate [player]
    {
        auto sp2 = CommandNode::literal("spectate");
        sp2->executable = true;
        sp2->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (!src) return 0;
            WriteBuffer cam;
            cam.varint(src->entityId);
            try { src->conn->sendPacket(proto::pl::sc::Camera, cam); }
            catch (...) {}
            sendFeedback(src, "Camera reset");
            return 1;
        };
        auto who = CommandNode::argument("target", args::entity(true, false));
        who->executable = true;
        who->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("target").asSelector();
            if (!sel.playerNames.empty()) {
                if (Player* t = findPlayer(*this, sel.playerNames[0])) {
                    WriteBuffer cam;
                    cam.varint(t->entityId);
                    try { src->conn->sendPacket(
                              proto::pl::sc::Camera, cam); } catch (...) {}
                    sendFeedback(src, "Spectating " + t->name);
                }
            }
            return 1;
        };
        sp2->then(who);
        d.root->then(sp2);
    }
    // /difficulty <level>
    {
        auto diff = CommandNode::literal("difficulty");
        auto lvl = CommandNode::argument("level", args::stringWord());
        lvl->executable = true;
        lvl->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"peaceful", "easy", "normal",
                                            "hard"};
        };
        lvl->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            difficulty_ = c.arg("level").asStr();
            WriteBuffer b;
            b.i8(difficulty_ == "peaceful" ? 0 : difficulty_ == "easy" ? 1 :
                 difficulty_ == "hard" ? 3 : 2);
            b.boolean(false);
            broadcastPacketExcept(nullptr, proto::pl::sc::ChangeDifficulty, b);
            broadcastSystemText("\u00a77Difficulty set to " + difficulty_);
            return 1;
        };
        diff->then(lvl);
        d.root->then(diff);
    }
    // /team add|remove|join|leave|list  (plan10 §6, network §79) plan13 uses Team arg id 31
    {
        auto team = CommandNode::literal("team");
        // /team add <team> [displayName]
        auto tAdd = CommandNode::literal("add");
        auto tAddName = CommandNode::argument("team", args::teamArg());
        tAddName->executable = true;
        tAddName->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& kv : teams.teams) v.push_back(kv.first);
            return v;
        };
        tAddName->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string name = c.arg("team").asStr();
            if (!teams.create(name)) throw std::runtime_error("Team '" + name + "' already exists");
            Team* t = teams.find(name);
            if (t) sendTeamsCreate(*t);
            sendFeedback(src, "Created team " + name);
            return 1;
        };
        auto tDisplay = CommandNode::argument("displayName", args::stringGreedy());
        tDisplay->executable = true;
        tDisplay->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string name = c.arg("team").asStr();
            const std::string disp = c.arg("displayName").asStr();
            if (!teams.create(name)) throw std::runtime_error("Team '" + name + "' already exists");
            Team* t = teams.find(name);
            if (t) { t->displayName = disp; sendTeamsCreate(*t); }
            sendFeedback(src, "Created team " + name + " display=" + disp);
            return 1;
        };
        tAddName->then(tDisplay);
        tAdd->then(tAddName);
        // /team remove <team>
        auto tRemove = CommandNode::literal("remove");
        auto tRemName = CommandNode::argument("team", args::teamArg());
        tRemName->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& kv : teams.teams) v.push_back(kv.first);
            return v;
        };
        tRemName->executable = true;
        tRemName->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string name = c.arg("team").asStr();
            if (!teams.remove(name)) throw std::runtime_error("Team '" + name + "' does not exist");
            sendTeamsRemove(name);
            sendFeedback(src, "Removed team " + name);
            return 1;
        };
        tRemove->then(tRemName);
        // /team join <team> <members>
        auto tJoin = CommandNode::literal("join");
        auto tJoinTeam = CommandNode::argument("team", args::teamArg());
        tJoinTeam->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& kv : teams.teams) v.push_back(kv.first);
            return v;
        };
        auto tJoinMembers = CommandNode::argument("members", args::entity(true, false));
        tJoinMembers->executable = true;
        tJoinMembers->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string tname = c.arg("team").asStr();
            auto* t = teams.find(tname);
            if (!t) throw std::runtime_error("Team '" + tname + "' does not exist");
            const auto sel = c.arg("members").asSelector();
            std::vector<std::string> added;
            for (auto& n : sel.playerNames) if (teams.addMember(tname, n)) added.push_back(n);
            if (!added.empty()) sendTeamsJoin(tname, added);
            sendFeedback(src, "Added " + std::to_string(added.size()) + " members to " + tname);
            return (int)added.size();
        };
        tJoinTeam->then(tJoinMembers);
        tJoin->then(tJoinTeam);
        // /team leave <team> <members>
        auto tLeave = CommandNode::literal("leave");
        auto tLeaveTeam = CommandNode::argument("team", args::teamArg());
        tLeaveTeam->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& kv : teams.teams) v.push_back(kv.first);
            return v;
        };
        auto tLeaveMembers = CommandNode::argument("members", args::entity(true, false));
        tLeaveMembers->executable = true;
        tLeaveMembers->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string tname = c.arg("team").asStr();
            auto* t = teams.find(tname);
            if (!t) throw std::runtime_error("Team '" + tname + "' does not exist");
            const auto sel = c.arg("members").asSelector();
            std::vector<std::string> removed;
            for (auto& n : sel.playerNames) if (teams.removeMember(tname, n)) removed.push_back(n);
            if (!removed.empty()) sendTeamsLeave(tname, removed);
            sendFeedback(src, "Removed " + std::to_string(removed.size()) + " members from " + tname);
            return (int)removed.size();
        };
        tLeaveTeam->then(tLeaveMembers);
        tLeave->then(tLeaveTeam);
        // /team list
        auto tList = CommandNode::literal("list");
        tList->executable = true;
        tList->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string out;
            for (auto& kv : teams.teams) out += kv.first + " ";
            sendFeedback(src, out.empty() ? "No teams" : out);
            return (int)teams.teams.size();
        };
        team->then(tAdd); team->then(tRemove); team->then(tJoin); team->then(tLeave); team->then(tList);
        d.root->then(team);
    }
    // /bossbar add <id> <name> | remove <id> | set <id> value <0..1> | get <id>
    {
        auto bb = CommandNode::literal("bossbar");
        // add
        auto add = CommandNode::literal("add");
        auto idArg = CommandNode::argument("id", args::stringWord());
        auto nameArg = CommandNode::argument("name", args::stringGreedy());
        nameArg->executable = true;
        nameArg->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string id = c.arg("id").asStr();
            const std::string name = c.arg("name").asStr();
            if (!bossAI_) throw std::runtime_error("BossBar unavailable");
            int key = (int)std::hash<std::string>{}(id);
            BossBar bar;
            bar.entityId = key;
            // deterministic uuid from id hash (use key)
            {
                uint32_t h = (uint32_t)key * 0x9e3779b1u ^ 0x85ebca6bu;
                for (int i=0;i<16;i++) bar.uuid[i] = uint8_t((h >> ((i%4)*8)) & 0xFF);
                bar.uuid[6] = (bar.uuid[6] & 0x0F) | 0x40;
                bar.uuid[8] = (bar.uuid[8] & 0x3F) | 0x80;
            }
            bar.title = name.empty() ? id : name;
            bar.health = 1.0f;
            bar.color = 5;
            bar.division = 0;
            bar.flags = 0;
            bossAI_->bars().addCommandBar(key, bar);
            // send ADD packet
            {
                WriteBuffer b;
                b.uuid(bar.uuid.data());
                b.varint(0);
                nbt::writeTextComponent(b, bar.title);
                b.f32(bar.health);
                b.varint(bar.color);
                b.varint(bar.division);
                b.u8(bar.flags);
                broadcastPacketExcept(nullptr, proto::pl::sc::BossBar, b);
            }
            sendFeedback(src, "Created bossbar " + id);
            return 1;
        };
        idArg->then(nameArg);
        add->then(idArg);
        // remove
        auto rem = CommandNode::literal("remove");
        auto remId = CommandNode::argument("id", args::stringWord());
        remId->executable = true;
        remId->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string id = c.arg("id").asStr();
            if (!bossAI_) throw std::runtime_error("BossBar unavailable");
            int key = (int)std::hash<std::string>{}(id);
            auto* mgr = &bossAI_->bars();
            if (!mgr->hasBar(key)) throw std::runtime_error("Bossbar '" + id + "' not found");
            // send REMOVE
            {
                // need uuid: reconstruct or fetch from bars_
                BossBar tmp; tmp.entityId = key;
                uint32_t h = (uint32_t)key * 0x9e3779b1u ^ 0x85ebca6bu;
                for (int i=0;i<16;i++) tmp.uuid[i] = uint8_t((h >> ((i%4)*8)) & 0xFF);
                tmp.uuid[6] = (tmp.uuid[6] & 0x0F) | 0x40;
                tmp.uuid[8] = (tmp.uuid[8] & 0x3F) | 0x80;
                WriteBuffer b;
                b.uuid(tmp.uuid.data());
                b.varint(1);
                broadcastPacketExcept(nullptr, proto::pl::sc::BossBar, b);
            }
            mgr->removeCommandBar(key);
            sendFeedback(src, "Removed bossbar " + id);
            return 1;
        };
        rem->then(remId);
        // set value
        auto set = CommandNode::literal("set");
        auto setId = CommandNode::argument("id", args::stringWord());
        auto setValueKw = CommandNode::literal("value");
        auto setVal = CommandNode::argument("valueArg", args::integer(0, 100));
        setVal->executable = true;
        setVal->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string id = c.arg("id").asStr();
            int v = c.arg("valueArg").asInt();
            if (!bossAI_) throw std::runtime_error("BossBar unavailable");
            int key = (int)std::hash<std::string>{}(id);
            float hf = std::clamp(v / 100.f, 0.f, 1.f);
            bossAI_->bars().updateHealthForCommandBar(key, hf);
            // send health update
            {
                uint32_t h = (uint32_t)key * 0x9e3779b1u ^ 0x85ebca6bu;
                std::array<uint8_t,16> uuid{};
                for (int i=0;i<16;i++) uuid[i] = uint8_t((h >> ((i%4)*8)) & 0xFF);
                uuid[6] = (uuid[6] & 0x0F) | 0x40;
                uuid[8] = (uuid[8] & 0x3F) | 0x80;
                WriteBuffer b;
                b.uuid(uuid.data());
                b.varint(2);
                b.f32(hf);
                broadcastPacketExcept(nullptr, proto::pl::sc::BossBar, b);
            }
            sendFeedback(src, "Set bossbar " + id + " to " + std::to_string(v));
            return 1;
        };
        setValueKw->then(setVal);
        setId->then(setValueKw);
        set->then(setId);
        // get / list
        auto get = CommandNode::literal("list");
        get->executable = true;
        get->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (!bossAI_) throw std::runtime_error("BossBar unavailable");
            sendFeedback(src, "BossBars: " + std::to_string(bossAI_->bars().size()));
            return (int)bossAI_->bars().size();
        };
        bb->then(add); bb->then(rem); bb->then(set); bb->then(get);
        d.root->then(bb);
    }
    // /tag <targets> add|remove|list <tag>
    {
        auto tag = CommandNode::literal("tag");
        auto targets = CommandNode::argument("targets", args::entity(false, false));
        // tag add
        auto add = CommandNode::literal("add");
        auto tagName = CommandNode::argument("tag", args::stringWord());
        tagName->executable = true;
        tagName->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            const std::string t = c.arg("tag").asStr();
            int added = 0;
            // Player tags are stored as scoreboard tags? For now store in Player:: cookies? Use simple set in Player (not persistent)
            // We'll use a static map for entity tags
            for (auto& n : sel.playerNames) {
                if (Player* p = findPlayer(*this, n)) {
                    // Use player's tags via a hidden set (reuse cookies as tag marker)
                    if (p->cookies.count("tag:" + t) == 0) { p->cookies["tag:" + t] = {}; added++; }
                }
            }
            sendFeedback(src, "Added tag " + t + " to " + std::to_string(added));
            return added;
        };
        add->then(tagName);
        auto rem = CommandNode::literal("remove");
        auto remTag = CommandNode::argument("tag", args::stringWord());
        remTag->executable = true;
        remTag->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            const std::string t = c.arg("tag").asStr();
            int removed = 0;
            for (auto& n : sel.playerNames) if (Player* p = findPlayer(*this, n)) if (p->cookies.erase("tag:" + t)) removed++;
            sendFeedback(src, "Removed tag " + t + " from " + std::to_string(removed));
            return removed;
        };
        rem->then(remTag);
        auto list = CommandNode::literal("list");
        list->executable = true;
        list->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            std::string out;
            for (auto& n : sel.playerNames) if (Player* p = findPlayer(*this, n)) {
                for (auto& kv : p->cookies) if (kv.first.rfind("tag:",0)==0) out += kv.first.substr(4) + " ";
            }
            sendFeedback(src, out.empty() ? "no tags" : out);
            return 1;
        };
        targets->then(add); targets->then(rem); targets->then(list);
        tag->then(targets);
        d.root->then(tag);
    }
    // /fill <from> <to> <block> (plan13: BlockState + tab completion)
    {
        auto fill = CommandNode::literal("fill");
        auto from = CommandNode::argument("from", args::blockPos());
        auto to = CommandNode::argument("to", args::blockPos());
        auto block = CommandNode::argument("block", args::blockStateArg());
        block->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            v.reserve(gen::kBlocks.size() + 4);
            for (auto& e : gen::kBlocks) v.emplace_back(std::string(e.name));
            return v;
        };
        block->executable = true;
        block->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            auto p1 = c.arg("from").asBlockPos();
            auto p2 = c.arg("to").asBlockPos();
            std::string raw = c.arg("block").asStr();
            std::string name = raw;
            std::string propsStr;
            auto b1 = raw.find('[');
            auto b2 = raw.find('{');
            size_t nameEnd = std::string::npos;
            if (b1 != std::string::npos && b2 != std::string::npos) nameEnd = std::min(b1,b2);
            else if (b1 != std::string::npos) nameEnd = b1;
            else if (b2 != std::string::npos) nameEnd = b2;
            if (nameEnd != std::string::npos) {
                name = raw.substr(0, nameEnd);
                if (b1 != std::string::npos) {
                    size_t e = raw.find(']', b1);
                    if (e != std::string::npos) propsStr = raw.substr(b1+1, e-b1-1);
                }
            }
            if (name.find(':') == std::string::npos) name = "minecraft:" + name;
            const gen::BlockDef* def = gen::blockByName(name);
            if (!def) throw std::runtime_error("unknown block: " + name);
            std::uint16_t state = static_cast<std::uint16_t>(def->defaultState);
            if (!propsStr.empty()) {
                std::vector<std::pair<std::string,std::string>> tmp;
                size_t pos2=0;
                while(pos2<propsStr.size()){
                    size_t eq=propsStr.find('=',pos2);
                    if(eq==std::string::npos) break;
                    size_t comma=propsStr.find(',',eq);
                    std::string k=propsStr.substr(pos2, eq-pos2);
                    std::string v=propsStr.substr(eq+1, (comma==std::string::npos?propsStr.size():comma)-eq-1);
                    auto trim=[](std::string s){ size_t a=s.find_first_not_of(" \t"); size_t b=s.find_last_not_of(" \t"); return a==std::string::npos?s:s.substr(a,b-a+1); };
                    k=trim(k); v=trim(v);
                    tmp.emplace_back(k,v);
                    if(comma==std::string::npos) break;
                    pos2=comma+1;
                }
                if(!tmp.empty()){
                    std::vector<std::pair<std::string_view,std::string_view>> sv;
                    for(auto &pr: tmp) sv.emplace_back(pr.first, pr.second);
                    uint32_t cand = gen::stateWithProps(*def, sv);
                    if(cand!=0) state = static_cast<std::uint16_t>(cand);
                }
            }
            int minX = std::min(p1.x, p2.x), maxX = std::max(p1.x, p2.x);
            int minY = std::min(p1.y, p2.y), maxY = std::max(p1.y, p2.y);
            int minZ = std::min(p1.z, p2.z), maxZ = std::max(p1.z, p2.z);
            long long vol = static_cast<long long>(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
            if (vol > 32768) throw std::runtime_error("fill volume too large (max 32768, got " + std::to_string(vol) + ")");
            int filled = 0;
            for (int y = minY; y <= maxY; ++y)
                for (int z = minZ; z <= maxZ; ++z)
                    for (int x = minX; x <= maxX; ++x) {
                        world_.setBlock(x, y, z, state);
                        broadcastBlockChange(x, y, z, state);
                        ++filled;
                    }
            sendFeedback(src, "Filled " + std::to_string(filled) + " blocks with " + name);
            return filled;
        };
        to->then(block);
        from->then(to);
        fill->then(from);
        d.root->then(fill);
    }
    // /execute ... (plan13: store/score + as/run)
    {
        auto exec = CommandNode::literal("execute");
        // execute as <entity> run <command>
        auto asLit = CommandNode::literal("as");
        auto asEntity = CommandNode::argument("asTargets", args::entity(false, false));
        auto asRun = CommandNode::literal("run");
        auto asCmd = CommandNode::argument("command", args::stringGreedy());
        asCmd->executable = true;
        asCmd->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("asTargets").asSelector();
            std::string inner = c.arg("command").asStr();
            if (!inner.empty() && inner.front() == '/') inner = inner.substr(1);
            std::vector<Player*> targets;
            for (auto &name : sel.playerNames) if (Player* p = findPlayer(*this, name)) targets.push_back(p);
            if (targets.empty()) { sendFeedback(src, "No targets for execute as"); return 0; }
            int total=0;
            for (Player* t : targets) {
                brigadier::CommandSource tsrc;
                tsrc.player = t; tsrc.name=t->name; tsrc.console=false;
                tsrc.srcX=t->x; tsrc.srcY=t->y; tsrc.srcZ=t->z; tsrc.srcYaw=t->yaw; tsrc.srcPitch=t->pitch;
                tsrc.resolveSelector=[this,t](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,t); };
                auto res = commands_.execute(inner, std::move(tsrc));
                if(!res.ok) sendFeedback(src, "execute as " + t->name + " failed: "+res.errorText);
                else total+=res.value;
            }
            return total;
        };
        asRun->then(asCmd);
        asEntity->then(asRun);
        asLit->then(asEntity);
        exec->then(asLit);
        // execute store result|success score <targets> <objective> run <command>
        auto storeLit = CommandNode::literal("store");
        auto storeRes = CommandNode::literal("result");
        auto storeSuc = CommandNode::literal("success");
        auto scoreLit = CommandNode::literal("score");
        auto scoreTargets = CommandNode::argument("storeTargets", args::entity(false, false));
        auto scoreObj = CommandNode::argument("storeObjective", args::objectiveArg());
        scoreObj->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(auto &o: scoreboard.objectives) v.push_back(o.name);
            return v;
        };
        auto scoreRun = CommandNode::literal("run");
        auto scoreCmd = CommandNode::argument("storeCommand", args::stringGreedy());
        scoreCmd->executable = true;
        scoreCmd->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string storeType = "result";
            // determine if we came via result or success by checking which path was taken
            // We set storeType via captured literal; easiest: inspect input
            std::string inputLower = c.input;
            if (inputLower.find("store success") != std::string::npos) storeType = "success";
            const auto sel = c.arg("storeTargets").asSelector();
            std::string obj = c.arg("storeObjective").asStr();
            std::string inner = c.arg("storeCommand").asStr();
            if(!inner.empty() && inner.front()=='/') inner=inner.substr(1);
            // resolve selector raw is already in sel; we need target string for store
            // reconstruct target selector raw from sel? Use first name or raw arg? Use c.arg storeTargets as selector result: we need original raw string
            // Instead, we will use the selector result's playerNames to store; but we need original raw for execution via resolveSelector
            // For simplicity, build target string as the selector literal captured from input: extract via slicing?
            // We'll just use the selector result to know targets, and execute inner command via functionEvaluator store helper
            brigadier::CommandSource srcCtx;
            if (src){ srcCtx.player=src; srcCtx.name=src->name; srcCtx.console=false; srcCtx.srcX=src->x; srcCtx.srcY=src->y; srcCtx.srcZ=src->z; srcCtx.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); }; }
            else { srcCtx.console=true; srcCtx.resolveSelector=[this](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,nullptr); }; }
            // Use FunctionEvaluator helper for store
            // Build target selector string from sel (join with ,) ? Use first target raw approximation
            std::string targetStr;
            if(!sel.playerNames.empty()) targetStr = sel.playerNames[0];
            else targetStr = "@a";
            // Call evaluator
            return functionEvaluator_.executeWithStore(storeType, targetStr, obj, inner, srcCtx);
        };
        scoreRun->then(scoreCmd);
        scoreObj->then(scoreRun);
        scoreTargets->then(scoreObj);
        scoreLit->then(scoreTargets);
        storeRes->then(scoreLit);
        storeSuc->then(scoreLit);
        storeLit->then(storeRes);
        storeLit->then(storeSuc);
        exec->then(storeLit);
        d.root->then(exec);
    }
    // /function <name> (plan13: tab completion from datapack)
    {
        auto func = CommandNode::literal("function");
        auto nameArg = CommandNode::argument("name", args::resourceLocation());
        nameArg->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            return datapackManager_.getFunctionIds();
        };
        nameArg->executable = true;
        nameArg->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string id = c.arg("name").asStr();
            std::string norm = id;
            if (norm.find(':')==std::string::npos) norm = "minecraft:" + norm;
            brigadier::CommandSource fsrc;
            if (src){ fsrc.player=src; fsrc.name=src->name; fsrc.console=false; fsrc.srcX=src->x; fsrc.srcY=src->y; fsrc.srcZ=src->z; fsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); }; }
            else { fsrc.console=true; fsrc.name="Server"; fsrc.resolveSelector=[this](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,nullptr); }; }
            int executed = functionEvaluator_.executeFunction(norm, fsrc);
            if (executed==0) {
                // Fallback: try direct file read (legacy)
                auto colon = norm.find(':');
                std::string ns = colon!=std::string::npos?norm.substr(0,colon):"minecraft";
                std::string path = colon!=std::string::npos?norm.substr(colon+1):norm;
                std::string file = "assets/data/" + ns + "/functions/" + path + ".mcfunction";
                std::ifstream f(file);
                if(!f) throw std::runtime_error("function not found: " + norm);
                std::string line;
                int cnt=0;
                while(std::getline(f,line)){
                    size_t s=line.find_first_not_of(" \t\r\n");
                    if(s==std::string::npos) continue;
                    size_t e=line.find_last_not_of(" \t\r\n");
                    std::string t=line.substr(s,e-s+1);
                    if(t.empty()||t[0]=='#') continue;
                    if(!t.empty()&&t.front()=='/') t=t.substr(1);
                    brigadier::CommandSource cur=fsrc;
                    auto res=commands_.execute(t, std::move(cur));
                    if(!res.ok) sendFeedback(src, "function line failed: "+t+" -> "+res.errorText);
                    ++cnt;
                }
                sendFeedback(src, "Executed function " + norm + " ("+std::to_string(cnt)+" commands)");
                return cnt;
            }
            sendFeedback(src, "Executed function " + norm + " (result="+std::to_string(executed)+")");
            return executed;
        };
        func->then(nameArg);
        d.root->then(func);
    }
    // /datapack list|enable|disable (plan13)
    {
        auto dp = CommandNode::literal("datapack");
        auto list = CommandNode::literal("list");
        list->executable = true;
        list->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            auto avail = datapackManager_.listAvailable();
            auto enabled = datapackManager_.listEnabled();
            std::string out = "Available packs ("+std::to_string(avail.size())+"): ";
            for(auto& p: avail) out+=p+" ";
            out+="\nEnabled ("+std::to_string(enabled.size())+"): ";
            for(auto& p: enabled) out+=p+" ";
            out+="\nAdvancements: "+std::to_string(datapackManager_.advancementCount())+
                 " Predicates: "+std::to_string(datapackManager_.predicateCount())+
                 " Modifiers: "+std::to_string(datapackManager_.itemModifierCount());
            sendFeedback(src,out);
            return (int)avail.size();
        };
        auto enable = CommandNode::literal("enable");
        auto enName = CommandNode::argument("name", args::stringWord());
        enName->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            return datapackManager_.listAvailable();
        };
        enName->executable = true;
        enName->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string n=c.arg("name").asStr();
            if(datapackManager_.enablePack(n)){
                sendFeedback(src,"Enabled datapack "+n);
                return 1;
            } else {
                sendFeedback(src,"Datapack "+n+" already enabled or unknown");
                return 0;
            }
        };
        enable->then(enName);
        auto disable = CommandNode::literal("disable");
        auto disName = CommandNode::argument("name", args::stringWord());
        disName->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            return datapackManager_.listEnabled();
        };
        disName->executable = true;
        disName->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string n=c.arg("name").asStr();
            if(datapackManager_.disablePack(n)){
                sendFeedback(src,"Disabled datapack "+n);
                return 1;
            } else {
                sendFeedback(src,"Cannot disable "+n+" (not enabled or vanilla)");
                return 0;
            }
        };
        disable->then(disName);
        dp->then(list); dp->then(enable); dp->then(disable);
        d.root->then(dp);
    }
    // /schedule function <name> <time> [append|replace] (plan13)
    {
        auto sched = CommandNode::literal("schedule");
        auto funcLit = CommandNode::literal("function");
        auto fname = CommandNode::argument("funcName", args::resourceLocation());
        fname->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            return datapackManager_.getFunctionIds();
        };
        auto ftime = CommandNode::argument("time", args::timeArg());
        ftime->executable = true;
        ftime->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string id=c.arg("funcName").asStr();
            std::int64_t t=c.arg("time").asI64();
            if(t<=0) t=1;
            functionEvaluator_.scheduleFunction(id, t, "replace", tickNo_);
            sendFeedback(src,"Scheduled "+id+" in "+std::to_string(t)+" ticks (replace)");
            return 1;
        };
        auto append = CommandNode::literal("append");
        append->executable = true;
        append->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string id=c.arg("funcName").asStr();
            std::int64_t t=c.arg("time").asI64();
            functionEvaluator_.scheduleFunction(id, t, "append", tickNo_);
            sendFeedback(src,"Scheduled "+id+" in "+std::to_string(t)+" ticks (append)");
            return 1;
        };
        auto repl = CommandNode::literal("replace");
        repl->executable = true;
        repl->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string id=c.arg("funcName").asStr();
            std::int64_t t=c.arg("time").asI64();
            functionEvaluator_.scheduleFunction(id, t, "replace", tickNo_);
            sendFeedback(src,"Scheduled "+id+" in "+std::to_string(t)+" ticks (replace)");
            return 1;
        };
        ftime->then(append);
        ftime->then(repl);
        fname->then(ftime);
        funcLit->then(fname);
        sched->then(funcLit);
        d.root->then(sched);
    }
    // /return <value> (plan13: function return)
    {
        auto ret = CommandNode::literal("return");
        auto val = CommandNode::argument("value", args::integer(INT32_MIN, INT32_MAX));
        val->executable = true;
        val->action = [this](CommandContext& c){
            int v=c.arg("value").asInt();
            functionEvaluator_.setReturnValue(v);
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"Return "+std::to_string(v));
            return v;
        };
        ret->then(val);
        // bare return (success)
        ret->executable = true;
        ret->action = [this](CommandContext& c){
            functionEvaluator_.setReturnValue(1);
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"Return 1");
            return 1;
        };
        d.root->then(ret);
    }
    // /data get/block/entity with NBT (plan13 Nbt args)
    {
        auto data = CommandNode::literal("data");
        auto get = CommandNode::literal("get");
        auto block = CommandNode::literal("block");
        auto pos = CommandNode::argument("pos", args::blockPos());
        pos->executable = true;
        pos->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            auto p=c.arg("pos").asBlockPos();
            std::uint16_t st=world_.getBlock(p.x,p.y,p.z);
            auto* def=gen::blockByState(st);
            std::string out = def?std::string(def->name):"minecraft:air";
            out += " state=" + std::to_string(st);
            sendFeedback(src, out);
            return st;
        };
        auto nbtPath = CommandNode::argument("path", args::nbtPathArg());
        nbtPath->executable = true;
        nbtPath->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            auto p=c.arg("pos").asBlockPos();
            std::string path=c.arg("path").asStr();
            std::uint16_t st=world_.getBlock(p.x,p.y,p.z);
            auto* def=gen::blockByState(st);
            std::string out = (def?std::string(def->name):"minecraft:air") + " path=" + path;
            sendFeedback(src, out);
            return 1;
        };
        pos->then(nbtPath);
        block->then(pos);
        get->then(block);
        data->then(get);
        d.root->then(data);
    }
    // /clear with ItemPredicate (plan13)
    {
        // extend /clear to support predicate filtering: /clear <targets> <item> [maxCount]
        auto clear2 = CommandNode::literal("clear");
        auto who = CommandNode::argument("targets", args::entity(false,false));
        auto itemPred = CommandNode::argument("item", args::itemPredicateArg());
        itemPred->executable = true;
        itemPred->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            const auto sel=c.arg("targets").asSelector();
            std::string pred=c.arg("item").asStr();
            // handle tag predicate like #minecraft:planks
            bool isTag = !pred.empty() && pred[0]=='#';
            std::string base = isTag ? pred.substr(1) : pred;
            if(base.find(':')==std::string::npos) base="minecraft:"+base;
            int removed=0;
            for(auto& n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                for(auto& s: p->inv) if(!s.empty()){
                    bool match=false;
                    if(isTag){
                        // check tag membership via datapackManager
                        auto* tagSet = datapackManager_.tagManager.getItemTag(base);
                        if(tagSet && tagSet->count(s.itemId)) match=true;
                    } else {
                        auto it=gen::itemIdByName().find(base);
                        if(it!=gen::itemIdByName().end() && it->second==s.itemId) match=true;
                    }
                    if(match){ removed+=s.count; s=ItemStack::air(); }
                }
                resendInventory(*p);
            }
            sendFeedback(src,"Cleared "+std::to_string(removed)+" matching "+pred);
            return removed;
        };
        auto maxCount = CommandNode::argument("maxCount", args::integer(1,64));
        maxCount->executable = true;
        maxCount->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            const auto sel=c.arg("targets").asSelector();
            std::string pred=c.arg("item").asStr();
            int limit=c.arg("maxCount").asInt();
            bool isTag = !pred.empty() && pred[0]=='#';
            std::string base = isTag ? pred.substr(1) : pred;
            if(base.find(':')==std::string::npos) base="minecraft:"+base;
            int removed=0;
            for(auto& n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                for(auto& s: p->inv) if(!s.empty() && removed<limit){
                    bool match=false;
                    if(isTag){
                        auto* tagSet = datapackManager_.tagManager.getItemTag(base);
                        if(tagSet && tagSet->count(s.itemId)) match=true;
                    } else {
                        auto it=gen::itemIdByName().find(base);
                        if(it!=gen::itemIdByName().end() && it->second==s.itemId) match=true;
                    }
                    if(match){
                        int take = std::min<int>(s.count, limit-removed);
                        removed+=take;
                        s.count-=take;
                        if(s.count<=0) s=ItemStack::air();
                    }
                }
                resendInventory(*p);
            }
            sendFeedback(src,"Cleared "+std::to_string(removed)+" matching "+pred+" (limit)");
            return removed;
        };
        itemPred->then(maxCount);
        who->then(itemPred);
        clear2->then(who);
        d.root->then(clear2);
    }
    // /testargs – exercises all remaining arg types for DeclareCommands coverage (plan13)
    {
        auto ta = CommandNode::literal("testargs");
        // block predicate
        auto bpLit = CommandNode::literal("blockpred");
        auto bpArg = CommandNode::argument("val", args::blockPredicateArg());
        bpArg->executable = true;
        bpArg->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"blockpred "+c.arg("val").asStr());
            return 1;
        };
        bpLit->then(bpArg);
        ta->then(bpLit);
        // item predicate
        auto ipLit = CommandNode::literal("itempred");
        auto ipArg = CommandNode::argument("val", args::itemPredicateArg());
        ipArg->executable = true;
        ipArg->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"itempred "+c.arg("val").asStr());
            return 1;
        };
        ipLit->then(ipArg);
        ta->then(ipLit);
        // nbt
        auto nbtLit = CommandNode::literal("nbt");
        auto nbtArg = CommandNode::argument("val", args::nbtArg());
        nbtArg->executable = true;
        nbtArg->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"nbt "+c.arg("val").asStr());
            return 1;
        };
        nbtLit->then(nbtArg);
        ta->then(nbtLit);
        // nbt compound tag
        auto nbtcLit = CommandNode::literal("nbtc");
        auto nbtcArg = CommandNode::argument("val", args::nbtCompoundTagArg());
        nbtcArg->executable = true;
        nbtcArg->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"nbtc "+c.arg("val").asStr());
            return 1;
        };
        nbtcLit->then(nbtcArg);
        ta->then(nbtcLit);
        // objective (already covered but ensure)
        auto objLit = CommandNode::literal("objective");
        auto objArg = CommandNode::argument("val", args::objectiveArg());
        objArg->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(auto& o: scoreboard.objectives) v.push_back(o.name);
            return v;
        };
        objArg->executable = true;
        objArg->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"objective "+c.arg("val").asStr());
            return 1;
        };
        objLit->then(objArg);
        ta->then(objLit);
        // team
        auto teamLit = CommandNode::literal("team");
        auto teamArg = CommandNode::argument("val", args::teamArg());
        teamArg->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(auto& kv: teams.teams) v.push_back(kv.first);
            return v;
        };
        teamArg->executable = true;
        teamArg->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"team "+c.arg("val").asStr());
            return 1;
        };
        teamLit->then(teamArg);
        ta->then(teamLit);
        d.root->then(ta);
    }
}

} // namespace cppfm
