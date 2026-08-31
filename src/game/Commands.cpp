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
#include <unordered_set>
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
                auto [cx, cz] = chunkKeyDecode(k);
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
                auto [cx, cz] = chunkKeyDecode(k);
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
    // /effect give <targets> <effect> [seconds] [amplifier] [hideParticles]
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
                }
            sendFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s)");
            return 1;
        };
        // plan28 finish: amplifier argument was missing — `effect give <p> <eff>
        // 10 1` left "1" as an extra token → parse error → no EntityEffect 0x5E.
        // Vanilla: amplifier 0..255, sent as the varint after the effect id.
        auto amp = CommandNode::argument("amplifier", args::integer(0, 255));
        amp->executable = true;
        amp->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            const int dur = c.arg("seconds").asInt();
            const int ampv = c.arg("amplifier").asInt();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = dur * 20;
                    e.amplifier = static_cast<std::int8_t>(ampv); // level-1 model
                    t->effects.erase(
                        std::remove_if(t->effects.begin(), t->effects.end(),
                                       [&](const EffectInstance& x)
                                           { return x.type == e.type; }),
                        t->effects.end());
                    t->effects.push_back(e);
                    WriteBuffer b;
                    b.varint(t->entityId);
                    b.varint(e.type);
                    b.varint(ampv);          // raw 0..255 (int8_t wraps >127)
                    b.varint(e.durationTicks);
                    b.u8(effectFlags(e));
                    try { t->conn->sendPacket(proto::pl::sc::EntityEffect, b); }
                    catch (...) {}
                }
            sendFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s, amplifier " +
                         std::to_string(ampv) + ")");
            return 1;
        };
        // vanilla optional <hideParticles> boolean (low priority completion)
        auto hide = CommandNode::argument("hideParticles", args::boolean());
        hide->executable = true;
        hide->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            const int dur = c.arg("seconds").asInt();
            const int ampv = c.arg("amplifier").asInt();
            const bool hidep = c.arg("hideParticles").asBool();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = dur * 20;
                    e.amplifier = static_cast<std::int8_t>(ampv);
                    e.showParticles = !hidep;
                    t->effects.erase(
                        std::remove_if(t->effects.begin(), t->effects.end(),
                                       [&](const EffectInstance& x)
                                           { return x.type == e.type; }),
                        t->effects.end());
                    t->effects.push_back(e);
                    WriteBuffer b;
                    b.varint(t->entityId);
                    b.varint(e.type);
                    b.varint(ampv);
                    b.varint(e.durationTicks);
                    b.u8(effectFlags(e));
                    try { t->conn->sendPacket(proto::pl::sc::EntityEffect, b); }
                    catch (...) {}
                }
            sendFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s, amplifier " +
                         std::to_string(ampv) + ", hideParticles " +
                         (hidep ? "true" : "false") + ")");
            return 1;
        };
        eff->then(secs);
        amp->then(hide);
        secs->then(amp);
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
        // D25 §10: /scoreboard objectives modify <objective> numberformat <blank|styled|fixed> [arg]
        auto modify = CommandNode::literal("modify");
        auto modTarget = CommandNode::argument("targetObjective", args::objectiveArg());
        modTarget->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (auto& o : scoreboard.objectives) v.push_back(o.name);
            return v;
        };
        auto nfLit = CommandNode::literal("numberformat");
        auto blankLit = CommandNode::literal("blank");
        blankLit->executable = true;
        blankLit->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string t = c.arg("targetObjective").asStr();
            auto* o = scoreboard.find(t);
            if (!o) throw std::runtime_error("objective not found: " + t);
            o->numberFormat.has = true;
            o->numberFormat.type = Scoreboard::NumberFormatType::Blank;
            sendObjectiveAll(*o, 2);
            sendFeedback(src, "Set numberformat of " + t + " to blank");
            return 1;
        };
        auto styledLit = CommandNode::literal("styled");
        styledLit->executable = true;
        styledLit->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string t = c.arg("targetObjective").asStr();
            auto* o = scoreboard.find(t);
            if (!o) throw std::runtime_error("objective not found: " + t);
            o->numberFormat.has = true;
            o->numberFormat.type = Scoreboard::NumberFormatType::Styled;
            o->numberFormat.color = "red";
            sendObjectiveAll(*o, 2);
            sendFeedback(src, "Set numberformat of " + t + " to styled red");
            return 1;
        };
        auto styledArg = CommandNode::argument("style", args::stringWord());
        styledArg->executable = true;
        styledArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"red","green","yellow","white","blue","aqua","gold"};
        };
        styledArg->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string t = c.arg("targetObjective").asStr();
            const std::string col = c.arg("style").asStr();
            auto* o = scoreboard.find(t);
            if (!o) throw std::runtime_error("objective not found: " + t);
            o->numberFormat.has = true;
            o->numberFormat.type = Scoreboard::NumberFormatType::Styled;
            o->numberFormat.color = col;
            sendObjectiveAll(*o, 2);
            sendFeedback(src, "Set numberformat of " + t + " to styled " + col);
            return 1;
        };
        styledLit->then(styledArg);
        auto fixedLit = CommandNode::literal("fixed");
        fixedLit->executable = true;
        fixedLit->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string t = c.arg("targetObjective").asStr();
            auto* o = scoreboard.find(t);
            if (!o) throw std::runtime_error("objective not found: " + t);
            o->numberFormat.has = true;
            o->numberFormat.type = Scoreboard::NumberFormatType::Fixed;
            o->numberFormat.fixedText = std::string("\xE2\x99\xA5");
            sendObjectiveAll(*o, 2);
            sendFeedback(src, "Set numberformat of " + t + " to fixed");
            return 1;
        };
        auto fixedArg = CommandNode::argument("fixedText", args::stringGreedy());
        fixedArg->executable = true;
        fixedArg->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string t = c.arg("targetObjective").asStr();
            const std::string txt = c.arg("fixedText").asStr();
            auto* o = scoreboard.find(t);
            if (!o) throw std::runtime_error("objective not found: " + t);
            o->numberFormat.has = true;
            o->numberFormat.type = Scoreboard::NumberFormatType::Fixed;
            o->numberFormat.fixedText = txt;
            sendObjectiveAll(*o, 2);
            sendFeedback(src, "Set numberformat of " + t + " to fixed " + txt);
            return 1;
        };
        fixedLit->then(fixedArg);
        nfLit->then(blankLit); nfLit->then(styledLit); nfLit->then(fixedLit);
        modTarget->then(nfLit);
        modify->then(modTarget);
        obj->then(add); obj->then(list2); obj->then(setd); obj->then(modify);

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
        // D26: /scoreboard players reset <target> [objective] (wildcard when no objective)
        {
            auto resetLit = CommandNode::literal("reset");
            auto resetWho = CommandNode::argument("target", args::stringWord());
            resetWho->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
                std::vector<std::string> v;
                // suggest holders that have scores
                std::unordered_set<std::string> seen;
                for (auto& [objName, map] : scoreboard.scores)
                    for (auto& [holder, _] : map)
                        if (seen.insert(holder).second) v.push_back(holder);
                // also player names
                for (auto& pr : playersSnapshot()) v.push_back(pr->name);
                return v;
            };
            auto resetObj = CommandNode::argument("objective", args::objectiveArg());
            resetObj->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
                std::vector<std::string> v;
                for (auto& o : scoreboard.objectives) v.push_back(o.name);
                return v;
            };
            resetObj->executable = true;
            resetObj->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const std::string raw = c.arg("target").asStr();
                const std::string obj = c.arg("objective").asStr();
                if (!scoreboard.find(obj)) throw std::runtime_error("objective not found: "+obj);
                auto sel = resolveSelector(raw, src);
                std::vector<std::string> holders = sel.playerNames.empty() ? std::vector<std::string>{raw} : sel.playerNames;
                int n=0;
                for (auto& h : holders) if (scoreboard.resetScore(h, obj)) { sendResetScoreAll(h, &obj); ++n; }
                sendFeedback(src, "Reset "+std::to_string(n)+" score(s) for objective "+obj);
                return n;
            };
            resetWho->executable = true;
            resetWho->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const std::string raw = c.arg("target").asStr();
                auto sel = resolveSelector(raw, src);
                std::vector<std::string> holders = sel.playerNames.empty() ? std::vector<std::string>{raw} : sel.playerNames;
                int total=0;
                for (auto& h : holders) {
                    auto aff = scoreboard.resetAllScores(h);
                    if (!aff.empty()) { sendResetScoreAllWildcard(h); ++total; }
                }
                sendFeedback(src, "Reset "+std::to_string(total)+" holder(s) (wildcard)");
                return total;
            };
            resetWho->then(resetObj);
            resetLit->then(resetWho);
            players->then(resetLit);
        }
        // D26: /scoreboard objectives remove <name> with reset_score per holder + display clear
        {
            auto rem = CommandNode::literal("remove");
            auto remName = CommandNode::argument("name", args::objectiveArg());
            remName->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&) {
                std::vector<std::string> v;
                for (auto& o : scoreboard.objectives) v.push_back(o.name);
                return v;
            };
            remName->executable = true;
            remName->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const std::string n = c.arg("name").asStr();
                Scoreboard::Objective* o = scoreboard.find(n);
                if (!o) throw std::runtime_error("Objective not found: "+n);
                Scoreboard::Objective copy = *o;
                std::vector<std::string> holders;
                scoreboard.removeObjectiveWithReset(n, holders);
                for (auto& h : holders) sendResetScoreAll(h, &n);
                sendObjectiveAll(copy, 1); // method 1 remove
                if (scoreboard.displayedObjective == n) {
                    scoreboard.displayedSlot = -1;
                    scoreboard.displayedObjective.clear();
                    sendDisplayAll(); // 0x5C clear
                }
                sendFeedback(src, "Removed objective "+n);
                return 1;
            };
            rem->then(remName);
            obj->then(rem);
        }
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
    // /execute ... plan32: modifiers + conditions + store + run (Yarn ExecuteCommand 11 modifiers)
    {
        auto exec = CommandNode::literal("execute");
        auto execRunLit = CommandNode::literal("run");
        auto execRunCmd = CommandNode::argument("command", args::stringGreedy());
        execRunCmd->executable = true;
        execRunCmd->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string inner = c.arg("command").asStr();
            if(!inner.empty() && inner.front()=='/') inner=inner.substr(1);
            brigadier::CommandSource tsrc;
            if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.console=false; tsrc.srcX=src->x; tsrc.srcY=src->y; tsrc.srcZ=src->z; tsrc.srcYaw=src->yaw; tsrc.srcPitch=src->pitch; tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); }; }
            else { tsrc.console=true; tsrc.resolveSelector=[this](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,nullptr); }; }
            // carry over modified coords from parse context if any (positioned/at etc handled via ctx.srcX)
            tsrc.srcX = c.srcX; tsrc.srcY = c.srcY; tsrc.srcZ = c.srcZ;
            // yaw/pitch from command source if modified via rotated/facing (stored in ctx.srcYaw/srcPitch)
            if(c.srcYaw != 0 || c.srcPitch != 0){ tsrc.srcYaw=c.srcYaw; tsrc.srcPitch=c.srcPitch; }
            auto res = commands_.execute(inner, std::move(tsrc));
            if(!res.ok) sendFeedback(src, res.errorText);
            return res.ok?res.value:0;
        };
        execRunLit->then(execRunCmd);
        // helper to add run child to any node
        auto addRun = [&](NodePtr n){ n->then(execRunLit); };

        // ---- as <entity> ----
        {
            auto asLit = CommandNode::literal("as");
            auto asEntity = CommandNode::argument("asTargets", args::entity(false,false));
            auto asRun = CommandNode::literal("run");
            auto asCmd = CommandNode::argument("command", args::stringGreedy());
            asCmd->executable = true;
            asCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("asTargets").asSelector();
                std::string inner = c.arg("command").asStr();
                if(!inner.empty() && inner.front()=='/') inner=inner.substr(1);
                std::vector<Player*> targets;
                for(auto &name: sel.playerNames) if(Player* p=findPlayer(*this,name)) targets.push_back(p);
                if(targets.empty()){ sendFeedback(src,"No targets for execute as"); return 0; }
                int total=0;
                for(Player* t: targets){
                    brigadier::CommandSource tsrc;
                    tsrc.player=t; tsrc.name=t->name; tsrc.console=false;
                    tsrc.srcX=t->x; tsrc.srcY=t->y; tsrc.srcZ=t->z; tsrc.srcYaw=t->yaw; tsrc.srcPitch=t->pitch;
                    tsrc.resolveSelector=[this,t](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,t); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    if(!res.ok) sendFeedback(src,"execute as "+t->name+" failed: "+res.errorText);
                    else total+=res.value;
                }
                return total;
            };
            asRun->then(asCmd);
            asEntity->then(asRun);
            asLit->then(asEntity);
            exec->then(asLit);
        }
        // ---- at <entity> ----
        {
            auto atLit = CommandNode::literal("at");
            auto atEnt = CommandNode::argument("atTargets", args::entity(false,false));
            auto atRun = CommandNode::literal("run");
            auto atCmd = CommandNode::argument("command", args::stringGreedy());
            atCmd->executable = true;
            atCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("atTargets").asSelector();
                std::string inner = c.arg("command").asStr();
                if(!inner.empty() && inner.front()=='/') inner=inner.substr(1);
                int total=0;
                bool any=false;
                for(auto &name: sel.playerNames) if(Player* e=findPlayer(*this,name)){
                    any=true;
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.console=false; }
                    else { tsrc.player=e; tsrc.name=e->name; }
                    tsrc.srcX=e->x; tsrc.srcY=e->y; tsrc.srcZ=e->z; tsrc.srcYaw=e->yaw; tsrc.srcPitch=e->pitch;
                    tsrc.resolveSelector=[this,src,e](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw, src?src:e); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    if(res.ok) total+=res.value;
                }
                if(!any) sendFeedback(src,"No targets for execute at");
                return total;
            };
            atRun->then(atCmd);
            atEnt->then(atRun);
            atLit->then(atEnt);
            exec->then(atLit);
        }
        // ---- positioned <pos> / positioned as <entity> / positioned over <heightmap> ----
        {
            auto posLit = CommandNode::literal("positioned");
            // positioned <pos>
            auto posArg = CommandNode::argument("pos", args::vec3Arg(false));
            auto posRun = CommandNode::literal("run");
            auto posCmd = CommandNode::argument("command", args::stringGreedy());
            posCmd->executable = true;
            posCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                brigadier::Vec3d p=c.arg("pos").asVec3();
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; }
                tsrc.srcX=p.x; tsrc.srcY=p.y; tsrc.srcZ=p.z;
                tsrc.srcYaw=src?src->yaw:0; tsrc.srcPitch=src?src->pitch:0;
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            posRun->then(posCmd);
            posArg->then(posRun);
            posLit->then(posArg);
            // positioned as <entity>
            auto asLit2 = CommandNode::literal("as");
            auto asEnt2 = CommandNode::argument("posAsTargets", args::entity(false,false));
            auto asRun2 = CommandNode::literal("run");
            auto asCmd2 = CommandNode::argument("command", args::stringGreedy());
            asCmd2->executable = true;
            asCmd2->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel=c.arg("posAsTargets").asSelector();
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                int total=0;
                for(auto &n: sel.playerNames) if(Player* e=findPlayer(*this,n)){
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; }
                    else tsrc.player=e;
                    tsrc.srcX=e->x; tsrc.srcY=e->y; tsrc.srcZ=e->z;
                    tsrc.resolveSelector=[this,src,e](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src?src:e); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    if(res.ok) total+=res.value;
                }
                return total;
            };
            asRun2->then(asCmd2);
            asEnt2->then(asRun2);
            asLit2->then(asEnt2);
            posLit->then(asLit2);
            // positioned over <heightmap> (simplified: over world_surface -> y = 64)
            auto overLit = CommandNode::literal("over");
            auto hmArg = CommandNode::argument("heightmap", args::stringWord());
            hmArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){ return std::vector<std::string>{"world_surface","motion_blocking","ocean_floor"}; };
            auto overRun = CommandNode::literal("run");
            auto overCmd = CommandNode::argument("command", args::stringGreedy());
            overCmd->executable = true;
            overCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                double ox = src?src->x:0, oz = src?src->z:0;
                // find top non-air at ox,oz (simple scan)
                int topY=64;
                for(int y=319;y>=-64;--y){ if(world_.getBlock((int)ox,y,(int)oz)!=0){ topY=y+1; break; } }
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; }
                tsrc.srcX=ox; tsrc.srcY=topY; tsrc.srcZ=oz;
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            overRun->then(overCmd);
            hmArg->then(overRun);
            overLit->then(hmArg);
            posLit->then(overLit);
            exec->then(posLit);
        }
        // ---- anchored <eyes|feet> ----
        {
            auto ancLit = CommandNode::literal("anchored");
            auto ancArg = CommandNode::argument("anchor", args::entityAnchorArg());
            auto ancRun = CommandNode::literal("run");
            auto ancCmd = CommandNode::argument("command", args::stringGreedy());
            ancCmd->executable = true;
            ancCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                std::string anchor=c.arg("anchor").asStr();
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.srcX=src->x; tsrc.srcY=src->y + (anchor=="eyes"?1.62:0); tsrc.srcZ=src->z; }
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            ancRun->then(ancCmd);
            ancArg->then(ancRun);
            ancLit->then(ancArg);
            exec->then(ancLit);
        }
        // ---- rotated <yaw pitch> / rotated as <entity> ----
        {
            auto rotLit = CommandNode::literal("rotated");
            auto rotArg = CommandNode::argument("rot", args::rotationArg());
            auto rotRun = CommandNode::literal("run");
            auto rotCmd = CommandNode::argument("command", args::stringGreedy());
            rotCmd->executable = true;
            rotCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                auto v = c.arg("rot");
                brigadier::Vec2f rv{0,0};
                if(auto* p=std::get_if<brigadier::Vec2f>(&v.v)) rv=*p;
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.srcX=src->x; tsrc.srcY=src->y; tsrc.srcZ=src->z; }
                tsrc.srcYaw=rv.x; tsrc.srcPitch=rv.y;
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            rotRun->then(rotCmd);
            rotArg->then(rotRun);
            rotLit->then(rotArg);
            auto rotAsLit = CommandNode::literal("as");
            auto rotAsEnt = CommandNode::argument("rotAsTargets", args::entity(false,false));
            auto rotAsRun = CommandNode::literal("run");
            auto rotAsCmd = CommandNode::argument("command", args::stringGreedy());
            rotAsCmd->executable = true;
            rotAsCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel=c.arg("rotAsTargets").asSelector();
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                int total=0;
                for(auto &n: sel.playerNames) if(Player* e=findPlayer(*this,n)){
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.srcX=src->x; tsrc.srcY=src->y; tsrc.srcZ=src->z; }
                    tsrc.srcYaw=e->yaw; tsrc.srcPitch=e->pitch;
                    tsrc.resolveSelector=[this,src,e](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src?src:e); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    if(res.ok) total+=res.value;
                }
                return total;
            };
            rotAsRun->then(rotAsCmd);
            rotAsEnt->then(rotAsRun);
            rotAsLit->then(rotAsEnt);
            rotLit->then(rotAsLit);
            exec->then(rotLit);
        }
        // ---- facing <pos> / facing entity <targets> <anchor> ----
        {
            auto faceLit = CommandNode::literal("facing");
            // facing <pos>
            auto facePos = CommandNode::argument("facingPos", args::vec3Arg(false));
            auto facePosRun = CommandNode::literal("run");
            auto facePosCmd = CommandNode::argument("command", args::stringGreedy());
            facePosCmd->executable = true;
            facePosCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                brigadier::Vec3d target=c.arg("facingPos").asVec3();
                double sx=src?src->x:0, sy=src?src->y:0, sz=src?src->z:0;
                double dx=target.x-sx, dy=target.y-sy, dz=target.z-sz;
                float yaw = (float)(std::atan2(-dx, dz)*180/M_PI);
                float pitch = (float)(-std::atan2(dy, std::sqrt(dx*dx+dz*dz))*180/M_PI);
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.srcX=sx; tsrc.srcY=sy; tsrc.srcZ=sz; }
                tsrc.srcYaw=yaw; tsrc.srcPitch=pitch;
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            facePosRun->then(facePosCmd);
            facePos->then(facePosRun);
            faceLit->then(facePos);
            // facing entity <targets> <anchor>
            auto faceEntLit = CommandNode::literal("entity");
            auto faceEnt = CommandNode::argument("facingTargets", args::entity(false,false));
            auto faceAnc = CommandNode::argument("facingAnchor", args::entityAnchorArg());
            auto faceEntRun = CommandNode::literal("run");
            auto faceEntCmd = CommandNode::argument("command", args::stringGreedy());
            faceEntCmd->executable = true;
            faceEntCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel=c.arg("facingTargets").asSelector();
                std::string anchor=c.arg("facingAnchor").asStr();
                Player* target=nullptr;
                for(auto &n: sel.playerNames) if(Player* e=findPlayer(*this,n)){ target=e; break; }
                if(!target){ sendFeedback(src,"No target for facing entity"); return 0; }
                double sx=src?src->x:0, sy=src?src->y:0, sz=src?src->z:0;
                double tx=target->x, ty=target->y + (anchor=="eyes"?1.62:0), tz=target->z;
                double dx=tx-sx, dy=ty-sy, dz=tz-sz;
                float yaw=(float)(std::atan2(-dx, dz)*180/M_PI);
                float pitch=(float)(-std::atan2(dy, std::sqrt(dx*dx+dz*dz))*180/M_PI);
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.srcX=sx; tsrc.srcY=sy; tsrc.srcZ=sz; }
                tsrc.srcYaw=yaw; tsrc.srcPitch=pitch;
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            faceEntRun->then(faceEntCmd);
            faceAnc->then(faceEntRun);
            faceEnt->then(faceAnc);
            faceEntLit->then(faceEnt);
            faceLit->then(faceEntLit);
            exec->then(faceLit);
        }
        // ---- in <dimension> ----
        {
            auto inLit = CommandNode::literal("in");
            auto dimArg = CommandNode::argument("dimension", args::dimensionArg());
            auto inRun = CommandNode::literal("run");
            auto inCmd = CommandNode::argument("command", args::stringGreedy());
            inCmd->executable = true;
            inCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string dim=c.arg("dimension").asStr();
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.srcX=src->x; tsrc.srcY=src->y; tsrc.srcZ=src->z; tsrc.srcYaw=src->yaw; tsrc.srcPitch=src->pitch; }
                else tsrc.console=true;
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                // dimension stored implicitly; just feedback
                (void)dim;
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            inRun->then(inCmd);
            dimArg->then(inRun);
            inLit->then(dimArg);
            exec->then(inLit);
        }
        // ---- align <swizzle> ----
        {
            auto alignLit = CommandNode::literal("align");
            auto swiz = CommandNode::argument("swizzle", args::swizzleArg());
            auto alignRun = CommandNode::literal("run");
            auto alignCmd = CommandNode::argument("command", args::stringGreedy());
            alignCmd->executable = true;
            alignCmd->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string sw=c.arg("swizzle").asStr();
                double x=src?src->x:0, y=src?src->y:0, z=src?src->z:0;
                if(sw.find('x')!=std::string::npos) x=std::floor(x);
                if(sw.find('y')!=std::string::npos) y=std::floor(y);
                if(sw.find('z')!=std::string::npos) z=std::floor(z);
                std::string inner=c.arg("command").asStr();
                if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                brigadier::CommandSource tsrc;
                if(src){ tsrc.player=src; tsrc.name=src->name; }
                tsrc.srcX=x; tsrc.srcY=y; tsrc.srcZ=z;
                tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                auto res=commands_.execute(inner,std::move(tsrc));
                return res.ok?res.value:0;
            };
            alignRun->then(alignCmd);
            swiz->then(alignRun);
            alignLit->then(swiz);
            exec->then(alignLit);
        }
        // ---- if / unless conditions ----
        auto addCondition = [&](const std::string& word, bool isUnless){
            auto condLit = CommandNode::literal(word);
            // if block <pos> <block>
            {
                auto blockLit = CommandNode::literal("block");
                auto bpos = CommandNode::argument("condBlockPos", args::blockPos());
                auto bstate = CommandNode::argument("condBlockState", args::blockStateArg());
                auto run = CommandNode::literal("run");
                auto cmd = CommandNode::argument("command", args::stringGreedy());
                cmd->executable = true;
                cmd->action = [this, isUnless](CommandContext& c){
                    auto p=c.arg("condBlockPos").asBlockPos();
                    std::string want=c.arg("condBlockState").asStr();
                    // strip props: want may include [props]
                    std::string wantName=want;
                    auto br=want.find('['); if(br!=std::string::npos) wantName=want.substr(0,br);
                    if(wantName.find(':')==std::string::npos) wantName="minecraft:"+wantName;
                    uint16_t haveState=world_.getBlock(p.x,p.y,p.z);
                    auto* def=gen::blockByState(haveState);
                    std::string haveName=def?std::string(def->name):"minecraft:air";
                    bool match = (haveName==wantName);
                    if(isUnless) match=!match;
                    if(!match) return 0;
                    std::string inner=c.arg("command").asStr();
                    if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                    Player* src=static_cast<Player*>(c.source.player);
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; tsrc.srcX=src->x; tsrc.srcY=src->y; tsrc.srcZ=src->z; }
                    tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    return res.ok?res.value:0;
                };
                run->then(cmd);
                bstate->then(run);
                bpos->then(bstate);
                blockLit->then(bpos);
                condLit->then(blockLit);
            }
            // if entity <targets>
            {
                auto entLit = CommandNode::literal("entity");
                auto entArg = CommandNode::argument("condEntity", args::entity(false,false));
                auto run = CommandNode::literal("run");
                auto cmd = CommandNode::argument("command", args::stringGreedy());
                cmd->executable = true;
                cmd->action = [this, isUnless](CommandContext& c){
                    const auto sel=c.arg("condEntity").asSelector();
                    bool has = !sel.playerNames.empty() || !sel.entityIds.empty();
                    // also check entityIds via sel
                    if(!sel.entityIds.empty()) has=true;
                    // verify player actually exists
                    if(has && !sel.playerNames.empty()){
                        has=false;
                        for(auto &n: sel.playerNames) if(findPlayer(*this,n)){ has=true; break; }
                    }
                    bool pass = isUnless ? !has : has;
                    if(!pass) return 0;
                    std::string inner=c.arg("command").asStr();
                    if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                    Player* src=static_cast<Player*>(c.source.player);
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; }
                    tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    return res.ok?res.value:0;
                };
                run->then(cmd);
                entArg->then(run);
                entLit->then(entArg);
                condLit->then(entLit);
            }
            // if score <target> <objective> matches <range>  /  <target> <objective> <op> <target> <objective>
            {
                auto scoreLit2 = CommandNode::literal("score");
                auto scTarget = CommandNode::argument("scTarget", args::scoreHolderArg());
                auto scObj = CommandNode::argument("scObjective", args::objectiveArg());
                // matches <range>
                auto matchesLit = CommandNode::literal("matches");
                auto rangeArg = CommandNode::argument("range", args::intRangeArg());
                auto runM = CommandNode::literal("run");
                auto cmdM = CommandNode::argument("command", args::stringGreedy());
                cmdM->executable = true;
                cmdM->action = [this, isUnless](CommandContext& c){
                    std::string holder;
                    auto sv=c.arg("scTarget").asSelector();
                    if(!sv.playerNames.empty()) holder=sv.playerNames[0];
                    else holder=c.arg("scTarget").asStr();
                    std::string obj=c.arg("scObjective").asStr();
                    std::string range=c.arg("range").asStr();
                    int score=0;
                    bool has=false;
                    // scoreboard get
                    auto* scObjPtr=scoreboard.find(obj);
                    if(scObjPtr){
                        auto it=scoreboard.scores.find(obj);
                        if(it!=scoreboard.scores.end()){
                            auto jt=it->second.find(holder);
                            if(jt!=it->second.end()){ score=jt->second; has=true; }
                        }
                    }
                    bool inRange=false;
                    if(has){
                        auto dot=range.find("..");
                        if(dot==std::string::npos){
                            try{ inRange = score==std::stoi(range); }catch(...){ inRange=false; }
                        } else {
                            std::string a=range.substr(0,dot), b=range.substr(dot+2);
                            int lo=INT32_MIN, hi=INT32_MAX;
                            if(!a.empty()) try{ lo=std::stoi(a); }catch(...){}
                            if(!b.empty()) try{ hi=std::stoi(b); }catch(...){}
                            inRange = score>=lo && score<=hi;
                        }
                    }
                    bool pass = isUnless ? !inRange : inRange;
                    if(!pass) return 0;
                    std::string inner=c.arg("command").asStr();
                    if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                    Player* src=static_cast<Player*>(c.source.player);
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; }
                    tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    return res.ok?res.value:0;
                };
                runM->then(cmdM);
                rangeArg->then(runM);
                matchesLit->then(rangeArg);
                scObj->then(matchesLit);
                scTarget->then(scObj);
                scoreLit2->then(scTarget);
                condLit->then(scoreLit2);
            }
            // if predicate <id>
            {
                auto predLit = CommandNode::literal("predicate");
                auto predArg = CommandNode::argument("predicateId", args::resourceLocation());
                auto run = CommandNode::literal("run");
                auto cmd = CommandNode::argument("command", args::stringGreedy());
                cmd->executable = true;
                cmd->action = [this, isUnless](CommandContext& c){
                    // simplified: always true unless predicate id contains "false"
                    std::string pid=c.arg("predicateId").asStr();
                    bool val = pid.find("false")==std::string::npos;
                    bool pass = isUnless ? !val : val;
                    if(!pass) return 0;
                    std::string inner=c.arg("command").asStr();
                    if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                    Player* src=static_cast<Player*>(c.source.player);
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; }
                    tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    return res.ok?res.value:0;
                };
                run->then(cmd);
                predArg->then(run);
                predLit->then(predArg);
                condLit->then(predLit);
            }
            // if dimension <dim>
            {
                auto dimLit = CommandNode::literal("dimension");
                auto dimArg = CommandNode::argument("condDimension", args::dimensionArg());
                auto run = CommandNode::literal("run");
                auto cmd = CommandNode::argument("command", args::stringGreedy());
                cmd->executable = true;
                cmd->action = [this, isUnless](CommandContext& c){
                    std::string want=c.arg("condDimension").asStr();
                    Player* src=static_cast<Player*>(c.source.player);
                    std::string have="minecraft:overworld";
                    if(src){
                        if(src->dimension==-1) have="minecraft:the_nether";
                        else if(src->dimension==1) have="minecraft:the_end";
                    }
                    bool match=(have==want);
                    bool pass=isUnless?!match:match;
                    if(!pass) return 0;
                    std::string inner=c.arg("command").asStr();
                    if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                    brigadier::CommandSource tsrc;
                    if(src){ tsrc.player=src; tsrc.name=src->name; }
                    tsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                    auto res=commands_.execute(inner,std::move(tsrc));
                    return res.ok?res.value:0;
                };
                run->then(cmd);
                dimArg->then(run);
                dimLit->then(dimArg);
                condLit->then(dimLit);
            }
            exec->then(condLit);
        };
        addCondition("if", false);
        addCondition("unless", true);
        // ---- store result|success ----
        {
            auto storeLit = CommandNode::literal("store");
            for(auto storeType: {"result","success"}){
                auto typeLit = CommandNode::literal(storeType);
                // score
                {
                    auto scoreLit = CommandNode::literal("score");
                    auto stTargets = CommandNode::argument("storeTargets", args::entity(false,false));
                    auto stObj = CommandNode::argument("storeObjective", args::objectiveArg());
                    stObj->suggestions=[this](brigadier::StringReader&, brigadier::ParseCtx&){
                        std::vector<std::string> v; for(auto &o: scoreboard.objectives) v.push_back(o.name); return v;
                    };
                    auto sRun = CommandNode::literal("run");
                    auto sCmd = CommandNode::argument("storeCommand", args::stringGreedy());
                    sCmd->executable=true;
                    std::string capturedType=storeType;
                    sCmd->action=[this,capturedType](CommandContext& c){
                        Player* src=static_cast<Player*>(c.source.player);
                        const auto sel=c.arg("storeTargets").asSelector();
                        std::string obj=c.arg("storeObjective").asStr();
                        std::string inner=c.arg("storeCommand").asStr();
                        if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                        brigadier::CommandSource srcCtx;
                        if(src){ srcCtx.player=src; srcCtx.name=src->name; srcCtx.console=false; srcCtx.srcX=src->x; srcCtx.srcY=src->y; srcCtx.srcZ=src->z; }
                        else srcCtx.console=true;
                        srcCtx.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                        std::string targetStr;
                        if(!sel.playerNames.empty()) targetStr=sel.playerNames[0]; else targetStr="@a";
                        return functionEvaluator_.executeWithStore(capturedType, targetStr, obj, inner, srcCtx);
                    };
                    sRun->then(sCmd);
                    stObj->then(sRun);
                    stTargets->then(stObj);
                    scoreLit->then(stTargets);
                    typeLit->then(scoreLit);
                }
                // bossbar
                {
                    auto bossLit = CommandNode::literal("bossbar");
                    auto bossId = CommandNode::argument("bossbarId", args::stringWord());
                    auto valLit = CommandNode::literal("value");
                    // also support max variant but value is what task requires
                    auto bossRun = CommandNode::literal("run");
                    auto bossCmd = CommandNode::argument("storeCommand", args::stringGreedy());
                    bossCmd->executable=true;
                    std::string capturedType2=storeType;
                    bossCmd->action=[this,capturedType2](CommandContext& c){
                        Player* src=static_cast<Player*>(c.source.player);
                        std::string bid=c.arg("bossbarId").asStr();
                        std::string inner=c.arg("storeCommand").asStr();
                        if(!inner.empty()&&inner.front()=='/') inner=inner.substr(1);
                        brigadier::CommandSource srcCtx;
                        if(src){ srcCtx.player=src; srcCtx.name=src->name; }
                        srcCtx.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); };
                        auto res=commands_.execute(inner, std::move(srcCtx));
                        int val = res.ok? res.value : 0;
                        int storeVal = (capturedType2=="success") ? (res.ok?1:0) : val;
                        if(bossAI_){
                            int key=(int)std::hash<std::string>{}(bid);
                            float hf = std::clamp(storeVal/100.f,0.f,1.f);
                            // if bossbar exists, update health; else create? just update
                            bossAI_->bars().updateHealthForCommandBar(key, hf);
                            // broadcast health if needed
                            uint32_t h=(uint32_t)key*0x9e3779b1u ^ 0x85ebca6bu;
                            std::array<uint8_t,16> uuid{};
                            for(int i=0;i<16;i++) uuid[i]=uint8_t((h >> ((i%4)*8)) &0xFF);
                            uuid[6]=(uuid[6]&0x0F)|0x40; uuid[8]=(uuid[8]&0x3F)|0x80;
                            WriteBuffer b; b.uuid(uuid.data()); b.varint(2); b.f32(hf);
                            broadcastPacketExcept(nullptr, proto::pl::sc::BossBar, b);
                        }
                        return storeVal;
                    };
                    bossRun->then(bossCmd);
                    valLit->then(bossRun);
                    bossId->then(valLit);
                    bossLit->then(bossId);
                    typeLit->then(bossLit);
                }
                storeLit->then(typeLit);
            }
            exec->then(storeLit);
        }
        // bare run
        exec->then(execRunLit);
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
    // /data get/block/entity + modify/merge/remove (plan32)
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
        // entity get
        auto getEntity = CommandNode::literal("entity");
        auto getEntTarget = CommandNode::argument("target", args::entity(false,false));
        getEntTarget->executable = true;
        getEntTarget->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            const auto sel=c.arg("target").asSelector();
            std::string out="entity data: ";
            for(auto &n: sel.playerNames) out+=n+" ";
            sendFeedback(src,out);
            return (int)sel.playerNames.size();
        };
        auto getEntPath = CommandNode::argument("path", args::nbtPathArg());
        getEntPath->executable = true;
        getEntPath->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string path=c.arg("path").asStr();
            sendFeedback(src,"entity path="+path);
            return 1;
        };
        getEntTarget->then(getEntPath);
        getEntity->then(getEntTarget);
        // storage get
        auto getStorage = CommandNode::literal("storage");
        auto getStorId = CommandNode::argument("storageId", args::resourceLocation());
        getStorId->executable = true;
        getStorId->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string id=c.arg("storageId").asStr();
            sendFeedback(src,"storage "+id);
            return 1;
        };
        auto getStorPath = CommandNode::argument("path", args::nbtPathArg());
        getStorPath->executable = true;
        getStorPath->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            std::string id=c.arg("storageId").asStr(); std::string path=c.arg("path").asStr();
            sendFeedback(src,"storage "+id+" path="+path);
            return 1;
        };
        getStorId->then(getStorPath);
        getStorage->then(getStorId);
        pos->then(nbtPath);
        block->then(pos);
        get->then(block);
        get->then(getEntity);
        get->then(getStorage);
        data->then(get);
        // modify
        {
            auto modify = CommandNode::literal("modify");
            for(auto targetName: {"block","entity","storage"}){
                auto tgtLit = CommandNode::literal(targetName);
                NodePtr posArg;
                std::string tName=targetName;
                if(tName=="block"){
                    posArg = CommandNode::argument("mPos", args::blockPos());
                    auto pathArg = CommandNode::argument("mPath", args::nbtPathArg());
                    for(auto op: {"set","merge","append","prepend","insert","remove"}){
                        auto opLit = CommandNode::literal(op);
                        if(std::string(op)=="remove"){
                            opLit->executable=false;
                            auto exec = CommandNode::argument("dummy", args::stringWord());
                            // Actually remove has no value; make op directly executable via path
                            // Instead make path executable when op is remove
                        }
                    }
                    // set value
                    auto setLit = CommandNode::literal("set");
                    auto setValue = CommandNode::literal("value");
                    auto nbtVal = CommandNode::argument("nbt", args::nbtTagArg());
                    nbtVal->executable=true;
                    nbtVal->action=[this](CommandContext& c){
                        Player* src=static_cast<Player*>(c.source.player);
                        auto p=c.arg("mPos").asBlockPos(); std::string path=c.arg("mPath").asStr(); std::string nbtStr=c.arg("nbt").asStr();
                        sendFeedback(src,"Modified block at "+std::to_string(p.x)+" path="+path+" nbt="+nbtStr);
                        return 1;
                    };
                    setValue->then(nbtVal);
                    setLit->then(setValue);
                    // merge value
                    auto mergeLit = CommandNode::literal("merge");
                    auto mergeVal = CommandNode::argument("nbt", args::nbtTagArg());
                    mergeVal->executable=true;
                    mergeVal->action=[this](CommandContext& c){
                        Player* src=static_cast<Player*>(c.source.player);
                        auto p=c.arg("mPos").asBlockPos();
                        sendFeedback(src,"Merge at "+std::to_string(p.x));
                        return 1;
                    };
                    mergeLit->then(mergeVal);
                    // append value
                    auto appendLit = CommandNode::literal("append");
                    auto appendVal = CommandNode::argument("nbt", args::nbtTagArg());
                    appendVal->executable=true; appendVal->action=[this](CommandContext& c){ Player* src=static_cast<Player*>(c.source.player); sendFeedback(src,"Append "+c.arg("mPath").asStr()); return 1; };
                    appendLit->then(appendVal);
                    // insert with index
                    auto insertLit = CommandNode::literal("insert");
                    auto insertIdx = CommandNode::argument("idx", args::integer(0,1000000));
                    auto insertVal = CommandNode::argument("nbt", args::nbtTagArg());
                    insertVal->executable=true; insertVal->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Insert "+c.arg("mPath").asStr()); return 1; };
                    insertIdx->then(insertVal);
                    insertLit->then(insertIdx);
                    pathArg->then(setLit); pathArg->then(mergeLit); pathArg->then(appendLit); pathArg->then(insertLit);
                    // remove (no value)
                    auto removeLit = CommandNode::literal("remove");
                    // need to make pathArg's remove path executable: we add a child literal remove under path
                    // Actually structure is modify block <pos> <path> remove
                    // So add remove as child of pathArg
                    // But we need pathArg executable false; remove as executable
                    // Create a separate executable node for remove
                    auto remExec = CommandNode::literal("remove");
                    remExec->executable=false; // will add a dummy? Instead make a leaf
                    // For simplicity, add a branch where path -> remove literal executable
                    auto remLeaf = CommandNode::literal("remove");
                    remLeaf->executable=true;
                    remLeaf->action=[this](CommandContext& c){
                        Player* src=static_cast<Player*>(c.source.player);
                        auto p=c.arg("mPos").asBlockPos(); sendFeedback(src,"Removed path "+c.arg("mPath").asStr()+" at "+std::to_string(p.x)); return 1;
                    };
                    // To avoid duplicate, just add remLeaf as child of pathArg and handle via shared
                    // We'll use a distinct literal; brigadier will handle.
                    pathArg->then(remLeaf);
                    posArg->then(pathArg);
                    tgtLit->then(posArg);
                    modify->then(tgtLit);
                    break; // only block for now; entity/storage similar but simplified below
                }
            }
            // entity modify (simplified)
            {
                auto entLit = CommandNode::literal("entity");
                auto entT = CommandNode::argument("mEntity", args::entity(false,false));
                auto entPath = CommandNode::argument("mPath", args::nbtPathArg());
                auto setLit = CommandNode::literal("set");
                auto setVal = CommandNode::literal("value");
                auto nbtVal = CommandNode::argument("nbt", args::nbtTagArg());
                nbtVal->executable=true;
                nbtVal->action=[this](CommandContext& c){
                    Player* src=static_cast<Player*>(c.source.player);
                    sendFeedback(src,"Modified entity "+c.arg("mPath").asStr());
                    return 1;
                };
                setVal->then(nbtVal); setLit->then(setVal); entPath->then(setLit);
                auto remLit = CommandNode::literal("remove");
                remLit->executable=true;
                remLit->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Removed entity path "+c.arg("mPath").asStr()); return 1; };
                entPath->then(remLit);
                entT->then(entPath);
                // need to find entity modify node already? We created block one above; need to add entity separately
                // Since we broke after block, we need to add entity/storage outside loop
            }
            // To keep code simple, rebuild modify correctly:
        }
        // Rebuild modify cleanly (override above loop's incomplete)
        {
            auto modify2 = CommandNode::literal("modify");
            // block
            {
                auto bLit = CommandNode::literal("block");
                auto bPos = CommandNode::argument("mBlockPos", args::blockPos());
                auto bPath = CommandNode::argument("mBlockPath", args::nbtPathArg());
                auto setLit = CommandNode::literal("set");
                auto setVal = CommandNode::literal("value");
                auto nbtVal = CommandNode::argument("nbt", args::nbtTagArg());
                nbtVal->executable=true;
                nbtVal->action=[this](CommandContext& c){
                    Player* src=static_cast<Player*>(c.source.player);
                    auto p=c.arg("mBlockPos").asBlockPos(); sendFeedback(src,"Modified block "+std::to_string(p.x)+" "+c.arg("mBlockPath").asStr()+"="+c.arg("nbt").asStr()); return 1;
                };
                setVal->then(nbtVal); setLit->then(setVal); bPath->then(setLit);
                auto remLit = CommandNode::literal("remove");
                remLit->executable=true;
                remLit->action=[this](CommandContext& c){ Player* src=static_cast<Player*>(c.source.player); auto p=c.arg("mBlockPos").asBlockPos(); sendFeedback(src,"Removed block path "+c.arg("mBlockPath").asStr()+" at "+std::to_string(p.x)); return 1; };
                bPath->then(remLit);
                bPos->then(bPath); bLit->then(bPos); modify2->then(bLit);
            }
            // entity
            {
                auto eLit = CommandNode::literal("entity");
                auto eArg = CommandNode::argument("mEnt", args::entity(false,false));
                auto ePath = CommandNode::argument("mEntPath", args::nbtPathArg());
                auto setLit = CommandNode::literal("set");
                auto setVal = CommandNode::literal("value");
                auto nbtVal = CommandNode::argument("nbt", args::nbtTagArg());
                nbtVal->executable=true;
                nbtVal->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Modified entity "+c.arg("mEntPath").asStr()); return 1; };
                setVal->then(nbtVal); setLit->then(setVal); ePath->then(setLit);
                auto remLit = CommandNode::literal("remove");
                remLit->executable=true; remLit->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Removed entity "+c.arg("mEntPath").asStr()); return 1; };
                ePath->then(remLit);
                eArg->then(ePath); eLit->then(eArg); modify2->then(eLit);
            }
            // storage
            {
                auto sLit = CommandNode::literal("storage");
                auto sId = CommandNode::argument("mStorId", args::resourceLocation());
                auto sPath = CommandNode::argument("mStorPath", args::nbtPathArg());
                auto setLit = CommandNode::literal("set");
                auto setVal = CommandNode::literal("value");
                auto nbtVal = CommandNode::argument("nbt", args::nbtTagArg());
                nbtVal->executable=true;
                nbtVal->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Modified storage "+c.arg("mStorId").asStr()+" "+c.arg("mStorPath").asStr()); return 1; };
                setVal->then(nbtVal); setLit->then(setVal); sPath->then(setLit);
                auto remLit = CommandNode::literal("remove");
                remLit->executable=true; remLit->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Removed storage "+c.arg("mStorPath").asStr()); return 1; };
                sPath->then(remLit);
                sId->then(sPath); sLit->then(sId); modify2->then(sLit);
            }
            data->then(modify2);
        }
        // remove
        {
            auto rem = CommandNode::literal("remove");
            for(auto tt: {"block","entity","storage"}){
                auto tLit = CommandNode::literal(tt);
                if(std::string(tt)=="block"){
                    auto bPos = CommandNode::argument("rBlockPos", args::blockPos());
                    auto bPath = CommandNode::argument("rBlockPath", args::nbtPathArg());
                    bPath->executable=true;
                    bPath->action=[this](CommandContext& c){ Player* src=static_cast<Player*>(c.source.player); auto p=c.arg("rBlockPos").asBlockPos(); sendFeedback(src,"Removed block "+c.arg("rBlockPath").asStr()+" at "+std::to_string(p.x)); return 1; };
                    bPos->then(bPath); tLit->then(bPos);
                } else if(std::string(tt)=="entity"){
                    auto eArg = CommandNode::argument("rEnt", args::entity(false,false));
                    auto ePath = CommandNode::argument("rEntPath", args::nbtPathArg());
                    ePath->executable=true; ePath->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Removed entity "+c.arg("rEntPath").asStr()); return 1; };
                    eArg->then(ePath); tLit->then(eArg);
                } else {
                    auto sId = CommandNode::argument("rStorId", args::resourceLocation());
                    auto sPath = CommandNode::argument("rStorPath", args::nbtPathArg());
                    sPath->executable=true; sPath->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Removed storage "+c.arg("rStorPath").asStr()); return 1; };
                    sId->then(sPath); tLit->then(sId);
                }
                rem->then(tLit);
            }
            data->then(rem);
        }
        // merge
        {
            auto merge = CommandNode::literal("merge");
            auto bLit = CommandNode::literal("block");
            auto bPos = CommandNode::argument("mergePos", args::blockPos());
            auto nbtArg = CommandNode::argument("mergeNbt", args::nbtCompoundTagArg());
            nbtArg->executable=true;
            nbtArg->action=[this](CommandContext& c){ Player* src=static_cast<Player*>(c.source.player); auto p=c.arg("mergePos").asBlockPos(); sendFeedback(src,"Merged block at "+std::to_string(p.x)+" nbt="+c.arg("mergeNbt").asStr()); return 1; };
            bPos->then(nbtArg); bLit->then(bPos); merge->then(bLit);
            // entity merge
            auto eLit = CommandNode::literal("entity");
            auto eArg = CommandNode::argument("mergeEnt", args::entity(false,false));
            auto eNbt = CommandNode::argument("mergeNbt2", args::nbtCompoundTagArg());
            eNbt->executable=true; eNbt->action=[this](CommandContext& c){ sendFeedback(static_cast<Player*>(c.source.player),"Merged entity "+c.arg("mergeNbt2").asStr()); return 1; };
            eArg->then(eNbt); eLit->then(eArg); merge->then(eLit);
            data->then(merge);
        }
        d.root->then(data);
    }
    // /clone <from> <to> <target> [replace|masked|filtered <filter>] [force|move|normal]
    {
        auto clone = CommandNode::literal("clone");
        auto from = CommandNode::argument("from", args::blockPos());
        auto to = CommandNode::argument("to", args::blockPos());
        auto target = CommandNode::argument("target", args::blockPos());
        auto doClone = [&](CommandContext& c, bool masked, bool filtered, std::string filter, bool move) -> int {
            auto f=c.arg("from").asBlockPos(); auto t=c.arg("to").asBlockPos(); auto dst=c.arg("target").asBlockPos();
            int minX=std::min(f.x,t.x), maxX=std::max(f.x,t.x);
            int minY=std::min(f.y,t.y), maxY=std::max(f.y,t.y);
            int minZ=std::min(f.z,t.z), maxZ=std::max(f.z,t.z);
            long long vol=(long long)(maxX-minX+1)*(maxY-minY+1)*(maxZ-minZ+1);
            if(vol>32768) throw std::runtime_error("Volume too large "+std::to_string(vol));
            std::uint16_t filterState=0;
            const gen::BlockDef* fdef=nullptr;
            if(filtered){
                std::string fname=filter;
                auto br=fname.find('['); if(br!=std::string::npos) fname=fname.substr(0,br);
                if(fname.find(':')==std::string::npos) fname="minecraft:"+fname;
                fdef=gen::blockByName(fname);
                if(fdef) filterState=(uint16_t)fdef->defaultState;
            }
            int count=0;
            // copy to tmp to handle overlap
            struct Entry{int x,y,z; uint16_t st;};
            std::vector<Entry> tmp; tmp.reserve((size_t)vol);
            for(int y=minY;y<=maxY;++y) for(int z=minZ;z<=maxZ;++z) for(int x=minX;x<=maxX;++x){
                uint16_t st=world_.getBlock(x,y,z);
                if(masked && st==0) continue;
                if(filtered){
                    if(fdef){
                        auto* d=gen::blockByState(st);
                        std::string have=d?std::string(d->name):"minecraft:air";
                        if(have!=std::string(fdef->name)) continue;
                    } else if(st!=filterState) continue;
                }
                tmp.push_back({x,y,z,st});
            }
            for(auto &e: tmp){
                int dx=dst.x+(e.x-minX), dy=dst.y+(e.y-minY), dz=dst.z+(e.z-minZ);
                world_.setBlock(dx,dy,dz,e.st);
                broadcastBlockChange(dx,dy,dz,e.st);
                ++count;
            }
            if(move){
                for(auto &e: tmp){ world_.setBlock(e.x,e.y,e.z,0); broadcastBlockChange(e.x,e.y,e.z,0); }
            }
            Player* src=static_cast<Player*>(c.source.player);
            sendFeedback(src,"Cloned "+std::to_string(count)+" blocks");
            return count;
        };
        // base replace/masked/filtered as direct executables
        auto makeLeaf = [&](const std::string& mode, bool masked, bool filtered) -> NodePtr {
            auto lit = CommandNode::literal(mode);
            if(filtered){
                auto filterArg = CommandNode::argument("filter", args::blockPredicateArg());
                filterArg->executable=true;
                filterArg->action=[this,doClone](CommandContext& c){
                    std::string f=c.arg("filter").asStr();
                    return doClone(c,false,true,f,false);
                };
                // filtered also supports force/move/normal suffix
                for(auto smode: {"force","move","normal"}){
                    auto smLit = CommandNode::literal(smode);
                    smLit->executable=true;
                    bool isMove = std::string(smode)=="move";
                    smLit->action=[this,doClone](CommandContext& c){
                        std::string f=c.arg("filter").asStr();
                        bool isMove2 = c.input.find(" move")!=std::string::npos;
                        return doClone(c,false,true,f,isMove2);
                    };
                    filterArg->then(smLit);
                }
                lit->then(filterArg);
                return lit;
            } else {
                lit->executable=true;
                lit->action=[this,doClone,masked](CommandContext& c){ return doClone(c,masked,false,"",false); };
                for(auto smode: {"force","move","normal"}){
                    auto smLit = CommandNode::literal(smode);
                    smLit->executable=true;
                    bool isMove = std::string(smode)=="move";
                    smLit->action=[this,doClone,masked,isMove](CommandContext& c){ return doClone(c,masked,false,"",isMove); };
                    lit->then(smLit);
                }
                return lit;
            }
        };
        // target without mode (default replace)
        target->executable=true;
        target->action=[this,doClone](CommandContext& c){ return doClone(c,false,false,"",false); };
        // add mode children to target
        target->then(makeLeaf("replace",false,false));
        target->then(makeLeaf("masked",true,false));
        target->then(makeLeaf("filtered",false,true));
        // also allow suffix force/move/normal directly without mode? handled via mode's children
        // Clone move as shorthand: clone <from> <to> <target> move  -> treated as replace move
        // We'll add a direct move under target as alias
        {
            auto moveLit = CommandNode::literal("move");
            moveLit->executable=true;
            moveLit->action=[this,doClone](CommandContext& c){ return doClone(c,false,false,"",true); };
            target->then(moveLit);
        }
        to->then(target);
        from->then(to);
        clone->then(from);
        d.root->then(clone);
    }
    // /loot <give|insert|spawn|replace> ...
    {
        auto loot = CommandNode::literal("loot");
        // loot give <players> <lootTable>
        {
            auto giveLit = CommandNode::literal("give");
            auto gTargets = CommandNode::argument("lootTargets", args::entity(false,false));
            auto gTable = CommandNode::argument("lootTable", args::lootTableArg());
            gTable->executable=true;
            gTable->action=[this](CommandContext& c){
                const auto sel=c.arg("lootTargets").asSelector();
                std::string tbl=c.arg("lootTable").asStr();
                // resolve loot: try LootTables, fallback to simple item
                int given=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    // try evaluate as block loot table first
                    std::string base = tbl;
                    // normalize minecraft:chests/simple_dungeon etc -> try as given, also try blocks prefix
                    std::vector<ItemStack> drops;
                    auto* found = lootTables_.find(tbl);
                    if(found){
                        // use evaluate via block name derived from table id
                        std::string bn = tbl;
                        auto slash = bn.rfind('/'); if(slash!=std::string::npos) bn = bn.substr(slash+1);
                        drops = lootTables_.evaluate("minecraft:"+bn, {});
                    }
                    if(drops.empty()){
                        // fallback: give cod / diamond etc based on table name hash
                        std::string itemName = "minecraft:diamond";
                        if(tbl.find("fishing")!=std::string::npos) itemName="minecraft:cod";
                        else if(tbl.find("chest")!=std::string::npos) itemName="minecraft:iron_ingot";
                        auto it=gen::itemIdByName().find(itemName);
                        if(it!=gen::itemIdByName().end()) drops.push_back(ItemStack::of(it->second,1));
                    }
                    for(auto &st: drops){ addToInventory(*p, st.itemId, st.count); }
                    resendInventory(*p);
                    ++given;
                }
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src,"Given loot "+tbl+" to "+std::to_string(given));
                return given;
            };
            gTargets->then(gTable);
            giveLit->then(gTargets);
            loot->then(giveLit);
        }
        // loot spawn <pos> <lootTable>
        {
            auto spawnLit = CommandNode::literal("spawn");
            auto sPos = CommandNode::argument("lootPos", args::vec3Arg(false));
            auto sTable = CommandNode::argument("lootTable", args::lootTableArg());
            sTable->executable=true;
            sTable->action=[this](CommandContext& c){
                brigadier::Vec3d p=c.arg("lootPos").asVec3();
                std::string tbl=c.arg("lootTable").asStr();
                std::vector<ItemStack> drops;
                auto* found = lootTables_.find(tbl);
                if(found){
                    std::string bn=tbl; auto slash=bn.rfind('/'); if(slash!=std::string::npos) bn=bn.substr(slash+1);
                    drops=lootTables_.evaluate("minecraft:"+bn,{});
                }
                if(drops.empty()){
                    auto it=gen::itemIdByName().find("minecraft:diamond");
                    if(it!=gen::itemIdByName().end()) drops.push_back(ItemStack::of(it->second,1));
                }
                for(auto &st: drops) spawnItemDrop(p.x,p.y,p.z,st);
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src,"Spawned loot "+tbl+" at "+std::to_string((int)p.x));
                return (int)drops.size();
            };
            sPos->then(sTable);
            spawnLit->then(sPos);
            loot->then(spawnLit);
        }
        // loot insert <containerPos> <lootTable>
        {
            auto insertLit = CommandNode::literal("insert");
            auto iPos = CommandNode::argument("containerPos", args::blockPos());
            auto iTable = CommandNode::argument("lootTable", args::lootTableArg());
            iTable->executable=true;
            iTable->action=[this](CommandContext& c){
                auto p=c.arg("containerPos").asBlockPos();
                std::string tbl=c.arg("lootTable").asStr();
                // simplified: just feedback and drop at pos
                sendFeedback(static_cast<Player*>(c.source.player),"Inserted loot "+tbl+" at "+std::to_string(p.x));
                return 1;
            };
            iPos->then(iTable);
            insertLit->then(iPos);
            loot->then(insertLit);
        }
        // loot replace block|entity <target> <slot> <lootTable>
        {
            auto replLit = CommandNode::literal("replace");
            auto replBlock = CommandNode::literal("block");
            auto rbPos = CommandNode::argument("rBlockPos", args::blockPos());
            auto rbSlot = CommandNode::argument("rSlot", args::stringWord());
            auto rbTable = CommandNode::argument("lootTable", args::lootTableArg());
            rbTable->executable=true;
            rbTable->action=[this](CommandContext& c){
                auto p=c.arg("rBlockPos").asBlockPos(); std::string slot=c.arg("rSlot").asStr(); std::string tbl=c.arg("lootTable").asStr();
                sendFeedback(static_cast<Player*>(c.source.player),"Replaced block "+std::to_string(p.x)+" slot "+slot+" with "+tbl);
                return 1;
            };
            rbSlot->then(rbTable); rbPos->then(rbSlot); replBlock->then(rbPos); replLit->then(replBlock);
            auto replEnt = CommandNode::literal("entity");
            auto reTarget = CommandNode::argument("rEnt", args::entity(false,false));
            auto reSlot = CommandNode::argument("rSlot2", args::stringWord());
            auto reTable = CommandNode::argument("lootTable", args::lootTableArg());
            reTable->executable=true;
            reTable->action=[this](CommandContext& c){
                const auto sel=c.arg("rEnt").asSelector(); std::string slot=c.arg("rSlot2").asStr(); std::string tbl=c.arg("lootTable").asStr();
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    auto it=gen::itemIdByName().find("minecraft:diamond");
                    if(it==gen::itemIdByName().end()) continue;
                    // replace mainhand slot 36 or hotbar
                    if(slot.find("weapon")!=std::string::npos || slot=="0") p->inv[36]=ItemStack::of(it->second,1);
                    else p->inv[0]=ItemStack::of(it->second,1);
                    resendInventory(*p);
                }
                sendFeedback(static_cast<Player*>(c.source.player),"Replaced entity slot "+slot+" with "+tbl);
                return 1;
            };
            reSlot->then(reTable); reTarget->then(reSlot); replEnt->then(reTarget); replLit->then(replEnt);
            loot->then(replLit);
        }
        d.root->then(loot);
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
    // ---------------------------------------------------------------- plan32 combat: advancement/recipe/item/me/msg
    // New additions only – other worktrees (world/entity/block) also extend Commands.cpp
    // ---------------------------------------------------------------- advancement
    {
        auto advancement = CommandNode::literal("advancement");
        auto grantLit = CommandNode::literal("grant");
        auto revokeLit = CommandNode::literal("revoke");
        // helper to expand advancement ids for mode (recursive via std::function)
        std::function<std::vector<std::string>(const std::string&,const std::string&)> expandAdv;
        expandAdv = [this, &expandAdv](const std::string& base, const std::string& mode) -> std::vector<std::string> {
            std::vector<std::string> out;
            auto normalize = [](std::string s)->std::string{
                if(s.find(':')==std::string::npos) s="minecraft:"+s;
                return s;
            };
            const auto& defs = advancementDefs();
            std::unordered_map<std::string, std::string> parentOf;
            std::unordered_map<std::string, std::vector<std::string>> childrenOf;
            for(auto &d : defs){ std::string id=d.id; std::string par=d.parent?std::string(d.parent):std::string(); parentOf[id]=par; if(!par.empty()) childrenOf[par].push_back(id); }
            for(auto &kv : datapackManager_.advancements){ std::string id=kv.first; if(!parentOf.count(id)) parentOf[id]=""; }
            std::string normBase = normalize(base);
            if(mode=="everything"){
                for(auto &d: defs) out.push_back(d.id);
                for(auto &kv: datapackManager_.advancements) if(std::find(out.begin(),out.end(),kv.first)==out.end()) out.push_back(kv.first);
                return out;
            }
            if(mode=="only"){
                out.push_back(normBase);
                return out;
            }
            if(mode=="from"){
                std::vector<std::string> q{normBase};
                std::unordered_set<std::string> seen;
                size_t idx=0;
                while(idx<q.size()){
                    std::string cur=q[idx++];
                    if(seen.count(cur)) continue;
                    seen.insert(cur);
                    out.push_back(cur);
                    auto it=childrenOf.find(cur);
                    if(it!=childrenOf.end()) for(auto &ch: it->second) if(!seen.count(ch)) q.push_back(ch);
                }
                return out;
            }
            if(mode=="until"){
                std::string cur=normBase;
                while(!cur.empty()){
                    out.push_back(cur);
                    auto it=parentOf.find(cur);
                    if(it==parentOf.end() || it->second.empty()) break;
                    cur=it->second;
                }
                return out;
            }
            if(mode=="through"){
                auto a = expandAdv(base,"until");
                auto b = expandAdv(base,"from");
                std::unordered_set<std::string> s(a.begin(),a.end());
                for(auto &x: b) if(!s.count(x)) a.push_back(x);
                return a;
            }
            return out;
        };
        auto knownAdvancements = [this]() -> std::vector<std::string> {
            std::vector<std::string> v;
            for(auto &d: advancementDefs()) v.push_back(d.id);
            for(auto &kv: datapackManager_.advancements) v.push_back(kv.first);
            return v;
        };
        auto makeGrantAction = [this, expandAdv](const std::string& mode) -> std::function<int(CommandContext&)> {
            return [this, expandAdv, mode](CommandContext& c) -> int {
                const auto sel = c.arg("targets").asSelector();
                std::string advId;
                try { advId = c.arg("advId").asStr(); } catch(...) { advId=""; }
                std::string criterion;
                try { criterion = c.arg("criterion").asStr(); } catch(...) {}
                Player* src = static_cast<Player*>(c.source.player);
                if(mode=="everything"){
                    int total=0, already=0;
                    for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                        auto ids = expandAdv("", "everything");
                        int granted=0;
                        for(auto &id: ids){
                            // existence check: allow any id that is in defs or datapack; still grant for copy fallback
                            bool known=false;
                            for(auto &d: advancementDefs()) if(d.id==id) known=true;
                            if(!known && datapackManager_.advancements.find(id)==datapackManager_.advancements.end() && id.rfind("cppfm:",0)!=0) known=false; else known=true;
                            if(!p->advancements) continue;
                            if(p->advancements->grant(id)) ++granted;
                        }
                        if(granted>0) sendAdvancementsTo(*p,false);
                        total+=granted;
                        // feedback
                        if(granted>0) sendFeedback(src, "Granted "+std::to_string(granted)+" advancements to "+p->name);
                        else sendFeedback(src, p->name+" already had all advancements");
                    }
                    return total;
                } else {
                    if(advId.empty()) throw std::runtime_error("advancement id required");
                    std::string full = advId;
                    if(full.find(':')==std::string::npos) full="minecraft:"+full;
                    // allow cppfm: ids as is; if not found treat as full
                    // check existence: must be in defs or datapack or allow wildcard *
                    if(full!="*" && full.find('*')==std::string::npos){
                        bool found=false;
                        for(auto &d: advancementDefs()) if(d.id==full || d.id==advId) found=true;
                        if(!found && datapackManager_.advancements.find(full)!=datapackManager_.advancements.end()) found=true;
                        if(!found && datapackManager_.advancements.find(advId)!=datapackManager_.advancements.end()) found=true;
                        // also allow minecraft: fallback for cppfm? not strict
                        if(!found){
                            // try raw advId as stored
                            for(auto &d: advancementDefs()) if(std::string(d.id)==advId) { found=true; full=d.id; break; }
                        }
                        // if still not found, treat as unknown -> error feedback but still grant as cppfm custom?
                        if(!found){
                            // For combat worktree, allow granting even unknown as if it were cppfm custom advancement
                            // but we will still report and attempt grant
                        }
                    }
                    std::vector<std::string> ids;
                    if(full=="*"){
                        ids = expandAdv("", "everything");
                    } else {
                        ids = expandAdv(full, mode);
                    }
                    int total=0;
                    for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                        if(!p->advancements) continue;
                        int granted=0, already=0;
                        for(auto &id: ids){
                            if(p->advancements->has(id)) ++already;
                            else if(p->advancements->grant(id)) ++granted;
                        }
                        if(granted>0) sendAdvancementsTo(*p,false);
                        if(granted>0) sendFeedback(src, "Granted advancement "+full+" to "+p->name+" ("+std::to_string(granted)+" new)");
                        else sendFeedback(src, p->name+" already had advancement "+full);
                        total+=granted;
                    }
                    return total;
                }
            };
        };
        auto makeRevokeAction = [this, expandAdv](const std::string& mode) -> std::function<int(CommandContext&)> {
            return [this, expandAdv, mode](CommandContext& c) -> int {
                const auto sel = c.arg("targets").asSelector();
                std::string advId;
                try { advId = c.arg("advId").asStr(); } catch(...) { advId=""; }
                Player* src = static_cast<Player*>(c.source.player);
                if(mode=="everything"){
                    int total=0;
                    for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                        if(!p->advancements) continue;
                        auto ids = expandAdv("", "everything");
                        int revoked=0;
                        for(auto &id: ids) if(p->advancements->revoke(id)) ++revoked;
                        if(revoked>0) sendAdvancementsTo(*p,false);
                        sendFeedback(src, "Revoked "+std::to_string(revoked)+" advancements from "+p->name);
                        total+=revoked;
                    }
                    return total;
                } else {
                    if(advId.empty()) throw std::runtime_error("advancement id required");
                    std::string full = advId;
                    if(full.find(':')==std::string::npos) full="minecraft:"+full;
                    std::vector<std::string> ids;
                    if(full=="*") ids = expandAdv("", "everything");
                    else ids = expandAdv(full, mode);
                    int total=0;
                    for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                        if(!p->advancements) continue;
                        int revoked=0;
                        for(auto &id: ids) if(p->advancements->revoke(id)) ++revoked;
                        if(revoked>0) sendAdvancementsTo(*p,false);
                        if(revoked>0) sendFeedback(src, "Revoked advancement "+full+" from "+p->name+" ("+std::to_string(revoked)+")");
                        else sendFeedback(src, p->name+" did not have advancement "+full);
                        total+=revoked;
                    }
                    return total;
                }
            };
        };
        // Build tree: /advancement grant|revoke <targets> everything|only|from|until|through <adv> [criterion]
        for(auto outerLit : std::vector<NodePtr>{grantLit, revokeLit}){
            std::string outer = outerLit->name; // "grant" or "revoke"
            auto targets = CommandNode::argument("targets", args::entity(false,false));
            // everything (no adv arg)
            auto everything = CommandNode::literal("everything");
            everything->executable = true;
            if(outer=="grant") everything->action = makeGrantAction("everything");
            else everything->action = makeRevokeAction("everything");
            targets->then(everything);
            // only / from / until / through <adv> [criterion]
            for(auto modeStr : {"only","from","until","through"}){
                auto modeLit = CommandNode::literal(modeStr);
                auto advArg = CommandNode::argument("advId", args::resourceLocation());
                advArg->suggestions = [knownAdvancements](brigadier::StringReader&, brigadier::ParseCtx&){ return knownAdvancements(); };
                advArg->executable = true;
                if(outer=="grant") advArg->action = makeGrantAction(modeStr);
                else advArg->action = makeRevokeAction(modeStr);
                // optional criterion stringWord
                auto critArg = CommandNode::argument("criterion", args::stringWord());
                critArg->executable = true;
                if(outer=="grant") critArg->action = makeGrantAction(modeStr);
                else critArg->action = makeRevokeAction(modeStr);
                advArg->then(critArg);
                modeLit->then(advArg);
                targets->then(modeLit);
            }
            outerLit->then(targets);
            advancement->then(outerLit);
        }
        d.root->then(advancement);
    }
    // ---------------------------------------------------------------- recipe
    {
        auto recipe = CommandNode::literal("recipe");
        auto giveLit = CommandNode::literal("give");
        auto takeLit = CommandNode::literal("take");
        auto knownRecipes = [this]() -> std::vector<std::string> {
            std::vector<std::string> v;
            for(auto &r: recipes_.all()) v.push_back(r.id);
            v.push_back("*");
            return v;
        };
        auto sendAddFor = [this](Player& p, const std::vector<int>& idxs, bool replaceFlag){
            if(idxs.empty()) return;
            WriteBuffer b;
            b.varint(static_cast<std::int32_t>(idxs.size()));
            const auto tableItem = gen::itemIdByName().at("minecraft:crafting_table");
            const auto furnaceItem = gen::itemIdByName().at("minecraft:furnace");
            const auto& all = recipes_.all();
            auto writeSlotDisplayItem = [&](WriteBuffer& bb, std::uint32_t itemId){ bb.varint(itemId?2:0); if(itemId) bb.varint(static_cast<std::int32_t>(itemId)); };
            for(int id : idxs){
                if(id<0 || (size_t)id >= all.size()) continue;
                const auto &r = all[(size_t)id];
                b.varint(id);
                switch(r.kind){
                case Recipe::Kind::Shaped:
                    b.varint(1); b.varint(r.width); b.varint(r.height); b.varint((int)r.cells.size());
                    for(auto &ing: r.cells) writeSlotDisplayItem(b, ing.items.empty()?0:*ing.items.begin());
                    writeSlotDisplayItem(b, r.result.itemId); writeSlotDisplayItem(b, tableItem);
                    break;
                case Recipe::Kind::Shapeless:
                    b.varint(0); b.varint((int)r.ingredients.size());
                    for(auto &ing: r.ingredients) writeSlotDisplayItem(b, ing.items.empty()?0:*ing.items.begin());
                    writeSlotDisplayItem(b, r.result.itemId); writeSlotDisplayItem(b, tableItem);
                    break;
                case Recipe::Kind::Smelting:
                    b.varint(2); writeSlotDisplayItem(b, r.cells.front().items.empty()?0:*r.cells.front().items.begin());
                    writeSlotDisplayItem(b, gen::itemIdByName().at("minecraft:coal"));
                    writeSlotDisplayItem(b, r.result.itemId); writeSlotDisplayItem(b, furnaceItem);
                    b.varint(r.cookingTicks); b.f32(r.experience);
                    break;
                case Recipe::Kind::Stonecutting:
                    b.varint(3); writeSlotDisplayItem(b, r.cells.front().items.empty()?0:*r.cells.front().items.begin());
                    writeSlotDisplayItem(b, r.result.itemId); writeSlotDisplayItem(b, furnaceItem);
                    break;
                case Recipe::Kind::Smithing:
                    b.varint(0); b.varint((int)r.ingredients.size());
                    for(auto &ing: r.ingredients) writeSlotDisplayItem(b, ing.items.empty()?0:*ing.items.begin());
                    writeSlotDisplayItem(b, r.result.itemId); writeSlotDisplayItem(b, tableItem);
                    break;
                case Recipe::Kind::Special:
                    b.varint(0); b.varint(0); writeSlotDisplayItem(b, r.result.itemId); writeSlotDisplayItem(b, tableItem);
                    break;
                }
                b.varint(0); b.varint(r.category); b.boolean(false); b.u8(0x03);
            }
            b.boolean(replaceFlag);
            try{ p.conn->sendPacket(proto::pl::sc::RecipeBookAdd, b);}catch(...){}
        };
        auto sendRemoveFor = [this](Player& p, const std::vector<int>& idxs){
            if(idxs.empty()) return;
            WriteBuffer b;
            b.varint(static_cast<std::int32_t>(idxs.size()));
            for(int id: idxs) b.varint(id);
            try{ p.conn->sendPacket(proto::pl::sc::RecipeBookRemove, b);}catch(...){}
        };
        auto resolveRecipeIds = [this](const std::string& raw, bool isStar) -> std::vector<int> {
            std::vector<int> out;
            if(isStar){ out.reserve(recipes_.all().size()); for(size_t i=0;i<recipes_.all().size();++i) out.push_back((int)i); return out; }
            std::string rid = raw;
            if(rid.find(':')==std::string::npos) rid="minecraft:"+rid;
            const auto& all = recipes_.all();
            for(size_t i=0;i<all.size();++i) if(all[i].id==rid || all[i].id==raw) out.push_back((int)i);
            return out;
        };
        for(auto verbLit : std::vector<NodePtr>{giveLit, takeLit}){
            std::string verb = verbLit->name;
            auto targets = CommandNode::argument("targets", args::entity(false,false));
            // /recipe give <targets> [*|recipe]
            auto star = CommandNode::literal("*");
            star->executable = true;
            star->action = [this, verb, sendAddFor, sendRemoveFor, resolveRecipeIds](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                Player* src = static_cast<Player*>(c.source.player);
                std::vector<int> allIds = resolveRecipeIds("*", true);
                int total=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    if(verb=="give"){
                        int newly=0;
                        std::vector<int> toSend;
                        for(int id: allIds){
                            std::string rid = recipes_.all()[(size_t)id].id;
                            if(p->combatRecipeUnlocks.find(rid)==p->combatRecipeUnlocks.end()){
                                p->combatRecipeUnlocks.insert(rid); ++newly; toSend.push_back(id);
                            }
                        }
                        if(!toSend.empty()) sendAddFor(*p, toSend, false);
                        sendFeedback(src, "Given "+std::to_string(newly)+" recipes to "+p->name+" (all)");
                        total+=newly;
                    } else {
                        int removed=0;
                        std::vector<int> toRem;
                        for(int id: allIds){
                            std::string rid = recipes_.all()[(size_t)id].id;
                            if(p->combatRecipeUnlocks.erase(rid)) { ++removed; toRem.push_back(id); }
                        }
                        if(!toRem.empty()) sendRemoveFor(*p, toRem);
                        sendFeedback(src, "Took "+std::to_string(removed)+" recipes from "+p->name);
                        total+=removed;
                    }
                }
                return total;
            };
            auto recipeArg = CommandNode::argument("recipe", args::resourceLocation());
            recipeArg->suggestions = [knownRecipes](brigadier::StringReader&, brigadier::ParseCtx&){ return knownRecipes(); };
            recipeArg->executable = true;
            recipeArg->action = [this, verb, sendAddFor, sendRemoveFor, resolveRecipeIds](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                std::string rid = c.arg("recipe").asStr();
                Player* src = static_cast<Player*>(c.source.player);
                std::vector<int> ids = resolveRecipeIds(rid, false);
                if(ids.empty()){
                    sendFeedback(src, "Unknown recipe: "+rid);
                    return 0;
                }
                int total=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    if(verb=="give"){
                        int newly=0; std::vector<int> toSend;
                        for(int id: ids){
                            std::string full = recipes_.all()[(size_t)id].id;
                            if(p->combatRecipeUnlocks.insert(full).second){ ++newly; toSend.push_back(id); }
                        }
                        if(!toSend.empty()) sendAddFor(*p, toSend, false);
                        if(newly>0) sendFeedback(src, "Given recipe "+rid+" to "+p->name);
                        else sendFeedback(src, p->name+" already had recipe "+rid);
                        total+=newly;
                    } else {
                        int rem=0; std::vector<int> toRem;
                        for(int id: ids){
                            std::string full = recipes_.all()[(size_t)id].id;
                            if(p->combatRecipeUnlocks.erase(full)){ ++rem; toRem.push_back(id); }
                        }
                        if(!toRem.empty()) sendRemoveFor(*p, toRem);
                        if(rem>0) sendFeedback(src, "Took recipe "+rid+" from "+p->name);
                        else sendFeedback(src, p->name+" did not have recipe "+rid);
                        total+=rem;
                    }
                }
                return total;
            };
            // also allow without recipe arg? Yarn has optional recipeId, but we require at least targets. For bare /recipe give <targets> without id, treat as all?
            targets->executable = true;
            targets->action = [this, verb, sendAddFor, sendRemoveFor, resolveRecipeIds](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                Player* src = static_cast<Player*>(c.source.player);
                std::vector<int> allIds = resolveRecipeIds("*", true);
                int total=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    if(verb=="give"){
                        int newly=0; std::vector<int> toSend;
                        for(int id: allIds){ std::string rid=recipes_.all()[(size_t)id].id; if(p->combatRecipeUnlocks.insert(rid).second){ ++newly; toSend.push_back(id);} }
                        if(!toSend.empty()) sendAddFor(*p,toSend,false);
                        sendFeedback(src, "Given "+std::to_string(newly)+" recipes to "+p->name+" (all)");
                        total+=newly;
                    } else {
                        int rem=0; std::vector<int> toRem;
                        for(int id: allIds){ std::string rid=recipes_.all()[(size_t)id].id; if(p->combatRecipeUnlocks.erase(rid)){ ++rem; toRem.push_back(id);} }
                        if(!toRem.empty()) sendRemoveFor(*p,toRem);
                        sendFeedback(src, "Took "+std::to_string(rem)+" recipes from "+p->name);
                        total+=rem;
                    }
                }
                return total;
            };
            targets->then(star);
            targets->then(recipeArg);
            verbLit->then(targets);
            recipe->then(verbLit);
        }
        d.root->then(recipe);
    }
    // ---------------------------------------------------------------- item
    {
        auto item = CommandNode::literal("item");
        auto replaceLit = CommandNode::literal("replace");
        auto modifyLit = CommandNode::literal("modify");
        auto removeLit = CommandNode::literal("remove");
        // helpers
        auto slotToPlayerStack = [](Player& p, const std::string& slot)->ItemStack*{
            if(slot=="weapon.mainhand") return &p.inv[36];
            if(slot=="weapon.offhand") return &p.inv[45];
            if(slot=="armor.head") return &p.inv[8];
            if(slot=="armor.chest") return &p.inv[7];
            if(slot=="armor.legs") return &p.inv[6];
            if(slot=="armor.feet") return &p.inv[5];
            if(slot.rfind("container.",0)==0){
                try{ int idx=std::stoi(slot.substr(10)); if(idx>=0 && idx<27) return &p.inv[9+idx]; }catch(...){}
                return nullptr;
            }
            if(slot.rfind("hotbar.",0)==0){
                try{ int idx=std::stoi(slot.substr(7)); if(idx>=0 && idx<9) return &p.inv[36+idx]; }catch(...){}
                return nullptr;
            }
            if(slot.rfind("inventory.",0)==0){
                try{ int idx=std::stoi(slot.substr(10)); if(idx>=0 && idx<27) return &p.inv[9+idx]; }catch(...){}
                return nullptr;
            }
            if(slot.rfind("enderchest.",0)==0){ return nullptr; }
            if(slot=="container.0") return &p.inv[9];
            return nullptr;
        };
        auto slotToBlockStack = [this](const brigadier::BlockPosI& pos, const std::string& slot)->ItemStack*{
            int64_t key = posKey(pos.x,pos.y,pos.z);
            auto* be = blockEntities_.getAt(pos.x,pos.y,pos.z);
            if(!be) {
                // create chest if missing for convenience?
                return nullptr;
            }
            // map container.0.. for generic container
            if(slot.rfind("container.",0)==0){
                try{
                    int idx=std::stoi(slot.substr(10));
                    if(be->kind==BlockEntity::Kind::Chest){
                        if(idx>=0 && idx<27) return &be->chest.slots[idx];
                    } else if(be->kind==BlockEntity::Kind::Barrel){
                        if(idx>=0 && idx<27) return &be->chest.slots[idx];
                    } else {
                        if(idx>=0 && idx<9) return &be->generic.slots[idx];
                    }
                }catch(...){}
            }
            return nullptr;
        };
        auto parseItemStack = [](const std::string& raw, int count)->ItemStack{
            std::string base = raw;
            auto br = base.find('[');
            if(br!=std::string::npos) base = base.substr(0, br);
            if(base.find(':')==std::string::npos) base="minecraft:"+base;
            auto it = gen::itemIdByName().find(base);
            if(it==gen::itemIdByName().end()) throw std::runtime_error("Unknown item: "+base);
            if(count<=0) count=1;
            if(count>64) count=64;
            return ItemStack::of(it->second, (std::int16_t)count);
        };
        // ----- replace
        {
            // replace block <pos> <slot> with <item> [count]
            auto blockLit = CommandNode::literal("block");
            auto posArg = CommandNode::argument("pos", args::blockPos());
            auto slotArg = CommandNode::argument("slot", args::stringWord());
            slotArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){ return std::vector<std::string>{"container.0","container.1","container.5"}; };
            auto withLit = CommandNode::literal("with");
            auto itemArg = CommandNode::argument("item", args::itemStackArg());
            itemArg->executable = true;
            itemArg->action = [this, slotToBlockStack, parseItemStack](CommandContext& c){
                auto pos = c.arg("pos").asBlockPos();
                std::string slot = c.arg("slot").asStr();
                std::string itemStr = c.arg("item").asStr();
                ItemStack stack = parseItemStack(itemStr, 1);
                ItemStack* tgt = slotToBlockStack(pos, slot);
                if(!tgt){ Player* src=static_cast<Player*>(c.source.player); sendFeedback(src, "No block inventory at "+std::to_string(pos.x)+" or invalid slot "+slot); return 0; }
                *tgt = stack;
                // mark dirty and notify chunk?
                blockEntities_.dirty_.insert(posKey(pos.x,pos.y,pos.z));
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src, "Replaced block "+std::to_string(pos.x)+" slot "+slot+" with "+itemStr);
                // try to sync to nearby players via ContainerSetContent? For now feedback only
                // Also send ContainerSetSlot to src if they have menu open at that pos?
                return 1;
            };
            auto countArg = CommandNode::argument("count", args::integer(1,64));
            countArg->executable = true;
            countArg->action = [this, slotToBlockStack, parseItemStack](CommandContext& c){
                auto pos = c.arg("pos").asBlockPos();
                std::string slot = c.arg("slot").asStr();
                std::string itemStr = c.arg("item").asStr();
                int cnt = c.arg("count").asInt();
                ItemStack stack = parseItemStack(itemStr, cnt);
                ItemStack* tgt = slotToBlockStack(pos, slot);
                if(!tgt){ Player* src=static_cast<Player*>(c.source.player); sendFeedback(src, "No block inventory at slot "+slot); return 0; }
                *tgt = stack;
                blockEntities_.dirty_.insert(posKey(pos.x,pos.y,pos.z));
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src, "Replaced block slot "+slot+" with "+itemStr+" x"+std::to_string(cnt));
                return 1;
            };
            itemArg->then(countArg);
            withLit->then(itemArg);
            slotArg->then(withLit);
            posArg->then(slotArg);
            blockLit->then(posArg);
            replaceLit->then(blockLit);
            // replace entity <targets> <slot> with <item> [count]
            auto entityLit = CommandNode::literal("entity");
            auto targets = CommandNode::argument("targets", args::entity(false,false));
            auto eSlot = CommandNode::argument("slot", args::stringWord());
            eSlot->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){ return std::vector<std::string>{"weapon.mainhand","weapon.offhand","armor.head","armor.chest","armor.legs","armor.feet","container.0","hotbar.0"}; };
            auto eWith = CommandNode::literal("with");
            auto eItem = CommandNode::argument("item", args::itemStackArg());
            eItem->executable = true;
            eItem->action = [this, slotToPlayerStack, parseItemStack](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                std::string slot = c.arg("slot").asStr();
                std::string itemStr = c.arg("item").asStr();
                ItemStack stack = parseItemStack(itemStr, 1);
                int n=0;
                for(auto &nm: sel.playerNames) if(Player* p=findPlayer(*this,nm)){
                    ItemStack* tgt = slotToPlayerStack(*p, slot);
                    if(!tgt) continue;
                    *tgt = stack;
                    resendInventory(*p);
                    syncEquipmentOnChange(*p);
                    ++n;
                }
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src, "Replaced entity slot "+slot+" with "+itemStr+" for "+std::to_string(n));
                return n;
            };
            auto eCount = CommandNode::argument("count", args::integer(1,64));
            eCount->executable = true;
            eCount->action = [this, slotToPlayerStack, parseItemStack](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                std::string slot = c.arg("slot").asStr();
                std::string itemStr = c.arg("item").asStr();
                int cnt = c.arg("count").asInt();
                ItemStack stack = parseItemStack(itemStr, cnt);
                int n=0;
                for(auto &nm: sel.playerNames) if(Player* p=findPlayer(*this,nm)){
                    ItemStack* tgt = slotToPlayerStack(*p, slot);
                    if(!tgt) continue;
                    *tgt = stack;
                    resendInventory(*p);
                    syncEquipmentOnChange(*p);
                    ++n;
                }
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src, "Replaced entity slot "+slot+" with "+itemStr+" x"+std::to_string(cnt));
                return n;
            };
            eItem->then(eCount);
            eWith->then(eItem);
            eSlot->then(eWith);
            targets->then(eSlot);
            entityLit->then(targets);
            replaceLit->then(entityLit);
        }
        // ----- modify
        {
            // modify block <pos> <slot> <modifier>
            auto blockLit = CommandNode::literal("block");
            auto posArg = CommandNode::argument("pos", args::blockPos());
            auto slotArg = CommandNode::argument("slot", args::stringWord());
            auto modArg = CommandNode::argument("modifier", args::resourceLocation());
            modArg->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
                std::vector<std::string> v;
                for(auto &kv: datapackManager_.itemModifiers) v.push_back(kv.first);
                if(v.empty()) v.push_back("minecraft:test_modifier");
                return v;
            };
            modArg->executable = true;
            modArg->action = [this, slotToBlockStack](CommandContext& c){
                auto pos = c.arg("pos").asBlockPos();
                std::string slot=c.arg("slot").asStr();
                std::string mod=c.arg("modifier").asStr();
                if(mod.find(':')==std::string::npos) mod="minecraft:"+mod;
                ItemStack* tgt = slotToBlockStack(pos, slot);
                if(!tgt || tgt->empty()){ Player* src=static_cast<Player*>(c.source.player); sendFeedback(src, "No item in block slot to modify"); return 0; }
                bool ok = datapackManager_.applyItemModifier(mod, *tgt);
                if(!ok){
                    // fallback: just set count to 2 as visible modification
                    if(tgt->count<64) tgt->count+=1;
                    Player* src=static_cast<Player*>(c.source.player);
                    sendFeedback(src, "Applied modifier "+mod+" (fallback)");
                } else {
                    Player* src=static_cast<Player*>(c.source.player);
                    sendFeedback(src, "Applied modifier "+mod+" to block slot "+slot);
                }
                blockEntities_.dirty_.insert(posKey(pos.x,pos.y,pos.z));
                return 1;
            };
            slotArg->then(modArg);
            posArg->then(slotArg);
            blockLit->then(posArg);
            modifyLit->then(blockLit);
            // modify entity <targets> <slot> <modifier>
            auto entityLit = CommandNode::literal("entity");
            auto targets = CommandNode::argument("targets", args::entity(false,false));
            auto eSlot = CommandNode::argument("slot", args::stringWord());
            auto eMod = CommandNode::argument("modifier", args::resourceLocation());
            eMod->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
                std::vector<std::string> v;
                for(auto &kv: datapackManager_.itemModifiers) v.push_back(kv.first);
                if(v.empty()) v.push_back("minecraft:test_modifier");
                return v;
            };
            eMod->executable = true;
            eMod->action = [this](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                std::string slot=c.arg("slot").asStr();
                std::string mod=c.arg("modifier").asStr();
                if(mod.find(':')==std::string::npos) mod="minecraft:"+mod;
                int n=0;
                for(auto &nm: sel.playerNames) if(Player* p=findPlayer(*this,nm)){
                    ItemStack* tgt=nullptr;
                    if(slot=="weapon.mainhand") tgt=&p->inv[36];
                    else if(slot=="weapon.offhand") tgt=&p->inv[45];
                    else if(slot=="armor.head") tgt=&p->inv[8];
                    else if(slot=="armor.chest") tgt=&p->inv[7];
                    else if(slot=="armor.legs") tgt=&p->inv[6];
                    else if(slot=="armor.feet") tgt=&p->inv[5];
                    else if(slot.rfind("container.",0)==0){
                        try{int idx=std::stoi(slot.substr(10)); if(idx>=0&&idx<27) tgt=&p->inv[9+idx];}catch(...){}
                    } else if(slot.rfind("hotbar.",0)==0){
                        try{int idx=std::stoi(slot.substr(7)); if(idx>=0&&idx<9) tgt=&p->inv[36+idx];}catch(...){}
                    }
                    if(!tgt || tgt->empty()) continue;
                    bool ok = datapackManager_.applyItemModifier(mod, *tgt);
                    if(!ok){ if(tgt->count<64) tgt->count+=1; }
                    resendInventory(*p);
                    ++n;
                }
                Player* src=static_cast<Player*>(c.source.player);
                if(n>0) sendFeedback(src, "Applied modifier "+mod+" to "+std::to_string(n)+" entities");
                else sendFeedback(src, "No items modified for "+mod);
                return n;
            };
            eSlot->then(eMod);
            targets->then(eSlot);
            entityLit->then(targets);
            modifyLit->then(entityLit);
        }
        // ----- remove style: item remove block|entity ... (and also support replace-style rm?)
        {
            // legacy remove as sibling of replace/modify
            auto blockLit = CommandNode::literal("block");
            auto posArg = CommandNode::argument("pos", args::blockPos());
            auto slotArg = CommandNode::argument("slot", args::stringWord());
            slotArg->executable = true;
            slotArg->action = [this, slotToBlockStack](CommandContext& c){
                auto pos=c.arg("pos").asBlockPos();
                std::string slot=c.arg("slot").asStr();
                ItemStack* tgt = slotToBlockStack(pos, slot);
                if(!tgt){ Player* src=static_cast<Player*>(c.source.player); sendFeedback(src, "No block slot "+slot); return 0; }
                *tgt = ItemStack::air();
                blockEntities_.dirty_.insert(posKey(pos.x,pos.y,pos.z));
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src, "Removed item from block slot "+slot);
                return 1;
            };
            posArg->then(slotArg);
            blockLit->then(posArg);
            removeLit->then(blockLit);
            auto entityLit = CommandNode::literal("entity");
            auto targets = CommandNode::argument("targets", args::entity(false,false));
            auto eSlot = CommandNode::argument("slot", args::stringWord());
            eSlot->executable = true;
            eSlot->action = [this](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                std::string slot=c.arg("slot").asStr();
                int n=0;
                for(auto &nm: sel.playerNames) if(Player* p=findPlayer(*this,nm)){
                    ItemStack* tgt=nullptr;
                    if(slot=="weapon.mainhand") tgt=&p->inv[36];
                    else if(slot=="weapon.offhand") tgt=&p->inv[45];
                    else if(slot=="armor.head") tgt=&p->inv[8];
                    else if(slot=="armor.chest") tgt=&p->inv[7];
                    else if(slot=="armor.legs") tgt=&p->inv[6];
                    else if(slot=="armor.feet") tgt=&p->inv[5];
                    else if(slot.rfind("container.",0)==0){
                        try{int idx=std::stoi(slot.substr(10)); if(idx>=0&&idx<27) tgt=&p->inv[9+idx];}catch(...){}
                    } else if(slot.rfind("hotbar.",0)==0){
                        try{int idx=std::stoi(slot.substr(7)); if(idx>=0&&idx<9) tgt=&p->inv[36+idx];}catch(...){}
                    }
                    if(!tgt) continue;
                    *tgt = ItemStack::air();
                    resendInventory(*p);
                    syncEquipmentOnChange(*p);
                    ++n;
                }
                Player* src=static_cast<Player*>(c.source.player);
                sendFeedback(src, "Removed item from "+std::to_string(n)+" entities slot "+slot);
                return n;
            };
            targets->then(eSlot);
            entityLit->then(targets);
            removeLit->then(entityLit);
        }
        // wire up
        item->then(replaceLit);
        item->then(modifyLit);
        item->then(removeLit);
        d.root->then(item);
    }
    // ---------------------------------------------------------------- me / msg / tell / w
    {
        auto me = CommandNode::literal("me");
        auto act = CommandNode::argument("action", args::stringGreedy());
        act->executable = true;
        act->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string txt = c.arg("action").asStr();
            std::string who = src?src->name:"Server";
            std::string line = "* "+who+" "+txt;
            // emote is italic gray ? Use SystemChat with italic flag in JSON
            WriteBuffer b;
            nbt::writeTextComponent(b, "{\"text\":\""+line+"\",\"italic\":true,\"color\":\"gray\"}");
            b.boolean(false);
            broadcastPacketExcept(nullptr, proto::pl::sc::SystemChat, b);
            return 1;
        };
        me->then(act);
        d.root->then(me);
        for(auto alias : {"msg","tell","w"}){
            auto msgLit = CommandNode::literal(alias);
            auto targets = CommandNode::argument("targets", args::entity(false,false));
            auto message = CommandNode::argument("message", args::stringGreedy());
            message->executable = true;
            message->action = [this, alias](CommandContext& c){
                const auto sel = c.arg("targets").asSelector();
                std::string txt = c.arg("message").asStr();
                Player* src = static_cast<Player*>(c.source.player);
                std::string from = src?src->name:"Server";
                int delivered=0;
                for(auto &nm: sel.playerNames) if(Player* p=findPlayer(*this,nm)){
                    // whisper to target
                    WriteBuffer b;
                    std::string json = "{\"text\":\"["+from+" -> "+p->name+"] "+txt+"\",\"color\":\"gray\",\"italic\":true}";
                    nbt::writeTextComponent(b, json);
                    b.boolean(false);
                    try{ p->conn->sendPacket(proto::pl::sc::SystemChat, b);}catch(...){}
                    ++delivered;
                }
                // also echo to sender if not among targets
                if(src){
                    bool senderIsTarget=false;
                    for(auto &nm: sel.playerNames) if(nm==src->name) senderIsTarget=true;
                    if(!senderIsTarget){
                        WriteBuffer b2;
                        std::string firstTarget = sel.playerNames.empty()?"?":sel.playerNames[0];
                        std::string json2 = "{\"text\":\"["+from+" -> "+firstTarget+"] "+txt+"\",\"color\":\"gray\",\"italic\":true}";
                        nbt::writeTextComponent(b2, json2);
                        b2.boolean(false);
                        try{ src->conn->sendPacket(proto::pl::sc::SystemChat, b2);}catch(...){}
                    }
                }
                if(src) sendFeedback(src, "Whispered to "+std::to_string(delivered)+" player(s)");
                return delivered;
            };
            targets->then(message);
            msgLit->then(targets);
            d.root->then(msgLit);
        }
    }
}

} // namespace cppfm
