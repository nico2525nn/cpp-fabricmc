// EntityManager: extracted entity / mob / projectile / XP responsibility (plan8 modular split)
// Hosts mob lifecycle, projectile, XP, and combat helpers (DamageCalculator).
#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include "Entities.hpp"
#include "DamageSource.hpp"
#include "Attributes.hpp"

namespace cppfm {
struct Player;
class GameServer;

class EntityManager {
public:
    EntityManager() = default;

    // pure calculation helpers (no GameServer needed) — use DamageCalculator
    static float calculatePlayerDamage(float base, const DamageSource& src,
                                       int armor, double toughness, int epf,
                                       const std::vector<EffectInstance>& effects) {
        return DamageCalculator::calculate(base, src, armor, toughness, epf, effects);
    }
    static float calculateMobDamage(float base, const DamageSource& src, int armor, int epf) {
        return DamageCalculator::calculate(base, src, armor, 0.0, epf, {});
    }
    static float calculateMobDamageFull(float base, const DamageSource& src,
                                        int armor, double toughness, int epf,
                                        const std::vector<EffectInstance>& effects) {
        return DamageCalculator::calculate(base, src, armor, toughness, epf, effects);
    }

    // mob lifecycle helpers
    void addMob(std::shared_ptr<MobEntity> m) {
        std::lock_guard<std::mutex> lk(mtx_);
        mobs_.push_back(std::move(m));
    }
    std::vector<std::shared_ptr<MobEntity>> snapshotMobs() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return mobs_;
    }
    void removeMob(std::int32_t eid) {
        std::lock_guard<std::mutex> lk(mtx_);
        mobs_.erase(std::remove_if(mobs_.begin(), mobs_.end(),
            [&](auto &p){ return p->entityId==eid; }), mobs_.end());
    }
    size_t mobCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return mobs_.size();
    }

    // item / xp storage
    void addItemDrop(std::shared_ptr<ItemEntity> e){
        std::lock_guard<std::mutex> lk(mtx_);
        itemDrops_.push_back(std::move(e));
    }
    void addXpOrb(std::shared_ptr<XpOrbEntity> e){
        std::lock_guard<std::mutex> lk(mtx_);
        xpOrbs_.push_back(std::move(e));
    }

private:
    mutable std::mutex mtx_;
    std::vector<std::shared_ptr<MobEntity>> mobs_;
    std::vector<std::shared_ptr<ItemEntity>> itemDrops_;
    std::vector<std::shared_ptr<XpOrbEntity>> xpOrbs_;
    std::vector<std::shared_ptr<ProjectileEntity>> projectiles_;
};
} // namespace cppfm
