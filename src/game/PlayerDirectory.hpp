#pragma once

#include "Player.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cppfm {

// Owns player roster membership and capacity reservations. Roster snapshots
// never expose the directory's mutex, and replacement callbacks run without
// the roster mutex held.
class PlayerDirectory {
public:
    using PlayerRef = std::shared_ptr<Player>;
    using DuplicatePlayers = std::vector<PlayerRef>;
    using ReservationId = std::uint64_t;
    using DuplicateRetirer =
        std::function<void(const DuplicatePlayers& victims)>;

    std::vector<PlayerRef> snapshot() const;
    std::size_t count() const;

    std::optional<ReservationId> reserve(std::size_t maximum,
                                         const Player& candidate);
    void releaseReservation(ReservationId reservation) noexcept;

    bool add(PlayerRef player, ReservationId reservation,
             const DuplicateRetirer& retireDuplicates);
    void remove(const Player* player);

private:
    struct Identity {
        std::array<std::uint8_t, 16> uuid{};
        std::string name;

        bool matches(const Identity& other) const noexcept;
    };

    struct AdmissionReservation {
        Identity identity;
        bool committing = false;
    };

    static Identity identityOf(const Player& player);
    static void applyIdentity(std::vector<Identity>& roster,
                              const Identity& identity);
    std::optional<ReservationId> reserveLocked(std::size_t maximum,
                                                Identity candidate);
    DuplicatePlayers duplicatesFor(const Identity& incoming) const;

    // Admission mutations are short and never include network I/O. A separate
    // transaction gate orders duplicate retirement/commit without preventing
    // capacity reservations or disconnect cleanup from making progress.
    mutable std::mutex admissionMutex_;
    mutable std::mutex rosterMutex_;
    mutable std::mutex replacementTransactionMutex_;
    std::vector<PlayerRef> players_;
    std::map<ReservationId, AdmissionReservation> reservations_;
    ReservationId nextReservationId_ = 1;
};

} // namespace cppfm
