// BlockTickScheduler: random + scheduled ticks (plan5 item 12-15).
#pragma once
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string>
#include "../game/World.hpp"
#include "../game/GameRules.hpp"

namespace cppfm {

class GameServer;

class IBlockBehavior {
public:
    virtual ~IBlockBehavior() = default;
    virtual void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                      std::uint16_t state, std::int64_t now, GameServer* srv) = 0;
    virtual bool fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                           std::uint16_t state, GameServer* srv) { return false; }
};

struct ScheduledTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const ScheduledTick& o) const { return dueTick > o.dueTick; }
};

class BlockTickScheduler {
public:
    explicit BlockTickScheduler(World& world, GameRuleManager* rules, GameServer* srv)
        : world_(world), rules_(rules), srv_(srv) {}

    void schedule(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t dueTick);
    void tick(std::int64_t now);

    void registerBehavior(const std::string& blockName, std::unique_ptr<IBlockBehavior> b) {
        behaviors_[blockName] = std::move(b);
    }
    IBlockBehavior* behaviorFor(const std::string& blockName) {
        auto it = behaviors_.find(blockName);
        return it == behaviors_.end() ? nullptr : it->second.get();
    }

private:
    World& world_;
    GameRuleManager* rules_;
    GameServer* srv_;
    std::priority_queue<ScheduledTick, std::vector<ScheduledTick>, std::greater<ScheduledTick>> queue_;
    std::unordered_map<std::string, std::unique_ptr<IBlockBehavior>> behaviors_;
    std::unordered_set<std::int64_t> pendingPos_;
    static std::int64_t posKey3(std::int32_t x, std::int32_t y, std::int32_t z) {
        return (static_cast<std::int64_t>(static_cast<std::uint32_t>(x))<<32) ^
               (static_cast<std::int64_t>(y & 0xFFF)<<20) ^ static_cast<std::uint32_t>(z);
    }
};

class CropBehavior : public IBlockBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
    bool fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                   std::uint16_t state, GameServer* srv) override;
};

class SaplingBehavior : public IBlockBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
    bool fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                   std::uint16_t state, GameServer* srv) override;
};

class StemBehavior : public IBlockBehavior {
public:
    explicit StemBehavior(int maxHeight) : maxH_(maxHeight) {}
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
private:
    int maxH_;
};

class FarmlandBehavior : public IBlockBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
};

class FireBehavior : public IBlockBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
};

class PortalAgeBehavior : public IBlockBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
};

} // namespace cppfm
