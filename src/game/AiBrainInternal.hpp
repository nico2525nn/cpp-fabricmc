#pragma once

#include "AiBrain.hpp"
#include <optional>

namespace cppfm {
class World;
}

namespace cppfm::ai_brain_detail {

std::optional<AiPlayerSnapshot> nearestPlayerView(AiContext& ctx,
                                                  const MobEntity& mob);
std::optional<AiPlayerSnapshot> dimensionPlayer(const AiContext& ctx,
                                                const MobEntity& mob,
                                                Player* candidate);
bool raycastObstructed(World* world, double x0, double y0, double z0,
                       double x1, double y1, double z1);

} // namespace cppfm::ai_brain_detail
