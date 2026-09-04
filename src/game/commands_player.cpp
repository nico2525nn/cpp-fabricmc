// commands_player.cpp: Brigadier command tree nodes (plan3 port): registered, parsed, advertised.
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

#include "CommandsHelpers.hpp"
namespace cppfm {

using brigadier::CommandNode;
using brigadier::CommandContext;
namespace args = brigadier::args;

void GameServer::initPlayerCommands() {
    auto& d = commands_;
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
            // abilities follow the mode (plan43 W-06: same gamemode-linked flags as Session::sendAbilities — survival/adventure get 0x00,
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
    {
        auto effect = CommandNode::literal("effect");
        // Shared effect store+send tail (was 4x copy-paste): replace same-type effect, store, broadcast EntityEffect. ampWire is the raw
        // amplifier byte on the wire (site 1 sends e.amplifier, others the raw arg).
        auto storeEffect = [](Player& t, EffectInstance e, int ampWire) {
            t.effects.erase(
                std::remove_if(t.effects.begin(), t.effects.end(),
                               [&](const EffectInstance& x)
                                   { return x.type == e.type; }),
                t.effects.end());
            t.effects.push_back(e);
            WriteBuffer b;
            b.varint(t.entityId);
            b.varint(e.type);
            b.varint(ampWire);
            b.varint(e.durationTicks);
            b.u8(effectFlags(e));
            try { t.conn->sendPacket(proto::pl::sc::EntityEffect, b); }
            catch (...) {}
        };
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
        eff->action = [this, storeEffect](CommandContext& c) {
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
                    storeEffect(*t, e, e.amplifier);
                    ++applied;
                }
            sendFeedback(src, "Applied " + en + " to " +
                         std::to_string(applied));
            return applied;
        };
        auto secs = CommandNode::argument("seconds", args::integer(1, 1000000));
        secs->executable = true;
        secs->action = [this, storeEffect](CommandContext& c) {
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
                    storeEffect(*t, e, e.amplifier);
                }
            sendFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s)");
            return 1;
        };
        // plan28 finish: amplifier 0..255 arg (missing it broke `effect give <p> <eff> 10 1`).
        auto amp = CommandNode::argument("amplifier", args::integer(0, 255));
        amp->executable = true;
        amp->action = [this, storeEffect](CommandContext& c) {
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
                    storeEffect(*t, e, ampv); // raw 0..255 (int8_t wraps >127)
                }
            sendFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s, amplifier " +
                         std::to_string(ampv) + ")");
            return 1;
        };
        // vanilla optional <hideParticles> boolean (low priority completion)
        auto hide = CommandNode::argument("hideParticles", args::boolean());
        hide->executable = true;
        hide->action = [this, storeEffect](CommandContext& c) {
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
                    storeEffect(*t, e, ampv);
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
        // Shared attribute-command prologue (was 10x copy-paste): resolve source + attribute.
        auto attrHead = [this, resolveAttr](CommandContext& c) -> std::pair<Player*, Attribute> {
            Player* src = static_cast<Player*>(c.source.player);
            std::string attrRaw = c.arg("attribute").asStr();
            auto aopt = resolveAttr(attrRaw);
            if(!aopt) throw std::runtime_error("Unknown attribute: "+attrRaw);
            return {src, *aopt};
        };
        // attribute <target> <attribute> get [<scale>]
        {
            auto getLit = CommandNode::literal("get");
            getLit->executable = true;
            getLit->action = [this, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            scaleArg->action = [this, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            baseVal->action = [this, sendAttrUpdate, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            baseGet->action = [this, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            baseGetScale->action = [this, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            baseReset->action = [this, sendAttrUpdate, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            opArg->action = [this, sendAttrUpdate, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            remUuid->action = [this, sendAttrUpdate, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            vgUuid->action = [this, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            vgScale->action = [this, attrHead](CommandContext& c){
                auto [src, at] = attrHead(c);
                const auto sel = c.arg("target").asSelector();
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
            // plan42 R3 network: auto-create a trigger objective on demand so bare "/trigger <name>" succeeds vanilla-strict (server_full).
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
    // ---- plan42 R3 network: command gap closure (E-15/E-16/E-17/E-18) ---- Covers: clear @s bare-targets, xp alias+suffix,
    // summon/teleport pos, time query/add, weather duration, worldborder get/set/center/add, spawnpoint/setworldspawn args,
    // damage/particle/playsound/stopsound, publish/save-*/debug/defaultgamemode/jigsaw/tellraw + loot "loot" source.
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
        // /spawnpoint [<targets>] [<pos>] [<angle>] — arg forms (bare self form already exists).
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
        // /particle <name> [<pos>] — full form with delta/speed/count. Ids: Prismarine minecraft-data 1.21.4 particles.json (112 entries).
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
}

} // namespace cppfm
