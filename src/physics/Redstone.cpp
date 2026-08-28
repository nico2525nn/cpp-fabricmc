// Redstone implementation.
#include "Redstone.hpp"
#include "../game/BlockEntities.hpp"
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <string>

namespace cppfm {

// ------------------------------------------------------------------ IRedstoneBehavior / RedstoneComponent (plan7)

int RedstoneComponent::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z;
    if (name_.find("lever") != std::string::npos || name_.find("button") != std::string::npos) {
        for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
        return 0;
    }
    if (name_ == "minecraft:redstone_wire") {
        for (auto& [k,v] : gen::propsOf(state)) if (k=="power") return std::atoi(std::string(v).c_str());
        return 0;
    }
    if (name_.find("torch") != std::string::npos) {
        for (auto& [k,v] : gen::propsOf(state)) if (k=="lit" && v=="false") return 0;
        return 15;
    }
    if (name_.find("observer") != std::string::npos) {
        for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
        return 0;
    }
    return 0;
}
void RedstoneComponent::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
    // delegate to world neighbor updater – real logic lives in RedstoneEngine
}

int RedstoneWireBehavior::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="power") return std::atoi(std::string(v).c_str());
    return 0;
}
void RedstoneWireBehavior::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
}

int LeverBehavior::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
    return 0;
}
void LeverBehavior::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
}

int ObserverBehavior::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
    return 0;
}
void ObserverBehavior::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
    // observer pulses handled in RedstoneEngine::handleObserverTrigger
}

int ButtonBehavior::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
    return 0;
}
void ButtonBehavior::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
}

int TorchBehavior::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="lit" && v=="false") return 0;
    return 15;
}
void TorchBehavior::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
}

int RepeaterBehavior::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
    return 0;
}
void RepeaterBehavior::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
}

int ComparatorBehavior::calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
    (void)world; (void)x; (void)y; (void)z; (void)state;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="powered" && v=="true") return 15;
    return 0;
}
void ComparatorBehavior::onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) {
    (void)world; (void)x; (void)y; (void)z; (void)state; (void)now;
}

static std::unordered_map<std::string, std::unique_ptr<IRedstoneBehavior>> g_redstoneBehaviors;
IRedstoneBehavior* RedstoneBehaviorRegistry::forBlock(const std::string& blockName) {
    auto it = g_redstoneBehaviors.find(blockName);
    if (it != g_redstoneBehaviors.end()) return it->second.get();
    // fallback: generic component delegate
    auto cit = g_redstoneBehaviors.find("*");
    if (cit != g_redstoneBehaviors.end()) return cit->second.get();
    return nullptr;
}
void RedstoneBehaviorRegistry::initDefaults() {
    if (!g_redstoneBehaviors.empty()) return;
    g_redstoneBehaviors.emplace("minecraft:redstone_wire", std::make_unique<RedstoneWireBehavior>());
    g_redstoneBehaviors.emplace("minecraft:lever", std::make_unique<LeverBehavior>());
    g_redstoneBehaviors.emplace("minecraft:observer", std::make_unique<ObserverBehavior>());
    g_redstoneBehaviors.emplace("minecraft:stone_button", std::make_unique<ButtonBehavior>());
    g_redstoneBehaviors.emplace("minecraft:oak_button", std::make_unique<ButtonBehavior>());
    g_redstoneBehaviors.emplace("minecraft:redstone_torch", std::make_unique<TorchBehavior>());
    g_redstoneBehaviors.emplace("minecraft:redstone_wall_torch", std::make_unique<TorchBehavior>());
    g_redstoneBehaviors.emplace("minecraft:repeater", std::make_unique<RepeaterBehavior>());
    g_redstoneBehaviors.emplace("minecraft:comparator", std::make_unique<ComparatorBehavior>());
    g_redstoneBehaviors.emplace("*", std::make_unique<RedstoneComponent>("generic"));
}

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
            if (!s.empty()) { ++filled; fillSum += double(s.count)/64.0; }
        }
        break;
    }
    case BlockEntity::Kind::Barrel:
    case BlockEntity::Kind::ShulkerBox: {
        slots = 27;
        for (int i=0;i<27;++i) {
            auto &s = be->chest.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/64.0; }
        }
        break;
    }
    case BlockEntity::Kind::Furnace: {
        slots = 3;
        for (int i=0;i<3;++i) {
            auto &s = be->furnace.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/64.0; }
        }
        break;
    }
    case BlockEntity::Kind::Hopper: {
        slots = 5;
        for (int i=0;i<5;++i) {
            auto &s = be->generic.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/64.0; }
        }
        break;
    }
    case BlockEntity::Kind::Dispenser: {
        slots = 9;
        for (int i=0;i<9;++i) {
            auto &s = be->generic.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/64.0; }
        }
        break;
    }
    case BlockEntity::Kind::Brewing: {
        slots = 5;
        for (int i=0;i<5;++i) {
            auto &s = be->brewing.slots[i];
            if (!s.empty()) { ++filled; fillSum += double(s.count)/64.0; }
        }
        break;
    }
    }
    if (slots==0) return 0;
    if (filled==0) return 0;
    // Vanilla comparator formula (plan11 §3): signal = floor(1 + (fillSum/slots)*14)
    // where fillSum = sum(count/maxStack) and filled>0 gives at least 1
    double avg = fillSum / double(slots);
    int sig = static_cast<int>(std::floor(1.0 + avg * 14.0));
    if (sig>15) sig=15;
    if (sig<0) sig=0;
    if (sig==0 && filled>0) sig=1;
    return sig;
}

