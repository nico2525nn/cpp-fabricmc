#include "PlayerDirectory.hpp"

#include <algorithm>
#include <stdexcept>
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

PlayerDirectory::DuplicatePlayers PlayerDirectory::findDuplicatesFor(
    const Player& candidate) const {
    return duplicatesFor(identityOf(candidate));
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
    std::lock_guard admissionLock(admissionMutex_);
    std::optional<Identity> publishedIdentity;
    {
        std::lock_guard rosterLock(rosterMutex_);
        if (playerIdentities_.size() != players_.size())
            throw std::logic_error("player roster identity index is inconsistent");
        for (std::size_t index = 0; index < players_.size(); ++index) {
            if (players_[index].get() == &candidate) {
                publishedIdentity = playerIdentities_[index];
                break;
            }
        }
    }
    auto identity = publishedIdentity
        ? std::move(*publishedIdentity) : identityOf(candidate);
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
    {
        std::lock_guard rosterLock(rosterMutex_);
        if (playerIdentities_.size() != players_.size())
            throw std::logic_error("player roster identity index is inconsistent");
        projected = playerIdentities_;
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

    const auto transaction = std::make_shared<MembershipTransaction>();
    {
        // Claim the incoming object before admission work so a concurrent
        // remove() can cancel this publication instead of losing a race just
        // before the player enters the roster.
        std::lock_guard claimLock(membershipClaimsMutex_);
        auto existingClaim = membershipClaims_.find(player.get());
        if (existingClaim != membershipClaims_.end() &&
            existingClaim->second.awaitingRemovalAcknowledgement) {
            const auto retiredPlayer = existingClaim->second.retiredPlayer.lock();
            if (!retiredPlayer || retiredPlayer.get() != player.get()) {
                membershipClaims_.erase(existingClaim);
                membershipClaimsChanged_.notify_all();
                existingClaim = membershipClaims_.end();
            }
        }
        if (existingClaim != membershipClaims_.end()) return false;
        membershipClaims_.emplace(player.get(),
            MembershipClaim{transaction, false, false});
    }

    Identity incoming;
    bool incomingIdentityIsFrozen = false;
    try {
        {
            std::lock_guard rosterLock(rosterMutex_);
            if (playerIdentities_.size() != players_.size())
                throw std::logic_error(
                    "player roster identity index is inconsistent");
            for (std::size_t index = 0; index < players_.size(); ++index) {
                if (players_[index] == player) {
                    incoming = playerIdentities_[index];
                    incomingIdentityIsFrozen = true;
                    break;
                }
            }
        }
        if (!incomingIdentityIsFrozen) incoming = identityOf(*player);
    } catch (...) {
        std::lock_guard claimLock(membershipClaimsMutex_);
        std::erase_if(membershipClaims_, [&transaction](const auto& entry) {
            return entry.second.transaction == transaction;
        });
        throw;
    }

    DuplicatePlayers victims;
    const auto releaseClaimsLocked = [this, &transaction, &victims, &player](
                                         const bool preserveRetiredVictims) {
        for (auto claim = membershipClaims_.begin();
             claim != membershipClaims_.end();) {
            if (claim->second.transaction != transaction) {
                ++claim;
                continue;
            }
            const bool preserve = preserveRetiredVictims &&
                claim->first != player.get() &&
                claim->second.retirementCompleted &&
                !claim->second.removalRequested &&
                std::any_of(victims.begin(), victims.end(),
                    [&claim](const PlayerRef& victim) {
                        return victim && victim.get() == claim->first;
                    });
            if (preserve) {
                claim->second.awaitingRemovalAcknowledgement = true;
                const auto retired = std::find_if(
                    victims.begin(), victims.end(), [&claim](const PlayerRef& victim) {
                        return victim && victim.get() == claim->first;
                    });
                claim->second.retiredPlayer = *retired;
                ++claim;
            } else {
                claim = membershipClaims_.erase(claim);
            }
        }
        membershipClaimsChanged_.notify_all();
    };
    bool publicationPending = false;
    bool retirementCompleted = false;
    const auto abandonPublication = [this, reservation, &publicationPending,
                                     &releaseClaimsLocked] {
        std::lock_guard admissionLock(admissionMutex_);
        if (auto found = reservations_.find(reservation);
            found != reservations_.end())
            found->second.committing = false;
        std::lock_guard claimLock(membershipClaimsMutex_);
        releaseClaimsLocked(false);
        if (!publicationPending) return;
        std::lock_guard rosterLock(rosterMutex_);
        if (pendingPublications_ > 0) --pendingPublications_;
        publicationPending = false;
    };
    const auto cancelPublication = [this, reservation, &publicationPending,
                                    &releaseClaimsLocked] {
        std::lock_guard admissionLock(admissionMutex_);
        reservations_.erase(reservation);
        std::lock_guard claimLock(membershipClaimsMutex_);
        releaseClaimsLocked(false);
        if (!publicationPending) return;
        std::lock_guard rosterLock(rosterMutex_);
        if (pendingPublications_ > 0) --pendingPublications_;
        publicationPending = false;
    };

    const auto excludeIncomingPlayer = [&player](DuplicatePlayers& entries) {
        std::erase_if(entries, [&player](const PlayerRef& entry) {
            return entry == player;
        });
    };
    try {
        std::lock_guard admissionLock(admissionMutex_);
        const auto found = reservations_.find(reservation);
        if (found == reservations_.end() || found->second.committing ||
            found->second.identity.uuid != incoming.uuid ||
            found->second.identity.name != incoming.name) {
            std::lock_guard claimLock(membershipClaimsMutex_);
            releaseClaimsLocked(false);
            return false;
        }
        std::lock_guard rosterLock(rosterMutex_);
        // Reserve for every in-flight commit before retiring incumbents. A
        // single size()+1 reservation is insufficient when unrelated callbacks
        // complete concurrently and consume the same spare vector capacity.
        const auto maximum = players_.max_size();
        if (pendingPublications_ >= maximum ||
            players_.size() > maximum - pendingPublications_ - 1)
            throw std::length_error("player roster capacity exhausted");
        const auto requiredCapacity = players_.size() + pendingPublications_ + 1;
        players_.reserve(requiredCapacity);
        playerIdentities_.reserve(requiredCapacity);
        ++pendingPublications_;
        publicationPending = true;
        found->second.committing = true;
    } catch (...) {
        abandonPublication();
        throw;
    }

    try {
        bool claimConflict = false;
        {
            std::lock_guard claimLock(membershipClaimsMutex_);
            // Read and claim the current roster atomically with respect to
            // other membership changes. A victim snapshot taken before this
            // claim lock could become stale while another replacement commits.
            victims = duplicatesFor(incoming);
            excludeIncomingPlayer(victims);
            for (const auto& victim : victims) {
                if (!victim || victim.get() == player.get()) continue;
                const auto found = membershipClaims_.find(victim.get());
                if (found != membershipClaims_.end() &&
                    found->second.transaction != transaction) {
                    claimConflict = true;
                    break;
                }
            }
            if (!claimConflict) {
                for (const auto& victim : victims) {
                    if (!victim || victim.get() == player.get()) continue;
                    membershipClaims_.try_emplace(
                        victim.get(), MembershipClaim{
                            transaction, false, false});
                }
            }
        }
        if (claimConflict) {
            abandonPublication();
            return false;
        }

        bool incomingRemovalRequested = false;
        {
            std::lock_guard claimLock(membershipClaimsMutex_);
            const auto found = membershipClaims_.find(player.get());
            incomingRemovalRequested = found == membershipClaims_.end() ||
                found->second.transaction != transaction ||
                found->second.removalRequested;
        }
        if (incomingRemovalRequested) {
            cancelPublication();
            return false;
        }

        // Revalidate under the claim lock immediately before crossing the
        // irreversible callback boundary. remove() can retire an incumbent
        // after the earlier snapshot; it must not then be retired a second
        // time by this replacement transaction.
        if (!victims.empty() && retireDuplicates) {
            bool canceledBeforeRetirement = false;
            bool claimConflict = false;
            bool shouldRetire = false;
            {
                std::lock_guard claimLock(membershipClaimsMutex_);
                const auto incomingClaim = membershipClaims_.find(player.get());
                canceledBeforeRetirement =
                    incomingClaim == membershipClaims_.end() ||
                    incomingClaim->second.transaction != transaction ||
                    incomingClaim->second.removalRequested;
                if (!canceledBeforeRetirement && !incomingIdentityIsFrozen) {
                    {
                        std::lock_guard playerLock(player->stateMtx);
                        canceledBeforeRetirement =
                            player->uuid != incoming.uuid ||
                            player->name != incoming.name;
                    }
                }
                if (!canceledBeforeRetirement) {
                    auto currentVictims = duplicatesFor(incoming);
                    excludeIncomingPlayer(currentVictims);
                    for (const auto& victim : currentVictims) {
                        if (!victim || victim.get() == player.get()) continue;
                        const auto found = membershipClaims_.find(victim.get());
                        if (found != membershipClaims_.end() &&
                            (found->second.transaction != transaction ||
                             found->second.removalRequested ||
                             found->second.retirementStarted ||
                             found->second.removalSideEffectsInProgress)) {
                            claimConflict = true;
                            break;
                        }
                    }
                    if (!claimConflict) {
                        for (const auto& victim : currentVictims) {
                            if (!victim || victim.get() == player.get()) continue;
                            auto [found, inserted] = membershipClaims_.try_emplace(
                                victim.get(), MembershipClaim{
                                    transaction, false, false});
                            (void)inserted;
                            found->second.retirementStarted = true;
                            found->second.retirementCompleted = false;
                            found->second.retirementThread =
                                std::this_thread::get_id();
                        }
                        victims = std::move(currentVictims);
                    }
                }
                if (!canceledBeforeRetirement && !claimConflict &&
                    !victims.empty()) {
                    // The victim claims now make concurrent removal wait until
                    // the callback either completes or fails before effects.
                    shouldRetire = true;
                }
            }
            if (canceledBeforeRetirement) {
                cancelPublication();
                return false;
            }
            if (claimConflict) {
                abandonPublication();
                return false;
            }
            // No directory mutex is held while a retirer can perform network
            // I/O or wait on a connection writer.
            if (shouldRetire) {
                const auto resolveRetirementAttempt = [&](const bool succeeded) {
                    std::lock_guard claimLock(membershipClaimsMutex_);
                    for (const auto& victim : victims) {
                        if (!victim) continue;
                        const auto found = membershipClaims_.find(victim.get());
                        if (found == membershipClaims_.end() ||
                            found->second.transaction != transaction)
                            continue;
                        found->second.retirementStarted = succeeded;
                        found->second.retirementCompleted = succeeded;
                        found->second.retirementThread = {};
                    }
                    membershipClaimsChanged_.notify_all();
                };
                try {
                    retireDuplicates(victims);
                } catch (...) {
                    resolveRetirementAttempt(false);
                    throw;
                }
                resolveRetirementAttempt(true);
                retirementCompleted = true;
            }
        }
    } catch (...) {
        abandonPublication();
        throw;
    }

    try {
        std::lock_guard admissionLock(admissionMutex_);
        const auto found = reservations_.find(reservation);
        std::lock_guard claimLock(membershipClaimsMutex_);
        std::unique_lock playerLock(player->stateMtx, std::defer_lock);
        if (!incomingIdentityIsFrozen) playerLock.lock();
        const auto incomingClaim = membershipClaims_.find(player.get());
        const bool incomingClaimValid = incomingClaim != membershipClaims_.end() &&
            incomingClaim->second.transaction == transaction;
        const bool incomingRemoved = !incomingClaimValid ||
            incomingClaim->second.removalRequested ||
            (!incomingIdentityIsFrozen &&
             (player->uuid != incoming.uuid || player->name != incoming.name));
        if (found == reservations_.end() || !found->second.committing ||
            incomingRemoved) {
            releaseClaimsLocked(retirementCompleted);
            std::lock_guard rosterLock(rosterMutex_);
            if (incomingRemoved && retirementCompleted) {
                eraseRosterEntriesLocked(&victims, nullptr);
            }
            if (incomingRemoved) {
                if (found != reservations_.end())
                    reservations_.erase(found);
            } else if (found != reservations_.end()) {
                found->second.committing = false;
            }
            if (pendingPublications_ > 0) --pendingPublications_;
            publicationPending = false;
            return false;
        }
        std::lock_guard rosterLock(rosterMutex_);
        if (!victims.empty()) eraseRosterEntriesLocked(&victims, nullptr);
        // Each in-flight publication reserved its own capacity above, and
        // PlayerRef's move is noexcept, so this cannot allocate after retire.
        const bool alreadyPublished =
            std::find(players_.begin(), players_.end(), player) != players_.end();
        if (!alreadyPublished) {
            playerIdentities_.push_back(std::move(incoming));
            players_.push_back(std::move(player));
        }
        --pendingPublications_;
        publicationPending = false;
        reservations_.erase(found);
        releaseClaimsLocked(retirementCompleted);
    } catch (...) {
        abandonPublication();
        throw;
    }
    return true;
}

void PlayerDirectory::remove(const Player* player) {
    if (!player) return;
    std::lock_guard claimLock(membershipClaimsMutex_);
    if (auto found = membershipClaims_.find(player);
        found != membershipClaims_.end()) {
        found->second.removalRequested = true;
        if (found->second.awaitingRemovalAcknowledgement) {
            membershipClaims_.erase(found);
            membershipClaimsChanged_.notify_all();
        }
    }
    std::lock_guard rosterLock(rosterMutex_);
    eraseRosterEntriesLocked(nullptr, player);
}

PlayerDirectory::RemovalTicket PlayerDirectory::beginRemoval(
    const Player* player) {
    if (!player) return {};
    std::unique_lock claimLock(membershipClaimsMutex_);
    auto found = membershipClaims_.find(player);
    while (found != membershipClaims_.end() &&
           found->second.retirementStarted &&
           !found->second.retirementCompleted) {
        if (found->second.retirementThread == std::this_thread::get_id()) {
            found->second.removalRequested = true;
            return RemovalTicket{
                player, found->second.transaction, false, false};
        }
        const auto waitingFor = found->second.transaction;
        ++found->second.waitingRemovals;
        membershipClaimsChanged_.notify_all();
        membershipClaimsChanged_.wait(claimLock);
        found = membershipClaims_.find(player);
        if (found != membershipClaims_.end() &&
            found->second.transaction == waitingFor &&
            found->second.waitingRemovals > 0)
            --found->second.waitingRemovals;
        membershipClaimsChanged_.notify_all();
    }
    if (found != membershipClaims_.end() &&
        found->second.awaitingRemovalAcknowledgement) {
        const auto retiredPlayer = found->second.retiredPlayer.lock();
        if (retiredPlayer && retiredPlayer.get() == player) {
            found->second.removalRequested = true;
            return RemovalTicket{
                player, found->second.transaction, false, true};
        }
        membershipClaims_.erase(found);
        membershipClaimsChanged_.notify_all();
        found = membershipClaims_.end();
    }
    if (found != membershipClaims_.end() &&
        (found->second.retirementCompleted ||
         found->second.removalSideEffectsInProgress)) {
        found->second.removalRequested = true;
        return RemovalTicket{player, found->second.transaction, false, false};
    }

    // Transfer a not-yet-started or failed membership claim to removal. The
    // pending add observes the token mismatch and aborts before duplicating
    // these disconnect side effects.
    const auto transaction = std::make_shared<MembershipTransaction>();
    if (found == membershipClaims_.end()) {
        membershipClaims_.emplace(
            player, MembershipClaim{transaction, true, false, false, true});
    } else {
        found->second = MembershipClaim{transaction, true, false, false, true};
    }
    return RemovalTicket{player, transaction, true};
}

void PlayerDirectory::completeRemoval(const RemovalTicket& ticket) {
    if (!ticket.player ||
        (!ticket.ownsSideEffects && !ticket.acknowledgesPriorRetirement))
        return;
    std::lock_guard claimLock(membershipClaimsMutex_);
    const auto found = membershipClaims_.find(ticket.player);
    if (found == membershipClaims_.end() ||
        found->second.transaction.get() != ticket.token.get())
        return;
    std::lock_guard rosterLock(rosterMutex_);
    eraseRosterEntriesLocked(nullptr, ticket.player);
    membershipClaims_.erase(found);
    membershipClaimsChanged_.notify_all();
}

void PlayerDirectory::eraseRosterEntriesLocked(
    const DuplicatePlayers* victims, const Player* singlePlayer) {
    if (playerIdentities_.size() != players_.size())
        throw std::logic_error("player roster identity index is inconsistent");

    std::size_t output = 0;
    for (std::size_t input = 0; input < players_.size(); ++input) {
        const auto& current = players_[input];
        const bool remove = singlePlayer
            ? current.get() == singlePlayer
            : victims && std::find(victims->begin(), victims->end(), current) !=
                             victims->end();
        if (remove) continue;
        if (output != input) {
            players_[output] = std::move(players_[input]);
            playerIdentities_[output] = std::move(playerIdentities_[input]);
        }
        ++output;
    }
    players_.erase(players_.begin() + static_cast<std::ptrdiff_t>(output),
                   players_.end());
    playerIdentities_.erase(
        playerIdentities_.begin() + static_cast<std::ptrdiff_t>(output),
        playerIdentities_.end());
}

PlayerDirectory::DuplicatePlayers PlayerDirectory::duplicatesFor(
    const Identity& incoming) const {
    DuplicatePlayers victims;
    std::lock_guard rosterLock(rosterMutex_);
    if (playerIdentities_.size() != players_.size())
        throw std::logic_error("player roster identity index is inconsistent");
    for (std::size_t index = 0; index < players_.size(); ++index) {
        if (players_[index] && playerIdentities_[index].matches(incoming))
            victims.push_back(players_[index]);
    }
    return victims;
}

} // namespace cppfm
