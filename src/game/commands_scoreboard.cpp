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
using NodePtr = brigadier::NodePtr;


void GameServer::initScoreboardCommands() {
    auto& d = commands_;
    auto sb = CommandNode::literal("scoreboard");
    auto obj = CommandNode::literal("objectives");
    initScoreboardObjectiveCommands(obj);
    auto players = CommandNode::literal("players");
    initScoreboardPlayerCommands(players);
    initScoreboardObjectiveRemoveCommands(obj);
    sb->then(obj);
    sb->then(players);
    d.root->then(sb);
    initScoreboardCommandsPart2();
    initScoreboardCommandsPart3();
    initScoreboardCommandsPart4();
}

void GameServer::initScoreboardObjectiveCommands(const brigadier::NodePtr& obj) {
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

}

void GameServer::initScoreboardPlayerCommands(const brigadier::NodePtr& players) {
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
}

void GameServer::initScoreboardObjectiveRemoveCommands(const brigadier::NodePtr& obj) {
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

void GameServer::initScoreboardCommandsPart2() {
    auto& d = commands_;
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
}

void GameServer::initScoreboardCommandsPart3() {
    auto& d = commands_;
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
}

void GameServer::initScoreboardCommandsPart4() {
    auto& d = commands_;
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