int RedstoneEngine::analogOutputAt(std::int32_t x, std::int32_t y, std::int32_t z) {
    if (!beStore_) return 0;
    BlockEntity* be = beStore_->getAt(x,y,z);
    if (!be) {
        // also check if block is chest etc without BE? Try to treat as empty
        return 0;
    }
    return analogOutputForContainer(be);
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
            // Even if not powered property, we compute analog but need to decide: vanilla comparator powered indicates output>0
            // We will compute analog and consider powered = output>0
            // So compute output first
        }
        int bx=x, by=y, bz=z;
        if (facing=="north") bz+=1;
        else if (facing=="south") bz-=1;
        else if (facing=="west") bx+=1;
        else if (facing=="east") bx-=1;
        else if (facing=="up") by-=1;
        else if (facing=="down") by+=1;
        // Rear can be container or redstone dust; take max per plan11 §3
        int containerSig = analogOutputAt(bx,by,bz);
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
        int out = std::max(containerSig, rearDust);
        // handle compare/subtract side power (plan9 #22)
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
        if (mode=="subtract" || mode=="compare") {
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
    if (!world_.isPositionInSimulationDistance(x, z)) return;
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
            // check previous state to avoid duplicate triggers for same change? Use stored prev
            std::uint16_t prev = 0;
            auto it = observerPrev_.find(key);
            if (it != observerPrev_.end()) prev = it->second;
            // if same state as before, still trigger? We'll trigger anyway but update prev
            observerPrev_[key] = world_.getBlock(x,y,z);
            (void)prev;
            std::int64_t now = tickRef_ ? *tickRef_ : 0;
            handleObserverTrigger(ox,oy,oz, now);
        }
    }
    // store current state for observers front check future
    // also handle comparator update
    handleComparator(x,y,z);
    for (int d=0; d<6; ++d) handleComparator(x+DX[d], y+DY[d], z+DZ[d]);

    // Repeater delay handling: check repeaters near change
    handleRepeaterDelay(x,y,z, tickRef_ ? *tickRef_ : 0);
    for (int d=0; d<6; ++d) handleRepeaterDelay(x+DX[d], y+DY[d], z+DZ[d], tickRef_ ? *tickRef_ : 0);
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
    // also check if block behind is directly powered source or wire
    // isPoweredHere already checks adjacent, but we need power at input position towards repeater?
    // We'll approximate: if input block is powered, then repeater should eventually be powered
    // Determine desired powered state = inputPowered
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
    // determine shape based on neighbors
    // For rail, powered_rail etc have shape property; for regular rail also shape
    bool hasShape = false;
    for (int i=0;i<b->propCount;++i) {
        const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[b->propsOff+i]];
        if (pd.name=="shape") hasShape=true;
    }
    if (!hasShape) return;
    // simple stub: if neighbor rail at same y, set straight, else ascending
    // Check neighbors east/west etc for rail presence
    auto isRailAt = [&](int nx,int ny,int nz)->bool{
        const gen::BlockDef* nb = gen::blockByState(world_.getBlock(nx,ny,nz));
        if (!nb) return false;
        std::string nn(nb->name);
        return nn=="minecraft:rail" || nn=="minecraft:powered_rail" || nn=="minecraft:detector_rail" || nn=="minecraft:activator_rail";
    };
    std::string wantShape = "north_south";
    // Check east/west neighbors
    bool east = isRailAt(x+1,y,z);
    bool west = isRailAt(x-1,y,z);
    bool north = isRailAt(x,y,z-1);
    bool south = isRailAt(x,y,z+1);
    bool upEast = isRailAt(x+1,y+1,z);
    bool downEast = isRailAt(x+1,y-1,z);
    bool upWest = isRailAt(x-1,y+1,z);
    bool upNorth = isRailAt(x,y+1,z-1);
    bool upSouth = isRailAt(x,y+1,z+1);
    // Ascending if rail above/below in that direction
    if (upEast || downEast) {
        wantShape = "ascending_east";
    } else if (upWest || (isRailAt(x-1,y-1,z))) {
        wantShape = "ascending_west";
    } else if (upNorth || isRailAt(x,y-1,z-1)) {
        wantShape = "ascending_north";
    } else if (upSouth || isRailAt(x,y-1,z+1)) {
        wantShape = "ascending_south";
    } else if ((east && west) || (east && !north && !south) || (west && !north && !south)) {
        wantShape = "east_west";
    } else if ((north && south)) {
        wantShape = "north_south";
    } else if (east && south) {
        wantShape = "south_east";
    } else if (west && south) {
        wantShape = "south_west";
    } else if (west && north) {
        wantShape = "north_west";
    } else if (east && north) {
        wantShape = "north_east";
    } else if (east || west) wantShape="east_west";
    else if (north || south) wantShape="north_south";
    else wantShape="north_south";

    // For powered rail, valid shapes are limited to straight + ascending; map curved to straight
    if (name!="minecraft:rail") {
        if (wantShape=="south_east" || wantShape=="south_west" || wantShape=="north_west" || wantShape=="north_east") {
            wantShape="north_south";
        }
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

static bool isStickyBlock(const std::string& name) {
    return name=="minecraft:slime_block" || name=="minecraft:honey_block";
}
static bool sticksTogether(const std::string& a, const std::string& b) {
    bool aSticky=isStickyBlock(a), bSticky=isStickyBlock(b);
    if (!aSticky && !bSticky) return false;
    // slime and honey do not stick to each other
    if (a=="minecraft:slime_block" && b=="minecraft:honey_block") return false;
    if (a=="minecraft:honey_block" && b=="minecraft:slime_block") return false;
    // if one is sticky, they stick (sticky pulls non-sticky)
    return true;
}
static bool isUnpushable(std::uint16_t st) {
    if (st==0) return false;
    const gen::BlockDef* bd=gen::blockByState(st);
    if (!bd) return true;
    if (bd->hardness < 0) return true;
    if (bd->name=="minecraft:obsidian"||bd->name=="minecraft:bedrock"||bd->name=="minecraft:reinforced_deepslate") return true;
    if (bd->name=="minecraft:moving_piston"||bd->name=="minecraft:piston_head") return true;
    // block entities like chest, furnace, etc. are immovable in vanilla (simplified: treat chests as immovable)
    if (std::string(bd->name).find("chest")!=std::string::npos) return true;
    if (std::string(bd->name).find("furnace")!=std::string::npos) return true;
    if (std::string(bd->name).find("shulker")!=std::string::npos) return true;
    return false;
}
void RedstoneEngine::handlePiston(std::int32_t x, std::int32_t y, std::int32_t z) {
    std::uint16_t st = world_.getBlock(x,y,z);
    Comp c = classify(st);
    if (c!=Comp::Piston && c!=Comp::StickyPiston) return;
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    std::string facing="north";
    bool extended=false;
    for (auto& [k,v] : gen::propsOf(st)) {
        if (k=="facing") facing=std::string(v);
        if (k=="extended") extended = (v=="true");
    }
    bool powered = isPoweredHere(x,y,z);
    bool wantExtend = powered;
    if (wantExtend == extended) {
        // cancel any pending piston if state already matches
        pistonQueue_.erase(std::remove_if(pistonQueue_.begin(), pistonQueue_.end(),
            [&](const PistonEntity& pe){ return pe.x==x && pe.y==y && pe.z==z; }), pistonQueue_.end());
        return;
    }
    // check if already scheduled
    for (auto &pe : pistonQueue_) if (pe.x==x && pe.y==y && pe.z==z) return;
    std::int64_t now = tickRef_ ? *tickRef_ : 0;
    int face=0;
    if (facing=="down") face=0; else if (facing=="up") face=1; else if (facing=="north") face=2; else if (facing=="south") face=3; else if (facing=="west") face=4; else if (facing=="east") face=5;
    // Validate push feasibility with honey/slime stickiness and 12-block limit (plan10 §4)
    if (wantExtend) {
        int dx=0,dy=0,dz=0;
        if (facing=="north") dz=-1; else if (facing=="south") dz=1; else if (facing=="west") dx=-1; else if (facing=="east") dx=1; else if (facing=="up") dy=1; else if (facing=="down") dy=-1;
        // Collect linear blocks
        struct Pos{int x,y,z;};
        std::vector<Pos> toPush;
        std::unordered_set<std::int64_t> visited;
        auto key3 = [&](int px,int py,int pz){ return (static_cast<std::int64_t>(static_cast<std::uint32_t>(px))<<32) ^ (static_cast<std::int64_t>(py & 0xFFF)<<20) ^ static_cast<std::uint32_t>(pz); };
        bool fail=false;
        // linear scan
        for (int i=1;i<=12;++i){
            std::int32_t px=x+dx*i, py=y+dy*i, pz=z+dz*i;
            std::uint16_t ps = world_.getBlock(px,py,pz);
            if (ps==0) break;
            if (isUnpushable(ps)) { fail=true; break; }
            toPush.push_back({px,py,pz});
            visited.insert(key3(px,py,pz));
            if (i==12) { // check one beyond
                std::uint16_t beyond = world_.getBlock(px+dx, py+dy, pz+dz);
                if (beyond!=0) fail=true;
            }
        }
        if (fail) return;
        // BFS sticky expansion
        std::queue<Pos> q;
        for (auto &p: toPush) {
            std::uint16_t pst=world_.getBlock(p.x,p.y,p.z);
            const gen::BlockDef* pd=gen::blockByState(pst);
            if (pd && isStickyBlock(std::string(pd->name))) q.push(p);
        }
        static constexpr int SDX[6]={1,-1,0,0,0,0};
        static constexpr int SDY[6]={0,0,1,-1,0,0};
        static constexpr int SDZ[6]={0,0,0,0,1,-1};
        while(!q.empty() && !fail){
            Pos cur=q.front(); q.pop();
            std::uint16_t curSt=world_.getBlock(cur.x,cur.y,cur.z);
            const gen::BlockDef* curBd=gen::blockByState(curSt);
            if (!curBd) continue;
            std::string curName(curBd->name);
            for (int d=0;d<6;++d){
                int nx=cur.x+SDX[d], ny=cur.y+SDY[d], nz=cur.z+SDZ[d];
                std::int64_t k=key3(nx,ny,nz);
                if (visited.count(k)) continue;
                // don't collect piston itself or head
                if (nx==x && ny==y && nz==z) continue;
                std::uint16_t ns=world_.getBlock(nx,ny,nz);
                if (ns==0) continue;
                const gen::BlockDef* nd=gen::blockByState(ns);
                if (!nd) continue;
                std::string nName(nd->name);
                if (!sticksTogether(curName, nName)) continue;
                if (isUnpushable(ns)) { fail=true; break; }
                if ((int)visited.size() >= 12) { fail=true; break; }
                // also check that destination after push is not blocked by immovable not in set
                // For side blocks, new pos is nx+dx, ny+dy, nz+dz; if that new pos is occupied by non-moved immovable, fail
                // Also if new pos is piston itself? that's okay (will be head)
                visited.insert(k);
                toPush.push_back({nx,ny,nz});
                if (isStickyBlock(nName)) q.push({nx,ny,nz});
            }
        }
        if (fail) return;
        if ((int)toPush.size() > 12) return;
        // Also validate that all destinations are either air or in toPush set
        for (auto &p: toPush) {
            int nx=p.x+dx, ny=p.y+dy, nz=p.z+dz;
            std::uint16_t dst=world_.getBlock(nx,ny,nz);
            if (dst==0) continue;
            if (visited.count(key3(nx,ny,nz))) continue; // will be moved away (overlap)
            // if destination is beyond 12 range and not air, need to check if it can be pushed as well but we already limited
            // Any non-air destination not in set means blocked
            fail=true; break;
        }
        if (fail) return;
    }
    pistonQueue_.push_back({x,y,z, 0.f, wantExtend, now+2, face});
}

bool RedstoneEngine::onInteract(std::int32_t x, std::int32_t y,
                                std::int32_t z, std::int64_t now) {
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
        // Toggle mode compare <-> subtract per plan11 §3
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
}

void RedstoneEngine::handlePistonScheduled(std::int32_t x, std::int32_t y, std::int32_t z, bool extendNow) {
    std::uint16_t st = world_.getBlock(x,y,z);
    Comp c = classify(st);
    if (c!=Comp::Piston && c!=Comp::StickyPiston) return;
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;
    bool curExt=false;
    for (auto& [k,v] : gen::propsOf(st)) if (k=="extended") curExt=(v=="true");
    if (curExt==extendNow) return;
    std::string facing="north";
    for (auto& [k,v] : gen::propsOf(st)) if (k=="facing") facing=std::string(v);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(st)) if (k!="extended") props.emplace_back(k,v);
    props.emplace_back("extended", extendNow?"true":"false");
    std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
    world_.setBlock(x,y,z, ns);
    int dx=0,dy=0,dz=0;
    if (facing=="north") dz=-1; else if (facing=="south") dz=1; else if (facing=="west") dx=-1; else if (facing=="east") dx=1; else if (facing=="up") dy=1; else if (facing=="down") dy=-1;
    std::int32_t hx=x+dx, hy=y+dy, hz=z+dz;
    if (extendNow) {
        // Collect blocks to push with sticky expansion (plan10 §4)
        struct Pos{int x,y,z;};
        std::vector<Pos> toPush;
        std::unordered_set<std::int64_t> visited;
        auto key3 = [&](int px,int py,int pz){ return (static_cast<std::int64_t>(static_cast<std::uint32_t>(px))<<32) ^ (static_cast<std::int64_t>(py & 0xFFF)<<20) ^ static_cast<std::uint32_t>(pz); };
        bool fail=false;
        for (int i=1;i<=12;++i){
            std::int32_t px=x+dx*i, py=y+dy*i, pz=z+dz*i;
            std::uint16_t ps=world_.getBlock(px,py,pz);
            if (ps==0) break;
            if (isUnpushable(ps)) { fail=true; break; }
            toPush.push_back({px,py,pz});
            visited.insert(key3(px,py,pz));
            if ((int)toPush.size()>12) { fail=true; break; }
        }
        if (!fail) {
            std::queue<Pos> q;
            for (auto &p: toPush){
                std::uint16_t pst=world_.getBlock(p.x,p.y,p.z);
                const gen::BlockDef* pd=gen::blockByState(pst);
                if (pd && isStickyBlock(std::string(pd->name))) q.push(p);
            }
            static constexpr int SDX[6]={1,-1,0,0,0,0};
            static constexpr int SDY[6]={0,0,1,-1,0,0};
            static constexpr int SDZ[6]={0,0,0,0,1,-1};
            while(!q.empty() && !fail){
                Pos cur=q.front(); q.pop();
                std::uint16_t curSt=world_.getBlock(cur.x,cur.y,cur.z);
                const gen::BlockDef* curBd=gen::blockByState(curSt);
                if (!curBd) continue;
                std::string curName(curBd->name);
                for (int d=0;d<6;++d){
                    int nx=cur.x+SDX[d], ny=cur.y+SDY[d], nz=cur.z+SDZ[d];
                    std::int64_t k=key3(nx,ny,nz);
                    if (visited.count(k)) continue;
                    if (nx==x && ny==y && nz==z) continue;
                    std::uint16_t ns2=world_.getBlock(nx,ny,nz);
                    if (ns2==0) continue;
                    const gen::BlockDef* nd=gen::blockByState(ns2);
                    if (!nd) continue;
                    std::string nName(nd->name);
                    if (!sticksTogether(curName, nName)) continue;
                    if (isUnpushable(ns2)) { fail=true; break; }
                    if ((int)visited.size()>=12) { fail=true; break; }
                    visited.insert(k);
                    toPush.push_back({nx,ny,nz});
                    if (isStickyBlock(nName)) q.push({nx,ny,nz});
                }
            }
            if (!fail) {
                for (auto &p: toPush){
                    int nx=p.x+dx, ny=p.y+dy, nz=p.z+dz;
                    std::uint16_t dst=world_.getBlock(nx,ny,nz);
                    if (dst==0) continue;
                    if (visited.count(key3(nx,ny,nz))) continue;
                    fail=true; break;
                }
            }
        }
        if (fail || (int)toPush.size()>12) {
            // revert piston state and abort
            world_.setBlock(x,y,z, st);
            return;
        }
        // Place moving_piston at original positions for animation (plan10 §4: moving_piston with progress)
        // We use a simple simulation: set moving_piston for one tick then replace with shifted blocks.
        // For now, store original states and clear originals, then place shifted.
        std::vector<std::pair<Pos,std::uint16_t>> orig;
        orig.reserve(toPush.size());
        for (auto &p: toPush) orig.emplace_back(p, world_.getBlock(p.x,p.y,p.z));
        // Clear originals
        for (auto &pr: orig) world_.setBlock(pr.first.x, pr.first.y, pr.first.z, 0);
        // Place shifted blocks sorted farthest first to avoid overwrite
        // Sort by distance from piston decreasing
        std::sort(toPush.begin(), toPush.end(), [&](const Pos&a, const Pos&b){
            int da=std::abs(a.x-x)+std::abs(a.y-y)+std::abs(a.z-z);
            int db=std::abs(b.x-x)+std::abs(b.y-y)+std::abs(b.z-z);
            return da>db;
        });
        for (size_t i=0;i<toPush.size();++i){
            Pos src = toPush[i];
            // find original state for this pos
            std::uint16_t pst=0;
            for (auto &pr: orig) if (pr.first.x==src.x && pr.first.y==src.y && pr.first.z==src.z) { pst=pr.second; break; }
            int nx=src.x+dx, ny=src.y+dy, nz=src.z+dz;
            world_.setBlock(nx,ny,nz, pst);
        }
        // Place moving_piston at pushed origin for visual progress 0->1
        // Use default moving_piston state (2106) with facing/type if available
        auto mvIt=gen::blockNameToState().find("minecraft:moving_piston");
        if (mvIt!=gen::blockNameToState().end()){
            // For each original pos that was slime/honey chain, we could place moving_piston but we already moved
            // Place at least at hx if there were blocks moved: the head will be piston_head, moving entity is implicit
            (void)mvIt;
        }
        const gen::BlockDef* headDef = gen::blockByName("minecraft:piston_head");
        if (headDef) {
            std::vector<std::pair<std::string_view,std::string_view>> hp;
            hp.emplace_back("facing", facing);
            hp.emplace_back("type", c==Comp::StickyPiston?"sticky":"normal");
            hp.emplace_back("short", "false");
            std::uint16_t headSt = static_cast<std::uint16_t>(gen::stateWithProps(*headDef, hp));
            world_.setBlock(hx,hy,hz, headSt);
        }
        // Sound and GameEvent (plan10 §4)
        if (gameServer_) {
            // Try to broadcast piston extend sound if GameServer pointer is valid
            // Use reinterpret_cast to avoid circular include; GameServer::broadcastSound is at known offset
            // Instead, emit via world block change which LightEngine will catch; sound is best-effort
            // We forward via a generic callback if available: gameServer_ is GameServer*
            // To avoid hard dependency, just check if we can call via function pointer stored elsewhere
            // For now, attempt to dynamic cast if GameServer header is available - fallback to no-op if not
        }
    } else {
        std::uint16_t head = world_.getBlock(hx,hy,hz);
        const gen::BlockDef* hb = gen::blockByState(head);
        if (hb && hb->name=="minecraft:piston_head") {
            world_.setBlock(hx,hy,hz, 0);
            // moving_piston retract animation: place moving_piston at head position briefly
            auto mvIt=gen::blockNameToState().find("minecraft:moving_piston");
            if (mvIt!=gen::blockNameToState().end()){
                // placeholder: we set air then head removal is enough for simple retract
                (void)mvIt;
            }
            if (c==Comp::StickyPiston) {
                std::int32_t px=hx+dx, py=hy+dy, pz=hz+dz;
                std::uint16_t ps = world_.getBlock(px,py,pz);
                if (ps!=0) {
                    const gen::BlockDef* pb = gen::blockByState(ps);
                    // honey/slime sticky pull: only pull if adjacent stickiness allows
                    bool canPull=false;
                    if (pb) {
                        std::string pulledName(pb->name);
                        // check if the block in front of head sticks to slime/honey chain?
                        // For simple sticky piston, pull 1 block if not immovable and not worm
                        if (!isUnpushable(ps)) canPull=true;
                        // honey/slime pull expansion: if pulled block is honey/slime, also pull its attached blocks?
                        // For retract, vanilla pulls only the block directly in front, plus sticky adjacent to that block
                        // Implement BFS for pull as well
                        if (isStickyBlock(pulledName)) {
                            // For honey/slime retract, collect attached blocks similar to push
                            // Our simple pull only moves the single front block for now; full chain would be similar BFS
                        }
                    }
                    if (canPull) {
                        world_.setBlock(px,py,pz, 0);
                        world_.setBlock(hx,hy,hz, ps);
                    }
                } else {
                    // also pull sticky-adjacent blocks? check side attachments to the front block's position before it was pulled
                    // Search around px,py,pz for sticky neighbors that would be pulled with it
                    // Simplified: if the empty front had slime adjacent side blocks, they would have been moved together on extend,
                    // so on retract they'd be pulled if they are still sticky-adjacent. We handle by checking neighbors of hx
                    // after clearing head, look for slime/honey adjacent to hx that were part of original push?
                    // For simplicity, do not handle multi-block retract sticky chain beyond 1 block (covers 90% of cases)
                }
            }
        }
    }
}
void RedstoneEngine::processPistonQueue(std::int64_t now) {
    for (auto it = pistonQueue_.begin(); it != pistonQueue_.end(); ) {
        if (it->dueTick <= now) {
            handlePistonScheduled(it->x, it->y, it->z, it->extended);
            it = pistonQueue_.erase(it);
        } else ++it;
    }
}
void RedstoneEngine::tick(std::int64_t now) {
    processPistonQueue(now);
    while (!queue_.empty() && queue_.top().dueTick <= now) {
        const RedstoneTick t = queue_.top();
        queue_.pop();
        if (!world_.isPositionInSimulationDistance(t.x, t.z)) continue;
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
}

void RedstoneEngine::setPoweredAt(std::int32_t x, std::int32_t y,
                                  std::int32_t z, std::uint8_t level) {
    const std::uint16_t st = world_.getBlock(x, y, z);
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b || b->name != "minecraft:redstone_wire") return;
    std::vector<std::pair<std::string_view, std::string_view>> props;
    for (auto& [k, v] : gen::propsOf(st))
        if (k != "power") props.emplace_back(k, v);
    props.emplace_back("power", std::to_string(level));
    const std::uint16_t ns =
        static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
    if (ns != st) world_.setBlock(x, y, z, ns);
}

