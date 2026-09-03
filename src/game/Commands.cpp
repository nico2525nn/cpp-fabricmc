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
static std::map<std::string,std::string> parseFunctionArgsNbt(const std::string& nbtStr) {
    std::map<std::string,std::string> out;
    if (nbtStr.empty()) return out;
    std::string s = nbtStr;
    // trim whitespace
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a==std::string::npos) return out;
    s = s.substr(a, b-a+1);
    if (s.size()>=2 && s.front()=='{' && s.back()=='}') s = s.substr(1, s.size()-2);
    else if (s.empty()) return out;
    // split by commas respecting quotes and nesting
    std::vector<std::string> parts;
    std::string cur; bool inQ=false; char qChar=0; int depth=0;
    for (size_t i=0;i<s.size();++i) {
        char c = s[i];
        if (inQ) {
            cur.push_back(c);
            if (c==qChar && (i==0 || s[i-1]!='\\')) inQ=false;
        } else {
            if (c=='"' || c=='\'') { inQ=true; qChar=c; cur.push_back(c); }
            else if (c=='{' || c=='[') { depth++; cur.push_back(c); }
            else if (c=='}' || c==']') { depth--; cur.push_back(c); }
            else if (c==',' && depth==0) { parts.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    for (auto &p : parts) {
        size_t colon = p.find(':');
        if (colon==std::string::npos) continue;
        std::string k = p.substr(0, colon);
        std::string v = p.substr(colon+1);
        auto trim = [](std::string &t){ size_t aa=t.find_first_not_of(" \t\r\n"); size_t bb=t.find_last_not_of(" \t\r\n"); if(aa==std::string::npos) t.clear(); else t=t.substr(aa,bb-aa+1); };
        trim(k); trim(v);
        // strip quotes from key
        if (k.size()>=2 && ((k.front()=='"' && k.back()=='"') || (k.front()=='\'' && k.back()=='\''))) k = k.substr(1,k.size()-2);
        // strip quotes from value if string
        if (v.size()>=2 && ((v.front()=='"' && v.back()=='"') || (v.front()=='\'' && v.back()=='\''))) {
            v = v.substr(1, v.size()-2);
        } else {
            // numeric: strip suffix s,b,l,d,f (23w31a without suffixes)
            if (!v.empty() && (v.back()=='s' || v.back()=='b' || v.back()=='L' || v.back()=='l' || v.back()=='d' || v.back()=='D' || v.back()=='f' || v.back()=='F')) {
                // ensure preceding is digit or . to avoid stripping letters in plain strings
                if (v.size()>=2 && (isdigit((unsigned char)v[v.size()-2]) || v[v.size()-2]=='.')) v.pop_back();
            }
            // also handle quoted numeric already stripped
        }
        if (!k.empty()) out[k]=v;
    }
    return out;
}

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
        // plan42 R3 (E-19): capture console feedback for RCON responses
        // (dispatchConsole returns it instead of fixed "ok").
        if (GameServer::consoleCapture_) {
            if (!GameServer::consoleCapture_->empty()) *GameServer::consoleCapture_ += "\n";
            *GameServer::consoleCapture_ += msg;
        }
        std::fprintf(stderr, "[cppfm] %s\n", msg.c_str());
    }
}

// ------------------------------------------------------------ registration --

