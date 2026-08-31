#pragma once
// StairsHelper — plan31 R6: stairs shape helpers extracted from GameServer.cpp (120-180 lines)
// Pure helpers + GameServer/world dependent helper, wire-unchanged.
// Yarn StairsBlock#getStairsShape + isDifferentOrientation strict (front+opposite only, no 4-dir side loop).
#include "GameServer.hpp"
#include "../generated/BlockStates.hpp"
#include <string>
#include <vector>

namespace cppfm {

// plan19 §1 B1 stairs strict: only front+opposite per Yarn StairsBlock#getStairsShape
inline bool isStairsBlock(const gen::BlockDef* d){
    if(!d) return false;
    std::string n(d->name);
    return n.find("_stairs")!=std::string::npos;
}
inline std::string getPropStr(std::uint16_t state, const char* key){
    for(auto&[k,v]: gen::propsOf(state)) if(k==key) return std::string(v);
    return "";
}
// plan19 §1 B1 stairs strict: isDifferentOrientation per Yarn StairsBlock#isDifferentOrientation (vanilla 1.21.4)
inline bool isDifferentOrientation(World& w, int x,int y,int z, const std::string& stateFacing, const std::string& dir){
    int nx=x, nz=z;
    if(dir=="north") nz-=1;
    else if(dir=="south") nz+=1;
    else if(dir=="east") nx+=1;
    else if(dir=="west") nx-=1;
    else return true;
    std::uint16_t ns = w.getBlock(nx,y,nz);
    const gen::BlockDef* nd = gen::blockByState(ns);
    if(!isStairsBlock(nd)) return true;
    std::string nf = getPropStr(ns,"facing");
    auto axisOf2 = [](const std::string& f)->char{
        if(f=="north"||f=="south") return 'z';
        if(f=="east"||f=="west") return 'x';
        return 'y';
    };
    if(axisOf2(nf) != axisOf2(stateFacing)) return true;
    auto opposite2 = [](const std::string& f)->std::string{
        if(f=="north") return "south";
        if(f=="south") return "north";
        if(f=="east") return "west";
        if(f=="west") return "east";
        return f;
    };
    if(nf == opposite2(stateFacing)) return true;
    if(nf == stateFacing) return false;
    return true;
}
inline std::string computeStairsShape(World& w, int x,int y,int z, const std::string& facing, const std::string& half){
    auto axisOf = [](const std::string& f)->char{
        if(f=="north"||f=="south") return 'z';
        if(f=="east"||f=="west") return 'x';
        return 'y';
    };
    auto rotateCCW = [](const std::string& f)->std::string{
        if(f=="north") return "west";
        if(f=="west") return "south";
        if(f=="south") return "east";
        if(f=="east") return "north";
        return f;
    };
    auto rotateCW = [](const std::string& f)->std::string{
        if(f=="north") return "east";
        if(f=="east") return "south";
        if(f=="south") return "west";
        if(f=="west") return "north";
        return f;
    };
    auto opposite = [](const std::string& f)->std::string{
        if(f=="north") return "south";
        if(f=="south") return "north";
        if(f=="east") return "west";
        if(f=="west") return "east";
        if(f=="up") return "down";
        if(f=="down") return "up";
        return f;
    };
    (void)rotateCW;
    {
        int nx=x, nz=z;
        if(facing=="north") nz-=1; else if(facing=="south") nz+=1;
        else if(facing=="east") nx+=1; else if(facing=="west") nx-=1;
        std::uint16_t ns = w.getBlock(nx,y,nz);
        const gen::BlockDef* nd = gen::blockByState(ns);
        if(isStairsBlock(nd)){
            std::string nf = getPropStr(ns,"facing");
            std::string nh = getPropStr(ns,"half");
            if(nh==half && axisOf(nf)!=axisOf(facing)){
                std::string checkDir = opposite(nf);
                if(isDifferentOrientation(w,x,y,z,facing,checkDir)){
                    if(nf == rotateCCW(facing)) return "outer_left";
                    else return "outer_right";
                }
            }
        }
    }
    {
        int nx=x, nz=z;
        std::string opp = opposite(facing);
        if(opp=="north") nz-=1; else if(opp=="south") nz+=1;
        else if(opp=="east") nx+=1; else if(opp=="west") nx-=1;
        std::uint16_t ns = w.getBlock(nx,y,nz);
        const gen::BlockDef* nd = gen::blockByState(ns);
        if(isStairsBlock(nd)){
            std::string nf = getPropStr(ns,"facing");
            std::string nh = getPropStr(ns,"half");
            if(nh==half && axisOf(nf)!=axisOf(facing)){
                std::string checkDir = nf;
                if(isDifferentOrientation(w,x,y,z,facing,checkDir)){
                    if(nf == rotateCCW(facing)) return "inner_left";
                    else return "inner_right";
                }
            }
        }
    }
    return "straight";
}
inline void updateNeighborStairsShapes(World& w, GameServer& srv, int x,int y,int z){
    uint16_t placed = w.getBlock(x,y,z);
    const gen::BlockDef* pd = gen::blockByState(placed);
    if(!isStairsBlock(pd)){
        static const int DX4[4]={1,-1,0,0}, DZ4[4]={0,0,1,-1};
        for(int i=0;i<4;++i){
            int nx=x+DX4[i], nz=z+DZ4[i];
            uint16_t ns=w.getBlock(nx,y,nz);
            const gen::BlockDef* nd=gen::blockByState(ns);
            if(!isStairsBlock(nd)) continue;
            std::string nf=getPropStr(ns,"facing");
            std::string nh=getPropStr(ns,"half");
            std::string shape=computeStairsShape(w,nx,y,nz,nf,nh);
            std::string curShape=getPropStr(ns,"shape");
            if(curShape==shape) continue;
            std::vector<std::pair<std::string_view,std::string_view>> props;
            for(auto&[k,v]: gen::propsOf(ns)) if(k!="shape") props.emplace_back(k,v);
            props.emplace_back("shape", shape);
            uint16_t nst=static_cast<uint16_t>(gen::stateWithProps(*nd, props));
            w.setBlock(nx,y,nz,nst);
            srv.broadcastBlockChange(nx,y,nz,nst);
        }
        return;
    }
    std::string pf=getPropStr(placed,"facing");
    int fdx=0,fdz=0,bdx=0,bdz=0;
    if(pf=="north"){ fdz=-1; bdz=1; }
    else if(pf=="south"){ fdz=1; bdz=-1; }
    else if(pf=="east"){ fdx=1; bdx=-1; }
    else if(pf=="west"){ fdx=-1; bdx=1; }
    else {
        static const int DX4[4]={1,-1,0,0}, DZ4[4]={0,0,1,-1};
        for(int i=0;i<4;++i){
            int nx=x+DX4[i], nz=z+DZ4[i];
            uint16_t ns=w.getBlock(nx,y,nz);
            const gen::BlockDef* nd=gen::blockByState(ns);
            if(!isStairsBlock(nd)) continue;
            std::string nf=getPropStr(ns,"facing");
            std::string nh=getPropStr(ns,"half");
            std::string shape=computeStairsShape(w,nx,y,nz,nf,nh);
            std::string curShape=getPropStr(ns,"shape");
            if(curShape==shape) continue;
            std::vector<std::pair<std::string_view,std::string_view>> props;
            for(auto&[k,v]: gen::propsOf(ns)) if(k!="shape") props.emplace_back(k,v);
            props.emplace_back("shape", shape);
            uint16_t nst=static_cast<uint16_t>(gen::stateWithProps(*nd, props));
            w.setBlock(nx,y,nz,nst);
            srv.broadcastBlockChange(nx,y,nz,nst);
        }
        return;
    }
    const int DX2[2]={fdx,bdx};
    const int DZ2[2]={fdz,bdz};
    for(int i=0;i<2;++i){
        int nx=x+DX2[i], nz=z+DZ2[i];
        uint16_t ns=w.getBlock(nx,y,nz);
        const gen::BlockDef* nd=gen::blockByState(ns);
        if(!isStairsBlock(nd)) continue;
        std::string nf=getPropStr(ns,"facing");
        std::string nh=getPropStr(ns,"half");
        std::string shape=computeStairsShape(w,nx,y,nz,nf,nh);
        std::string curShape=getPropStr(ns,"shape");
        if(curShape==shape) continue;
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for(auto&[k,v]: gen::propsOf(ns)) if(k!="shape") props.emplace_back(k,v);
        props.emplace_back("shape", shape);
        uint16_t nst=static_cast<uint16_t>(gen::stateWithProps(*nd, props));
        w.setBlock(nx,y,nz,nst);
        srv.broadcastBlockChange(nx,y,nz,nst);
    }
}

} // namespace cppfm
