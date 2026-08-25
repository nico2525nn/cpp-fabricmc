// Xp: vanilla-compatible experience level curve + orb helpers.
#pragma once
#include <algorithm>
#include <cstdint>

namespace cppfm {

// Experience required to advance from `level` to `level+1` (vanilla formula).
inline int xpToNextLevel(int level) {
    if (level >= 31) return 62 + (level - 30) * 9;
    if (level >= 16) return 37 + (level - 15) * 5;
    return 2 * level + 7;
}

struct XpState {
    std::int32_t totalXp = 0;      // lifetime points
    float progress = 0.f;          // 0..1 within current level
    std::int32_t level = 0;

    void addPoints(int points) {
        if (points < 0) { takePoints(-points); return; }
        totalXp += points;
        int carry = points;
        while (carry > 0) {
            const int need = xpToNextLevel(level);
            const float remainF = need * (1.f - progress);
            const int remain = static_cast<int>(remainF + 0.999f);   // ceil-ish
            if (carry >= remain && remain > 0) {
                carry -= remain;
                ++level;
                progress = 0.f;
            } else {
                progress += static_cast<float>(carry) / need;
                if (progress >= 1.f) { progress -= 1.f; ++level; }
                carry = 0;
            }
        }
    }
    // Removes points across levels (used on death: drop orbs).
    void takePoints(int points) {
        totalXp = std::max(0, totalXp - points);
        int remove = points;
        while (remove > 0 && level > 0 && progress <= 0.f) {
            --level;
            progress = 1.f - 1.f / xpToNextLevel(level);
            remove -= 1;
        }
        while (remove > 0) {
            const float have = xpToNextLevel(level) * progress;
            if (remove >= static_cast<int>(have)) {
                remove -= static_cast<int>(have);
                if (level == 0) { progress = 0.f; break; }
                --level;
                progress = 1.f;
            } else {
                progress -= static_cast<float>(remove) / xpToNextLevel(level);
                remove = 0;
            }
            if (level == 0 && progress < 0.f) { progress = 0.f; break; }
        }
    }
};

} // namespace cppfm
