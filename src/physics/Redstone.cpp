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
    }
    if (slots==0) return 0;
    // spec: 15 * filledRatio where filledRatio = filled/slots
    double ratio = double(filled)/double(slots);
    // also consider average fill for partial stacks? Use fillSum for more accurate but keep spec simple
    // Blend: use max of ratio and fillSum/slots to handle partially filled case
    double avg = fillSum / double(slots);
    ratio = std::max(ratio, avg);
    int sig = static_cast<int>(std::floor(ratio * 15.0));
    if (filled>0 && sig==0) sig=1;
    if (sig>15) sig=15;
    if (sig<0) sig=0;
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
        int out = analogOutputAt(bx,by,bz);
        // subtract mode: subtract side power
        if (mode=="subtract") {
            int sx1=x, sz1=z, sx2=x, sz2=z;
            if (facing=="north" || facing=="south") { sx1+=1; sx2-=1; }
            else { sz1+=1; sz2-=1; }
            int side1 = 0, side2 = 0;
            // get max emission at side positions
            for (int d=0; d<6; ++d) {
                // we approximate side power by checking wire power or source at side
                // Check block at sx1
            }
            // Simplified: check wire power at side positions
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
            side1 = sidePower(sx1, sz1);
            side2 = sidePower(sx2, sz2);
            int side = std::max(side1, side2);
            out = std::max(0, out - side);
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
    pistonQueue_.push_back({x,y,z, 0.f, wantExtend, now+2, face});
    // For immediate feedback in non-tick contexts (e.g., direct setBlock), also apply quickly if no tick loop yet
    // But vanilla has 2-tick delay, so we keep scheduled only; processPistonQueue will handle on next tick()
    // Validate piston push feasibility now to avoid scheduling impossible moves
    if (wantExtend) {
        int dx=0,dy=0,dz=0;
        if (facing=="north") dz=-1; else if (facing=="south") dz=1; else if (facing=="west") dx=-1; else if (facing=="east") dx=1; else if (facing=="up") dy=1; else if (facing=="down") dy=-1;
        std::int32_t hx=x+dx, hy=y+dy, hz=z+dz;
        std::uint16_t target = world_.getBlock(hx,hy,hz);
        if (target!=0) {
            const gen::BlockDef* tb = gen::blockByState(target);
            if (!tb || tb->hardness <0 || tb->name=="minecraft:obsidian" || tb->name=="minecraft:bedrock") {
                // impossible, cancel scheduling
                pistonQueue_.pop_back();
                return;
            }
            bool ok=true;
            for (int i=1;i<=12;++i) {
                std::int32_t px=x+dx*i, py=y+dy*i, pz=z+dz*i;
                std::uint16_t ps = world_.getBlock(px,py,pz);
                if (ps==0) break;
                const gen::BlockDef* pb = gen::blockByState(ps);
                if (!pb || pb->hardness <0) { ok=false; break; }
                if (i==12) { ok=false; break; }
            }
            if (!ok) { pistonQueue_.pop_back(); return; }
        }
    }
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
    // delegate to handlePiston logic by temporarily setting powered expectation
    // Reuse handlePiston by toggling extended state via direct set
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
        // push blocks up to 12 like vanilla
        std::uint16_t target = world_.getBlock(hx,hy,hz);
        if (target!=0) {
            for (int i=12;i>=1;--i) {
                std::int32_t px=x+dx*i, py=y+dy*i, pz=z+dz*i;
                std::int32_t prevx=x+dx*(i-1), prevy=y+dy*(i-1), prevz=z+dz*(i-1);
                if (i-1==0) continue;
                std::uint16_t prevSt = world_.getBlock(prevx, prevy, prevz);
                if (prevSt!=0) world_.setBlock(px,py,pz, prevSt);
            }
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
    } else {
        std::uint16_t head = world_.getBlock(hx,hy,hz);
        const gen::BlockDef* hb = gen::blockByState(head);
        if (hb && hb->name=="minecraft:piston_head") {
            world_.setBlock(hx,hy,hz, 0);
            if (c==Comp::StickyPiston) {
                std::int32_t px=hx+dx, py=hy+dy, pz=hz+dz;
                std::uint16_t ps = world_.getBlock(px,py,pz);
                if (ps!=0) {
                    const gen::BlockDef* pb = gen::blockByState(ps);
                    if (pb && pb->hardness>=0 && std::string(pb->name)!="minecraft:obsidian") {
                        world_.setBlock(px,py,pz, 0);
                        world_.setBlock(hx,hy,hz, ps);
                    }
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
