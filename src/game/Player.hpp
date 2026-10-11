#pragma once

#include "Attributes.hpp"
#include "ClientEntityTracking.hpp"
#include "Items.hpp"
#include "MobEffects.hpp"
#include "Stats.hpp"
#include "Xp.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cppfm {

class Connection;

// Player inventory slot = full ItemStack (components preserved end-to-end).
using InvSlot = ItemStack;

struct Player {
    std::string name;
    std::array<std::uint8_t, 16> uuid{};
    std::int32_t entityId = 0;
    double x = 0.5, y = -60.0, z = 0.5;
    float yaw = 0, pitch = 0;
    bool onGround = true;
    std::int32_t heldSlot = 0;
    std::int64_t lastSeenMs = 0;
    std::int64_t pendingKeepAlive = 0;
    std::int64_t lastKeepAliveSentMs = 0;
    std::int64_t keepAliveCounter = 0;
    bool spawned = false;
    // All teleports share one per-player confirmation sequence. The serial
    // mutex keeps packet enqueue order aligned with the latest pending id.
    mutable std::mutex teleportMtx;
    std::int32_t lastTeleportId = 1;
    std::optional<std::int32_t> pendingTeleportId;
    // Read by the session, directory, and JVM bridge threads. The state mutex
    // still protects compound player state; this flag is atomic because fast
    // roster probes intentionally avoid taking that lock.
    std::atomic<bool> inPlay{false};
    // 0 = handle lifetime open, 1 = retirement started, 2 = retirement
    // finalized. Kept on the Player so delayed retirement callbacks cannot
    // recreate per-address tombstones after the roster has released it.
    std::atomic<std::uint8_t> jvmHandleLifecycle{0};
    // Kept distinct from inPlay so a transitioning player cannot leak into
    // another joiner's entity snapshot before its ordered spawn.
    bool trackingSpawnPublished = false;
    std::uint8_t gamemode = 0;
    std::uint8_t previousGamemode = 0;
    float health = 20.f;
    std::int32_t food = 20;
    float saturation = 5.f;
    double exhaustion = 0;
    std::int32_t foodTickTimer = 0;
    double fallDist = 0;
    double prevFeetY = -60.0;
    bool airborne = false;
    std::int32_t airTicks = 300;
    std::int32_t freezeTicks = 0;
    std::int32_t fireTicks = 0;
    bool isSneaking = false;
    bool isSprinting = false;
    bool isSwimming = false;
    // Session handlers and the authoritative tick share this model lock.
    mutable std::recursive_mutex stateMtx;
    mutable std::mutex inventoryPacketMtx;
    mutable std::mutex equipmentPacketMtx;
    mutable std::vector<std::uint8_t> lastEquipmentPacket;
    mutable std::int8_t lastEquipmentPacketDimension = 0;
    mutable bool hasLastEquipmentPacket = false;
    std::array<InvSlot, kPlayerInventorySlotCount> inv{};
    std::array<InvSlot, 27> enderItems{};
    std::int32_t invStateId = 1;
    bool dead = false;
    std::int64_t pendingDeathTick = -1;
    std::string pendingDeathCause;
    struct LoginProp { std::string name, value, signature; };
    std::vector<LoginProp> loginProps;
    // When both dig locks are needed, acquire digTransitionMtx before digMtx.
    mutable std::mutex digTransitionMtx;
    mutable std::mutex digMtx;
    bool digActive = false;
    std::int32_t digX=0, digY=0, digZ=0;
    std::int64_t digStartTick = 0;
    std::int32_t digTotalTicks = 0;
    std::uint8_t digLastStage = 255;
    std::uint64_t digRevision = 0;
    double sentX = 0, sentY = 0, sentZ = 0;
    float sentYaw = 0, sentPitch = 0;
    XpState xp{};
    std::unique_ptr<StatsManager> stats;
    std::unique_ptr<AdvancementManager> advancements;
    std::int64_t joinTick = 0;
    std::unordered_set<std::string> enabledTriggerObjectives;
    std::vector<EffectInstance> effects;
    bool hasChatSession = false;
    bool onlineAuthenticated = false;
    std::vector<std::vector<std::uint8_t>> mojangProfileKeys;
    std::vector<std::uint8_t> chatPubKey;
    std::vector<std::uint8_t> chatSessionSignature;
    std::array<std::uint8_t, 16> chatSessionId{};
    std::int32_t chatMessageIndex = 0;
    std::int64_t chatSessionExpiry = 0;
    std::vector<std::int64_t> lastSeenChatSalts;
    std::unordered_set<std::string> scoreboardTags;
    // Per-client entity visibility and the chunk snapshots that authorize it.
    // The component serializes its own state; packet transitions still run
    // through the server's simulation dispatch ordering.
    ClientEntityTracking entityTracking;
    static constexpr std::size_t kMaxScoreboardTags = 1024;

    bool addScoreboardTag(std::string tag) {
        std::lock_guard playerLock(stateMtx);
        if (scoreboardTags.contains(tag) ||
            scoreboardTags.size() >= kMaxScoreboardTags)
            return false;
        return scoreboardTags.insert(std::move(tag)).second;
    }

    std::int8_t dimension = 0;
    std::int64_t portalCooldownUntilTick = -100000;
    bool sleeping = false;
    std::int32_t bedX=0, bedY=0, bedZ=0;
    bool hasRespawnPoint = false;
    std::int32_t respawnX = 0, respawnY = -60, respawnZ = 0;
    std::int8_t respawnDimension = 0;
    float respawnAngle = 0.f;
    std::unordered_set<std::string> clientChannels;
    std::shared_ptr<Connection> conn;
    std::int32_t hurtCooldown = 0;
    std::int32_t attackCooldownTicks = 20;
    std::int32_t blockingTicks = 0;
    std::int32_t shieldDisableTicks = 0;
    bool isBlocking = false;
    std::int32_t vehicleId = -1;
    std::int32_t arrowsStuck = 0;
    std::int64_t lastEnderPearlTick = -10000;
    std::int64_t invulnUntilTick = 0;
    std::int32_t enchantmentSeed = 0;
    bool isFlying = false;
    std::int32_t flyingTicks = 0;
    std::shared_future<bool> cleanupCompletion;
    AttributeManager attributes;
    struct ClientSettings {
        std::string locale = "en_us";
        std::int32_t viewDistance = 6;
        std::int32_t chatFlags = 0;
        bool chatColors = true;
        std::uint8_t skinParts = 0x7F;
        std::int32_t mainHand = 1;
        bool textFiltering = false;
        bool serverListing = true;
        std::int32_t particleStatus = 0;
    } clientSettings;
    bool boatLeftPaddle = false, boatRightPaddle = false;
    std::int32_t recipeBookId = 0;
    bool recipeBookOpen = false, recipeBookFilter = false;
    std::int32_t displayedRecipe = -1;
    std::int32_t advancementTabAction = -1;
    std::string advancementTabId;
    bool difficultyLocked = false;
    std::int32_t lastPlayPongId = 0;
    std::int64_t lastPlayPongMs = 0;
    std::array<std::uint8_t,16> spectateTarget{};
    bool hasSpectateTarget = false;
    std::optional<std::int32_t> beaconPrimary, beaconSecondary;
    struct BookEdit {
        std::int32_t hand = 0;
        std::vector<std::string> pages;
        std::string title;
        bool signedCopy = false;
    };
    BookEdit lastBookEdit;
    std::unordered_set<std::string> combatRecipeUnlocks;
};

} // namespace cppfm
