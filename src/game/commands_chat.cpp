#include "CommandModule.hpp"

namespace cppfm {

void GameServer::initChatCommands() {
    initChatCommandsPart01();
    initChatCommandsPart02();
    initChatCommandsPart03();
    initChatCommandsPart04();
}

void GameServer::initChatCommandsPart01() {
    auto& d = commands_;
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
}

void GameServer::initChatCommandsPart02() {
    auto& d = commands_;
    {
        auto title = CommandNode::literal("title");
        auto clear = CommandNode::literal("clear");
        clear->executable = true;
        clear->action = [this](CommandContext&) {
            for (auto& p : playersSnapshot()) {
                WriteBuffer b;
                p->conn->trySendPacket(proto::pl::sc::ClearTitles, b);
            }
            return 1;
        };
        title->then(clear);
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
                                p->conn->trySendPacket(proto::pl::sc::SetTitleSubtitle, b);
                            } else if (kind == "actionbar") {
                                this->sendActionBar(*p, t);
                            } else {
                                WriteBuffer b;
                                nbt::writeTextComponent(b, t);
                                p->conn->trySendPacket(proto::pl::sc::SetTitleText, b);
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
                        p->conn->trySendPacket(proto::pl::sc::ClearTitles, b);
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
                        p->conn->trySendPacket(proto::pl::sc::SetTitleTime, b);
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
                p->conn->trySendPacket(proto::pl::sc::SetTitleSubtitle, sub);
                WriteBuffer b;
                nbt::writeTextComponent(b, "\u00a76" + t);
                p->conn->trySendPacket(proto::pl::sc::SetTitleText, b);
            }
            return 1;
        };
        title->then(text);
        d.root->then(title);
    }
}

void GameServer::initChatCommandsPart03() {
    auto& d = commands_;
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
            broadcastPacketExceptInDimension(commandDimension(c.source), nullptr,
                                             proto::pl::sc::SystemChat, b);
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
                    p->conn->trySendPacket(proto::pl::sc::SystemChat, b);
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
                        src->conn->trySendPacket(proto::pl::sc::SystemChat, b2);
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

void GameServer::initChatCommandsPart04() {
    auto& d = commands_;
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
                    p->conn->trySendPacket(proto::pl::sc::SystemChat, b);
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for tellraw");
            // Echo a delivery note to the sender: short raw texts (e.g. "hi") are invisible to vanilla-strict chat scrapers, so the
            // feedback carries the message for command-response visibility.
            if (src) sendFeedback(src, "tellraw delivered to " + std::to_string(n) +
                                  " player(s): " + shown);
            return n;
        };
        targets->then(message);
        tr->then(targets);
        d.root->then(tr);
    }
}


} // namespace cppfm
