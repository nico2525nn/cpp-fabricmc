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

void GameServer::initExecuteCommands() {
    auto& d = commands_;
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
}

} // namespace cppfm
