// Redstone signal and neighbor-response implementation.
#include "Redstone.hpp"
#include "RedstoneInternal.hpp"
#include "../game/BlockEntities.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/BlockStates.hpp"
#include "../game/MenuInteraction.hpp" // maxStackForId single truth
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>

namespace cppfm {

RedstoneEngine::Comp RedstoneEngine::classify(std::uint16_t state) {
    const gen::BlockDef* b = gen::blockByState(state);
    if (!b) return Comp::None;
    auto prop = [&](std::string_view key) -> std::string {
        for (auto& [k, v] : gen::propsOf(state))
            if (k == key) return std::string(v);
        return {};
    };
    if (b->name == "minecraft:redstone_wire") return Comp::Wire;
    if (b->name == "minecraft:lever") {
        return prop("powered") == "true" ? Comp::LeverOn : Comp::LeverOff;
    }
    if (b->name.find("button") != std::string::npos &&
        b->name.find("_button") != std::string::npos)
        return prop("powered") == "true" ? Comp::ButtonOn : Comp::None;
    if (b->name == "minecraft:redstone_torch" ||
        b->name == "minecraft:redstone_wall_torch")
        return prop("lit") != "false" ? Comp::TorchOn : Comp::TorchOff;
    if (b->name == "minecraft:redstone_lamp")
        return prop("lit") == "true" ? Comp::LampLit : Comp::LampOff;
    if (b->name == "minecraft:redstone_block") return Comp::BlockSource;
    if (b->name == "minecraft:repeater") return Comp::Repeater;
    // comparator: name contains comparator
    if (b->name.find("comparator") != std::string::npos) return Comp::Comparator;
    if (b->name == "minecraft:observer") return Comp::Observer;
    if (b->name == "minecraft:powered_rail") return Comp::PoweredRail;
    if (b->name == "minecraft:detector_rail") return Comp::DetectorRail;
    if (b->name == "minecraft:activator_rail") return Comp::ActivatorRail;
    if (b->name == "minecraft:rail") return Comp::Rail;
    if (b->name == "minecraft:piston") return Comp::Piston;
    if (b->name == "minecraft:sticky_piston") return Comp::StickyPiston;
    return Comp::None;
}

int RedstoneEngine::maxEmissionFor(Comp c) {
    switch (c) {
    case Comp::LeverOn:
    case Comp::ButtonOn:
    case Comp::BlockSource:
    case Comp::TorchOn:
        return 15;
    // comparator/observer/rails emit via emissionLevel variable
    case Comp::DetectorRail:
    case Comp::PoweredRail:
    case Comp::Observer:
    case Comp::Comparator:
    case Comp::Repeater:
        return 0;
    default: return 0;
    }
}

int RedstoneEngine::analogOutputForContainer(BlockEntity* be) {
    if (!be) return 0;
    int slots = 0;
    int filled = 0;
    double fillSum = 0;
    switch (be->kind) {
    case BlockEntity::Kind::Chest: {
        slots = 27;
        for (int i=0;i<27;++i) {
            auto &s = be->chest.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/double(maxStackForId(s.itemId)); }
        }
        break;
    }
    case BlockEntity::Kind::Barrel:
    case BlockEntity::Kind::ShulkerBox: {
        slots = 27;
        for (int i=0;i<27;++i) {
            auto &s = be->chest.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/double(maxStackForId(s.itemId)); }
        }
        break;
    }
    case BlockEntity::Kind::Furnace: {
        slots = 3;
        for (int i=0;i<3;++i) {
            auto &s = be->furnace.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/double(maxStackForId(s.itemId)); }
        }
        break;
    }
    case BlockEntity::Kind::Hopper: {
        slots = 5;
        for (int i=0;i<5;++i) {
            auto &s = be->generic.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/double(maxStackForId(s.itemId)); }
        }
        break;
    }
    case BlockEntity::Kind::Dispenser:
    case BlockEntity::Kind::Dropper: {
        slots = 9;
        for (int i=0;i<9;++i) {
            auto &s = be->generic.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/double(maxStackForId(s.itemId)); }
        }
        break;
    }
    case BlockEntity::Kind::Brewing: {
        slots = 5;
        for (int i=0;i<5;++i) {
            auto &s = be->brewing.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/double(maxStackForId(s.itemId)); }
        }
        break;
    }
    case BlockEntity::Kind::Crafter: {
        // Disabled recipe slots are not part of the crafter's inventory for
        // comparator purposes.  Counting them would make a crafter emit a
        // weaker signal simply because its recipe layout has been configured.
        for (int i = 0; i < CrafterData::kSlots; ++i) {
            if (be->crafter.isSlotDisabled(i)) continue;
            ++slots;
            const auto& s = be->crafter.slots[i];
            if (!s.empty()) {
                ++filled;
                fillSum += double(s.count) / double(maxStackForId(s.itemId));
            }
        }
        break;
    }
    case BlockEntity::Kind::MovingPiston: {
        return 0;
    }
    case BlockEntity::Kind::Sign:
        break; // signs hold no items: 0 output (same as unlisted kinds)
    }
    if (slots==0) return 0;
    if (filled==0) return 0;
    // where fillSum = sum(count/maxStack) and filled>0 gives at least 1
    double avg = fillSum / double(slots);
    int sig = static_cast<int>(std::floor(1.0 + avg * 14.0));
    if (sig>15) sig=15;
    if (sig<0) sig=0;
    if (sig==0 && filled>0) sig=1;
    return sig;
}

