// Redstone implementation.
#include "Redstone.hpp"
#include "../game/BlockEntities.hpp"
#include <cstdlib>
#include <algorithm>
#include <cmath>

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
        // lever has open/facing/face; powered when open=true? Vanilla: lever
        // "powered" is stored via the `powered`... actually levers use
        // `open`? No — lever blockstate property is `powered`? It's `open`?
        // In vanilla it is `powered` for buttons and `open`?? Levers use
        // `powered`? — dataset: lever[face,facing,powered]. Use powered.
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
    return Comp::None;
}

int RedstoneEngine::maxEmissionFor(Comp c) {
    switch (c) {
    case Comp::LeverOn:
    case Comp::ButtonOn:
    case Comp::BlockSource:
    case Comp::TorchOn:
        return 15;
    default: return 0;
    }
}


bool RedstoneEngine::isPoweredHere(std::int32_t x, std::int32_t y,
                                   std::int32_t z) {
    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};
    for (int d = 0; d < 6; ++d) {
        const std::uint16_t ns =
            world_.getBlock(x + DX[d], y + DY[d], z + DZ[d]);
        const Comp nc = classify(ns);
        if (maxEmissionFor(nc) > 0) return true;
        if (nc == Comp::Wire) {
            for (auto& [k, v] : gen::propsOf(ns))
                if (k == "power" && std::atoi(std::string(v).c_str()) > 0)
                    return true;
        }
    }
    return false;
}

void RedstoneEngine::onBlockChanged(std::int32_t x, std::int32_t y,
                                    std::int32_t z) {
    recomputeAround(x, y, z);
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

void RedstoneEngine::tick(std::int64_t now) {
    while (!queue_.empty() && queue_.top().dueTick <= now) {
        const RedstoneTick t = queue_.top();
        queue_.pop();
        const std::uint16_t st = world_.getBlock(t.x, t.y, t.z);
        const Comp c = classify(st);
        if (c == Comp::ButtonOn) {                       // release pulse
            const gen::BlockDef* b = gen::blockByState(st);
            const std::uint16_t ns = static_cast<std::uint16_t>(
                gen::stateWithProps(*b, {{"powered", "false"}}));
            world_.setBlock(t.x, t.y, t.z, ns);
            recomputeAround(t.x, t.y, t.z);
        } else {
            recomputeAround(t.x, t.y, t.z);              // repeater delay etc.
        }
    }
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
}

void RedstoneEngine::reactToPower(std::int32_t x, std::int32_t y,
                                  std::int32_t z) {
    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};

    const std::uint16_t st = world_.getBlock(x, y, z);
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return;

    // gather strongest adjacent wire/source power
    int power = 0;
    for (int d = 0; d < 6; ++d) {
        const std::uint16_t ns = world_.getBlock(x + DX[d], y + DY[d],
                                                 z + DZ[d]);
        const Comp nc = classify(ns);
        if (maxEmissionFor(nc) > 0) power = 15;
        else if (nc == Comp::Wire) {
            for (auto& [k, v] : gen::propsOf(ns))
                if (k == "power") power = std::max(power,
                                                   std::atoi(std::string(v).c_str()));
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
        // torch inverts: off when the block BELOW (standing) / behind (wall)
        // receives power. Approximate with below-block check.
        const std::uint16_t support = world_.getBlock(x, y - 1, z);
        int sp = 0;
        const Comp sc = classify(support);
        if (maxEmissionFor(sc) > 0) sp = 15;
        else if (sc == Comp::Wire)
            for (auto& [k, v] : gen::propsOf(support))
                if (k == "power") sp = std::atoi(std::string(v).c_str());
        const bool litNow = classify(st) == Comp::TorchOn;
        const bool wantLit = sp == 0;
        if (litNow != wantLit) {
            const std::uint16_t ns = static_cast<std::uint16_t>(
                gen::stateWithProps(*b, {{"lit", wantLit ? "true" : "false"}}));
            world_.setBlock(x, y, z, ns);
        }
    }
}

void RedstoneEngine::updateWireNetwork(std::int32_t sx, std::int32_t sy,
                                       std::int32_t sz) {
    // BFS over wires from every source within a small radius.
    struct Node { std::int32_t x, y, z; };
    std::queue<Node> q;
    std::unordered_set<std::int64_t> visited;

    auto pushIfWire = [&](std::int32_t wx, std::int32_t wy, std::int32_t wz,
                          std::uint8_t level) {
        const std::uint64_t k = chunkKey(wx >> 4, wz >> 4);
        (void)k;
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

    // seed: sources adjacent to seed position OR wires adjacent to them
    auto sourceAt = [&](std::int32_t wx, std::int32_t wy, std::int32_t wz) {
        return maxEmissionFor(classify(world_.getBlock(wx, wy, wz))) > 0;
    };

    static constexpr int DX[6] = {1,-1,0,0,0,0};
    static constexpr int DY[6] = {0,0,1,-1,0,0};
    static constexpr int DZ[6] = {0,0,0,0,1,-1};

    // If the seed itself is wire next to a source, start from it at full power.
    if (classify(world_.getBlock(sx, sy, sz)) == Comp::Wire) {
        bool fed = false;
        int feedLevel = 0;
        for (int d = 0; d < 6; ++d)
            if (sourceAt(sx + DX[d], sy + DY[d], sz + DZ[d])) {
                fed = true;
                feedLevel = 15;
            }
        if (fed) {
            visited.insert(posKey(sx, sy, sz));
            setPoweredAt(sx, sy, sz, 15);
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

    // after flood, refresh neighbours of all touched wires (lamps/torches)
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
