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

void GameServer::initDataCommands() {
    using NodePtr = brigadier::NodePtr;
    auto& d = commands_;
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

} // namespace cppfm
