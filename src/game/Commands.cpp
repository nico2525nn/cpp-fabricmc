// Commands.cpp: Brigadier command tree + selector resolution (plan3.md
// "Brigadier完全移植"). All commands are registered on a real CommandNode
// tree, parsed by the dispatcher and advertised via declare_commands.
#include "GameServer.hpp"
#include "../generated/EntityIds.hpp"
#include <algorithm>
#include <cmath>
#include <set>

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
            const std::string itemName = c.arg("item").asStr();
            auto it = gen::itemIdByName().find(itemName);
            if (it == gen::itemIdByName().end())
                throw std::runtime_error("Unknown item: " + itemName);
            int given = 0;
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    addToInventory(*t, it->second, 1);
                    resendInventory(*t);
                    ++given;
                }
            sendFeedback(src, "Given 1 x " + itemName);
            return given;
        };
        auto cnt = CommandNode::argument("count", args::integer(1, 576));
        cnt->executable = true;
        cnt->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("target").asSelector();
            const std::string itemName = c.arg("item").asStr();
            auto it = gen::itemIdByName().find(itemName);
            if (it == gen::itemIdByName().end())
                throw std::runtime_error("Unknown item: " + itemName);
            const int n2 = c.arg("count").asInt();
            int given = 0;
            for (auto& nm : sel.playerNames)
                if (Player* t = findPlayer(*this, nm)) {
                    addToInventory(*t, it->second,
                                   static_cast<std::uint16_t>(n2));
                    resendInventory(*t);
                    ++given;
                }
            sendFeedback(src, "Given " + std::to_string(n2) + " x " + itemName);
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
    // /gamerule <rule> [value]
    {
        auto gr = CommandNode::literal("gamerule");
        auto rule = CommandNode::argument("rule", args::stringWord());
        rule->executable = true;
        rule->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"doDaylightCycle", "doMobSpawning",
                                            "keepInventory", "randomTickSpeed",
                                            "mobGriefing", "doFireTick"};
        };
        rule->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string r = c.arg("rule").asStr();
            sendFeedback(src, "\u00a77" + r + " = " + gamerules_.get(r));
            return 1;
        };
        auto value = CommandNode::argument("value", args::stringWord());
        value->executable = true;
        value->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string r = c.arg("rule").asStr();
            const std::string v = c.arg("value").asStr();
            gamerules_.set(r, v);
            broadcastSystemText("\u00a77Gamerule " + r + " is now " + v);
            return 1;
        };
        rule->then(value);
        gr->then(rule);
        d.root->then(gr);
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
    // /setblock <pos> <block>
    {
        auto sb = CommandNode::literal("setblock");
        auto pos = CommandNode::argument("pos", args::blockPos());
        auto block = CommandNode::argument("block", args::itemStackArg());
        block->executable = true;
        block->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v{"minecraft:stone", "minecraft:dirt",
                                       "minecraft:oak_planks", "minecraft:glass",
                                       "minecraft:tnt", "minecraft:redstone_block"};
            return v;
        };
        block->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto p = c.arg("pos").asBlockPos();
            std::string bn = c.arg("block").asStr();
            if (bn.find(':') == std::string::npos) bn = "minecraft:" + bn;
            const gen::BlockDef* def = gen::blockByName(bn);
            if (!def) throw std::runtime_error("unknown block: " + bn);
            world_.generateChunkIfMissing(p.x >> 4, p.z >> 4);
            world_.setBlock(p.x, p.y, p.z,
                            static_cast<std::uint16_t>(def->defaultState));
            broadcastBlockChange(p.x, p.y, p.z,
                                 static_cast<std::uint16_t>(def->defaultState));
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
    // /scoreboard objectives add <name> <criteria> | players set <p> <obj> <v>
    {
        auto sb = CommandNode::literal("scoreboard");
        auto obj = CommandNode::literal("objectives");
        auto add = CommandNode::literal("add");
        auto name = CommandNode::argument("name", args::stringWord());
        auto crit = CommandNode::argument("criteria", args::stringWord());
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
        auto objName = CommandNode::argument("objective", args::stringWord());
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
        auto oname = CommandNode::argument("objective", args::stringWord());
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
    // /team add|remove|join|leave|list  (plan10 §6, network §79)
    {
        auto team = CommandNode::literal("team");
        // /team add <team> [displayName]
        auto tAdd = CommandNode::literal("add");
        auto tAddName = CommandNode::argument("team", args::stringWord());
        tAddName->executable = true;
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
        auto tRemName = CommandNode::argument("team", args::stringWord());
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
        auto tJoinTeam = CommandNode::argument("team", args::stringWord());
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
        auto tLeaveTeam = CommandNode::argument("team", args::stringWord());
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
}

} // namespace cppfm
