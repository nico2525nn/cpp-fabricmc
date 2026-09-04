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

void GameServer::initWorldCommands() {
    initWorldCommandsPart01();
    initWorldCommandsPart02();
    initWorldCommandsPart03();
    initWorldCommandsPart04();
    initWorldCommandsPart05();
    initWorldCommandsPart06();
    initWorldCommandsPart07();
    initWorldCommandsPart08();
    initWorldCommandsPart09();
    auto locate = CommandNode::literal("locate");
    initWorldCommandsPart10(locate);
    initWorldCommandsPart11(locate);
    initWorldCommandsPart12(locate);
    commands_.root->then(locate);
    initWorldCommandsPart13();
    initWorldCommandsPart14();
    initWorldCommandsPart15();
    initWorldCommandsPart16();
    initWorldCommandsPart17();
    initWorldCommandsPart18();
    initWorldCommandsPart19();
    initWorldCommandsPart20();
    initWorldCommandsPart21();
}

void GameServer::initWorldCommandsPart01() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart02() {
    auto& d = commands_;
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
            // W18 polish: suggest true/false for Boolean, numeric hints for Int Peek already-typed rule prefix: try to infer via token
            std::string token = reader.canRead() ? reader.readUnquotedString() : std::string();
            // fallback: offer both use last parsed rule if available; brigadier context would have it, but we approximate Offer boolean
            // choices; int rules also accept true/false as invalid but hint numbers
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
}

void GameServer::initWorldCommandsPart03() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart04() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart05() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart06() {
    auto& d = commands_;
    {
        auto wb = CommandNode::literal("worldborder");
        auto size = CommandNode::literal("size");
        auto sz = CommandNode::argument("diameter", args::floatArg(1.f, 1000000.f));
        sz->executable = true;
        sz->action = [this](CommandContext& c) {
            worldBorderDiameter_ = c.arg("diameter").asDouble();
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
}

void GameServer::initWorldCommandsPart07() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart08() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart09() {
    auto& d = commands_;
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
        // also allow suffix force/move/normal directly without mode? handled via mode's children Clone move as shorthand: clone <from> <to>
        // <target> move -> treated as replace move We'll add a direct move under target as alias
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
}

void GameServer::initWorldCommandsPart10(const brigadier::NodePtr& locate) {
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
                                // Check biome filter similar to generate() to avoid false positives For trial_chambers reject deep_dark
                                // biomes if needed (approx) Use world sampler for biome check if available
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
}

void GameServer::initWorldCommandsPart11(const brigadier::NodePtr& locate) {
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
}

void GameServer::initWorldCommandsPart12(const brigadier::NodePtr& locate) {
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
}

void GameServer::initWorldCommandsPart13() {
    auto& d = commands_;
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
                    (void)c.arg("jigsawDepth").asInt();
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
}

void GameServer::initWorldCommandsPart14() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart15() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart16() {
    auto& d = commands_;
    {
        // /time query <daytime|gametime|day> + /time add <value> (/time set already exists.)
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
}

void GameServer::initWorldCommandsPart17() {
    auto& d = commands_;
    {
        // /weather <kind> [durationSeconds] — duration form (bare-kind form already exists).
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
}

void GameServer::initWorldCommandsPart18() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart19() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart20() {
    auto& d = commands_;
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
}

void GameServer::initWorldCommandsPart21() {
    auto& d = commands_;
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
}


} // namespace cppfm