static bool isConductiveBlock(std::uint16_t st) {
    if (st==0) return false;
    const gen::BlockDef* bd = gen::blockByState(st);
    if (!bd) return false;
    std::string_view n(bd->name);
    if (n=="minecraft:air" || n=="minecraft:cave_air" || n=="minecraft:void_air") return false;
    if (n.find("glass")!=std::string_view::npos) return false;
    if (bd->transparent) return false;
    if (n=="minecraft:barrier" || n=="minecraft:light") return false;
    return bd->filterLight==15;
}

int RedstoneEngine::analogOutputAt(std::int32_t x, std::int32_t y, std::int32_t z) {
    if (!beStore_) return 0;
    const auto be = beStore_->getAt(x, y, z);
    if (!be) {
        // also check if block is chest etc without BE? Try to treat as empty
        return 0;
    }
    std::lock_guard lock(*be->stateMtx);
    return analogOutputForContainer(be.get());
}

int RedstoneEngine::emissionLevel(std::uint16_t state, std::int32_t x, std::int32_t y, std::int32_t z) {
    Comp c = classify(state);
    switch (c) {
    case Comp::LeverOn:
    case Comp::ButtonOn:
    case Comp::BlockSource:
    case Comp::TorchOn:
        return 15;
    case Comp::Repeater: {
        // repeater emits 15 when powered property true
        for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
        // also check locked? If locked, not emit
        return 0;
    }
    case Comp::Comparator: {
        bool powered = false;
        std::string facing="north";
        std::string mode="compare";
        for (auto& [k,v] : gen::propsOf(state)) {
            if (k=="powered" && v=="true") powered=true;
            if (k=="facing") facing=std::string(v);
            if (k=="mode") mode=std::string(v);
        }
        if (!powered) {
            // Even if not powered property, we compute analog but need to decide: vanilla comparator powered indicates output>0 We will
            // compute analog and consider powered = output>0 So compute output first
        }
        int bx=x, by=y, bz=z;
        if (facing=="north") bz+=1;
        else if (facing=="south") bz-=1;
        else if (facing=="west") bx+=1;
        else if (facing=="east") bx-=1;
        else if (facing=="up") by-=1;
        else if (facing=="down") by+=1;
        int directContainerSig = analogOutputAt(bx,by,bz);
        const auto rearBE = beStore_ ? beStore_->getAt(bx, by, bz)
                                     : BlockEntityStore::Owner{};
        const bool hasDirectContainer = static_cast<bool>(rearBE);
        int containerSig = directContainerSig;
        uint16_t rearSt = world_.getBlock(bx,by,bz);
        bool rearConductive = isConductiveBlock(rearSt);
        bool overPowered = false;
        if (!hasDirectContainer && rearConductive) {
            // check one more block behind through conductive
            int dx = bx - x, dy = by - y, dz = bz - z;
            int bx2 = bx + dx, by2 = by + dy, bz2 = bz + dz;
            int sig2 = analogOutputAt(bx2,by2,bz2);
            const auto be2 = beStore_ ? beStore_->getAt(bx2, by2, bz2)
                                      : BlockEntityStore::Owner{};
            if (be2) {
                containerSig = sig2;
                // check overpower: if conductive block is powered to 15, output 15 irrespective of analog
                // compute strongest neighbor power at (bx,by,bz)
                int rearPowerLevel = 0;
                static constexpr int DX6[6]={1,-1,0,0,0,0};
                static constexpr int DY6[6]={0,0,1,-1,0,0};
                static constexpr int DZ6[6]={0,0,0,0,1,-1};
                for(int d=0;d<6;++d){
                    int nx=bx+DX6[d], ny=by+DY6[d], nz=bz+DZ6[d];
                    uint16_t ns = world_.getBlock(nx,ny,nz);
                    Comp sc = classify(ns);
                    int lvl=0;
                    if(sc==Comp::Wire){
                        for(auto&[k,v]: gen::propsOf(ns)) if(k=="power") lvl=std::atoi(std::string(v).c_str());
                    } else {
                        lvl = emissionLevel(ns,nx,ny,nz);
                    }
                    if(lvl>rearPowerLevel) rearPowerLevel=lvl;
                }
                if(rearPowerLevel>=15) overPowered=true;
            }
        }
        auto rearPower = [&]()->int {
            std::uint16_t sst = world_.getBlock(bx, by, bz);
            Comp sc = classify(sst);
            if (sc==Comp::Wire) {
                for (auto& [k,v] : gen::propsOf(sst)) if (k=="power") return std::atoi(std::string(v).c_str());
            }
            if (maxEmissionFor(sc)>0) return 15;
            int lvl = emissionLevel(sst, bx, by, bz);
            return lvl;
        };
        int rearDust = rearPower();
        int out;
        if (overPowered) out = 15;
        else out = std::max(containerSig, rearDust);
        auto sidePower = [&](int sx, int sz)->int {
            std::uint16_t sst = world_.getBlock(sx, y, sz);
            Comp sc = classify(sst);
            if (sc==Comp::Wire) {
                for (auto& [k,v] : gen::propsOf(sst)) if (k=="power") return std::atoi(std::string(v).c_str());
            }
            if (maxEmissionFor(sc)>0) return 15;
            int lvl = emissionLevel(sst, sx, y, sz);
            return lvl;
        };
        if (!overPowered && (mode=="subtract" || mode=="compare")) {
            int sx1=x, sz1=z, sx2=x, sz2=z;
            if (facing=="north" || facing=="south") { sx1+=1; sx2-=1; }
            else if (facing=="east" || facing=="west") { sz1+=1; sz2-=1; }
            else { // up/down
                sx1+=1; sx2-=1; // fallback
            }
            int side = std::max(sidePower(sx1, sz1), sidePower(sx2, sz2));
            if (mode=="subtract") {
                out = std::max(0, out - side);
            } else { // compare: output only if input >= side, else 0
                if (side > out) out = 0;
            }
        }
        return out;
    }
    case Comp::Observer: {
        for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
        return 0;
    }
    case Comp::PoweredRail:
    case Comp::DetectorRail:
    case Comp::ActivatorRail: {
        for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
        return 0;
    }
    default:
        return maxEmissionFor(c);
    }
}

