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

#include "CommandsHelpers.hpp"
namespace cppfm {

using brigadier::CommandNode;
using brigadier::CommandContext;
namespace args = brigadier::args;

void GameServer::initMiscCommands() {
    auto& d = commands_;
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
    // /testargs — DeclareCommands arg-type coverage helper (test helper, like plan41test).
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
