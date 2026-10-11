// Redstone wire-component discovery and power propagation.
#include "Redstone.hpp"
#include "../generated/BlockStates.hpp"
#include <algorithm>
#include <cstdlib>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cppfm {

void RedstoneEngine::setPoweredAt(std::int32_t x, std::int32_t y,
                                  std::int32_t z, std::uint8_t level) {
    const std::uint16_t st = world_.getBlock(x, y, z);
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b || b->name != "minecraft:redstone_wire") return;
    std::vector<std::pair<std::string_view, std::string_view>> props;
    for (auto& [k, v] : gen::propsOf(st))
        if (k != "power") props.emplace_back(k, v);
    const std::string power = std::to_string(level);
    props.emplace_back("power", power);
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
        // No minecart check, just propagate power inversion? Keep as is
    }
}

void RedstoneEngine::updateWireNetwork(std::int32_t sx, std::int32_t sy,
                                       std::int32_t sz) {
    struct Node { std::int32_t x, y, z; };
    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};

    // First collect the complete connected component.  The previous
    // implementation only visited wires reached from a powered seed, so a
    // wire that lost its last source could never be visited and retained its
    // old power forever.
    std::vector<Node> component;
    std::unordered_map<std::int64_t, std::size_t> componentIndex;
    std::queue<Node> pending;
    auto enqueueWire = [&](std::int32_t x, std::int32_t y,
                           std::int32_t z) {
        if (classify(world_.getBlock(x, y, z)) != Comp::Wire) return;
        const std::int64_t key = posKey(x, y, z);
        if (componentIndex.emplace(key, component.size()).second) {
            component.push_back({x, y, z});
            pending.push({x, y, z});
        }
    };

    enqueueWire(sx, sy, sz);
    if (component.empty()) {
        for (int d = 0; d < 6; ++d)
            enqueueWire(sx + DX[d], sy + DY[d], sz + DZ[d]);
    }
    while (!pending.empty()) {
        const Node n = pending.front();
        pending.pop();
        for (int d = 0; d < 6; ++d)
            enqueueWire(n.x + DX[d], n.y + DY[d], n.z + DZ[d]);
    }
    if (component.empty()) return;

    // Seed a max-heap with every non-wire emitter touching the component, then
    // relax wire neighbours.  This is a multi-source shortest-path update:
    // each edge costs one redstone level and every wire is assigned the
    // strongest source reachable through the component.  Recomputing from
    // zero is what makes power removal and source replacement symmetric with
    // power insertion.
    std::vector<int> levels(component.size(), 0);
    std::priority_queue<std::pair<int, std::size_t>> frontier;
    for (std::size_t i = 0; i < component.size(); ++i) {
        const Node n = component[i];
        for (int d = 0; d < 6; ++d) {
            const std::int32_t nx = n.x + DX[d];
            const std::int32_t ny = n.y + DY[d];
            const std::int32_t nz = n.z + DZ[d];
            const std::uint16_t state = world_.getBlock(nx, ny, nz);
            if (classify(state) == Comp::Wire) continue;
            const int emitted = std::clamp(emissionLevel(state, nx, ny, nz), 0, 15);
            if (emitted > levels[i]) {
                levels[i] = emitted;
                frontier.push({emitted, i});
            }
        }
    }

    while (!frontier.empty()) {
        const auto [power, index] = frontier.top();
        frontier.pop();
        if (power != levels[index] || power <= 1) continue;
        const Node n = component[index];
        for (int d = 0; d < 6; ++d) {
            const std::int64_t key = posKey(n.x + DX[d], n.y + DY[d], n.z + DZ[d]);
            const auto it = componentIndex.find(key);
            if (it == componentIndex.end()) continue;
            const std::size_t neighbour = it->second;
            const int candidate = power - 1;
            if (candidate > levels[neighbour]) {
                levels[neighbour] = candidate;
                frontier.push({candidate, neighbour});
            }
        }
    }

    for (std::size_t i = 0; i < component.size(); ++i) {
        const Node n = component[i];
        setPoweredAt(n.x, n.y, n.z, static_cast<std::uint8_t>(levels[i]));
    }
    for (const Node n : component) {
        for (int d = 0; d < 6; ++d)
            reactToPower(n.x + DX[d], n.y + DY[d], n.z + DZ[d]);
        reactToPower(n.x, n.y, n.z);
    }
}

} // namespace cppfm