bool RedstoneEngine::isPoweredHere(std::int32_t x, std::int32_t y,
                                   std::int32_t z) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    return isPoweredHereImpl(x, y, z);
}

bool RedstoneEngine::isPoweredHereImpl(std::int32_t x, std::int32_t y,
                                       std::int32_t z) {
    constexpr std::int32_t kMinCoord = -(1 << 25);
    constexpr std::int32_t kMaxCoord = (1 << 25) - 1;
    if (x < kMinCoord || x > kMaxCoord || z < kMinCoord || z > kMaxCoord ||
        y < world_.minY() || y >= world_.maxY()) return false;
    // A directly powered input (notably a redstone block) is power at this
    // position.  The old query only inspected neighbours, so repeaters fed
    // directly by a source never latched.
    const std::uint16_t here = world_.getBlock(x, y, z);
    if (emissionLevel(here, x, y, z) > 0) return true;
    if (classify(here) == Comp::Wire) {
        for (const auto& [key, value] : gen::propsOf(here))
            if (key == "power" && std::atoi(std::string(value).c_str()) > 0)
                return true;
    }
    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};
    for (int d = 0; d < 6; ++d) {
        const std::int32_t nx = x + DX[d], ny = y + DY[d], nz = z + DZ[d];
        const std::uint16_t ns = world_.getBlock(nx, ny, nz);
        const Comp nc = classify(ns);
        int lvl = emissionLevel(ns, nx, ny, nz);
        if (lvl > 0) return true;
        if (nc == Comp::Wire) {
            for (auto& [k, v] : gen::propsOf(ns))
                if (k == "power" && std::atoi(std::string(v).c_str()) > 0)
                    return true;
        }
        // also check if wire network provides power indirectly via updateWireNetwork? Already covered.
    }
    return false;
}

void RedstoneEngine::onBlockChanged(std::int32_t x, std::int32_t y,
                                    std::int32_t z) {
    if (!enqueueBlockChanged(x, y, z)) return;
    {
        std::lock_guard<std::mutex> notificationLock(notificationMutex_);
        if (processingBlockChanges_) return;
    }

    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    {
        std::lock_guard<std::mutex> notificationLock(notificationMutex_);
        // Another operation may have become the processor after the first
        // check.  It will also drain this notification, so do not process a
        // second recursive walk here.
        if (processingBlockChanges_) return;
        processingBlockChanges_ = true;
    }
    drainBlockChanges();
    {
        std::lock_guard<std::mutex> notificationLock(notificationMutex_);
        processingBlockChanges_ = false;
    }
}

bool RedstoneEngine::enqueueBlockChanged(std::int32_t x, std::int32_t y,
                                         std::int32_t z) {
    constexpr std::int32_t kMinCoord = -(1 << 25);
    constexpr std::int32_t kMaxCoord = (1 << 25) - 1;
    if (x < kMinCoord || x > kMaxCoord || z < kMinCoord || z > kMaxCoord ||
        y < world_.minY() || y >= world_.maxY()) return false;
    std::lock_guard<std::mutex> notificationLock(notificationMutex_);
    pendingBlockChanges_.insert(posKey(x, y, z));
    return true;
}

void RedstoneEngine::drainBlockChanges() {
    for (;;) {
        std::int64_t key = 0;
        {
            std::lock_guard<std::mutex> notificationLock(notificationMutex_);
            if (pendingBlockChanges_.empty()) return;
            key = *pendingBlockChanges_.begin();
            pendingBlockChanges_.erase(pendingBlockChanges_.begin());
        }
        processBlockChanged(posKeyUnpackX(key), posKeyUnpackY(key),
                            posKeyUnpackZ(key));
    }
}