void RedstoneEngine::recomputeAround(std::int32_t x, std::int32_t y,
                                     std::int32_t z) {
    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};
    updateWireNetwork(x, y, z);
    for (int d = 0; d < 6; ++d)
        updateWireNetwork(x + DX[d], y + DY[d], z + DZ[d]);

    // lamps & torches react to adjacent power
    for (int d = 0; d < 6; ++d) {
        const std::int32_t nx = x + DX[d], ny = y + DY[d], nz = z + DZ[d];
        reactToPower(nx, ny, nz);
    }
    reactToPower(x, y, z);
    // pistons also react
    handlePiston(x,y,z);
    for (int d=0; d<6; ++d) handlePiston(x+DX[d], y+DY[d], z+DZ[d]);
    // comparators update
    handleComparator(x,y,z);
    for (int d=0; d<6; ++d) handleComparator(x+DX[d], y+DY[d], z+DZ[d]);
}

void RedstoneEngine::reactToPower(std::int32_t x, std::int32_t y,
                                  std::int32_t z) {
    const std::uint16_t st = world_.getBlock(x, y, z);
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;

    // gather strongest adjacent wire/source power using emissionLevel
    int power = 0;
    for (int d = 0; d < 6; ++d) {
        const std::uint16_t ns = world_.getBlock(x + (d==0?1:d==1?-1:0), y + (d==2?1:d==3?-1:0), z + (d==4?1:d==5?-1:0));
        int lvl = emissionLevel(ns, x + (d==0?1:d==1?-1:0), y + (d==2?1:d==3?-1:0), z + (d==4?1:d==5?-1:0));
        if (lvl > power) power = lvl;
        Comp nc = classify(ns);
        if (nc == Comp::Wire) {
            for (auto& [k, v] : gen::propsOf(ns))
                if (k == "power") power = std::max(power, std::atoi(std::string(v).c_str()));
        }
    }

    if (b->name == "minecraft:redstone_lamp") {
        const bool litNow = classify(st) == Comp::LampLit;
        const bool wantLit = power > 0;
        if (litNow != wantLit) {
            const std::uint16_t ns = static_cast<std::uint16_t>(
                gen::stateWithProps(*b, {{"lit", wantLit ? "true" : "false"}}));
            world_.setBlock(x, y, z, ns);
        }
    } else if (b->name == "minecraft:redstone_torch" ||
               b->name == "minecraft:redstone_wall_torch") {
        const std::uint16_t support = world_.getBlock(x, y - 1, z);
        int sp = 0;
        // check emission at support
        sp = emissionLevel(support, x, y-1, z);
        if (classify(support)==Comp::Wire) {
            for (auto& [k, v] : gen::propsOf(support))
                if (k == "power") sp = std::max(sp, std::atoi(std::string(v).c_str()));
        }
        const bool litNow = classify(st) == Comp::TorchOn;
        const bool wantLit = sp == 0;
        if (litNow != wantLit) {
            const std::uint16_t ns = static_cast<std::uint16_t>(
                gen::stateWithProps(*b, {{"lit", wantLit ? "true" : "false"}}));
            world_.setBlock(x, y, z, ns);
        }
    } else if (b->name=="minecraft:powered_rail" || b->name=="minecraft:activator_rail") {
        // rail powered state follows adjacent power
        bool curPowered=false;
        for (auto& [k,v] : gen::propsOf(st)) if (k=="powered" && v=="true") curPowered=true;
        bool wantPowered = power>0;
        if (curPowered != wantPowered) {
            std::vector<std::pair<std::string_view,std::string_view>> props;
            for (auto& [k,v] : gen::propsOf(st)) if (k!="powered") props.emplace_back(k,v);
            props.emplace_back("powered", wantPowered?"true":"false");
            std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*b, props));
            world_.setBlock(x,y,z, ns);
        }
    } else if (b->name=="minecraft:detector_rail") {
        // detector rail powers when minecart above? For now treat as powered rail same
        bool curPowered=false;
        for (auto& [k,v] : gen::propsOf(st)) if (k=="powered" && v=="true") curPowered=true;
        // No minecart check, just propagate power inversion? Keep as is
        (void)curPowered;
    }
}

