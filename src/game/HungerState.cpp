#include "HungerManager.hpp"
#include "GameServer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cppfm {

void HungerManager::addFoodAndSaturation(Player& player, const int food,
                                          const float saturation) {
    const auto foodTotal = static_cast<std::int64_t>(player.food) + food;
    player.food = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(foodTotal, 0, FULL_FOOD_LEVEL));

    const double currentSaturation = std::isfinite(player.saturation)
        ? static_cast<double>(player.saturation) : 0.0;
    const double addedSaturation = std::isfinite(saturation)
        ? static_cast<double>(saturation) : 0.0;
    player.saturation = static_cast<float>(std::clamp(
        currentSaturation + addedSaturation, 0.0,
        static_cast<double>(player.food)));
}

} // namespace cppfm