void RedstoneEngine::processBlockChanged(std::int32_t x, std::int32_t y,
                                         std::int32_t z) {
    // Includes ChunkTicket SPAWN forced always-tick for spawn chunks.
    if (!world_.isChunkInSimulationDistance(x >> 4, z >> 4) && !world_.isPositionInSimulationDistance(x, z)) return;
    recomputeAround(x, y, z);
    // Rails shape recompute for changed pos and neighbors
    recomputeRailShape(x,y,z);
    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};
    for (int d=0; d<6; ++d) recomputeRailShape(x+DX[d], y+DY[d], z+DZ[d]);
    // Pistons react at changed pos and neighbors
    handlePiston(x,y,z);
    for (int d=0; d<6; ++d) handlePiston(x+DX[d], y+DY[d], z+DZ[d]);
    // QC y+1 evaluation ensures non-adjacent power (lever on block above piston) propagates even if recomputeAround misses due to wire
    // flood-fill order. Re-evaluate one above/below for QC.
    handlePiston(x, y - 1, z);
    handlePiston(x, y + 1, z);
    // Doors react to redstone (powered & hinge already handled at placement, here update powered/open)
    handleDoor(x,y,z);
    for (int d=0; d<6; ++d) handleDoor(x+DX[d], y+DY[d], z+DZ[d]);
    // also check one up for QC door power (door is 2 tall, QC via above)
    handleDoor(x, y+1, z);
    handleDoor(x, y-1, z);

    // Observer detection: any observer whose front faces the changed block should pulse
    // Check 6 neighbors of the changed block; each could be an observer facing toward changed block
    for (int d=0; d<6; ++d) {
        const std::int32_t ox = x + DX[d], oy = y + DY[d], oz = z + DZ[d];
        std::uint16_t ost = world_.getBlock(ox,oy,oz);
        if (classify(ost) != Comp::Observer) continue;
        std::string facing;
        for (auto& [k,v] : gen::propsOf(ost)) if (k=="facing") facing = std::string(v);
        int fdx=0,fdy=0,fdz=0;
        if (facing=="north") fdz=-1;
        else if (facing=="south") fdz=1;
        else if (facing=="west") fdx=-1;
        else if (facing=="east") fdx=1;
        else if (facing=="up") fdy=1;
        else if (facing=="down") fdy=-1;
        else continue;
        // observer front is ox+fdx, oy+fdy, oz+fdz ; if that equals changed pos, trigger
        if (ox + fdx == x && oy + fdy == y && oz + fdz == z) {
            // trigger only if not already pulsing
            std::int64_t key = posKey(ox,oy,oz);
            if (observerPulseEnd_.count(key)) continue;
            std::int64_t now = redstone_detail::currentTick(
                tickRef_, atomicTickRef_);
            handleObserverTrigger(ox,oy,oz, now);
        }
    }
    // store current state for observers front check future also handle comparator update
    handleComparator(x,y,z);
    for (int d=0; d<6; ++d) handleComparator(x+DX[d], y+DY[d], z+DZ[d]);

    // Repeater delay handling: check repeaters near change
    const std::int64_t now = redstone_detail::currentTick(
        tickRef_, atomicTickRef_);
    handleRepeaterDelay(x, y, z, now);
    for (int d = 0; d < 6; ++d)
        handleRepeaterDelay(x + DX[d], y + DY[d], z + DZ[d], now);
}

void RedstoneEngine::handleObserverTrigger(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t now) {
    std::uint16_t st = world_.getBlock(x,y,z);
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    // set powered true
    bool curPowered = false;
    for (auto& [k,v] : gen::propsOf(st)) if (k=="powered" && v=="true") curPowered=true;
    if (curPowered) return;
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(st)) if (k!="powered") props.emplace_back(k,v);
    props.emplace_back("powered", "true");
    std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
    world_.setBlock(x,y,z, ns);
    recomputeAround(x,y,z);
    // schedule 2-tick pulse off
    queue_.push({x,y,z, now+2});
    observerPulseEnd_[posKey(x,y,z)] = now+2;
}

void RedstoneEngine::handleComparator(std::int32_t x, std::int32_t y, std::int32_t z) {
    std::uint16_t st = world_.getBlock(x,y,z);
    if (classify(st) != Comp::Comparator) return;
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    int out = emissionLevel(st, x,y,z);
    bool wantPowered = out > 0;
    bool curPowered = false;
    for (auto& [k,v] : gen::propsOf(st)) if (k=="powered" && v=="true") curPowered=true;
    if (curPowered != wantPowered) {
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for (auto& [k,v] : gen::propsOf(st)) if (k!="powered") props.emplace_back(k,v);
        props.emplace_back("powered", wantPowered?"true":"false");
        std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
        world_.setBlock(x,y,z, ns);
        recomputeAround(x,y,z);
    }
    // also if mode is compare/subtract, recompute may affect wire?
}

