#pragma once

#include "Player.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cppfm {

// Owns player roster membership and capacity reservations. Roster snapshots
// never expose directory locks, and replacement callbacks run without any
// directory mutex held.
class PlayerDirectory {
public:
    using PlayerRef = std::shared_ptr<Player>;
    using DuplicatePlayers = std::vector<PlayerRef>;
    using ReservationId = std::uint64_t;
    // A retirer that throws must do so before irreversible side effects. While
    // it is running, removals of its victims wait for the callback outcome;
    // it must not synchronously wait for delegated work that calls beginRemoval
    // on one of those victims. Same-thread re-entry is supported.
    using DuplicateRetirer =
        std::function<void(const DuplicatePlayers& victims)>;

    struct RemovalTicket {
        const Player* player = nullptr;
        std::shared_ptr<const void> token;
        bool ownsSideEffects = false;
        bool acknowledgesPriorRetirement = false;
    };

    std::vector<PlayerRef> snapshot() const;
    std::size_t count() const;
    // Resolve collisions against the identities captured at roster publication,
    // not mutable Player::name/uuid fields on already-published entries.
    DuplicatePlayers findDuplicatesFor(const Player& candidate) const;

    std::optional<ReservationId> reserve(std::size_t maximum,
                                         const Player& candidate);
    void releaseReservation(ReservationId reservation) noexcept;

    bool add(PlayerRef player, ReservationId reservation,
             const DuplicateRetirer& retireDuplicates);
    // Exactly one caller owns irreversible disconnect/JVM-retirement effects.
    // A duplicate-login transaction that already crossed its retirement
    // boundary retains ownership; otherwise removal takes over its claim.
    RemovalTicket beginRemoval(const Player* player);
    void completeRemoval(const RemovalTicket& ticket);

    // Low-level roster removal for callers that do not own disconnect effects.
    void remove(const Player* player);

private:
    friend struct NativeIntegrationTestAccess;

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
    void eraseRosterEntriesLocked(const DuplicatePlayers* victims,
                                  const Player* singlePlayer);
    std::optional<ReservationId> reserveLocked(std::size_t maximum,
                                                Identity candidate);
    DuplicatePlayers duplicatesFor(const Identity& incoming) const;

    struct MembershipTransaction {};

    struct MembershipClaim {
        MembershipClaim() = default;
        MembershipClaim(
            const std::shared_ptr<const MembershipTransaction>& token,
            bool requestRemoval = false, bool startRetirement = false,
            bool completeRetirement = false, bool removalInProgress = false)
            : transaction(token), removalRequested(requestRemoval),
              retirementStarted(startRetirement),
              retirementCompleted(completeRetirement),
              removalSideEffectsInProgress(removalInProgress) { }

        std::shared_ptr<const MembershipTransaction> transaction;
        bool removalRequested = false;
        bool retirementStarted = false;
        bool retirementCompleted = false;
        bool removalSideEffectsInProgress = false;
        bool awaitingRemovalAcknowledgement = false;
        // Callers waiting for an in-flight retirement callback's outcome.
        std::size_t waitingRemovals = 0;
        std::thread::id retirementThread;
        std::weak_ptr<Player> retiredPlayer;
    };

    // Admission mutations are short and never include network I/O. Claims
    // reserve only the affected identities while callbacks run unlocked.
    mutable std::mutex admissionMutex_;
    mutable std::mutex rosterMutex_;
    mutable std::mutex membershipClaimsMutex_;
    std::condition_variable membershipClaimsChanged_;
    std::vector<PlayerRef> players_;
    // Identity is frozen at publication. Player::name/uuid are mutable state
    // fields, but roster collision semantics must not change mid-transaction.
    // This vector is index-aligned with players_ and shares its capacity plan.
    std::vector<Identity> playerIdentities_;
    // Includes additions currently outside the locks in their retirement
    // callback. Reserving this many future slots prevents concurrent commits
    // from reallocating after an incumbent has already been retired.
    std::size_t pendingPublications_ = 0;
    std::map<ReservationId, AdmissionReservation> reservations_;
    std::unordered_map<const Player*, MembershipClaim> membershipClaims_;
    ReservationId nextReservationId_ = 1;
};

} // namespace cppfm