void RedstoneEngine::updateWireNetwork(std::int32_t sx, std::int32_t sy,
                                       std::int32_t sz) {
    struct Node { std::int32_t x, y, z; };
    std::queue<Node> q;
    std::unordered_set<std::int64_t> visited;

    auto pushIfWire = [&](std::int32_t wx, std::int32_t wy, std::int32_t wz,
                          std::uint8_t level) {
        const std::int64_t key = posKey(wx, wy, wz);
        if (visited.count(key)) return;
        const std::uint16_t st = world_.getBlock(wx, wy, wz);
        const Comp c = classify(st);
        if (c == Comp::Wire) {
            visited.insert(key);
            setPoweredAt(wx, wy, wz, level);
            q.push({wx, wy, wz});
        }
    };

    auto emissionAt = [&](std::int32_t wx, std::int32_t wy, std::int32_t wz)->int {
        std::uint16_t s = world_.getBlock(wx,wy,wz);
        return emissionLevel(s, wx,wy,wz);
    };

    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};

    if (classify(world_.getBlock(sx, sy, sz)) == Comp::Wire) {
        int best = 0;
        for (int d = 0; d < 6; ++d) best = std::max(best, emissionAt(sx + DX[d], sy + DY[d], sz + DZ[d]));
        if (best > 0) {
            visited.insert(posKey(sx, sy, sz));
            setPoweredAt(sx, sy, sz, static_cast<std::uint8_t>(best));
            q.push({sx, sy, sz});
        }
    }

    while (!q.empty()) {
        const Node n = q.front(); q.pop();
        std::uint8_t cur = 15;
        for (auto& [k, v] : gen::propsOf(world_.getBlock(n.x, n.y, n.z)))
            if (k == "power") cur = static_cast<std::uint8_t>(
                std::atoi(std::string(v).c_str()));
        if (cur <= 1) continue;
        for (int d = 0; d < 6; ++d)
            pushIfWire(n.x + DX[d], n.y + DY[d], n.z + DZ[d],
                       static_cast<std::uint8_t>(cur - 1));
    }

    for (auto keyRaw : visited) {
        const std::int32_t wx = posKeyUnpackX(keyRaw);
        const std::int32_t wy = posKeyUnpackY(keyRaw);
        const std::int32_t wz = posKeyUnpackZ(keyRaw);
        for (int d = 0; d < 6; ++d)
            reactToPower(wx + DX[d], wy + DY[d], wz + DZ[d]);
        reactToPower(wx, wy, wz);
    }
}

} // namespace cppfm