void RedstoneEngine::handleRepeaterDelay(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t now) {
    std::uint16_t st = world_.getBlock(x,y,z);
    if (classify(st) != Comp::Repeater) return;
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    // get facing and delay
    std::string facing="north";
    int delay=1;
    bool curPowered=false;
    bool locked=false;
    for (auto& [k,v] : gen::propsOf(st)) {
        if (k=="facing") facing=std::string(v);
        else if (k=="delay") delay=std::atoi(std::string(v).c_str());
        else if (k=="powered") curPowered = (v=="true");
        else if (k=="locked") locked = (v=="true");
    }
    if (locked) return;
    // input pos is opposite of facing
    int bx=x, bz=z;
    int by=y;
    if (facing=="north") bz+=1;
    else if (facing=="south") bz-=1;
    else if (facing=="west") bx+=1;
    else if (facing=="east") bx-=1;
    // check if input is powered
    bool inputPowered = isPoweredHere(bx,by,bz);
    // also check if block behind is directly powered source or wire isPoweredHere already checks adjacent, but we need power at input
    // position towards repeater? We'll approximate: if input block is powered, then repeater should eventually be powered Determine desired
    // powered state = inputPowered
    bool wantPowered = inputPowered;
    if (wantPowered == curPowered) {
        // cancel pending if any?
        pendingRepeater_.erase(posKey(x,y,z));
        return;
    }
    // schedule change after delay*2 ticks
    std::int64_t key = posKey(x,y,z);
    std::int64_t due = now + delay*2;
    auto it = pendingRepeater_.find(key);
    if (it != pendingRepeater_.end() && it->second == due) return;
    pendingRepeater_[key] = due;
    queue_.push({x,y,z, due});
}

