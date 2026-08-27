// BossAI + BossBarManager (plan7 entity section)
// Data-driven boss combat phases and synchronized BossBar UI.
// Sends BossBar ADD/HEALTH/REMOVE packets (0x0A) on spawn/damage/death.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include "Entities.hpp"

namespace cppfm {

class GameServer;
struct Player;
struct AiContext;

// BossAI interface — each boss kind implements phases/attacks.
// Brain BehaviorTree handles basic AI; BossAI adds phase transitions & bar.
class BossAI {
public:
    virtual ~BossAI() = default;
    virtual bool isBoss(MobKind k) const = 0;
    virtual const char* bossName(MobKind k) const = 0;
    virtual void onSpawn(GameServer& srv, MobEntity& mob) = 0;
    virtual void onDamage(GameServer& srv, MobEntity& mob) = 0;
    virtual void onDeath(GameServer& srv, MobEntity& mob) = 0;
    virtual void tick(GameServer& srv, MobEntity& mob, AiContext& ctx, std::int64_t now) = 0;
};

// BossBar entry tracked per boss entity
struct BossBar {
    std::array<std::uint8_t,16> uuid{};
    std::int32_t entityId = -1;
    std::string title;
    float health = 1.f; // 0..1
    int color = 5;      // 0 pink 1 blue 2 red 3 green 4 yellow 5 purple 6 white
    int division = 0;   // 0 none 1:6 2:10 3:12 4:20
    std::uint8_t flags = 0; // 0x01 darken 0x02 dragon 0x04 fog
};

class BossBarManager {
public:
    // GameServer hooks — broadcast to all inPlay players
    void onBossSpawn(GameServer& srv, const MobEntity& mob);
    void onBossDamage(GameServer& srv, const MobEntity& mob);
    void onBossRemove(GameServer& srv, std::int32_t entityId);
    void onPlayerJoin(GameServer& srv, Player& p);
    void removeIfExists(GameServer& srv, std::int32_t entityId) { onBossRemove(srv, entityId); }

    // Command-created bars (plan10 §6 bossbar add/remove)
    void addCommandBar(std::int32_t key, const BossBar& bar) { bars_[key] = bar; }
    void removeCommandBar(std::int32_t key) { bars_.erase(key); }
    void updateHealthForCommandBar(std::int32_t key, float health) {
        auto it = bars_.find(key);
        if (it != bars_.end()) it->second.health = std::clamp(health, 0.f, 1.f);
    }

    bool hasBar(std::int32_t eid) const { return bars_.find(eid)!=bars_.end(); }
    size_t size() const { return bars_.size(); }

private:
    std::unordered_map<std::int32_t, BossBar> bars_;
    static std::array<std::uint8_t,16> uuidForEntity(std::int32_t eid);
    static int colorForKind(MobKind k);
    static std::string titleForKind(MobKind k);
    void sendAdd(GameServer& srv, const BossBar& bar);
    void sendAddToPlayer(GameServer& srv, const BossBar& bar, Player& p);
    void sendHealth(GameServer& srv, const BossBar& bar);
    void sendRemove(GameServer& srv, const BossBar& bar);
};

// Concrete boss AIs
class WitherBossAI final : public BossAI {
public:
    bool isBoss(MobKind k) const override { return k==MobKind::Wither; }
    const char* bossName(MobKind k) const override { (void)k; return "Wither"; }
    void onSpawn(GameServer& srv, MobEntity& mob) override;
    void onDamage(GameServer& srv, MobEntity& mob) override;
    void onDeath(GameServer& srv, MobEntity& mob) override;
    void tick(GameServer& srv, MobEntity& mob, AiContext& ctx, std::int64_t now) override;
};

class DragonBossAI final : public BossAI {
public:
    bool isBoss(MobKind k) const override { return k==MobKind::EnderDragon; }
    const char* bossName(MobKind k) const override { (void)k; return "Ender Dragon"; }
    void onSpawn(GameServer& srv, MobEntity& mob) override;
    void onDamage(GameServer& srv, MobEntity& mob) override;
    void onDeath(GameServer& srv, MobEntity& mob) override;
    void tick(GameServer& srv, MobEntity& mob, AiContext& ctx, std::int64_t now) override;
};

// Manager owning both AIs and the bar manager
class BossAIManager {
public:
    explicit BossAIManager(GameServer& srv);
    BossBarManager& bars() { return bars_; }
    const BossBarManager& bars() const { return bars_; }
    // Dispatch based on kind
    void onSpawn(MobEntity& mob);
    void onDamage(MobEntity& mob);
    void onDeath(MobEntity& mob);
    void tick(MobEntity& mob, AiContext& ctx, std::int64_t now);
    void onPlayerJoin(Player& p);

private:
    GameServer& srv_;
    BossBarManager bars_;
    WitherBossAI wither_;
    DragonBossAI dragon_;
};

} // namespace cppfm