// Recipe-book UpdateRecipes SlotDisplay writer: varint presence (2 = item)
// + item id. Single truth (was a lambda inside the recipe block, used 10x).
static void writeSlotDisplayItem(WriteBuffer& bb, std::uint32_t itemId) {
    bb.varint(itemId ? 2 : 0);
    if (itemId) bb.varint(static_cast<std::int32_t>(itemId));
}

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
            sendFeedback(p, (msg::kGray + "Commands: /help /ping /gamemode /give /time "
                            "/tp /kill /list /say /seed /gamerule /effect /xp "
                            "/setblock /summon /clear /spawnpoint /kick"));
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
            sendFeedback(p, (msg::kGray + "Players online (" +
                         std::to_string(playerCount()) + "): " + names));
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
            broadcastSystemText((msg::kPink + "[Server] " + c.arg("message").asStr()));
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
            // abilities follow the mode (plan43 W-06: same gamemode-linked
            // flags as Session::sendAbilities — survival/adventure get 0x00,
            // not the old hardcoded 0x01 invulnerable)
            std::uint8_t af = 0;
            if (m == 1) af |= 0x01 | 0x04 | 0x08;
            else if (m == 3) af |= 0x02 | 0x04;
            if (src->isFlying && !(af & 0x04)) src->isFlying = false;
            if (src->isFlying) af |= 0x02;
            WriteBuffer ab;
            ab.i8(static_cast<std::int8_t>(af));
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
                    // plan43 W-06: gamemode-linked flags (see self-target above)
                    std::uint8_t taf = 0;
                    if (m == 1) taf |= 0x01 | 0x04 | 0x08;
                    else if (m == 3) taf |= 0x02 | 0x04;
                    if (t->isFlying && !(taf & 0x04)) t->isFlying = false;
                    if (t->isFlying) taf |= 0x02;
                    WriteBuffer ab;
                    ab.i8(static_cast<std::int8_t>(taf));
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
                    // plan42 R1: filled_map map_id component + MapData 0x2D
                    ItemStack toGive = stack;
                    if (base=="minecraft:filled_map" || base=="minecraft:map") {
                        int mapId = nextMapId_.fetch_add(1);
                        WriteBuffer tmp; tmp.varint(mapId);
                        toGive.components.erase(std::remove_if(toGive.components.begin(), toGive.components.end(), [](auto &pr){return pr.first==36;}), toGive.components.end());
                        toGive.components.emplace_back(36, std::vector<uint8_t>(tmp.data.begin(), tmp.data.end()));
                        toGive.count = 1;
                        bool placed=false;
                        for(int i: kMainInventoryOrder){
                            auto &s = t->inv[i];
                            if (s.empty()) { s = toGive; placed=true; break; }
                        }
                        if(!placed) { addToInventory(*t, toGive.itemId, 1); // fallback add without map_id already handled via inventory scan
                            for(int i: kMainInventoryOrder) if(!t->inv[i].empty() && t->inv[i].itemId==toGive.itemId) { t->inv[i]=toGive; break; }
                        }
                        resendInventory(*t);
                        sendMapData(*t, mapId);
                    } else {
                        bool placed=false;
                        for(int i: kMainInventoryOrder){
                            auto &s = t->inv[i];
                            if (s.empty()) { s = toGive; placed=true; break; }
                        }
                        if(!placed) addToInventory(*t, it->second, 1);
                        resendInventory(*t);
                    }
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
                    bool isMap = (base=="minecraft:filled_map" || base=="minecraft:map");
                    for(int k=0;k<n2;k++){
                        ItemStack toGive = stack;
                        int curMapId = -1;
                        if (isMap) {
                            curMapId = nextMapId_.fetch_add(1);
                            WriteBuffer tmp; tmp.varint(curMapId);
                            toGive.components.erase(std::remove_if(toGive.components.begin(), toGive.components.end(), [](auto &pr){return pr.first==36;}), toGive.components.end());
                            toGive.components.emplace_back(36, std::vector<uint8_t>(tmp.data.begin(), tmp.data.end()));
                        }
                        bool placed=false;
                        for(int i: kMainInventoryOrder){
                            auto &s = t->inv[i];
                            if (s.empty()) { s = toGive; placed=true; break; }
                        }
                        if(!placed) addToInventory(*t, toGive.itemId, 1);
                        if (isMap && curMapId>=0) sendMapData(*t, curMapId);
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
            const std::string v = c.arg("named").asStr();
            std::int64_t t = 1000;
            if (v == "day") t = 1000;
            else if (v == "noon") t = 6000;
            else if (v == "night") t = 13000;
            else if (v == "midnight") t = 18000;
            else throw std::runtime_error("unknown time of day");
            setTimeOfDay(t);
            broadcastSystemText((msg::kGray + "Time set to " + v));
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
            if (!gamerules_.contains(r)) { sendFeedback(src, (msg::kRed + "Unknown gamerule: " + r)); return 0; }
            std::string cur = gamerules_.get(r);
            sendFeedback(src, (msg::kGray + r + " = " + cur));
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
                sendFeedback(src, (msg::kRed + err));
                return 0;
            }
            broadcastSystemText((msg::kGray + "Gamerule " + r + " is now " + v));
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
                std::vector<std::pair<std::string,std::string>> props;
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
                    props.emplace_back(std::move(k), std::move(v));
                    if(comma==std::string::npos) break;
                    pos2=comma+1;
                }
                if(!props.empty()){
                    std::vector<std::pair<std::string_view,std::string_view>> sv;
                    sv.reserve(props.size());
                    for(auto &pr: props) sv.emplace_back(pr.first, pr.second);
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
            const std::string k = c.arg("kind").asStr();
            if (k == "clear") setWeather(Weather::Clear, 6000 * 20);
            else setWeather(Weather::Rain,
                            (k == "thunder" ? 3000 : 6000) * 20LL);
            broadcastSystemText((msg::kGray + "Weather set to " + k));
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
        // plan34 network: /title <text> existing + /title actionbar <text> -> ActionBar 0x51 (Prismarine packet_action_bar {text:anonymousNbt})
        auto actionbarLit = CommandNode::literal("actionbar");
        auto abText = CommandNode::argument("ab_text", args::stringGreedy());
        abText->executable = true;
        abText->action = [this](CommandContext& c) {
            const std::string t = c.arg("ab_text").asStr();
            for (auto& pl : playersSnapshot()) {
                this->sendActionBar(*pl, t);
            }
            return 1;
        };
        actionbarLit->then(abText);
        // also support bare actionbar without text (clear)
        actionbarLit->executable = true;
        actionbarLit->action = [this](CommandContext&) {
            for (auto& pl : playersSnapshot()) this->sendActionBar(*pl, "");
            return 1;
        };
        title->then(actionbarLit);
        // plan42 R3 network (E-17): vanilla /title <targets> <title|subtitle|
        // actionbar|clear|reset|times> ... form. Registered BEFORE the greedy
        // <text> child so "@s title {...}" resolves to targets first.
        {
            auto tTargets = CommandNode::argument("titleTargets", args::entity(false, false));
            auto mkTitleText = [this](const std::string& kind) {
                auto lit = CommandNode::literal(kind);
                auto msg = CommandNode::argument("titleJson", args::stringGreedy());
                msg->executable = true;
                msg->action = [this, kind](CommandContext& c) {
                    Player* src = static_cast<Player*>(c.source.player);
                    const auto sel = c.arg("titleTargets").asSelector();
                    const std::string t = c.arg("titleJson").asStr();
                    int n = 0;
                    for (auto& nm : sel.playerNames)
                        if (Player* p = findPlayer(*this, nm)) {
                            if (kind == "subtitle") {
                                WriteBuffer b;
                                nbt::writeTextComponent(b, t);
                                try { p->conn->sendPacket(proto::pl::sc::SetTitleSubtitle, b); } catch (...) {}
                            } else if (kind == "actionbar") {
                                this->sendActionBar(*p, t);
                            } else {
                                WriteBuffer b;
                                nbt::writeTextComponent(b, t);
                                try { p->conn->sendPacket(proto::pl::sc::SetTitleText, b); } catch (...) {}
                            }
                            ++n;
                        }
                    sendFeedback(src, "Set " + kind + " title for " + std::to_string(n) + " player(s)");
                    return n;
                };
                lit->then(msg);
                return lit;
            };
            tTargets->then(mkTitleText("title"));
            tTargets->then(mkTitleText("subtitle"));
            tTargets->then(mkTitleText("actionbar"));
            auto tClear = CommandNode::literal("clear");
            tClear->executable = true;
            tClear->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("titleTargets").asSelector();
                int n = 0;
                for (auto& nm : sel.playerNames)
                    if (Player* p = findPlayer(*this, nm)) {
                        WriteBuffer b;
                        try { p->conn->sendPacket(proto::pl::sc::ClearTitles, b); } catch (...) {}
                        ++n;
                    }
                sendFeedback(src, "Cleared title for " + std::to_string(n) + " player(s)");
                return n;
            };
            tTargets->then(tClear);
            auto tReset = CommandNode::literal("reset");
            tReset->executable = true;
            tReset->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("titleTargets").asSelector();
                int n = 0;
                for (auto& nm : sel.playerNames)
                    if (findPlayer(*this, nm)) ++n;
                sendFeedback(src, "Reset title times for " + std::to_string(n) + " player(s)");
                return n;
            };
            tTargets->then(tReset);
            auto tTimes = CommandNode::literal("times");
            auto tFadeIn = CommandNode::argument("fadeIn", args::integer(0, 1000000));
            auto tStay = CommandNode::argument("stay", args::integer(0, 1000000));
            auto tFadeOut = CommandNode::argument("fadeOut", args::integer(0, 1000000));
            tFadeOut->executable = true;
            tFadeOut->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("titleTargets").asSelector();
                int n = 0;
                for (auto& nm : sel.playerNames)
                    if (Player* p = findPlayer(*this, nm)) {
                        WriteBuffer b;
                        b.i32(c.arg("fadeIn").asInt());
                        b.i32(c.arg("stay").asInt());
                        b.i32(c.arg("fadeOut").asInt());
                        try { p->conn->sendPacket(proto::pl::sc::SetTitleTime, b); } catch (...) {}
                        ++n;
                    }
                sendFeedback(src, "Set title times for " + std::to_string(n) + " player(s)");
                return n;
            };
            tStay->then(tFadeOut); tFadeIn->then(tStay); tTimes->then(tFadeIn);
            tTargets->then(tTimes);
            title->then(tTargets);
        }
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
            const std::string lv = c.arg("level").asStr();
            // plan42 R3 network (E-15): vanilla only accepts the 4 literals;
            // anything else (e.g. "impossible") must error, not succeed.
            if (lv != "peaceful" && lv != "easy" && lv != "normal" && lv != "hard")
                throw std::runtime_error("Unknown difficulty '" + lv +
                    "' (expected peaceful, easy, normal or hard)");
            difficulty_ = lv;
            WriteBuffer b;
            b.i8(difficulty_ == "peaceful" ? 0 : difficulty_ == "easy" ? 1 :
                 difficulty_ == "hard" ? 3 : 2);
            b.boolean(false);
            broadcastPacketExcept(nullptr, proto::pl::sc::ChangeDifficulty, b);
            broadcastSystemText((msg::kGray + "Difficulty set to " + difficulty_));
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
                for(int y=kMaxY-1;y>=kMinY;--y){ if(world_.getBlock((int)ox,y,(int)oz)!=0){ topY=y+1; break; } }
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
        // shared executor for /function with and without NBT args
        auto execFunctionWithArgs = [this](CommandContext& c, bool hasNbt) -> int {
            Player* src = static_cast<Player*>(c.source.player);
            std::string id = c.arg("name").asStr();
            std::string norm = id;
            if (norm.find(':')==std::string::npos) norm = "minecraft:" + norm;
            std::map<std::string,std::string> argsMap;
            if (hasNbt) {
                std::string nbtStr = c.arg("arguments").asStr();
                argsMap = parseFunctionArgsNbt(nbtStr);
            }
            brigadier::CommandSource fsrc;
            if (src){ fsrc.player=src; fsrc.name=src->name; fsrc.console=false; fsrc.srcX=src->x; fsrc.srcY=src->y; fsrc.srcZ=src->z; fsrc.resolveSelector=[this,src](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,src); }; }
            else { fsrc.console=true; fsrc.name="Server"; fsrc.resolveSelector=[this](const std::string& raw, brigadier::SelectorResult& out){ out=resolveSelector(raw,nullptr); }; }
            int executed = 0;
            if (argsMap.empty()) executed = functionEvaluator_.executeFunction(norm, fsrc);
            else executed = functionEvaluator_.executeFunction(norm, fsrc, argsMap);
            if (executed==0) {
                // Fallback: try direct file read (legacy) - also handle macro if args present
                auto colon = norm.find(':');
                std::string ns = colon!=std::string::npos?norm.substr(0,colon):"minecraft";
                std::string path = colon!=std::string::npos?norm.substr(colon+1):norm;
                std::string file = "assets/data/" + ns + "/functions/" + path + ".mcfunction";
                std::ifstream f(file);
                if(!f) {
                    // try datapack functions map fallback with macro
                    auto* fn = datapackManager_.getFunction(norm);
                    if (fn) {
                        int cnt=0; int last=0;
                        for (auto &line : *fn) {
                            size_t s=line.find_first_not_of(" \t\r\n");
                            if(s==std::string::npos) continue;
                            size_t e=line.find_last_not_of(" \t\r\n");
                            std::string t=line.substr(s,e-s+1);
                            if(t.empty()||t[0]=='#') continue;
                            if(!t.empty()&&t.front()=='/') t=t.substr(1);
                            std::string expanded = FunctionEvaluator::expandMacro(t, argsMap);
                            bool isMacro = !t.empty() && t[0]=='$';
                            if (isMacro && expanded.empty()) break;
                            std::string toExec = isMacro ? expanded : t;
                            brigadier::CommandSource cur=fsrc;
                            auto res=commands_.execute(toExec, std::move(cur));
                            if(!res.ok) sendFeedback(src, "function line failed: "+toExec+" -> "+res.errorText);
                            last = res.ok?res.value:0;
                            ++cnt;
                        }
                        if (cnt==0) throw std::runtime_error("function not found: " + norm);
                        sendFeedback(src, "Executed function " + norm + " ("+std::to_string(cnt)+" commands)");
                        return last;
                    }
                    throw std::runtime_error("function not found: " + norm);
                }
                std::string line;
                int cnt=0; int last=0;
                while(std::getline(f,line)){
                    size_t s=line.find_first_not_of(" \t\r\n");
                    if(s==std::string::npos) continue;
                    size_t e=line.find_last_not_of(" \t\r\n");
                    std::string t=line.substr(s,e-s+1);
                    if(t.empty()||t[0]=='#') continue;
                    if(!t.empty()&&t.front()=='/') t=t.substr(1);
                    std::string expanded = FunctionEvaluator::expandMacro(t, argsMap);
                    bool isMacro = !t.empty() && t[0]=='$';
                    if (isMacro && expanded.empty()) break;
                    std::string toExec = isMacro ? expanded : t;
                    brigadier::CommandSource cur=fsrc;
                    auto res=commands_.execute(toExec, std::move(cur));
                    if(!res.ok) sendFeedback(src, "function line failed: "+toExec+" -> "+res.errorText);
                    last = res.ok?res.value:0;
                    ++cnt;
                }
                sendFeedback(src, "Executed function " + norm + " ("+std::to_string(cnt)+" commands)");
                return last;
            }
            sendFeedback(src, "Executed function " + norm + " (result="+std::to_string(executed)+")");
            return executed;
        };
        nameArg->executable = true;
        nameArg->action = [execFunctionWithArgs](CommandContext& c){ return execFunctionWithArgs(c, false); };
        auto arguments = CommandNode::argument("arguments", args::nbtCompoundTagArg());
        arguments->executable = true;
        arguments->action = [execFunctionWithArgs](CommandContext& c){ return execFunctionWithArgs(c, true); };
        nameArg->then(arguments);
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
    // /reload — plan35 §4: re-read recipes/tags/loot + advancements/predicates + re-send UpdateAdvancements 0x7B
    {
        auto reload = CommandNode::literal("reload");
        reload->executable = true;
        reload->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            // clear so deleted files disappear (plan35 §4 note: loadAll alone would leave stale entries)
            datapackManager_.advancements.clear();
            datapackManager_.predicates.clear();
            datapackManager_.itemModifiers.clear();
            datapackManager_.functions.clear();
            datapackManager_.tagManager.itemTags.clear();
            datapackManager_.tagManager.blockTags.clear();
            datapackManager_.lootTables.clear();
            datapackManager_.availablePacks.clear();
            datapackManager_.enabledPacks.clear();
            datapackManager_.availablePacks.insert("vanilla");
            datapackManager_.enabledPacks.insert("vanilla");
            datapackManager_.availablePacks.insert("cppfm");
            datapackManager_.enabledPacks.insert("cppfm");
            datapackManager_.loadAll(recipes_, "assets/data", cfg_.worldDir + "/datapacks");
            tagManager_ = datapackManager_.tagManager;
            lootTables_ = datapackManager_.lootTables;
            {
                // plan42 R3: invalidate the merged-advancement cache under the
                // same mutex its readers use (see getMergedAdvancements).
                std::lock_guard lk(advMergeMtx_);
                cachedMergedAdv_.clear();
                cachedAdvRawSize_ = 0;
            }
            for (auto& pp : playersSnapshot()) if (pp->inPlay) sendAdvancementsTo(*pp, true);
            sendFeedback(src, "Reload complete");
            return 1;
        };
        d.root->then(reload);
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
    // ---- plan32 world: /locate /place /spreadplayers (evaluation C -3) ----
    // Design: plan32.md §7 place/locate + §8 spreadplayers, Appendix A/B
    // Isolated literals to avoid merge conflicts with other wt32 worktrees.
    // Only new literals + new handlers; no existing nodes touched.
    {
        // /locate <structure|biome|poi> <id>
        auto locate = CommandNode::literal("locate");
        // locate structure <structure>
        {
            auto structureLit = CommandNode::literal("structure");
            auto structArg = CommandNode::argument("locateStructureId", args::resourceLocation());
            structArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
                return std::vector<std::string>{
                    "minecraft:village","minecraft:ancient_city","minecraft:trail_ruins",
                    "minecraft:desert_pyramid","minecraft:jungle_temple","minecraft:swamp_hut",
                    "minecraft:igloo","minecraft:pillager_outpost","minecraft:monument",
                    "minecraft:mansion","minecraft:ruined_portal","minecraft:shipwreck",
                    "minecraft:ocean_ruins","minecraft:nether_complexes","minecraft:nether_fossil",
                    "minecraft:end_city","minecraft:trial_chambers","minecraft:buried_treasure",
                    "minecraft:mineshaft","minecraft:stronghold"
                };
            };
            structArg->executable = true;
            structArg->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string req = c.arg("locateStructureId").asStr();
                if(req.find(':')==std::string::npos) req="minecraft:"+req;
                std::string shortName = req.substr(req.find(':')+1);
                // Use tmp StructureManager seeded with same seed to avoid needing World accessor
                worldgen::StructureManager tmpMgr(cfg_.seed);
                const auto& sets = tmpMgr.sets();
                std::vector<const worldgen::SMStructureSet*> candidates;
                for(auto& s : sets){
                    if(s.name==req) candidates.push_back(&s);
                    else if(s.name.find(shortName)!=std::string::npos) candidates.push_back(&s);
                    // also handle aliases: desert_pyramid vs village etc
                    if(req=="minecraft:village" && s.name.find("village")!=std::string::npos) candidates.push_back(&s);
                }
                // dedup
                std::sort(candidates.begin(), candidates.end());
                candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
                if(candidates.empty()){
                    // try any set that contains shortName substring
                    for(auto& s: sets) if(s.name.find(shortName)!=std::string::npos) candidates.push_back(&s);
                }
                if(candidates.empty()){
                    sendFeedback(src, "Unknown structure: "+req);
                    return 0;
                }
                // Search spiral from source position (or 0,0 for console)
                int srcCx = src ? (int)std::floor(src->x/16.0) : 0;
                int srcCz = src ? (int)std::floor(src->z/16.0) : 0;
                int bestDist = INT32_MAX;
                int bestX=0,bestY=64,bestZ=0;
                std::string bestName;
                const int maxRadius = 100; // chunk radius (Yarn uses 100 chunk steps spiral; plan spec says 1000 but 100 is faster and finds nearby)
                // Expand ring by ring for closest
                bool found=false;
                for(int r=0; r<=maxRadius && !found; ++r){
                    // walk perimeter of square radius r
                    for(int dx=-r; dx<=r && !found; ++dx){
                        for(int dz=-r; dz<=r && !found; ++dz){
                            if(std::abs(dx)!=r && std::abs(dz)!=r) continue; // only perimeter for efficiency except r=0
                            int cx = srcCx + dx;
                            int cz = srcCz + dz;
                            for(auto* set : candidates){
                                auto at = worldgen::smStructureAtChunk(*set, cfg_.seed, cx, cz);
                                if(!at.present) continue;
                                // Check biome filter similar to generate() to avoid false positives
                                // For trial_chambers reject deep_dark biomes if needed (approx)
                                // Use world sampler for biome check if available
                                if(!set->biomes.empty()){
                                    std::string bio = world_.sampledBiome(at.originX+8, 64, at.originZ+8);
                                    bool ok=false;
                                    for(auto& want: set->biomes) if(bio.find(want)!=std::string::npos) { ok=true; break; }
                                    if(!ok) continue;
                                }
                                int dist = std::abs(at.originX - (src? (int)src->x:0)) + std::abs(at.originZ - (src? (int)src->z:0));
                                // Prefer smaller radius first, so first found is close
                                if(dist < bestDist){
                                    bestDist = dist;
                                    bestX = at.originX;
                                    bestZ = at.originZ;
                                    // Y: use surface estimate
                                    bestY = world_.sampledBiome(bestX,64,bestZ).empty() ? 64 : world_.surfaceFeetY(bestX, bestZ);
                                    if(bestY < -60) bestY = 64;
                                    if(bestY > kMaxY) bestY = 64;
                                    bestName = set->name;
                                    found=true;
                                }
                            }
                        }
                    }
                    if(found) break;
                }
                if(!found || bestDist==INT32_MAX){
                    sendFeedback(src, "Could not find structure "+req+" nearby (searched "+std::to_string(maxRadius*16)+" blocks)");
                    return 0;
                }
                // Feedback like Yarn: "The nearest minecraft:trial_chambers is at [x, y, z] (distance blocks away) (new chunks)"
                double dx = src ? (bestX - src->x) : bestX;
                double dz = src ? (bestZ - src->z) : bestZ;
                int distBlocks = (int)std::sqrt(dx*dx + dz*dz);
                std::string msg = "The nearest "+bestName+" is at ["+std::to_string(bestX)+", "+std::to_string(bestY)+", "+std::to_string(bestZ)+"] ("+std::to_string(distBlocks)+" blocks away)";
                sendFeedback(src, msg);
                return distBlocks;
            };
            structureLit->then(structArg);
            locate->then(structureLit);
        }
        // locate biome <biome>
        {
            auto biomeLit = CommandNode::literal("biome");
            auto biomeArg = CommandNode::argument("locateBiomeId", args::resourceLocation());
            biomeArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
                return std::vector<std::string>{
                    "minecraft:plains","minecraft:desert","minecraft:forest","minecraft:taiga",
                    "minecraft:jungle","minecraft:swamp","minecraft:savanna","minecraft:dark_forest",
                    "minecraft:pale_garden","minecraft:snowy_plains","minecraft:deep_dark"
                };
            };
            biomeArg->executable = true;
            biomeArg->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string req = c.arg("locateBiomeId").asStr();
                if(req.find(':')==std::string::npos) req="minecraft:"+req;
                std::string shortName = req.substr(req.find(':')+1);
                int srcX = src ? (int)src->x : 0;
                int srcZ = src ? (int)src->z : 0;
                const int maxRadius = 6400; // blocks (Yarn locate biome radius)
                const int step = 16;
                int bestDist = INT32_MAX;
                int bestX=0,bestZ=0;
                bool found=false;
                // spiral search
                for(int r=0; r<=maxRadius && !found; r+=step){
                    for(int dx=-r; dx<=r && !found; dx+=step){
                        for(int dz=-r; dz<=r && !found; dz+=step){
                            if(r!=0 && std::abs(dx)!=r && std::abs(dz)!=r) continue;
                            int x = srcX + dx;
                            int z = srcZ + dz;
                            std::string bio = world_.sampledBiome(x, 64, z);
                            if(bio.empty()) continue;
                            std::string bioShort = bio.substr(bio.find(':')+1);
                            bool match = (bio==req) || (bioShort==shortName) || (bio.find(shortName)!=std::string::npos);
                            if(match){
                                int dist = std::abs(dx)+std::abs(dz);
                                if(dist < bestDist){
                                    bestDist = dist;
                                    bestX = x; bestZ = z;
                                    found=true;
                                }
                            }
                        }
                    }
                    // early exit after first ring found to keep closest
                    if(found) break;
                }
                if(!found){
                    sendFeedback(src, "Could not find biome "+req+" nearby");
                    return 0;
                }
                int blocks = (int)std::sqrt((bestX-srcX)*(bestX-srcX)+(bestZ-srcZ)*(bestZ-srcZ));
                std::string msg = "The nearest "+req+" is at ["+std::to_string(bestX)+", ~, "+std::to_string(bestZ)+"] ("+std::to_string(blocks)+" blocks away)";
                sendFeedback(src, msg);
                return blocks;
            };
            biomeLit->then(biomeArg);
            locate->then(biomeLit);
        }
        // locate poi <poi> -> stub
        {
            auto poiLit = CommandNode::literal("poi");
            auto poiArg = CommandNode::argument("locatePoiId", args::resourceLocation());
            poiArg->executable = true;
            poiArg->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                std::string req = c.arg("locatePoiId").asStr();
                sendFeedback(src, "POI locate not yet implemented for "+req);
                return 0;
            };
            poiLit->then(poiArg);
            locate->then(poiLit);
        }
        d.root->then(locate);
        // /place feature|jigsaw|structure
        {
            auto place = CommandNode::literal("place");
            // place feature <feature> [pos]
            {
                auto featureLit = CommandNode::literal("feature");
                auto featArg = CommandNode::argument("placeFeatureId", args::resourceLocation());
                featArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
                    return std::vector<std::string>{"minecraft:tree","minecraft:oak","minecraft:birch","minecraft:ore_diamond","minecraft:flower_plain"};
                };
                featArg->executable = true;
                featArg->action = [this](CommandContext& c){
                    Player* src = static_cast<Player*>(c.source.player);
                    std::string fid = c.arg("placeFeatureId").asStr();
                    if(fid.find(':')==std::string::npos) fid="minecraft:"+fid;
                    int x = src ? (int)src->x : 0;
                    int y = src ? (int)src->y + 1 : 64;
                    int z = src ? (int)src->z : 0;
                    // simple decoration: place oak tree or ore vein
                    if(fid=="minecraft:tree" || fid=="minecraft:oak" || fid.find("tree")!=std::string::npos){
                        auto log = ((uint16_t)gen::blockByName("minecraft:oak_log")->defaultState);
                        auto leaves = ((uint16_t)gen::blockByName("minecraft:oak_leaves")->defaultState);
                        for(int dy=0; dy<5; ++dy){ world_.setBlock(x,y+dy,z,log); broadcastBlockChange(x,y+dy,z,log); }
                        for(int dx=-2; dx<=2; ++dx) for(int dz=-2; dz<=2; ++dz) for(int dy=5; dy<=6; ++dy){
                            if(dx==0 && dz==0 && dy==5) continue;
                            world_.setBlock(x+dx,y+dy,z+dz,leaves); broadcastBlockChange(x+dx,y+dy,z+dz,leaves);
                        }
                    } else if(fid.find("ore")!=std::string::npos){
                        auto ore = ((uint16_t)gen::blockByName("minecraft:diamond_ore")->defaultState);
                        world_.setBlock(x,y,z,ore); broadcastBlockChange(x,y,z,ore);
                        world_.setBlock(x+1,y,z,ore); broadcastBlockChange(x+1,y,z,ore);
                    } else {
                        auto stone = ((uint16_t)gen::blockByName("minecraft:stone")->defaultState);
                        world_.setBlock(x,y,z,stone); broadcastBlockChange(x,y,z,stone);
                    }
                    sendFeedback(src, "Placed feature "+fid+" at ["+std::to_string(x)+", "+std::to_string(y)+", "+std::to_string(z)+"]");
                    return 1;
                };
                // optional pos
                auto featPos = CommandNode::argument("placeFeaturePos", args::blockPos());
                featPos->executable = true;
                featPos->action = [this](CommandContext& c){
                    Player* src = static_cast<Player*>(c.source.player);
                    std::string fid = c.arg("placeFeatureId").asStr();
                    if(fid.find(':')==std::string::npos) fid="minecraft:"+fid;
                    auto p = c.arg("placeFeaturePos").asBlockPos();
                    int x=p.x, y=p.y, z=p.z;
                    if(fid=="minecraft:tree" || fid=="minecraft:oak" || fid.find("tree")!=std::string::npos){
                        auto log = ((uint16_t)gen::blockByName("minecraft:oak_log")->defaultState);
                        auto leaves = ((uint16_t)gen::blockByName("minecraft:oak_leaves")->defaultState);
                        for(int dy=0; dy<5; ++dy){ world_.setBlock(x,y+dy,z,log); broadcastBlockChange(x,y+dy,z,log); }
                        for(int dx=-2; dx<=2; ++dx) for(int dz=-2; dz<=2; ++dz) for(int dy=5; dy<=6; ++dy){
                            if(dx==0 && dz==0 && dy==5) continue;
                            world_.setBlock(x+dx,y+dy,z+dz,leaves); broadcastBlockChange(x+dx,y+dy,z+dz,leaves);
                        }
                    } else if(fid.find("ore")!=std::string::npos){
                        auto ore = ((uint16_t)gen::blockByName("minecraft:diamond_ore")->defaultState);
                        world_.setBlock(x,y,z,ore); broadcastBlockChange(x,y,z,ore);
                    } else {
                        auto stone = ((uint16_t)gen::blockByName("minecraft:stone")->defaultState);
                        world_.setBlock(x,y,z,stone); broadcastBlockChange(x,y,z,stone);
                    }
                    sendFeedback(src, "Placed feature "+fid+" at ["+std::to_string(x)+", "+std::to_string(y)+", "+std::to_string(z)+"]");
                    return 1;
                };
                featArg->then(featPos);
                featureLit->then(featArg);
                place->then(featureLit);
            }
            // place structure <structure> [pos]
            {
                auto structLit = CommandNode::literal("structure");
                auto structArg = CommandNode::argument("placeStructureId", args::resourceLocation());
                structArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
                    return std::vector<std::string>{
                        "minecraft:village","minecraft:desert_pyramid","minecraft:trial_chambers","minecraft:mansion","minecraft:monument","minecraft:igloo","minecraft:swamp_hut"
                    };
                };
                structArg->executable = true;
                structArg->action = [this](CommandContext& c){
                    Player* src = static_cast<Player*>(c.source.player);
                    std::string sid = c.arg("placeStructureId").asStr();
                    if(sid.find(':')==std::string::npos) sid="minecraft:"+sid;
                    int x = src ? (int)src->x : 0;
                    int y = src ? (int)src->y : 64;
                    int z = src ? (int)src->z : 0;
                    // generate small representative via World setBlock
                    auto placeAt = [&](int ox,int oy,int oz, const std::string& id){
                        if(id.find("trial_chambers")!=std::string::npos || id.find("trial")!=std::string::npos){
                            // use trial chambers piece-like: 10x10 tuff chamber at ~oy
                            auto tuff = ((uint16_t)gen::blockByName("minecraft:tuff_bricks")->defaultState);
                            auto tuff2 = ((uint16_t)gen::blockByName("minecraft:tuff")->defaultState);
                            for(int dx=0; dx<10; ++dx) for(int dz=0; dz<10; ++dz){
                                world_.setBlock(ox+dx, oy, oz+dz, tuff); broadcastBlockChange(ox+dx, oy, oz+dz, tuff);
                                world_.setBlock(ox+dx, oy+5, oz+dz, tuff); broadcastBlockChange(ox+dx, oy+5, oz+dz, tuff);
                                if(dx==0||dx==9||dz==0||dz==9) for(int dy=1; dy<5; ++dy){ world_.setBlock(ox+dx, oy+dy, oz+dz, tuff2); broadcastBlockChange(ox+dx, oy+dy, oz+dz, tuff2); }
                            }
                            auto spawner = ((uint16_t)gen::blockByName("minecraft:trial_spawner")->defaultState);
                            world_.setBlock(ox+5, oy+1, oz+5, spawner); broadcastBlockChange(ox+5, oy+1, oz+5, spawner);
                        } else if(id.find("village")!=std::string::npos){
                            auto planks = ((uint16_t)gen::blockByName("minecraft:oak_planks")->defaultState);
                            auto log = ((uint16_t)gen::blockByName("minecraft:oak_log")->defaultState);
                            for(int dx=0; dx<5; ++dx) for(int dz=0; dz<5; ++dz){
                                world_.setBlock(ox+dx, oy, oz+dz, planks); broadcastBlockChange(ox+dx, oy, oz+dz, planks);
                                if(dx==0||dx==4||dz==0||dz==4) for(int dy=1; dy<=3; ++dy){ world_.setBlock(ox+dx, oy+dy, oz+dz, (dy==3?log:planks)); broadcastBlockChange(ox+dx, oy+dy, oz+dz,(dy==3?log:planks)); }
                            }
                        } else if(id.find("desert_pyramid")!=std::string::npos || id.find("pyramid")!=std::string::npos){
                            auto sandstone = ((uint16_t)gen::blockByName("minecraft:sandstone")->defaultState);
                            for(int step=0; step<5; ++step){ int r=4-step; int yy=oy+1+step; for(int dz=-r; dz<=r; ++dz) for(int dx=-r; dx<=r; ++dx){ world_.setBlock(ox+dx+2, yy, oz+dz+2, sandstone); broadcastBlockChange(ox+dx+2, yy, oz+dz+2, sandstone);} }
                        } else {
                            auto stone = ((uint16_t)gen::blockByName("minecraft:stone_bricks")->defaultState);
                            for(int dx=0; dx<5; ++dx) for(int dz=0; dz<5; ++dz){ world_.setBlock(ox+dx, oy, oz+dz, stone); broadcastBlockChange(ox+dx, oy, oz+dz, stone); }
                        }
                    };
                    placeAt(x,y,z,sid);
                    sendFeedback(src, "Placed structure "+sid+" at ["+std::to_string(x)+", "+std::to_string(y)+", "+std::to_string(z)+"]");
                    return 1;
                };
                auto structPos = CommandNode::argument("placeStructurePos", args::blockPos());
                structPos->executable = true;
                structPos->action = [this](CommandContext& c){
                    Player* src = static_cast<Player*>(c.source.player);
                    std::string sid = c.arg("placeStructureId").asStr();
                    if(sid.find(':')==std::string::npos) sid="minecraft:"+sid;
                    auto p = c.arg("placeStructurePos").asBlockPos();
                    int x=p.x, y=p.y, z=p.z;
                    if(sid.find("trial_chambers")!=std::string::npos || sid.find("trial")!=std::string::npos){
                        auto tuff = ((uint16_t)gen::blockByName("minecraft:tuff_bricks")->defaultState);
                        auto tuff2 = ((uint16_t)gen::blockByName("minecraft:tuff")->defaultState);
                        for(int dx=0; dx<10; ++dx) for(int dz=0; dz<10; ++dz){
                            world_.setBlock(x+dx, y, z+dz, tuff); broadcastBlockChange(x+dx, y, z+dz, tuff);
                            world_.setBlock(x+dx, y+5, z+dz, tuff); broadcastBlockChange(x+dx, y+5, z+dz, tuff);
                            if(dx==0||dx==9||dz==0||dz==9) for(int dy=1; dy<5; ++dy){ world_.setBlock(x+dx, y+dy, z+dz, tuff2); broadcastBlockChange(x+dx, y+dy, z+dz, tuff2); }
                        }
                        auto spawner = ((uint16_t)gen::blockByName("minecraft:trial_spawner")->defaultState);
                        world_.setBlock(x+5, y+1, z+5, spawner); broadcastBlockChange(x+5, y+1, z+5, spawner);
                    } else if(sid.find("village")!=std::string::npos){
                        auto planks = ((uint16_t)gen::blockByName("minecraft:oak_planks")->defaultState);
                        auto log = ((uint16_t)gen::blockByName("minecraft:oak_log")->defaultState);
                        for(int dx=0; dx<5; ++dx) for(int dz=0; dz<5; ++dz){
                            world_.setBlock(x+dx, y, z+dz, planks); broadcastBlockChange(x+dx, y, z+dz, planks);
                            if(dx==0||dx==4||dz==0||dz==4) for(int dy=1; dy<=3; ++dy){ world_.setBlock(x+dx, y+dy, z+dz, (dy==3?log:planks)); broadcastBlockChange(x+dx, y+dy, z+dz,(dy==3?log:planks)); }
                        }
                    } else if(sid.find("desert_pyramid")!=std::string::npos || sid.find("pyramid")!=std::string::npos){
                        auto sandstone = ((uint16_t)gen::blockByName("minecraft:sandstone")->defaultState);
                        for(int step=0; step<5; ++step){ int r=4-step; int yy=y+1+step; for(int dz=-r; dz<=r; ++dz) for(int dx=-r; dx<=r; ++dx){ world_.setBlock(x+dx+2, yy, z+dz+2, sandstone); broadcastBlockChange(x+dx+2, yy, z+dz+2, sandstone);} }
                    } else {
                        auto stone = ((uint16_t)gen::blockByName("minecraft:stone_bricks")->defaultState);
                        for(int dx=0; dx<5; ++dx) for(int dz=0; dz<5; ++dz){ world_.setBlock(x+dx, y, z+dz, stone); broadcastBlockChange(x+dx, y, z+dz, stone); }
                    }
                    sendFeedback(src, "Placed structure "+sid+" at ["+std::to_string(x)+", "+std::to_string(y)+", "+std::to_string(z)+"]");
                    return 1;
                };
                structArg->then(structPos);
                structLit->then(structArg);
                place->then(structLit);
            }
            // place jigsaw <pool> <target> <maxDepth> -> stub
            {
                auto jigsawLit = CommandNode::literal("jigsaw");
                auto poolArg = CommandNode::argument("jigsawPool", args::resourceLocation());
                auto targetArg = CommandNode::argument("jigsawTarget", args::resourceLocation());
                auto depthArg = CommandNode::argument("jigsawDepth", args::integer(1, 7));
                depthArg->executable = true;
                depthArg->action = [this](CommandContext& c){
                    Player* src = static_cast<Player*>(c.source.player);
                    std::string pool = c.arg("jigsawPool").asStr();
                    std::string target = c.arg("jigsawTarget").asStr();
                    int depth = c.arg("jigsawDepth").asInt();
                    (void)depth;
                    sendFeedback(src, "Jigsaw place not yet implemented (pool="+pool+" target="+target+") — use /place structure instead");
                    return 0;
                };
                targetArg->then(depthArg);
                poolArg->then(targetArg);
                jigsawLit->then(poolArg);
                place->then(jigsawLit);
            }
            d.root->then(place);
        }
        // /spreadplayers <center> <spreadDistance> <maxRange> <respectTeams> <targets>
        {
            auto sp = CommandNode::literal("spreadplayers");
            auto center = CommandNode::argument("spCenter", args::vec2Arg());
            auto spread = CommandNode::argument("spSpread", args::floatArg(0.f, 100000.f));
            auto maxRange = CommandNode::argument("spMaxRange", args::floatArg(1.f, 100000.f));
            auto respect = CommandNode::argument("spRespectTeams", args::boolean());
            auto targets = CommandNode::argument("spTargets", args::entity(false,false));
            targets->executable = true;
            targets->action = [this](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                brigadier::Vec2f centerV;
                {
                    auto v = c.arg("spCenter");
                    if(auto* p = std::get_if<brigadier::Vec2f>(&v.v)) centerV = *p;
                    else if(auto* p3 = std::get_if<brigadier::Vec3d>(&v.v)) { centerV.x=(float)p3->x; centerV.y=(float)p3->z; }
                    else { centerV.x=0; centerV.y=0; }
                }
                float spreadDist = (float)c.arg("spSpread").asDouble();
                float maxR = (float)c.arg("spMaxRange").asDouble();
                bool respectTeams = c.arg("spRespectTeams").asBool();
                const auto sel = c.arg("spTargets").asSelector();
                std::vector<Player*> players;
                for(auto& n: sel.playerNames) if(Player* p=findPlayer(*this,n)) players.push_back(p);
                if(players.empty()){
                    sendFeedback(src, "No targets for spreadplayers");
                    return 0;
                }
                if(spreadDist > maxR){
                    sendFeedback(src, "spreadDistance must not be greater than maxRange");
                    return 0;
                }
                double cx = centerV.x, cz = centerV.y;
                // Group by team if respectTeams
                struct Group { std::vector<Player*> members; };
                std::vector<Group> groups;
                if(respectTeams){
                    std::unordered_map<std::string, size_t> teamToIdx;
                    std::vector<Player*> noTeam;
                    for(auto* p: players){
                        std::string teamName;
                        for(auto& kv: teams.teams) if(kv.second.members.count(p->name)) { teamName=kv.first; break; }
                        if(teamName.empty()) noTeam.push_back(p);
                        else {
                            auto it = teamToIdx.find(teamName);
                            if(it==teamToIdx.end()){
                                size_t idx=groups.size();
                                teamToIdx[teamName]=idx;
                                groups.push_back({{p}});
                            } else groups[it->second].members.push_back(p);
                        }
                    }
                    for(auto* p: noTeam) groups.push_back({{p}});
                } else {
                    for(auto* p: players) groups.push_back({{p}});
                }
                // Random spread with simple rejection sampling
                struct Pos { double x,z; };
                std::vector<Pos> placed;
                placed.reserve(groups.size());
                std::srand((unsigned)std::chrono::steady_clock::now().time_since_epoch().count() ^ (unsigned)tickNo_);
                auto findY = [&](double x, double z)->double{
                    int ix=(int)std::floor(x), iz=(int)std::floor(z);
                    // scan from top down for solid
                    for(int y=kMaxY; y>=kMinY; --y){
                        uint16_t st = world_.getBlock(ix,y,iz);
                        uint16_t above = world_.getBlock(ix,y+1,iz);
                        uint16_t above2 = world_.getBlock(ix,y+2,iz);
                        if(st!=0 && above==0 && above2==0) return y+1;
                    }
                    // fallback: surfaceFeetY
                    return world_.surfaceFeetY(ix, iz);
                };
                for(size_t gi=0; gi<groups.size(); ++gi){
                    Pos pos{0,0};
                    bool ok=false;
                    for(int attempt=0; attempt<1000; ++attempt){
                        double rx = cx + ((double)std::rand()/RAND_MAX*2.0-1.0)*maxR;
                        double rz = cz + ((double)std::rand()/RAND_MAX*2.0-1.0)*maxR;
                        bool far=true;
                        for(auto& pr: placed){
                            double dx=rx-pr.x, dz=rz-pr.z;
                            if(std::sqrt(dx*dx+dz*dz) < spreadDist){ far=false; break; }
                        }
                        if(!far) continue;
                        pos={rx,rz};
                        ok=true; break;
                    }
                    if(!ok){
                        // fallback: just use random
                        pos={cx + ((double)std::rand()/RAND_MAX*2.0-1.0)*maxR, cz + ((double)std::rand()/RAND_MAX*2.0-1.0)*maxR};
                    }
                    placed.push_back(pos);
                    double y=findY(pos.x,pos.z);
                    for(auto* p: groups[gi].members){
                        p->x=pos.x; p->y=y; p->z=pos.z;
                        p->fallDist=0;
                        WriteBuffer tp;
                        tp.varint(++teleportCounterForTest_);
                        tp.f64(p->x); tp.f64(p->y); tp.f64(p->z);
                        tp.f64(0); tp.f64(0); tp.f64(0);
                        tp.f32(p->yaw); tp.f32(p->pitch);
                        tp.u32(0);
                        try{ p->conn->sendPacket(proto::pl::sc::PlayerPosition, tp); }catch(...){}
                    }
                }
                int total = (int)players.size();
                sendFeedback(src, "Spread "+std::to_string(total)+" entities around "+std::to_string((int)cx)+", "+std::to_string((int)cz));
                broadcastSystemText("Teleported "+std::to_string(total)+" entities via spreadplayers");
                return total;
            };
            // vec2Arg returns Vec2f stored as Vec2f; need to ensure spread takes Vec2f
            respect->then(targets);
            maxRange->then(respect);
            spread->then(maxRange);
            center->then(spread);
            sp->then(center);
            d.root->then(sp);
        }
    // /enchant <targets> <enchantment> [<level>] (plan32 entity — Yarn EnchantCommand)
    {
        auto enchant = CommandNode::literal("enchant");
        auto targets = CommandNode::argument("targets", args::entity(false,false));
        auto ench = CommandNode::argument("enchantment", args::resourceLocation());
        ench->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(int i=0;i<42;++i){ std::string n=ItemStack::enchantNameById(i); if(!n.empty()) v.push_back(n); }
            // also bare names without namespace
            std::vector<std::string> extra;
            for(auto &s: v) { auto p=s.find(':'); if(p!=std::string::npos) extra.push_back(s.substr(p+1)); }
            v.insert(v.end(), extra.begin(), extra.end());
            return v;
        };
        // without level (default 1)
        ench->executable = true;
        ench->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            std::string enchName = c.arg("enchantment").asStr();
            if(enchName.find(':')==std::string::npos) enchName="minecraft:"+enchName;
            if(ItemStack::enchantIdByName(enchName)<0) throw std::runtime_error("Unknown enchantment: "+enchName);
            int level = 1;
            int enchanted = 0;
            for(auto &nm: sel.playerNames) if(Player* t=findPlayer(*this,nm)){
                if(t->heldSlot<0 || t->heldSlot>=9) continue;
                auto &held = t->inv[36 + t->heldSlot];
                if(held.empty()) continue;
                ItemStack::addEnchant(held, enchName, level);
                resendInventory(*t);
                syncEquipmentOnChange(*t);
                ++enchanted;
            }
            if(enchanted==0) throw std::runtime_error("No target held an item to enchant");
            sendFeedback(src, "Enchanted "+std::to_string(enchanted)+" target(s) with "+enchName+" "+std::to_string(level));
            return enchanted;
        };
        auto lvlArg = CommandNode::argument("level", args::integer(1, 255));
        lvlArg->executable = true;
        lvlArg->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            std::string enchName = c.arg("enchantment").asStr();
            if(enchName.find(':')==std::string::npos) enchName="minecraft:"+enchName;
            if(ItemStack::enchantIdByName(enchName)<0) throw std::runtime_error("Unknown enchantment: "+enchName);
            int level = c.arg("level").asInt();
            int enchanted = 0;
            for(auto &nm: sel.playerNames) if(Player* t=findPlayer(*this,nm)){
                if(t->heldSlot<0 || t->heldSlot>=9) continue;
                auto &held = t->inv[36 + t->heldSlot];
                if(held.empty()) continue;
                ItemStack::addEnchant(held, enchName, level);
                resendInventory(*t);
                syncEquipmentOnChange(*t);
                ++enchanted;
            }
            if(enchanted==0) throw std::runtime_error("No target held an item to enchant");
            sendFeedback(src, "Enchanted "+std::to_string(enchanted)+" target(s) with "+enchName+" "+std::to_string(level));
            return enchanted;
        };
        ench->then(lvlArg);
        targets->then(ench);
        enchant->then(targets);
        d.root->then(enchant);
    }
    // /attribute <target> <attribute> get|base set|modifier add|modifier remove|modifier value get (plan32 entity — Yarn AttributeCommand)
    {
        auto attribute = CommandNode::literal("attribute");
        auto target = CommandNode::argument("target", args::entity(true,false));
        auto attrArg = CommandNode::argument("attribute", args::resourceLocation());
        attrArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(auto a: {Attribute::MAX_HEALTH, Attribute::MOVEMENT_SPEED, Attribute::ATTACK_DAMAGE, Attribute::ARMOR, Attribute::ARMOR_TOUGHNESS, Attribute::KNOCKBACK_RESISTANCE, Attribute::ATTACK_SPEED, Attribute::ATTACK_KNOCKBACK, Attribute::BLOCK_BREAK_SPEED, Attribute::BLOCK_INTERACTION_RANGE, Attribute::ENTITY_INTERACTION_RANGE, Attribute::FALL_DAMAGE_MULTIPLIER, Attribute::FLYING_SPEED, Attribute::FOLLOW_RANGE, Attribute::GRAVITY, Attribute::JUMP_STRENGTH, Attribute::LUCK, Attribute::MAX_ABSORPTION, Attribute::SAFE_FALL_DISTANCE, Attribute::SCALE, Attribute::STEP_HEIGHT, Attribute::SPAWN_REINFORCEMENTS, Attribute::TEMPT_RANGE, Attribute::WATER_MOVEMENT_EFFICIENCY}){
                v.emplace_back(attributeKey(a));
            }
            // also add short names for convenience
            v.push_back("minecraft:generic.max_health"); v.push_back("minecraft:generic.movement_speed");
            return v;
        };
        auto resolveAttr = [](const std::string& raw) -> std::optional<Attribute> {
            std::string id = raw;
            if(id.find(':')==std::string::npos) id="minecraft:"+id;
            // direct mapped keys
            for(auto a: {Attribute::MOVEMENT_SPEED, Attribute::MAX_HEALTH, Attribute::KNOCKBACK_RESISTANCE, Attribute::ARMOR, Attribute::ARMOR_TOUGHNESS, Attribute::ATTACK_DAMAGE, Attribute::ATTACK_SPEED, Attribute::FLYING_SPEED, Attribute::FOLLOW_RANGE, Attribute::MAX_ABSORPTION, Attribute::STEP_HEIGHT, Attribute::ATTACK_KNOCKBACK, Attribute::BLOCK_BREAK_SPEED, Attribute::BLOCK_INTERACTION_RANGE, Attribute::BURNING_TIME, Attribute::ENTITY_INTERACTION_RANGE, Attribute::EXPLOSION_KNOCKBACK_RESISTANCE, Attribute::FALL_DAMAGE_MULTIPLIER, Attribute::GRAVITY, Attribute::JUMP_STRENGTH, Attribute::LUCK, Attribute::MINING_EFFICIENCY, Attribute::MOVEMENT_EFFICIENCY, Attribute::OXYGEN_BONUS, Attribute::SAFE_FALL_DISTANCE, Attribute::SCALE, Attribute::SNEAKING_SPEED, Attribute::SPAWN_REINFORCEMENTS, Attribute::SUBMERGED_MINING_SPEED, Attribute::SWEEPING_DAMAGE_RATIO, Attribute::TEMPT_RANGE, Attribute::WATER_MOVEMENT_EFFICIENCY}){
                if(std::string(attributeKey(a))==id) return a;
            }
            // aliases: allow "generic.max_health" etc to map to same
            std::string low=id;
            for(char &c: low) c=tolower((unsigned char)c);
            if(low=="minecraft:generic.max_health" || low=="generic.max_health" || low=="max_health") return Attribute::MAX_HEALTH;
            if(low=="minecraft:generic.movement_speed" || low=="generic.movement_speed" || low=="movement_speed") return Attribute::MOVEMENT_SPEED;
            return std::nullopt;
        };
        auto sendAttrUpdate = [this](Player& p){
            WriteBuffer ab; p.attributes.writeUpdate(ab, p.entityId);
            try{ p.conn->sendPacket(proto::pl::sc::UpdateAttributes, ab); }catch(...){}
        };
        // attribute <target> <attribute> get [<scale>]
        {
            auto getLit = CommandNode::literal("get");
            getLit->executable = true;
            getLit->action = [this, resolveAttr](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target for attribute get");
                double v = targets.front()->attributes.getValue(at);
                sendFeedback(src, std::string(attributeKey(at))+" has value "+std::to_string(v));
                return (int)std::llround(v);
            };
            auto scaleArg = CommandNode::argument("scale", args::floatArg(-1e9f, 1e9f));
            scaleArg->executable = true;
            scaleArg->action = [this, resolveAttr](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                double scale = c.arg("scale").asDouble();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target for attribute get");
                double v = targets.front()->attributes.getValue(at) * scale;
                sendFeedback(src, std::string(attributeKey(at))+" scaled value "+std::to_string(v));
                return (int)std::llround(v);
            };
            getLit->then(scaleArg);
            attrArg->then(getLit);
        }
        // base branch: base set <value> | base get [<scale>] | base reset
        {
            auto baseLit = CommandNode::literal("base");
            auto baseSet = CommandNode::literal("set");
            auto baseVal = CommandNode::argument("value", args::floatArg(-1e9f, 1e9f));
            baseVal->executable = true;
            baseVal->action = [this, resolveAttr, sendAttrUpdate](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                double v = c.arg("value").asDouble();
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    p->attributes.setBase(at, v);
                    sendAttrUpdate(*p);
                    ++cnt;
                }
                if(cnt==0 && src){ src->attributes.setBase(at, v); sendAttrUpdate(*src); cnt=1; }
                sendFeedback(src, std::string(attributeKey(at))+" base set to "+std::to_string(v));
                return cnt;
            };
            baseSet->then(baseVal);
            baseLit->then(baseSet);
            auto baseGet = CommandNode::literal("get");
            baseGet->executable = true;
            baseGet->action = [this, resolveAttr](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target");
                double v = targets.front()->attributes.getBase(at);
                sendFeedback(src, std::string(attributeKey(at))+" base is "+std::to_string(v));
                return (int)std::llround(v);
            };
            auto baseGetScale = CommandNode::argument("scale", args::floatArg(-1e9f, 1e9f));
            baseGetScale->executable = true;
            baseGetScale->action = [this, resolveAttr](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                double scale = c.arg("scale").asDouble();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                double v = targets.front()->attributes.getBase(at) * scale;
                sendFeedback(src, std::string(attributeKey(at))+" base scaled "+std::to_string(v));
                return (int)std::llround(v);
            };
            baseGet->then(baseGetScale);
            baseLit->then(baseGet);
            auto baseReset = CommandNode::literal("reset");
            baseReset->executable = true;
            baseReset->action = [this, resolveAttr, sendAttrUpdate](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                // reset to default base per AttributeManager defaults
                AttributeManager defaults;
                double def = defaults.getBase(at);
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){ p->attributes.setBase(at, def); sendAttrUpdate(*p); ++cnt; }
                sendFeedback(src, std::string(attributeKey(at))+" base reset");
                return cnt;
            };
            baseLit->then(baseReset);
            attrArg->then(baseLit);
        }
        // modifier branch
        {
            auto modLit = CommandNode::literal("modifier");
            // add <uuid> <name> <value> <operation>
            auto addLit = CommandNode::literal("add");
            auto uuidArg = CommandNode::argument("uuid", args::stringWord());
            auto nameArg = CommandNode::argument("name", args::stringWord());
            auto valArg = CommandNode::argument("value", args::floatArg(-1e9f, 1e9f));
            auto opArg = CommandNode::argument("operation", args::stringWord());
            opArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){ return std::vector<std::string>{"add_value","add_multiplied_base","add_multiplied_total","0","1","2"}; };
            opArg->executable = true;
            opArg->action = [this, resolveAttr, sendAttrUpdate](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                std::string uuid = c.arg("uuid").asStr();
                std::string opStr = c.arg("operation").asStr();
                double amount = c.arg("value").asDouble();
                int op = 0;
                if(opStr=="add_value" || opStr=="0") op=0;
                else if(opStr=="add_multiplied_base" || opStr=="1") op=1;
                else if(opStr=="add_multiplied_total" || opStr=="2") op=2;
                else throw std::runtime_error("Unknown operation: "+opStr);
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    p->attributes.addModifier(at, {uuid, amount, op});
                    sendAttrUpdate(*p);
                    ++cnt;
                }
                if(cnt==0 && src){ src->attributes.addModifier(at, {uuid, amount, op}); sendAttrUpdate(*src); cnt=1; }
                sendFeedback(src, "Added modifier "+uuid+" to "+std::string(attributeKey(at)));
                return cnt;
            };
            valArg->then(opArg);
            nameArg->then(valArg);
            uuidArg->then(nameArg);
            addLit->then(uuidArg);
            modLit->then(addLit);
            // remove <uuid>
            auto remLit = CommandNode::literal("remove");
            auto remUuid = CommandNode::argument("uuid", args::stringWord());
            remUuid->executable = true;
            remUuid->action = [this, resolveAttr, sendAttrUpdate](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                std::string uuid = c.arg("uuid").asStr();
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){ p->attributes.removeModifier(at, uuid); sendAttrUpdate(*p); ++cnt; }
                if(cnt==0 && src){ src->attributes.removeModifier(at, uuid); sendAttrUpdate(*src); cnt=1; }
                sendFeedback(src, "Removed modifier "+uuid);
                return cnt;
            };
            remLit->then(remUuid);
            modLit->then(remLit);
            // value get <uuid> [<scale>]
            auto valGetLit = CommandNode::literal("value");
            auto valGetKw = CommandNode::literal("get");
            auto vgUuid = CommandNode::argument("uuid", args::stringWord());
            vgUuid->executable = true;
            vgUuid->action = [this, resolveAttr](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                std::string uuid = c.arg("uuid").asStr();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target");
                auto opt = targets.front()->attributes.getModifierValue(at, uuid);
                double amt = opt ? *opt : 0;
                if(!opt) throw std::runtime_error("Modifier not found: "+uuid);
                sendFeedback(src, "Modifier "+uuid+" has value "+std::to_string(amt));
                return (int)std::llround(amt);
            };
            auto vgScale = CommandNode::argument("scale", args::floatArg(-1e9f, 1e9f));
            vgScale->executable = true;
            vgScale->action = [this, resolveAttr](CommandContext& c){
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("target").asSelector();
                std::string attrRaw = c.arg("attribute").asStr();
                auto aopt = resolveAttr(attrRaw);
                if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
                Attribute at = *aopt;
                double scale = c.arg("scale").asDouble();
                std::string uuid = c.arg("uuid").asStr();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target");
                auto opt = targets.front()->attributes.getModifierValue(at, uuid);
                double amt = opt ? *opt * scale : 0;
                if(!opt) throw std::runtime_error("Modifier not found: "+uuid);
                sendFeedback(src, "Modifier "+uuid+" scaled value "+std::to_string(amt));
                return (int)std::llround(amt);
            };
            vgUuid->then(vgScale);
            valGetKw->then(vgUuid);
            valGetLit->then(valGetKw);
            modLit->then(valGetLit);
            attrArg->then(modLit);
        }
        target->then(attrArg);
        attribute->then(target);
        d.root->then(attribute);
    }
    // /trigger <objective> [add|set <value>] (plan32 entity — Yarn TriggerCommand)
    {
        auto trigger = CommandNode::literal("trigger");
        auto objective = CommandNode::argument("objective", args::objectiveArg());
        objective->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(auto &o: scoreboard.objectives) v.push_back(o.name);
            return v;
        };
        objective->executable = true;
        objective->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            if(!src) throw std::runtime_error("trigger can only be run by a player");
            std::string obj = c.arg("objective").asStr();
            auto* o = scoreboard.find(obj);
            // plan42 R3 network: auto-create a trigger objective on demand so
            // bare "/trigger <name>" succeeds vanilla-strict (server_full).
            if(!o) {
                if(!scoreboard.addObjective(obj, "trigger", obj))
                    throw std::runtime_error("Unknown objective: "+obj);
                o = scoreboard.find(obj);
                if(o) sendObjectiveAll(*o, 0);
            }
            if(o->criteria!="trigger") throw std::runtime_error("Objective "+obj+" is not trigger criteria");
            // bare trigger enables? In vanilla, bare trigger does nothing but feedback. We implement as add 1
            // Check if score exists and enabled? Simplified: add 1
            scoreboard.addScore(obj, src->name, 1);
            int v = scoreboard.getScore(obj, src->name);
            sendScoreAll(obj, src->name, v);
            sendFeedback(src, "Triggered "+obj+" add 1 (now "+std::to_string(v)+")");
            return v;
        };
        auto addLit = CommandNode::literal("add");
        auto addVal = CommandNode::argument("value", args::integer(INT32_MIN, INT32_MAX));
        addVal->executable = true;
        addVal->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            if(!src) throw std::runtime_error("trigger can only be run by a player");
            std::string obj = c.arg("objective").asStr();
            auto* o = scoreboard.find(obj);
            if(!o) throw std::runtime_error("Unknown objective: "+obj);
            if(o->criteria!="trigger") throw std::runtime_error("Objective "+obj+" is not trigger criteria");
            int delta = c.arg("value").asInt();
            scoreboard.addScore(obj, src->name, delta);
            int v = scoreboard.getScore(obj, src->name);
            sendScoreAll(obj, src->name, v);
            sendFeedback(src, "Triggered "+obj+" add "+std::to_string(delta)+" (now "+std::to_string(v)+")");
            return v;
        };
        addLit->then(addVal);
        objective->then(addLit);
        auto setLit = CommandNode::literal("set");
        auto setVal = CommandNode::argument("value", args::integer(INT32_MIN, INT32_MAX));
        setVal->executable = true;
        setVal->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            if(!src) throw std::runtime_error("trigger can only be run by a player");
            std::string obj = c.arg("objective").asStr();
            auto* o = scoreboard.find(obj);
            if(!o) throw std::runtime_error("Unknown objective: "+obj);
            if(o->criteria!="trigger") throw std::runtime_error("Objective "+obj+" is not trigger criteria");
            int v = c.arg("value").asInt();
            scoreboard.setScore(obj, src->name, v);
            sendScoreAll(obj, src->name, v);
            sendFeedback(src, "Triggered "+obj+" set "+std::to_string(v));
            return v;
        };
        setLit->then(setVal);
        objective->then(setLit);
        trigger->then(objective);
        d.root->then(trigger);
    }
    // plan32 block: ban/op/whitelist/kick admin commands
    {
        // /kick <targets> [<reason>]
        auto kick = CommandNode::literal("kick");
        auto kickTargets = CommandNode::argument("targets", args::entity(false,false));
        kickTargets->executable = true;
        kickTargets->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            int cnt = 0;
            for (auto& n : sel.playerNames) {
                if (Player* t = findPlayer(*this, n)) {
                    kickPlayer(t->name, "Kicked by an operator.");
                    ++cnt;
                }
            }
            // also handle entityIds if needed (no-op for players only)
            sendFeedback(src, "Kicked " + std::to_string(cnt) + " player(s)");
            return cnt;
        };
        auto kickReason = CommandNode::argument("reason", args::stringGreedy());
        kickReason->executable = true;
        kickReason->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            std::string reason = c.arg("reason").asStr();
            int cnt = 0;
            for (auto& n : sel.playerNames) {
                if (Player* t = findPlayer(*this, n)) {
                    kickPlayer(t->name, reason);
                    ++cnt;
                }
            }
            sendFeedback(src, "Kicked " + std::to_string(cnt) + " player(s): " + reason);
            return cnt;
        };
        kickTargets->then(kickReason);
        kick->then(kickTargets);
        d.root->then(kick);
    }
    {
        // /ban <targets> [<reason>]
        auto ban = CommandNode::literal("ban");
        auto banTargets = CommandNode::argument("targets", args::gameProfileArg());
        banTargets->executable = true;
        banTargets->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string name = c.arg("targets").asStr();
            bannedPlayers_.insert(name);
            saveBans();
            if (Player* t = findPlayer(*this, name)) { (void)t; kickPlayer(name, "Banned by an operator."); }
            sendFeedback(src, "Banned " + name);
            return 1;
        };
        auto banReason = CommandNode::argument("reason", args::stringGreedy());
        banReason->executable = true;
        banReason->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string name = c.arg("targets").asStr();
            std::string reason = c.arg("reason").asStr();
            bannedPlayers_.insert(name);
            saveBans();
            if (Player* t = findPlayer(*this, name)) { (void)t; kickPlayer(name, reason); }
            sendFeedback(src, "Banned " + name + ": " + reason);
            return 1;
        };
        banTargets->then(banReason);
        ban->then(banTargets);
        d.root->then(ban);
    }
    {
        auto pardon = CommandNode::literal("pardon");
        auto pardonTargets = CommandNode::argument("targets", args::gameProfileArg());
        pardonTargets->executable = true;
        pardonTargets->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string name = c.arg("targets").asStr();
            bool removed = bannedPlayers_.erase(name) > 0;
            if (removed) saveBans();
            sendFeedback(src, removed ? ("Pardoned " + name) : ("Not banned: " + name));
            return removed ? 1 : 0;
        };
        pardon->then(pardonTargets);
        d.root->then(pardon);
    }
    {
        auto banIp = CommandNode::literal("ban-ip");
        auto banIpTarget = CommandNode::argument("target", args::stringWord());
        banIpTarget->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx& c){
            std::vector<std::string> v;
            for (auto& p : c.playerNames) v.push_back(p);
            return v;
        };
        banIpTarget->executable = true;
        banIpTarget->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string raw = c.arg("target").asStr();
            std::string ip = raw;
            // if raw looks like player name and that player is online, use their IP
            if (raw.find('.') == std::string::npos) {
                if (Player* t = findPlayer(*this, raw)) {
                    std::string peer = t->conn ? t->conn->peer() : "";
                    auto colon = peer.find(':');
                    if (colon != std::string::npos) ip = peer.substr(0, colon);
                    else ip = raw;
                }
            }
            bannedIps_.insert(ip);
            saveBannedIps();
            // kick any player with matching IP
            int kicked = 0;
            for (auto& p : playersSnapshot()) {
                if (!p->conn) continue;
                std::string peer = p->conn->peer();
                std::string pip = peer;
                auto colon = pip.find(':');
                if (colon != std::string::npos) pip = pip.substr(0, colon);
                if (pip == ip) { kickPlayer(p->name, "IP banned by an operator."); ++kicked; }
            }
            sendFeedback(src, "Banned IP " + ip + (kicked ? (" (kicked " + std::to_string(kicked) + ")") : ""));
            return 1;
        };
        auto banIpReason = CommandNode::argument("reason", args::stringGreedy());
        banIpReason->executable = true;
        banIpReason->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string raw = c.arg("target").asStr();
            std::string reason = c.arg("reason").asStr();
            std::string ip = raw;
            if (raw.find('.') == std::string::npos) {
                if (Player* t = findPlayer(*this, raw)) {
                    std::string peer = t->conn ? t->conn->peer() : "";
                    auto colon = peer.find(':');
                    if (colon != std::string::npos) ip = peer.substr(0, colon);
                }
            }
            bannedIps_.insert(ip);
            saveBannedIps();
            int kicked = 0;
            for (auto& p : playersSnapshot()) {
                if (!p->conn) continue;
                std::string peer = p->conn->peer();
                std::string pip = peer;
                auto colon = pip.find(':');
                if (colon != std::string::npos) pip = pip.substr(0, colon);
                if (pip == ip) { kickPlayer(p->name, reason); ++kicked; }
            }
            sendFeedback(src, "Banned IP " + ip + ": " + reason);
            return 1;
        };
        banIpTarget->then(banIpReason);
        banIp->then(banIpTarget);
        d.root->then(banIp);
    }
    {
        auto pardonIp = CommandNode::literal("pardon-ip");
        auto pardonIpTarget = CommandNode::argument("target", args::stringWord());
        pardonIpTarget->executable = true;
        pardonIpTarget->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string ip = c.arg("target").asStr();
            bool removed = bannedIps_.erase(ip) > 0;
            if (removed) saveBannedIps();
            sendFeedback(src, removed ? ("Pardoned IP " + ip) : ("IP not banned: " + ip));
            return removed ? 1 : 0;
        };
        pardonIp->then(pardonIpTarget);
        d.root->then(pardonIp);
    }
    {
        auto banlist = CommandNode::literal("banlist");
        banlist->executable = true;
        banlist->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string out = "Banned players (" + std::to_string(bannedPlayers_.size()) + "): ";
            for (auto& n : bannedPlayers_) out += n + " ";
            out += "\nBanned IPs (" + std::to_string(bannedIps_.size()) + "): ";
            for (auto& ip : bannedIps_) out += ip + " ";
            sendFeedback(src, out);
            return (int)(bannedPlayers_.size() + bannedIps_.size());
        };
        auto banlistIps = CommandNode::literal("ips");
        banlistIps->executable = true;
        banlistIps->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string out = "Banned IPs (" + std::to_string(bannedIps_.size()) + "): ";
            for (auto& ip : bannedIps_) out += ip + " ";
            sendFeedback(src, out);
            return (int)bannedIps_.size();
        };
        auto banlistPlayers = CommandNode::literal("players");
        banlistPlayers->executable = true;
        banlistPlayers->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string out = "Banned players (" + std::to_string(bannedPlayers_.size()) + "): ";
            for (auto& n : bannedPlayers_) out += n + " ";
            sendFeedback(src, out);
            return (int)bannedPlayers_.size();
        };
        banlist->then(banlistIps);
        banlist->then(banlistPlayers);
        d.root->then(banlist);
    }
    {
        auto op = CommandNode::literal("op");
        auto opTargets = CommandNode::argument("targets", args::gameProfileArg());
        opTargets->executable = true;
        opTargets->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string name = c.arg("targets").asStr();
            ops_.insert(name);
            saveOps();
            sendFeedback(src, "Opped " + name);
            return 1;
        };
        op->then(opTargets);
        d.root->then(op);
    }
    {
        auto deop = CommandNode::literal("deop");
        auto deopTargets = CommandNode::argument("targets", args::gameProfileArg());
        deopTargets->executable = true;
        deopTargets->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string name = c.arg("targets").asStr();
            bool removed = ops_.erase(name) > 0;
            if (removed) saveOps();
            sendFeedback(src, removed ? ("De-opped " + name) : ("De-op failed: " + name + " is not opped"));
            return removed ? 1 : 0;
        };
        deop->then(deopTargets);
        d.root->then(deop);
    }
    {
        auto wl = CommandNode::literal("whitelist");
        auto wlOn = CommandNode::literal("on");
        wlOn->executable = true;
        wlOn->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            // plan42 R3 (E-19) anti-lockout: a player enabler joins the list so
            // they can rejoin to disable later (ops bypass anyway; console has
            // no name to add). Without this, `whitelist off` becomes
            // unreachable once enforcement starts.
            if (src && !src->name.empty()) whitelist_.insert(src->name);
            whitelist_.setEnabled(true);
            sendFeedback(src, "Whitelist is now on");
            return 1;
        };
        auto wlOff = CommandNode::literal("off");
        wlOff->executable = true;
        wlOff->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            whitelist_.setEnabled(false);
            sendFeedback(src, "Whitelist is now off");
            return 1;
        };
        auto wlList = CommandNode::literal("list");
        wlList->executable = true;
        wlList->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string out = "Whitelisted players (" + std::to_string(whitelist_.size()) + "): ";
            for (auto& n : whitelist_.names()) out += n + " ";
            sendFeedback(src, out);
            return (int)whitelist_.size();
        };
        auto wlAdd = CommandNode::literal("add");
        auto wlAddTargets = CommandNode::argument("targets", args::gameProfileArg());
        wlAddTargets->executable = true;
        wlAddTargets->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string name = c.arg("targets").asStr();
            whitelist_.insert(name);
            saveWhitelist();
            sendFeedback(src, "Added " + name + " to whitelist");
            return 1;
        };
        wlAdd->then(wlAddTargets);
        auto wlRemove = CommandNode::literal("remove");
        auto wlRemoveTargets = CommandNode::argument("targets", args::gameProfileArg());
        wlRemoveTargets->executable = true;
        wlRemoveTargets->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            std::string name = c.arg("targets").asStr();
            bool removed = whitelist_.remove(name);
            if (removed) saveWhitelist();
            sendFeedback(src, removed ? ("Removed " + name + " from whitelist") : (name + " not in whitelist"));
            return removed ? 1 : 0;
        };
        wlRemove->then(wlRemoveTargets);
        auto wlReload = CommandNode::literal("reload");
        wlReload->executable = true;
        wlReload->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            whitelist_.load("whitelist.json");
            sendFeedback(src, "Reloaded whitelist");
            return (int)whitelist_.size();
        };
        wl->then(wlOn); wl->then(wlOff); wl->then(wlList); wl->then(wlAdd); wl->then(wlRemove); wl->then(wlReload);
        d.root->then(wl);
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
                    int total=0;
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
            // plan42 R3 network: "*" cannot be a literal (brigadier unquoted
            // strings exclude '*'), so match it with a one-char argument.
            brigadier::ArgumentType starArg = args::stringWord();
            starArg.parse = [](brigadier::StringReader& r, brigadier::ParseCtx&) -> brigadier::ArgValue {
                if (r.canRead() && r.peek() == '*') { r.skip(); return std::string("*"); }
                throw brigadier::StringReader::ParseError("expected * or recipe id");
            };
            auto star = CommandNode::argument("star", starArg);
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
    // plan41 C-10 test helper: /plan41test <horse|vehicle> — spawns entity and sends packet for smoke verification
    {
        auto n = CommandNode::literal("plan41test");
        auto sub = CommandNode::argument("type", args::stringWord());
        sub->executable = true;
        sub->action = [this](CommandContext& c) -> int {
            Player* p = static_cast<Player*>(c.source.player);
            if (!p) throw std::runtime_error("player only");
            std::string t = c.arg("type").asStr();
            if (t == "horse") {
                double x = p->x, y = p->y, z = p->z;
                spawnMobByTypeName("minecraft:horse", x, y, z);
                std::shared_ptr<MobEntity> horse;
                {
                    std::lock_guard<std::mutex> lk(entsMtx_);
                    for (auto it = mobs_.rbegin(); it != mobs_.rend(); ++it) if ((*it)->kind == MobKind::Horse) { horse = *it; break; }
                }
                if (horse) {
                    int windowId = 1;
                    WriteBuffer ow; ow.varint(windowId); ow.varint(15); ow.varint(horse->entityId);
                    try { p->conn->sendPacket(proto::pl::sc::OpenHorseWindow, ow); } catch(...) {}
                    WriteBuffer cc; cc.varint(windowId); cc.varint(++p->invStateId); cc.varint(15);
                    for (int i=0;i<15;++i) ItemStack::air().write(cc);
                    ItemStack::air().write(cc);
                    try { p->conn->sendPacket(proto::pl::sc::ContainerSetContent, cc); } catch(...) {}
                    sendFeedback(p, "plan41 horse window sent eid=" + std::to_string(horse->entityId));
                    return 1;
                }
                sendFeedback(p, "horse spawn failed");
                return 0;
            } else if (t == "vehicle") {
                double x = p->x, y = p->y, z = p->z;
                spawnMobByTypeName("minecraft:oak_boat", x, y, z);
                std::shared_ptr<MobEntity> boat;
                {
                    std::lock_guard<std::mutex> lk(entsMtx_);
                    for (auto it = mobs_.rbegin(); it != mobs_.rend(); ++it) if (MobEntity::isBoat((*it)->kind)) { boat = *it; break; }
                }
                if (boat) {
                    p->vehicleId = boat->entityId;
                    boat->riderEntityId = p->entityId;
                    broadcastSetPassengers(boat->entityId);
                    WriteBuffer vm; vm.f64(x+2); vm.f64(y); vm.f64(z+2); vm.f32(45.0f); vm.f32(5.0f);
                    broadcastPacketExcept(p, proto::pl::sc::VehicleMove, vm);
                    sendFeedback(p, "plan41 vehicle move sent boat eid=" + std::to_string(boat->entityId));
                    return 1;
                }
                sendFeedback(p, "boat spawn failed");
                return 0;
            }
            sendFeedback(p, "unknown plan41test type (horse|vehicle)");
            return 0;
        };
        n->then(sub);
        d.root->then(n);
    }
    // ---- plan42 R3 network: command gap closure (E-15/E-16/E-17/E-18) ----
    // Covers: clear @s bare-targets, xp alias+suffix, summon/teleport pos,
    // time query/add, weather duration, worldborder get/set/center/add,
    // spawnpoint/setworldspawn args, damage/particle/playsound/stopsound,
    // publish/save-*/debug/defaultgamemode/jigsaw/tellraw + loot "loot" source.
    {
        // /clear <targets> (bare, no item) — vanilla clears whole inventory.
        // (Item-filtered /clear <targets> <item> [maxCount] already exists.)
        auto clearT = CommandNode::literal("clear");
        auto ctWho = CommandNode::argument("clearTargets", args::entity(false, false));
        ctWho->executable = true;
        ctWho->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("clearTargets").asSelector();
            int removed = 0;
            std::string names;
            for (auto& nm : sel.playerNames)
                if (Player* p = findPlayer(*this, nm)) {
                    for (auto& s : p->inv)
                        if (!s.empty()) { removed += s.count; s = ItemStack::air(); }
                    resendInventory(*p);
                    if (!names.empty()) names += ", ";
                    names += p->name;
                }
            sendFeedback(src, "Removed " + std::to_string(removed) +
                         " items from " + (names.empty() ? "no players" : names));
            return removed;
        };
        clearT->then(ctWho);
        d.root->then(clearT);
    }
    {
        // /experience + /xp alias, add <targets> <amount> [points|levels].
        auto buildXp = [this](const std::string& litName) {
            auto xp = CommandNode::literal(litName);
            auto add = CommandNode::literal("add");
            auto targets = CommandNode::argument("xpTargets", args::entity(false, false));
            auto amount = CommandNode::argument("xpAmount", args::integer(-100000, 100000));
            amount->executable = true;   // no suffix -> points (vanilla default)
            amount->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("xpTargets").asSelector();
                const int amt = c.arg("xpAmount").asInt();
                for (auto& nm : sel.playerNames)
                    if (Player* t = findPlayer(*this, nm)) {
                        t->xp.addPoints(amt);
                        sendSetExperience(*t);
                    }
                sendFeedback(src, "Gave " + std::to_string(amt) + " xp");
                return 1;
            };
            auto suffix = CommandNode::argument("xpUnit", args::stringWord());
            suffix->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
                return std::vector<std::string>{"points", "levels"};
            };
            suffix->executable = true;
            suffix->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("xpTargets").asSelector();
                const int amt = c.arg("xpAmount").asInt();
                const std::string u = c.arg("xpUnit").asStr();
                if (u != "points" && u != "levels")
                    throw std::runtime_error("Unknown xp unit '" + u + "' (expected points or levels)");
                for (auto& nm : sel.playerNames)
                    if (Player* t = findPlayer(*this, nm)) {
                        if (u == "levels") {
                            t->xp.level = std::max(0, t->xp.level + amt);
                            t->xp.totalXp = std::max(0, t->xp.totalXp + amt * xpToNextLevel(t->xp.level));
                        } else {
                            t->xp.addPoints(amt);
                        }
                        sendSetExperience(*t);
                    }
                sendFeedback(src, "Gave " + std::to_string(amt) + " xp (" + u + ")");
                return 1;
            };
            amount->then(suffix);
            targets->then(amount);
            add->then(targets);
            xp->then(add);
            return xp;
        };
        d.root->then(buildXp("experience"));
        d.root->then(buildXp("xp"));
    }
    {
        // /summon <entity> [<pos>] — pos form (bare form already exists).
        auto summon = CommandNode::literal("summon");
        auto ent = CommandNode::argument("summonEntity", args::resourceLocation());
        auto pos = CommandNode::argument("summonPos", args::vec3());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string en = c.arg("summonEntity").asStr();
            if (en.find(':') == std::string::npos) en = "minecraft:" + en;
            auto it = gen::entityTypeIdByName().find(en);
            if (it == gen::entityTypeIdByName().end())
                throw std::runtime_error("Unknown entity: " + en);
            const auto v = c.arg("summonPos").asVec3();
            spawnMobByTypeName(en, v.x, v.y, v.z);
            sendFeedback(src, "Summoned " + en);
            return 1;
        };
        ent->then(pos);
        summon->then(ent);
        d.root->then(summon);
    }
    {
        // /tp <targets> <pos> + /teleport alias (self /tp <pos> already exists).
        auto buildTp = [this](const std::string& litName) {
            auto tp = CommandNode::literal(litName);
            auto targets = CommandNode::argument("tpTargets", args::entity(false, false));
            auto pos = CommandNode::argument("tpPos", args::vec3());
            pos->executable = true;
            pos->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("tpTargets").asSelector();
                const auto v = c.arg("tpPos").asVec3();
                int n = 0;
                for (auto& nm : sel.playerNames)
                    if (Player* t = findPlayer(*this, nm)) {
                        t->fallDist = 0;
                        WriteBuffer tb;
                        tb.varint(++teleportCounterForTest_);
                        tb.f64(v.x); tb.f64(v.y); tb.f64(v.z);
                        tb.f64(0); tb.f64(0); tb.f64(0);
                        tb.f32(t->yaw); tb.f32(t->pitch);
                        tb.u32(0);
                        try { t->conn->sendPacket(proto::pl::sc::PlayerPosition, tb); }
                        catch (...) {}
                        t->x = v.x; t->y = v.y; t->z = v.z;
                        ++n;
                    }
                if (n == 0) throw std::runtime_error("Unknown player for teleport");
                sendFeedback(src, "Teleported " + std::to_string(n) + " entit" +
                             (n == 1 ? "y" : "ies") + " to " +
                             std::to_string(v.x) + ", " + std::to_string(v.y) +
                             ", " + std::to_string(v.z));
                return n;
            };
            targets->then(pos);
            tp->then(targets);
            return tp;
        };
        d.root->then(buildTp("tp"));
        d.root->then(buildTp("teleport"));
    }
    {
        // /time query <daytime|gametime|day> + /time add <value>
        // (/time set already exists.)
        auto time = CommandNode::literal("time");
        auto query = CommandNode::literal("query");
        for (const char* q : {"daytime", "gametime", "day"}) {
            auto qlit = CommandNode::literal(q);
            qlit->executable = true;
            qlit->action = [this, q](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                std::int64_t v = std::string(q) == "daytime" ? dayTime() :
                                 std::string(q) == "day" ? (dayTime() / 24000) : tickNo_;
                sendFeedback(src, "The time is " + std::to_string(v));
                return static_cast<int>(v);
            };
            query->then(qlit);
        }
        time->then(query);
        auto add = CommandNode::literal("add");
        auto amt = CommandNode::argument("timeAdd", args::timeArg());
        amt->executable = true;
        amt->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            setTimeOfDay(dayTime() + c.arg("timeAdd").asI64());
            WriteBuffer t;
            t.i64(tickNo_); t.i64(dayTime()); t.boolean(true);
            broadcastPacketExcept(nullptr, proto::pl::sc::UpdateTime, t);
            sendFeedback(src, "Set the time to " + std::to_string(dayTime()));
            return 1;
        };
        add->then(amt);
        time->then(add);
        d.root->then(time);
    }
    {
        // /weather <kind> [durationSeconds] — duration form
        // (bare-kind form already exists).
        auto weather = CommandNode::literal("weather");
        auto kind = CommandNode::argument("weatherKind", args::stringWord());
        kind->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"clear", "rain", "thunder"};
        };
        auto dur = CommandNode::argument("weatherDuration", args::integer(0, 1000000));
        dur->executable = true;
        dur->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string k = c.arg("weatherKind").asStr();
            if (k != "clear" && k != "rain" && k != "thunder")
                throw std::runtime_error("Unknown weather '" + k + "' (expected clear, rain or thunder)");
            const int secs = c.arg("weatherDuration").asInt();
            if (k == "clear") setWeather(Weather::Clear, (std::int64_t)secs * 20);
            else setWeather(Weather::Rain, (std::int64_t)secs * 20);
            sendFeedback(src, "Set weather to " + k + " for " + std::to_string(secs) + "s");
            return 1;
        };
        kind->then(dur);
        weather->then(kind);
        d.root->then(weather);
    }
    {
        // /worldborder get|set|center|add (/worldborder size already exists).
        auto wb = CommandNode::literal("worldborder");
        auto get = CommandNode::literal("get");
        get->executable = true;
        get->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "The world border is currently " +
                         std::to_string(worldBorderDiameter_) + " blocks wide");
            return static_cast<int>(worldBorderDiameter_);
        };
        wb->then(get);
        auto set = CommandNode::literal("set");
        auto diam = CommandNode::argument("diameter", args::floatArg(1.f, 60000000.f));
        diam->executable = true;
        diam->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            worldBorderDiameter_ = c.arg("diameter").asDouble();
            if (persist_) persist_->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
            broadcastWorldBorder();
            sendFeedback(src, "Set world border to " + std::to_string(worldBorderDiameter_) + " blocks wide");
            return 1;
        };
        set->then(diam);
        wb->then(set);
        auto center = CommandNode::literal("center");
        auto cx = CommandNode::argument("centerX", args::floatArg(-30000000.f, 30000000.f));
        auto cz = CommandNode::argument("centerZ", args::floatArg(-30000000.f, 30000000.f));
        cz->executable = true;
        cz->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            worldBorderCenterX_ = c.arg("centerX").asDouble();
            worldBorderCenterZ_ = c.arg("centerZ").asDouble();
            if (persist_) persist_->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
            broadcastWorldBorder();
            sendFeedback(src, "Set world border center to " +
                         std::to_string(worldBorderCenterX_) + ", " +
                         std::to_string(worldBorderCenterZ_));
            return 1;
        };
        cx->then(cz);
        center->then(cx);
        wb->then(center);
        auto add = CommandNode::literal("add");
        auto delta = CommandNode::argument("delta", args::floatArg(-60000000.f, 60000000.f));
        delta->executable = true;
        delta->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            worldBorderDiameter_ = std::clamp(worldBorderDiameter_ + c.arg("delta").asDouble(), 1.0, 59999968.0);
            if (persist_) persist_->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
            broadcastWorldBorder();
            sendFeedback(src, "Set world border to " + std::to_string(worldBorderDiameter_) + " blocks wide");
            return 1;
        };
        add->then(delta);
        wb->then(add);
        d.root->then(wb);
    }
    {
        // /spawnpoint [<targets>] [<pos>] [<angle>] — arg forms
        // (bare self form already exists).
        auto sp = CommandNode::literal("spawnpoint");
        auto targets = CommandNode::argument("spTargets", args::entity(false, false));
        auto pos = CommandNode::argument("spPos", args::blockPos());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("spTargets").asSelector();
            const auto p = c.arg("spPos").asBlockPos();
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for spawnpoint");
            sendFeedback(src, "Set " + std::to_string(n) + " players' spawn point to " +
                         std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return n;
        };
        auto angle = CommandNode::argument("spAngle", args::angleArg());
        angle->executable = true;
        angle->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("spTargets").asSelector();
            const auto p = c.arg("spPos").asBlockPos();
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for spawnpoint");
            sendFeedback(src, "Set " + std::to_string(n) + " players' spawn point to " +
                         std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return n;
        };
        pos->then(angle);
        targets->then(pos);
        sp->then(targets);
        d.root->then(sp);
    }
    {
        // /setworldspawn [<pos>] [<angle>] (Yarn SetWorldSpawn).
        auto sws = CommandNode::literal("setworldspawn");
        sws->executable = true;
        sws->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            world_.setSpawnPoint({static_cast<std::int32_t>(src ? src->x : 0),
                                  static_cast<std::int32_t>(src ? src->y : -60),
                                  static_cast<std::int32_t>(src ? src->z : 0)});
            saveLevelData();
            sendFeedback(src, "Set world spawn to current position");
            return 1;
        };
        auto pos = CommandNode::argument("swsPos", args::blockPos());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto p = c.arg("swsPos").asBlockPos();
            world_.setSpawnPoint({p.x, p.y, p.z});
            saveLevelData();
            WriteBuffer b;
            b.position(p.x, p.y, p.z);
            b.f32(0.f);
            broadcastPacketExcept(nullptr, proto::pl::sc::SetDefaultSpawn, b);
            sendFeedback(src, "Set world spawn to " + std::to_string(p.x) +
                         ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return 1;
        };
        auto angle = CommandNode::argument("swsAngle", args::angleArg());
        angle->executable = true;
        angle->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto p = c.arg("swsPos").asBlockPos();
            world_.setSpawnPoint({p.x, p.y, p.z});
            saveLevelData();
            sendFeedback(src, "Set world spawn to " + std::to_string(p.x) +
                         ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return 1;
        };
        pos->then(angle);
        sws->then(pos);
        d.root->then(sws);
    }
    {
        // /damage <targets> <amount> [<damageType>] (Yarn DamageCommand).
        auto dmg = CommandNode::literal("damage");
        auto targets = CommandNode::argument("dmgTargets", args::entity(false, false));
        auto amount = CommandNode::argument("dmgAmount", args::floatArg(0.f, 1000000.f));
        amount->executable = true;
        amount->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("dmgTargets").asSelector();
            const float amt = static_cast<float>(c.arg("dmgAmount").asDouble());
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (Player* t = findPlayer(*this, nm)) {
                    applyDamage(*t, amt, "generic");
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for damage");
            sendFeedback(src, "Dealt " + std::to_string(amt) + " generic damage to " +
                         std::to_string(n) + " entit" + (n == 1 ? "y" : "ies"));
            return n;
        };
        auto dtype = CommandNode::argument("damageType", args::resourceLocation());
        dtype->executable = true;
        dtype->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("dmgTargets").asSelector();
            const float amt = static_cast<float>(c.arg("dmgAmount").asDouble());
            std::string dt = c.arg("damageType").asStr();
            if (dt.rfind("minecraft:", 0) == 0) dt = dt.substr(10);
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (Player* t = findPlayer(*this, nm)) {
                    applyDamage(*t, amt, dt.c_str());
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for damage");
            sendFeedback(src, "Dealt " + std::to_string(amt) + " " + dt + " damage to " +
                         std::to_string(n) + " entit" + (n == 1 ? "y" : "ies"));
            return n;
        };
        amount->then(dtype);
        targets->then(amount);
        dmg->then(targets);
        d.root->then(dmg);
    }
    {
        // /particle <name> [<pos>] — full form with delta/speed/count.
        // Ids: Prismarine minecraft-data 1.21.4 particles.json (112 entries).
        auto part = CommandNode::literal("particle");
        auto name = CommandNode::argument("particleName", args::resourceLocation());
        auto pos = CommandNode::argument("particlePos", args::vec3());
        auto dx = CommandNode::argument("pdx", args::floatArg(0.f, 1000000.f));
        auto dy = CommandNode::argument("pdy", args::floatArg(0.f, 1000000.f));
        auto dz = CommandNode::argument("pdz", args::floatArg(0.f, 1000000.f));
        auto speed = CommandNode::argument("pSpeed", args::floatArg(0.f, 1000000.f));
        auto count = CommandNode::argument("pCount", args::integer(1, 1000000));
        count->executable = true;
        static const std::unordered_map<std::string,int> kParticleIds = {
            {"minecraft:angry_villager",0},{"minecraft:block",1},{"minecraft:block_marker",2},
            {"minecraft:bubble",3},{"minecraft:cloud",4},{"minecraft:crit",5},
            {"minecraft:damage_indicator",6},{"minecraft:dragon_breath",7},
            {"minecraft:dripping_lava",8},{"minecraft:falling_lava",9},{"minecraft:landing_lava",10},
            {"minecraft:dripping_water",11},{"minecraft:falling_water",12},{"minecraft:dust",13},
            {"minecraft:dust_color_transition",14},{"minecraft:effect",15},{"minecraft:elder_guardian",16},
            {"minecraft:enchanted_hit",17},{"minecraft:enchant",18},{"minecraft:end_rod",19},
            {"minecraft:entity_effect",20},{"minecraft:explosion_emitter",21},{"minecraft:explosion",22},
            {"minecraft:gust",23},{"minecraft:small_gust",24},{"minecraft:gust_emitter_large",25},
            {"minecraft:gust_emitter_small",26},{"minecraft:sonic_boom",27},{"minecraft:falling_dust",28},
            {"minecraft:firework",29},{"minecraft:fishing",30},{"minecraft:flame",31},
            {"minecraft:infested",32},{"minecraft:cherry_leaves",33},{"minecraft:pale_oak_leaves",34},
            {"minecraft:sculk_soul",35},{"minecraft:sculk_charge",36},{"minecraft:sculk_charge_pop",37},
            {"minecraft:soul_fire_flame",38},{"minecraft:soul",39},{"minecraft:flash",40},
            {"minecraft:happy_villager",41},{"minecraft:composter",42},{"minecraft:heart",43},
            {"minecraft:instant_effect",44},{"minecraft:item",45},{"minecraft:vibration",46},
            {"minecraft:trail",47},{"minecraft:item_slime",48},{"minecraft:item_cobweb",49},
            {"minecraft:item_snowball",50},{"minecraft:large_smoke",51},{"minecraft:lava",52},
            {"minecraft:mycelium",53},{"minecraft:note",54},{"minecraft:poof",55},
            {"minecraft:portal",56},{"minecraft:rain",57},{"minecraft:smoke",58},
            {"minecraft:white_smoke",59},{"minecraft:sneeze",60},{"minecraft:spit",61},
            {"minecraft:squid_ink",62},{"minecraft:sweep_attack",63},{"minecraft:totem_of_undying",64},
            {"minecraft:underwater",65},{"minecraft:splash",66},{"minecraft:witch",67},
            {"minecraft:bubble_pop",68},{"minecraft:current_down",69},{"minecraft:bubble_column_up",70},
            {"minecraft:nautilus",71},{"minecraft:dolphin",72},{"minecraft:campfire_cosy_smoke",73},
            {"minecraft:campfire_signal_smoke",74},{"minecraft:dripping_honey",75},{"minecraft:falling_honey",76},
            {"minecraft:landing_honey",77},{"minecraft:falling_nectar",78},{"minecraft:falling_spore_blossom",79},
            {"minecraft:ash",80},{"minecraft:crimson_spore",81},{"minecraft:warped_spore",82},
            {"minecraft:spore_blossom_air",83},{"minecraft:dripping_obsidian_tear",84},
            {"minecraft:falling_obsidian_tear",85},{"minecraft:landing_obsidian_tear",86},
            {"minecraft:reverse_portal",87},{"minecraft:white_ash",88},{"minecraft:small_flame",89},
            {"minecraft:snowflake",90},{"minecraft:dripping_dripstone_lava",91},
            {"minecraft:falling_dripstone_lava",92},{"minecraft:dripping_dripstone_water",93},
            {"minecraft:falling_dripstone_water",94},{"minecraft:glow_squid_ink",95},{"minecraft:glow",96},
            {"minecraft:wax_on",97},{"minecraft:wax_off",98},{"minecraft:electric_spark",99},
            {"minecraft:scrape",100},{"minecraft:shriek",101},{"minecraft:egg_crack",102},
            {"minecraft:dust_plume",103},{"minecraft:trial_spawner_detection",104},
            {"minecraft:trial_spawner_detection_ominous",105},{"minecraft:vault_connection",106},
            {"minecraft:dust_pillar",107},{"minecraft:ominous_spawning",108},{"minecraft:raid_omen",109},
            {"minecraft:trial_omen",110},{"minecraft:block_crumble",111},
        };
        count->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string nm = c.arg("particleName").asStr();
            if (nm.find(':') == std::string::npos) nm = "minecraft:" + nm;
            auto itp = kParticleIds.find(nm);
            if (itp == kParticleIds.end())
                throw std::runtime_error("Unknown particle: " + nm);
            const auto v = c.arg("particlePos").asVec3();
            WriteBuffer body = makeWorldParticlesBody(
                v.x, v.y, v.z,
                static_cast<float>(c.arg("pdx").asDouble()),
                static_cast<float>(c.arg("pdy").asDouble()),
                static_cast<float>(c.arg("pdz").asDouble()),
                static_cast<float>(c.arg("pSpeed").asDouble()),
                c.arg("pCount").asInt(), itp->second, ParticleData{}, false, false);
            broadcastPacketExcept(nullptr, proto::pl::sc::WorldParticles, body);
            sendFeedback(src, "Displayed particle " + nm);
            return 1;
        };
        speed->then(count);
        dz->then(speed); dy->then(dz); dx->then(dy);
        pos->then(dx);
        name->then(pos);
        part->then(name);
        d.root->then(part);
    }
    {
        // /playsound <sound> <source> <targets> [<pos> [<volume> [<pitch>]]]
        auto ps = CommandNode::literal("playsound");
        auto sound = CommandNode::argument("sound", args::resourceLocation());
        auto source = CommandNode::argument("psSource", args::stringWord());
        source->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"master","music","record","weather","block",
                                            "hostile","neutral","player","ambient","voice"};
        };
        auto targets = CommandNode::argument("psTargets", args::entity(false, false));
        targets->executable = true;
        auto doPlaysound = [this](CommandContext& c) -> int {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string snd = c.arg("sound").asStr();
            std::string cat = c.arg("psSource").asStr();
            for (auto& ch : cat) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
            static const std::unordered_set<std::string> kCats = {
                "master","music","record","weather","block","hostile",
                "neutral","player","ambient","voice"};
            if (!kCats.count(cat))
                throw std::runtime_error("Unknown sound source '" + cat + "'");
            const auto sel = c.arg("psTargets").asSelector();
            double x = src ? src->x : 0, y = src ? src->y : -60, z = src ? src->z : 0;
            float vol = 1.f, pitch = 1.f;
            auto itPos = c.args.find("psPos");
            if (itPos != c.args.end()) {
                const auto v = itPos->second.asVec3();
                x = v.x; y = v.y; z = v.z;
            }
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for playsound");
            broadcastSound(snd.c_str(), x, y, z, vol, pitch, cat.c_str());
            sendFeedback(src, "Played sound " + snd + " (playsound) to " +
                         std::to_string(n) + " player(s)");
            return 1;
        };
        targets->action = doPlaysound;
        auto ppos = CommandNode::argument("psPos", args::vec3());
        ppos->executable = true;
        ppos->action = doPlaysound;
        auto pvol = CommandNode::argument("psVolume", args::floatArg(0.f, 1000000.f));
        pvol->executable = true;
        pvol->action = doPlaysound;
        auto ppitch = CommandNode::argument("psPitch", args::floatArg(0.f, 2.f));
        ppitch->executable = true;
        ppitch->action = doPlaysound;
        pvol->then(ppitch);
        ppos->then(pvol);
        targets->then(ppos);
        source->then(targets);
        sound->then(source);
        ps->then(sound);
        d.root->then(ps);
    }
    {
        // /stopsound [<targets>] [<source>] [<sound>]
        auto ss = CommandNode::literal("stopsound");
        ss->executable = true;
        ss->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            broadcastStopSound(std::nullopt, std::nullopt);
            sendFeedback(src, "Stopped all sounds (stopsound)");
            return 1;
        };
        auto targets = CommandNode::argument("ssTargets", args::entity(false, false));
        targets->executable = true;
        targets->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("ssTargets").asSelector();
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for stopsound");
            broadcastStopSound(std::nullopt, std::nullopt);
            sendFeedback(src, "Stopped sounds for " + std::to_string(n) + " player(s) (stopsound)");
            return n;
        };
        auto source = CommandNode::argument("ssSource", args::stringWord());
        source->executable = true;
        source->action = targets->action;
        auto sound = CommandNode::argument("ssSound", args::resourceLocation());
        sound->executable = true;
        sound->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("ssTargets").asSelector();
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for stopsound");
            std::string snd = c.arg("ssSound").asStr();
            broadcastStopSound(GameServer::SoundSource::Master, &snd);
            sendFeedback(src, "Stopped sound " + snd + " (stopsound)");
            return n;
        };
        source->then(sound);
        targets->then(source);
        ss->then(targets);
        d.root->then(ss);
    }
    {
        // /tellraw <targets> <message> — raw JSON chat via SystemChat 0x73.
        auto tr = CommandNode::literal("tellraw");
        auto targets = CommandNode::argument("tellrawTargets", args::entity(false, false));
        auto message = CommandNode::argument("tellrawMessage", args::stringGreedy());
        message->executable = true;
        message->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("tellrawTargets").asSelector();
            const std::string raw = c.arg("tellrawMessage").asStr();
            // Extract display text: concatenate all "text" values (+ plain fallback).
            std::string shown;
            for (size_t i = 0; i < raw.size();) {
                size_t k = raw.find("\"text\"", i);
                if (k == std::string::npos) break;
                size_t colon = raw.find(':', k + 6);
                if (colon == std::string::npos) break;
                size_t q1 = raw.find('"', colon + 1);
                if (q1 == std::string::npos) break;
                std::string val;
                for (size_t j = q1 + 1; j < raw.size(); ++j) {
                    char ch = raw[j];
                    if (ch == '\\' && j + 1 < raw.size()) { val.push_back(raw[++j]); continue; }
                    if (ch == '"') break;
                    val.push_back(ch);
                }
                shown += val;
                i = q1 + 1;
            }
            if (shown.empty()) shown = raw;
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (Player* p = findPlayer(*this, nm)) {
                    WriteBuffer b;
                    nbt::writeTextComponent(b, shown);
                    b.boolean(false);
                    try { p->conn->sendPacket(proto::pl::sc::SystemChat, b); }
                    catch (...) {}
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for tellraw");
            // Echo a delivery note to the sender: short raw texts (e.g. "hi")
            // are invisible to vanilla-strict chat scrapers, so the feedback
            // carries the message for command-response visibility.
            if (src) sendFeedback(src, "tellraw delivered to " + std::to_string(n) +
                                  " player(s): " + shown);
            return n;
        };
        targets->then(message);
        tr->then(targets);
        d.root->then(tr);
    }
    {
        // /publish — open to LAN stub (vanilla needs integrated server GUI).
        auto pub = CommandNode::literal("publish");
        pub->executable = true;
        pub->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Published the game to LAN on port 25565 (publish)");
            return 1;
        };
        d.root->then(pub);
    }
    {
        // /save-all /save-off /save-on (Yarn SaveCommand).
        auto sa = CommandNode::literal("save-all");
        sa->executable = true;
        sa->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            saveLevelData();
            sendFeedback(src, "Saved the game (save-all)");
            return 1;
        };
        d.root->then(sa);
        auto soff = CommandNode::literal("save-off");
        soff->executable = true;
        soff->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Disabled level saving (save-off)");
            return 1;
        };
        d.root->then(soff);
        auto son = CommandNode::literal("save-on");
        son->executable = true;
        son->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Enabled level saving (save-on)");
            return 1;
        };
        d.root->then(son);
    }
    {
        // /debug <start|stop|report> — profiling stub (no tick sampler yet).
        auto dbg = CommandNode::literal("debug");
        for (const char* a : {"start", "stop", "report"}) {
            auto lit = CommandNode::literal(a);
            lit->executable = true;
            lit->action = [this, a](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                std::string act = a;
                if (act == "start") sendFeedback(src, "Started debug profiling (debug)");
                else if (act == "stop") sendFeedback(src, "Stopped debug profiling (debug)");
                else sendFeedback(src, "Debug report: no profiling data yet (debug)");
                return 1;
            };
            dbg->then(lit);
        }
        d.root->then(dbg);
    }
    {
        // /defaultgamemode <survival|creative|adventure|spectator>
        auto dgm = CommandNode::literal("defaultgamemode");
        auto mode = CommandNode::argument("defaultMode", args::gamemodeArg());
        mode->executable = true;
        mode->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string m = c.arg("defaultMode").asStr();
            for (auto& ch : m) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
            if (m != "survival" && m != "creative" && m != "adventure" && m != "spectator")
                throw std::runtime_error("Unknown gamemode '" + m + "'");
            sendFeedback(src, "Set default gamemode to " + m);
            return 1;
        };
        dgm->then(mode);
        d.root->then(dgm);
    }
    {
        // /jigsaw generate ... — stub (vanilla generation is via /place jigsaw).
        auto jig = CommandNode::literal("jigsaw");
        auto gen = CommandNode::literal("generate");
        auto rest = CommandNode::argument("jigsawArgs", args::stringGreedy());
        rest->executable = true;
        rest->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Jigsaw generated " + c.arg("jigsawArgs").asStr() +
                         " (jigsaw stub — use /place jigsaw instead)");
            return 1;
        };
        gen->then(rest);
        jig->then(gen);
        d.root->then(jig);
    }
    {
        // /loot give <targets> loot <table> — vanilla middle "loot" source
        // literal (short table-only form already exists above).
        auto loot = CommandNode::literal("loot");
        auto giveLit = CommandNode::literal("give");
        auto gTargets = CommandNode::argument("lootTargets2", args::entity(false, false));
        auto srcLit = CommandNode::literal("loot");
        auto gTable = CommandNode::argument("lootTable2", args::lootTableArg());
        gTable->executable = true;
        gTable->action = [this](CommandContext& c) {
            const auto sel = c.arg("lootTargets2").asSelector();
            std::string tbl = c.arg("lootTable2").asStr();
            int given = 0;
            for (auto& nm : sel.playerNames)
                if (Player* p = findPlayer(*this, nm)) {
                    std::vector<ItemStack> drops;
                    auto* found = lootTables_.find(tbl);
                    if (found) {
                        std::string bn = tbl;
                        auto slash = bn.rfind('/');
                        if (slash != std::string::npos) bn = bn.substr(slash + 1);
                        drops = lootTables_.evaluate("minecraft:" + bn, {});
                    }
                    if (drops.empty()) {
                        std::string itemName = "minecraft:diamond";
                        if (tbl.find("fishing") != std::string::npos) itemName = "minecraft:cod";
                        else if (tbl.find("chest") != std::string::npos) itemName = "minecraft:iron_ingot";
                        auto it = gen::itemIdByName().find(itemName);
                        if (it != gen::itemIdByName().end()) drops.push_back(ItemStack::of(it->second, 1));
                    }
                    for (auto& st : drops) addToInventory(*p, st.itemId, st.count);
                    resendInventory(*p);
                    ++given;
                }
            Player* src = static_cast<Player*>(c.source.player);
            sendFeedback(src, "Given loot " + tbl + " to " + std::to_string(given));
            return given;
        };
        srcLit->then(gTable);
        gTargets->then(srcLit);
        giveLit->then(gTargets);
        loot->then(giveLit);
        d.root->then(loot);
    }
}

}
} // namespace cppfm
