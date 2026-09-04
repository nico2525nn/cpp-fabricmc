#pragma once
// CommandsHelpers — shared command helpers (single truth). Extracted from Commands.cpp initCommands surroundings (cleanup P3).
#include <map>
#include <string>
#include "GameServer.hpp"
#include "../core/ByteBuffer.hpp"
#include "../core/NBT.hpp"
#include "../proto/Ids.hpp"

namespace cppfm {

inline std::map<std::string,std::string> parseFunctionArgsNbt(const std::string& nbtStr) {
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

inline Player* findPlayer(GameServer& srv, const std::string& name) {
    for (auto& p : srv.playersSnapshot())
        if (p->name == name) return p.get();
    return nullptr;
}

inline void sendFeedback(Player* p, const std::string& msg) {
    if (p && p->conn) {
        WriteBuffer b;
        nbt::writeTextComponent(b, msg);
        b.boolean(false);
        try { p->conn->sendPacket(proto::pl::sc::SystemChat, b); } catch (...) {}
    } else {
        // plan42 R3 (E-19): capture console feedback for RCON responses (dispatchConsole returns it instead of fixed "ok").
        if (GameServer::consoleCapture_) {
            if (!GameServer::consoleCapture_->empty()) *GameServer::consoleCapture_ += "\n";
            *GameServer::consoleCapture_ += msg;
        }
        std::fprintf(stderr, "[cppfm] %s\n", msg.c_str());
    }
}

inline void writeSlotDisplayItem(WriteBuffer& bb, std::uint32_t itemId) {
    bb.varint(itemId ? 2 : 0);
    if (itemId) bb.varint(static_cast<std::int32_t>(itemId));
}

} // namespace cppfm
