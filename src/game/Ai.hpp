// Pathfinder: A* over walkable block columns (plan3.md 経路探索).
//
// Nodes are feet positions; a node is walkable when the block at feet and
// head are passable and ground below is solid. Neighbours are the four
// horizontal steps plus one-up "jump" and one-down "drop" transitions.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>
#include "../game/World.hpp"
#include "../game/BlockEntities.hpp"
#include "../generated/BlockStates.hpp"

namespace cppfm::ai {

struct PathNode {
    std::int32_t x, y, z;
    float g = 0, f = 0;
    std::int64_t key() const {
        return cppfm::posKey(x, y, z);
    }
};

inline bool isPassableState(std::uint16_t state) {
    if (state == 0) return true;
    const gen::BlockDef* b = gen::blockByState(state);
    if (!b) return true;
    // treat fluids and short plants as passable for pathing purposes
    const std::string_view n = b->name;
    return n == "minecraft:water" || n == "minecraft:lava" ||
           n.find("sapling") != std::string::npos ||
           (n.find("grass") != std::string::npos && n != "minecraft:grass_block");
}

inline bool isSolidGround(std::uint16_t state) {
    if (state == 0) return false;
    return !isPassableState(state);
}

class Pathfinder {
public:
    struct Result {
        bool found = false;
        std::vector<PathNode> points;                // start..goal
    };

    Pathfinder(World& world) : world_(world) {}

    Result find(std::int32_t sx, std::int32_t sy, std::int32_t sz,
                std::int32_t gx, std::int32_t gy, std::int32_t gz,
                int maxNodes = 3000) {
        Result res;
        std::unordered_map<std::int64_t, PathNode> cameFrom;
        auto h = [&](std::int32_t x, std::int32_t y, std::int32_t z) {
            return static_cast<float>(std::abs(x - gx) + std::abs(y - gy) +
                                      std::abs(z - gz));
        };
        auto cmp = [](const PathNode& a, const PathNode& b) { return a.f > b.f; };
        std::priority_queue<PathNode, std::vector<PathNode>, decltype(cmp)> open(cmp);

        const PathNode start{sx, sy, sz, 0.f, h(sx, sy, sz)};
        open.push(start);
        std::unordered_map<std::int64_t, float> bestG{{start.key(), 0.f}};
        int expanded = 0;

        while (!open.empty() && expanded++ < maxNodes) {
            const PathNode cur = open.top();
            open.pop();
            if (cur.x == gx && cur.y == gy && cur.z == gz) {
                // reconstruct
                std::int64_t k = cur.key();
                while (true) {
                    const auto& n = cameFrom.count(k)
                                        ? cameFrom.at(k)
                                        : cur;
                    res.points.push_back(n);
                    if (n.x == sx && n.y == sy && n.z == sz) break;
                    if (!cameFrom.count(k)) break;
                    k = n.key();
                    if (res.points.size() > 4096) break;
                }
                std::reverse(res.points.begin(), res.points.end());
                res.found = true;
                return res;
            }
            static constexpr int DX[4] = {1,-1,0,0};
            static constexpr int DZ[4] = {0,0,1,-1};
            for (int d = 0; d < 4; ++d) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const std::int32_t nx = cur.x + DX[d];
                    const std::int32_t ny = cur.y + dy;
                    const std::int32_t nz = cur.z + DZ[d];
                    if (ny < kMinY + 1 || ny >= kMaxY - 1) continue;
                    if (!walkable(nx, ny, nz)) continue;
                    const float step =
                        dy > 0 ? 1.6f : 1.f;             // jump costs extra
                    const float ng = cur.g + step;
                    const PathNode nn{nx, ny, nz, ng, ng + h(nx, ny, nz)};
                    auto it = bestG.find(nn.key());
                    if (it != bestG.end() && it->second <= ng) continue;
                    bestG[nn.key()] = ng;
                    cameFrom[nn.key()] = cur;
                    open.push(nn);
                }
            }
        }
        return res;
    }

private:
    bool walkable(std::int32_t x, std::int32_t y, std::int32_t z) const {
        return isPassableState(world_.getBlock(x, y, z)) &&
               isPassableState(world_.getBlock(x, y + 1, z)) &&
               isSolidGround(world_.getBlock(x, y - 1, z));
    }
    World& world_;
};

} // namespace cppfm::ai
