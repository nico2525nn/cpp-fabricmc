#include "GameServer.hpp"
#include "Session.hpp"

namespace cppfm {

void GameServer::resyncOpenMenusAtBlocks(
    std::int8_t dimension,
    const std::unordered_set<std::int64_t>& changedBlockKeys) {
    if (changedBlockKeys.empty()) return;

    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    const auto canonical = canonicalDimension(dimension);
    for (Session* session : activeSessions_) {
        if (session)
            session->resyncOpenMenuForBlockChanges(canonical, changedBlockKeys);
    }
}

} // namespace cppfm