void RedstoneEngine::recomputeRailShape(std::int32_t x, std::int32_t y, std::int32_t z) {
    std::uint16_t st = world_.getBlock(x,y,z);
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    std::string name(b->name);
    bool isRail = (name=="minecraft:rail" || name=="minecraft:powered_rail" || name=="minecraft:detector_rail" || name=="minecraft:activator_rail");
    if (!isRail) return;
    // determine shape based on neighbors For rail, powered_rail etc have shape property; for regular rail also shape
    bool hasShape = false;
    for (int i=0;i<b->propCount;++i) {
        const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[b->propsOff+i]];
        if (pd.name=="shape") hasShape=true;
    }
    if (!hasShape) return;
    // A rail shape is not just a pair of compass directions: an ascending
    // shape has one end at the bottom of the block and the other one block
    // higher. Describing shapes as endpoint heights lets the same code
    // validate both sides of a slope. For example, two rails at (x,y,z) and
    // (x+1,y+1,z) both use ascending_east; the lower rail's east endpoint and
    // the upper rail's west endpoint are at the same world height.
    struct Endpoint {
        const char* direction;
        int height;
    };
    auto endpointsFor = [](const std::string& shape) {
        std::vector<Endpoint> endpoints;
        auto add = [&](const char* direction, int height) {
            endpoints.push_back({direction, height});
        };
        if (shape == "north_south") { add("north", 0); add("south", 0); }
        else if (shape == "east_west") { add("east", 0); add("west", 0); }
        else if (shape == "ascending_east") { add("west", 0); add("east", 1); }
        else if (shape == "ascending_west") { add("east", 0); add("west", 1); }
        else if (shape == "ascending_north") { add("south", 0); add("north", 1); }
        else if (shape == "ascending_south") { add("north", 0); add("south", 1); }
        else if (shape == "south_east") { add("south", 0); add("east", 0); }
        else if (shape == "south_west") { add("south", 0); add("west", 0); }
        else if (shape == "north_west") { add("north", 0); add("west", 0); }
        else if (shape == "north_east") { add("north", 0); add("east", 0); }
        return endpoints;
    };
    auto opposite = [](const char* direction) -> const char* {
        if (std::string(direction) == "east") return "west";
        if (std::string(direction) == "west") return "east";
        if (std::string(direction) == "north") return "south";
        return "north";
    };
    auto offsetFor = [](const char* direction) {
        if (std::string(direction) == "east") return std::pair<int,int>{1, 0};
        if (std::string(direction) == "west") return std::pair<int,int>{-1, 0};
        if (std::string(direction) == "north") return std::pair<int,int>{0, -1};
        return std::pair<int,int>{0, 1};
    };
    auto isRailState = [&](std::uint16_t state) {
        const gen::BlockDef* rail = gen::blockByState(state);
        if (!rail) return false;
        const std::string railName(rail->name);
        return railName == "minecraft:rail" ||
               railName == "minecraft:powered_rail" ||
               railName == "minecraft:detector_rail" ||
               railName == "minecraft:activator_rail";
    };
    auto shapeOf = [&](std::uint16_t state) {
        std::string shape;
        for (auto& [key, value] : gen::propsOf(state))
            if (key == "shape") shape = std::string(value);
        return shape;
    };
    auto endpointHeight = [&](const std::string& shape, const char* direction) {
        for (const Endpoint endpoint : endpointsFor(shape))
            if (std::string(endpoint.direction) == direction) return endpoint.height;
        return -1;
    };

    struct Neighbor {
        bool present = false;
        std::int32_t x = 0, y = 0, z = 0;
        std::uint16_t state = 0;
    };
    const std::array<const char*, 4> directions = {"east", "west", "north", "south"};
    std::array<Neighbor, 4> neighbors;
    for (std::size_t i = 0; i < directions.size(); ++i) {
        const auto [dx, dz] = offsetFor(directions[i]);
        // A rail one block above/below is a valid slope neighbor. Keep the
        // nearest candidate in deterministic order (above, same level,
        // below), matching vanilla's preference for an ascending connection.
        for (const int dy : {1, 0, -1}) {
            const std::int32_t nx = x + dx, ny = y + dy, nz = z + dz;
            const std::uint16_t neighborState = world_.getBlock(nx, ny, nz);
            if (isRailState(neighborState)) {
                neighbors[i] = {true, nx, ny, nz, neighborState};
                break;
            }
        }
    }

    const std::string currentShape = shapeOf(st);
    const bool curvesAllowed = name == "minecraft:rail";
    const std::array<const char*, 10> allShapes = {
        "north_south", "east_west", "ascending_east", "ascending_west",
        "ascending_north", "ascending_south", "south_east", "south_west",
        "north_west", "north_east"};

    struct ShapeScore {
        int exact = 0;
        int matched = 0;
        int vertical = 0;
        int ascending = 0;
        int straight = 0;
        int preserves = 0;
    };
    auto better = [](const ShapeScore& lhs, const ShapeScore& rhs) {
        if (lhs.exact != rhs.exact) return lhs.exact > rhs.exact;
        if (lhs.matched != rhs.matched) return lhs.matched > rhs.matched;
        if (lhs.vertical != rhs.vertical) return lhs.vertical > rhs.vertical;
        if (lhs.ascending != rhs.ascending) return lhs.ascending > rhs.ascending;
        if (lhs.straight != rhs.straight) return lhs.straight > rhs.straight;
        return lhs.preserves > rhs.preserves;
    };

    std::string wantShape = currentShape.empty() ? "north_south" : currentShape;
    ShapeScore bestScore{};
    bool haveCandidate = false;
    for (const char* candidate : allShapes) {
        const std::string candidateShape(candidate);
        if (!curvesAllowed &&
            (candidateShape == "south_east" || candidateShape == "south_west" ||
             candidateShape == "north_west" || candidateShape == "north_east"))
            continue;

        ShapeScore score;
        for (std::size_t i = 0; i < directions.size(); ++i) {
            if (!neighbors[i].present) continue;
            const char* direction = directions[i];
            const int ownHeight = endpointHeight(candidateShape, direction);
            if (ownHeight < 0) continue;
            const Neighbor& neighbor = neighbors[i];
            const int worldHeight = y + ownHeight;
            const std::string neighborShape = shapeOf(neighbor.state);
            const int neighborHeight = endpointHeight(neighborShape, opposite(direction));
            if (neighborHeight >= 0 && neighbor.y + neighborHeight == worldHeight) {
                ++score.exact;
                ++score.matched;
            } else if (neighbor.y == worldHeight || neighbor.y + 1 == worldHeight) {
                // The neighbor may still be waiting for its own neighbor
                // update. Count a geometrically possible connection, but
                // prefer the mutually aligned result above.
                ++score.matched;
            }
            if (neighbor.y != y) {
                ++score.vertical;
                const bool candidateIsAscending = candidateShape.rfind("ascending_", 0) == 0;
                const bool risesTowardNeighbor = neighbor.y > y && ownHeight == 1;
                const bool risesAwayFromNeighbor = neighbor.y < y && ownHeight == 0;
                if (candidateIsAscending && (risesTowardNeighbor || risesAwayFromNeighbor))
                    ++score.ascending;
            }
        }
        if (candidateShape == "north_south" || candidateShape == "east_west")
            score.straight = 1;
        if (candidateShape == currentShape) score.preserves = 1;
        if (!haveCandidate || better(score, bestScore)) {
            bestScore = score;
            wantShape = candidateShape;
            haveCandidate = true;
        }
    }

    // With no neighbors, vanilla's default is the north/south state.
    if (bestScore.matched == 0) {
        wantShape = "north_south";
    }

    // Apply shape
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(st)) if (k!="shape") props.emplace_back(k,v);
    // Only set if different
    std::string curShape;
    for (auto& [k,v] : gen::propsOf(st)) if (k=="shape") curShape=std::string(v);
    if (curShape==wantShape) return;
    // Verify that wantShape is valid for this block: check if stateWithProps succeeds and stays same block type
    props.emplace_back("shape", wantShape);
    std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
    const gen::BlockDef* nb = gen::blockByState(ns);
    if (!nb || nb->name != b->name) {
        // fallback to straight
        props.back().second = "north_south";
        ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
        nb = gen::blockByState(ns);
        if (!nb || nb->name != b->name) return;
    }
    world_.setBlock(x,y,z, ns);
}

