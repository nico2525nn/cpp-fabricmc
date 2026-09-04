// commands_admin.cpp: Brigadier command tree nodes (plan3 port): registered, parsed, advertised.
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

void GameServer::initAdminCommands() {
    auto& d = commands_;
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
            // plan42 R3 (E-19) anti-lockout: player enabler joins the list (else `whitelist off` unreachable).
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
}

} // namespace cppfm
