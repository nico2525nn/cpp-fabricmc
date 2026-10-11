#include "PlayerDirectory.hpp"

#include <algorithm>
#include <utility>

namespace cppfm {

std::vector<PlayerDirectory::PlayerRef> PlayerDirectory::snapshot() const {
    std::lock_guard rosterLock(rosterMutex_);
    return players_;
}

std::size_t PlayerDirectory::count() const {
    std::lock_guard rosterLock(rosterMutex_);
    return players_.size();
}

PlayerDirectory::Identity PlayerDirectory::identityOf(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    return Identity{player.uuid, player.name};
}

bool PlayerDirectory::Identity::matches(const Identity& other) const noexcept {
    const auto hasUuid = [](const auto& value) {
        return std::any_of(value.begin(), value.end(),
                           [](const std::uint8_t byte) { return byte != 0; });
    };
    const bool sameUuid = hasUuid(uuid) && hasUuid(other.uuid) &&
                          uuid == other.uuid;
    const bool sameName = !name.empty() && name == other.name;
    return sameUuid || sameName;
}

void PlayerDirectory::applyIdentity(std::vector<Identity>& roster,
                                   const Identity& identity) {
    std::erase_if(roster, [&identity](const Identity& current) {
        return identity.matches(current);
    });
    roster.push_back(identity);
}

std::optional<PlayerDirectory::ReservationId> PlayerDirectory::reserve(
    const std::size_t maximum, const Player& candidate) {
    auto identity = identityOf(candidate);
    std::lock_guard admissionLock(admissionMutex_);
    return reserveLocked(maximum, std::move(identity));
}

std::optional<PlayerDirectory::ReservationId> PlayerDirectory::reserveLocked(
    const std::size_t maximum, Identity candidate) {
    if (maximum == 0 || nextReservationId_ == 0) return std::nullopt;

    for (const auto& [id, reservation] : reservations_) {
        (void)id;
        if (reservation.identity.matches(candidate)) return std::nullopt;
    }

    std::vector<Identity> projected;
    for (const auto& player : snapshot()) {
        if (player) projected.push_back(identityOf(*player));
    }
    for (const auto& [id, reservation] : reservations_) {
        (void)id;
        applyIdentity(projected, reservation.identity);
    }
    applyIdentity(projected, candidate);
    if (projected.size() > maximum) return std::nullopt;

    const ReservationId id = nextReservationId_++;
    reservations_.emplace(id, AdmissionReservation{
                                  std::move(candidate), false});
    return id;
}

void PlayerDirectory::releaseReservation(const ReservationId reservation) noexcept {
    std::lock_guard admissionLock(admissionMutex_);
    const auto found = reservations_.find(reservation);
    if (found != reservations_.end() && !found->second.committing)
        reservations_.erase(found);
}

bool PlayerDirectory::add(PlayerRef player, const ReservationId reservation,
                          const DuplicateRetirer& retireDuplicates) {
    if (!player || reservation == 0) return false;

    const Identity incoming = identityOf(*player);
    std::lock_guard transactionLock(replacementTransactionMutex_);
    DuplicatePlayers victims;
    {
        std::lock_guard admissionLock(admissionMutex_);
        const auto found = reservations_.find(reservation);
        if (found == reservations_.end() || found->second.committing ||
            found->second.identity.uuid != incoming.uuid ||
            found->second.identity.name != incoming.name)
            return false;
        victims = duplicatesFor(incoming);
        {
            std::lock_guard rosterLock(rosterMutex_);
            // Reserve before retiring incumbents. Once the callback has sent
            // disconnects, the final shared_ptr insertion must not allocate
            // and fail after old sessions have already been torn down.
            players_.reserve(players_.size() + 1);
        }
        found->second.committing = true;
    }

    try {
        // This callback can close a connection and wait for its writer. It
        // runs without the admission or roster locks so disconnect cleanup
        // and unrelated reservations remain able to proceed.
        if (!victims.empty() && retireDuplicates) retireDuplicates(victims);
    } catch (...) {
        std::lock_guard admissionLock(admissionMutex_);
        if (auto found = reservations_.find(reservation);
            found != reservations_.end())
            found->second.committing = false;
        throw;
    }

    try {
        std::lock_guard admissionLock(admissionMutex_);
        const auto found = reservations_.find(reservation);
        if (found == reservations_.end() || !found->second.committing)
            return false;
        std::lock_guard rosterLock(rosterMutex_);
        if (!victims.empty()) {
            std::erase_if(players_, [&victims](const PlayerRef& current) {
                return std::find(victims.begin(), victims.end(), current) !=
                       victims.end();
            });
        }
        // replacementTransactionMutex_ prevents competing insertions, and
        // the capacity was reserved before victim retirement, so this move
        // cannot allocate and PlayerRef's move is noexcept.
        players_.push_back(std::move(player));
        reservations_.erase(found);
    } catch (...) {
        std::lock_guard admissionLock(admissionMutex_);
        if (auto found = reservations_.find(reservation);
            found != reservations_.end())
            found->second.committing = false;
        throw;
    }
    return true;
}

void PlayerDirectory::remove(const Player* player) {
    std::lock_guard rosterLock(rosterMutex_);
    std::erase_if(players_, [player](const PlayerRef& current) {
        return current.get() == player;
    });
}

PlayerDirectory::DuplicatePlayers PlayerDirectory::duplicatesFor(
    const Identity& incoming) const {
    DuplicatePlayers victims;
    for (const auto& existing : snapshot()) {
        if (!existing) continue;
        std::lock_guard existingLock(existing->stateMtx);
        const Identity current{existing->uuid, existing->name};
        if (current.matches(incoming))
            victims.push_back(existing);
    }
    return victims;
}

} // namespace cppfm