void RedstoneEngine::handleDoor(std::int32_t x, std::int32_t y, std::int32_t z) {
    // handle both halves; only process lower half to avoid double
    std::uint16_t st = world_.getBlock(x,y,z);
    const gen::BlockDef* b = gen::blockByState(st);
    if(!b || std::string(b->name).find("_door")==std::string::npos) return;
    std::string half;
    for(auto& [k,v] : gen::propsOf(st)) if(k=="half") half=std::string(v);
    int lx = x, ly = y, lz = z;
    if(half=="upper"){ ly = y-1; st = world_.getBlock(lx,ly,lz); b = gen::blockByState(st); if(!b) return; }
    // verify lower is door
    std::uint16_t lowerSt = world_.getBlock(lx,ly,lz);
    std::uint16_t upperSt = world_.getBlock(lx,ly+1,lz);
    const gen::BlockDef* lb = gen::blockByState(lowerSt);
    const gen::BlockDef* ub = gen::blockByState(upperSt);
    if(!lb || std::string(lb->name).find("_door")==std::string::npos) return;
    if(!ub || std::string(ub->name).find("_door")==std::string::npos) return;
    bool curPowered=false;
    std::string facing, hinge;
    for(auto& [k,v]: gen::propsOf(lowerSt)){ if(k=="powered") curPowered=(v=="true"); if(k=="facing") facing=std::string(v); if(k=="hinge") hinge=std::string(v); }
    bool powered = isPoweredHere(lx,ly,lz) || isPoweredHere(lx,ly+1,lz);
    // QC for doors? also check above
    if(!powered) powered = isPoweredHere(lx,ly+1,lz);
    if(curPowered==powered) return;
    // update both halves: powered changes, open follows powered (vanilla: powered doors open)
    std::string openStr = powered ? "true" : "false";
    std::string poweredStr = powered ? "true" : "false";
    // lower
    {
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for(auto& [k,v]: gen::propsOf(lowerSt)) if(k!="open" && k!="powered") props.emplace_back(k,v);
        props.emplace_back("open", openStr);
        props.emplace_back("powered", poweredStr);
        uint16_t ns = static_cast<uint16_t>(gen::stateWithProps(*lb, props));
        world_.setBlock(lx,ly,lz, ns);
    }
    {
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for(auto& [k,v]: gen::propsOf(upperSt)) if(k!="open" && k!="powered") props.emplace_back(k,v);
        props.emplace_back("open", openStr);
        props.emplace_back("powered", poweredStr);
        uint16_t ns = static_cast<uint16_t>(gen::stateWithProps(*ub, props));
        world_.setBlock(lx,ly+1,lz, ns);
    }
}

bool RedstoneEngine::onInteract(std::int32_t x, std::int32_t y,
                                std::int32_t z, std::int64_t now) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    bool ownsProcessing = false;
    {
        std::lock_guard<std::mutex> notificationLock(notificationMutex_);
        ownsProcessing = !processingBlockChanges_;
        if (ownsProcessing) processingBlockChanges_ = true;
    }
    const bool handled = [&]() -> bool {
    const std::uint16_t st = world_.getBlock(x, y, z);
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return false;

    if (b->name == "minecraft:lever") {
        bool on = false;
        for (auto& [k, v] : gen::propsOf(st))
            if (k == "powered") on = v == "true";
        const std::uint16_t ns = static_cast<std::uint16_t>(
            gen::stateWithProps(*b, {{"powered", on ? "false" : "true"}}));
        world_.setBlock(x, y, z, ns);
        recomputeAround(x, y, z);
        return true;
    }
    if (b->name.find("_button") != std::string::npos) {
        const std::uint16_t ns = static_cast<std::uint16_t>(
            gen::stateWithProps(*b, {{"powered", "true"}}));
        world_.setBlock(x, y, z, ns);
        queue_.push({x, y, z, now + 30});                // auto-release
        recomputeAround(x, y, z);
        return true;
    }
    if (b->name.find("comparator") != std::string::npos) {
        std::string curMode="compare";
        for (auto& [k,v] : gen::propsOf(st)) if (k=="mode") curMode=std::string(v);
        std::string newMode = (curMode=="compare"?"subtract":"compare");
        // preserve other props
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for (auto& [k,v] : gen::propsOf(st)) if (k!="mode") props.emplace_back(k,v);
        props.emplace_back("mode", newMode);
        const std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
        world_.setBlock(x, y, z, ns);
        // recompute output after mode switch
        handleComparator(x,y,z);
        recomputeAround(x, y, z);
        return true;
    }
    return false;
    }();
    if (ownsProcessing) {
        drainBlockChanges();
        std::lock_guard<std::mutex> notificationLock(notificationMutex_);
        processingBlockChanges_ = false;
    }
    return handled;
}

void RedstoneEngine::setBlockAndBroadcast(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    world_.setBlock(x,y,z,state);
    if (broadcastFn_) broadcastFn_(x,y,z,state);
}

std::size_t RedstoneEngine::pendingCount() const {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    std::lock_guard<std::mutex> notificationLock(notificationMutex_);
    return pistonQueue_.size() + pendingPistonCommits_.size() + queue_.size()
         + pendingRepeater_.size() + observerPulseEnd_.size()
         + pendingBlockChanges_.size();
}

