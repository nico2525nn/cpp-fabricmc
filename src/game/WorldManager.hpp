// light/fluid/redstone hooks. Delegates to World; GameServer owns one manager per dimension.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>
#include "World.hpp"

namespace cppfm {
class GameServer;
class LightEngine;
class FluidSim;
class RedstoneEngine;
class BlockTickScheduler;

class WorldManager {
public:
    explicit WorldManager(World& overworld, World& nether, World& end)
        : overworld_(overworld), nether_(nether), end_(end) {}

    World& worldFor(std::int8_t dim) {
        switch(dim){
            case -1: return nether_;
            case 1: return end_;
            default: return overworld_;
        }
    }
    const World& worldFor(std::int8_t dim) const {
        return const_cast<WorldManager*>(this)->worldFor(dim);
    }
    World& overworld() { return overworld_; }
    World& nether() { return nether_; }
    World& end() { return end_; }

    bool isChunkInSimulationDistance(std::int32_t cx, std::int32_t cz, double playerX, double playerZ, int simDistance) const {
        if (simDistance <= 0) return true;
        double limit = simDistance * 16.0;
        double chX = cx * 16.0 + 8.0;
        double chZ = cz * 16.0 + 8.0;
        double dx = playerX - chX, dz = playerZ - chZ;
        return std::max(std::abs(dx), std::abs(dz)) < limit;
    }

    // chunk unload decision extracted from GameServer::chunksUnloadTick — Chebyshev
    static bool shouldUnload(int32_t cx, int32_t cz, double px, double pz, double unloadDist2) {
        double chX = cx*16.0+8.0, chZ = cz*16.0+8.0;
        double dx = px - chX, dz = pz - chZ;
        double limit = std::sqrt(unloadDist2);
        return std::max(std::abs(dx), std::abs(dz)) >= limit;
    }

private:
    World& overworld_;
    World& nether_;
    World& end_;
};
} // namespace cppfm
