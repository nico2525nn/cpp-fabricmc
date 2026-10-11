#pragma once

#include "../proto/Ids.hpp"

#include <cstdint>

namespace cppfm {

// Chunk snapshots/deltas and entity-tracking transitions share a FIFO. This
// keeps spawn plus its dependent state/movement in order and ensures entities
// are not introduced ahead of the chunk batch that establishes their world.
// Control packets may still overtake bulk world streaming.
inline bool requiresWorldStateOrdering(std::uint8_t id) noexcept {
    return id == proto::pl::sc::LevelChunkWithLight ||
           id == proto::pl::sc::ForgetLevelChunk ||
           id == proto::pl::sc::UpdateLight ||
           id == proto::pl::sc::BundleDelimiter ||
           id == proto::pl::sc::BlockUpdate ||
           id == proto::pl::sc::MultiBlockChange ||
           id == proto::pl::sc::BlockEntityData ||
           id == proto::pl::sc::SpawnEntity ||
           id == proto::pl::sc::SpawnExperienceOrb ||
           id == proto::pl::sc::Animation ||
           id == proto::pl::sc::EntityEvent ||
           id == proto::pl::sc::MoveEntityPos ||
           id == proto::pl::sc::MoveEntityPosRot ||
           id == proto::pl::sc::EntityLook ||
           id == proto::pl::sc::RemoveEntities ||
           id == proto::pl::sc::RotateHead ||
           id == proto::pl::sc::SetEntityMetadata ||
           id == proto::pl::sc::SetPassengers ||
           id == proto::pl::sc::EntityVelocity ||
           id == proto::pl::sc::MoveMinecart ||
           id == proto::pl::sc::SetEquipment ||
           id == proto::pl::sc::EntityTeleport ||
           id == proto::pl::sc::SyncEntityPosition ||
           id == proto::pl::sc::UpdateAttributes ||
           id == proto::pl::sc::EntityEffect ||
           id == proto::pl::sc::Collect;
}

} // namespace cppfm