bool RedstoneEngine::isQuasiPowered(std::int32_t x, std::int32_t y,
                                    std::int32_t z) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    return isPoweredHereImpl(x, y, z) || isPoweredHereImpl(x, y + 1, z);
}

void RedstoneEngine::setBlockEntityStore(BlockEntityStore* s) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    beStore_ = s;
}

void RedstoneEngine::setTickRef(std::int64_t* t) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    tickRef_ = t;
    atomicTickRef_ = nullptr;
}

void RedstoneEngine::setTickRef(const std::atomic<std::int64_t>* t) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    atomicTickRef_ = t;
    tickRef_ = nullptr;
}

void RedstoneEngine::setBlockTickScheduler(BlockTickScheduler* bts) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    blockTicks_ = bts;
}

void RedstoneEngine::setGameServer(void* srv) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    gameServer_ = srv;
}

void RedstoneEngine::setBroadcastFn(std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t)> fn) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    broadcastFn_ = std::move(fn);
}

void RedstoneEngine::tick(std::int64_t now) {
    std::lock_guard<std::recursive_mutex> operationLock(operationMutex_);
    {
        std::lock_guard<std::mutex> notificationLock(notificationMutex_);
        processingBlockChanges_ = true;
    }
    // Process callback notifications on the same serialized operation as the
    // delayed queues.  World callbacks generated below only append to this
    // set and are drained after the current World mutation returns.
    drainBlockChanges();
    processPistonQueue(now);
    processPendingPistonCommits(now, std::nullopt,
                                std::numeric_limits<std::size_t>::max(), true);
    int budget = 4096;
    while (!queue_.empty() && queue_.top().dueTick <= now && --budget > 0) {
        const RedstoneTick t = queue_.top();
        queue_.pop();
        if (!world_.isChunkInSimulationDistance(t.x >> 4, t.z >> 4) && !world_.isPositionInSimulationDistance(t.x, t.z)) continue;
        const std::uint16_t st = world_.getBlock(t.x, t.y, t.z);
        const Comp c = classify(st);
        if (c == Comp::ButtonOn) {                       // release pulse
            const gen::BlockDef* b = gen::blockByState(st);
            if (!b) continue;
            const std::uint16_t ns = static_cast<std::uint16_t>(
                gen::stateWithProps(*b, {{"powered", "false"}}));
            world_.setBlock(t.x, t.y, t.z, ns);
            recomputeAround(t.x, t.y, t.z);
        } else if (c == Comp::Repeater || [&]{
            // check pending repeater
            std::int64_t key = posKey(t.x,t.y,t.z);
            auto it = pendingRepeater_.find(key);
            return it != pendingRepeater_.end() && it->second <= now;
        }()) {
            std::int64_t key = posKey(t.x,t.y,t.z);
            auto it = pendingRepeater_.find(key);
            if (it != pendingRepeater_.end() && it->second > now) continue;
            if (it != pendingRepeater_.end()) pendingRepeater_.erase(it);
            const gen::BlockDef* b = gen::blockByState(st);
            if (!b) continue;
            // recompute desired powered
            std::string facing="north";
            bool curPowered=false;
            for (auto& [k,v] : gen::propsOf(st)) {
                if (k=="facing") facing=std::string(v);
                if (k=="powered") curPowered=(v=="true");
            }
            int bx=t.x, by=t.y, bz=t.z;
            if (facing=="north") bz+=1;
            else if (facing=="south") bz-=1;
            else if (facing=="west") bx+=1;
            else if (facing=="east") bx-=1;
            bool wantPowered = isPoweredHere(bx,by,bz);
            if (wantPowered != curPowered) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for (auto& [k,v] : gen::propsOf(st)) if (k!="powered") props.emplace_back(k,v);
                props.emplace_back("powered", wantPowered?"true":"false");
                std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
                world_.setBlock(t.x,t.y,t.z, ns);
                recomputeAround(t.x,t.y,t.z);
            }
        } else if (c == Comp::Observer) {
            std::int64_t key = posKey(t.x,t.y,t.z);
            auto it = observerPulseEnd_.find(key);
            if (it != observerPulseEnd_.end() && it->second <= now) {
                observerPulseEnd_.erase(it);
                const gen::BlockDef* b = gen::blockByState(st);
                if (!b) continue;
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for (auto& [k,v] : gen::propsOf(st)) if (k!="powered") props.emplace_back(k,v);
                props.emplace_back("powered", "false");
                std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
                world_.setBlock(t.x,t.y,t.z, ns);
                recomputeAround(t.x,t.y,t.z);
            } else {
                recomputeAround(t.x, t.y, t.z);
            }
        } else {
            recomputeAround(t.x, t.y, t.z);              // repeater delay etc.
        }
    }
    // also expire pending repeaters without queue? Already handled
    drainBlockChanges();
    {
        std::lock_guard<std::mutex> notificationLock(notificationMutex_);
        processingBlockChanges_ = false;
    }
}

} // namespace cppfm
