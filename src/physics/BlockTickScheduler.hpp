// BlockTickScheduler: random + scheduled ticks (plan5 item 12-15).
#pragma once
#include <cstdint>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string>
#include <vector>
#include "../game/World.hpp"
#include "../game/GameRules.hpp"

namespace cppfm {

class GameServer;

class IBlockBehavior {
public:
    virtual ~IBlockBehavior() = default;
    virtual void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                      std::uint16_t state, std::int64_t now, GameServer* srv) = 0;
    virtual void randomTick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, std::int64_t now, GameServer* srv) {
        tick(w, x, y, z, state, now, srv);
    }
    virtual bool fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                           std::uint16_t state, GameServer* srv) { return false; }
    virtual void onNeighborChange(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                  std::uint16_t state, std::int64_t now, GameServer* srv) {}
    virtual bool isFlammable(const std::string& blockName) const { return false; }
    virtual int getSpreadChance() const { return 0; }
};

// RandomTickScheduler (plan7): multiset queue sorted by dueTick with scheduleRandomTick/tick
struct RandomTickEntry {
    std::int32_t x = 0, y = 0, z = 0;
    std::int64_t dueTick = 0;
    bool operator<(const RandomTickEntry& o) const {
        if (dueTick != o.dueTick) return dueTick < o.dueTick;
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
};

class RandomTickScheduler {
public:
    explicit RandomTickScheduler(World& world, GameRuleManager* rules, GameServer* srv)
        : world_(world), rules_(rules), srv_(srv) {}
    RandomTickScheduler(const RandomTickScheduler&) = delete;
    RandomTickScheduler& operator=(const RandomTickScheduler&) = delete;
    void scheduleRandomTick(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t delay);
    void scheduleRandomTick(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t delay, std::int64_t now);
    void tick(std::int64_t now);
    std::size_t size() const { return queue_.size(); }
    bool empty() const { return queue_.empty(); }
    void clear() { queue_.clear(); }
private:
    World& world_;
    GameRuleManager* rules_;
    GameServer* srv_;
    std::multiset<RandomTickEntry> queue_;
};

struct ScheduledTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const ScheduledTick& o) const { return dueTick > o.dueTick; }
};

class BlockTickScheduler {
public:
    explicit BlockTickScheduler(World& world, GameRuleManager* rules, GameServer* srv)
        : world_(world), rules_(rules), srv_(srv), randomScheduler_(world, rules, srv) {}

    void schedule(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t dueTick);
    void tick(std::int64_t now);

    void registerBehavior(const std::string& blockName, std::unique_ptr<IBlockBehavior> b) {
        behaviors_[blockName] = std::move(b);
    }
    IBlockBehavior* behaviorFor(const std::string& blockName) {
        auto it = behaviors_.find(blockName);
        return it == behaviors_.end() ? nullptr : it->second.get();
    }

    // RandomTickScheduler integration (plan7)
    void scheduleRandomTick(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t delay) {
        randomScheduler_.scheduleRandomTick(x, y, z, delay);
    }
    void scheduleRandomTick(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t delay, std::int64_t now) {
        randomScheduler_.scheduleRandomTick(x, y, z, delay, now);
    }
    RandomTickScheduler& randomTicks() { return randomScheduler_; }
    const RandomTickScheduler& randomTicks() const { return randomScheduler_; }

private:
    World& world_;
    GameRuleManager* rules_;
    GameServer* srv_;
    std::priority_queue<ScheduledTick, std::vector<ScheduledTick>, std::greater<ScheduledTick>> queue_;
    std::unordered_map<std::string, std::unique_ptr<IBlockBehavior>> behaviors_;
    std::unordered_set<std::int64_t> pendingPos_;
    RandomTickScheduler randomScheduler_;
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
    bool isFlammable(const std::string& blockName) const override;
    int getSpreadChance() const override { return 10; }
};

class SoulFireBehavior : public FireBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
    bool isFlammable(const std::string& blockName) const override;
    int getSpreadChance() const override { return 10; }
};

class CampfireBehavior : public FireBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
    bool isFlammable(const std::string& blockName) const override;
    int getSpreadChance() const override { return 5; }
};

class PortalAgeBehavior : public IBlockBehavior {
public:
    void tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
              std::uint16_t state, std::int64_t now, GameServer* srv) override;
};

} // namespace cppfm
