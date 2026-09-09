#include "GameServer.hpp"
#include "Messages.hpp"
#include "BlockEvent.hpp"
#include "MetadataTypes.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "StairsHelper.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "BehaviorTree.hpp"
#include "EquipmentComponent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "CombatManager.hpp"
#include "MobSpawner.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
#include "PotionBrewing.hpp"
#include "Particles.hpp"
#include "MiningCalculator.hpp"

namespace cppfm {
using namespace proto;

namespace {
const ItemStack* heldMiningItem(const Player& player) {
    if (player.heldSlot < 0 || player.heldSlot >= 9) return nullptr;
    const auto& held = player.inv[36 + player.heldSlot];
    return held.empty() ? nullptr : &held;
}

bool waterAtEyeLevel(const GameServer& server, const Player& player) {
    const auto& world = server.worldFor(player.dimension);
    const int x = static_cast<int>(std::floor(player.x));
    const int y = static_cast<int>(std::floor(player.y + 1.62));
    const int z = static_cast<int>(std::floor(player.z));
    const auto* def = gen::blockByState(world.getBlock(x, y, z));
    return def && def->name == "minecraft:water";
}

MiningContext miningContextFor(const GameServer& server, const Player& player) {
    MiningContext context;
    const ItemStack* held = heldMiningItem(player);
    const std::string itemName = held ? held->name() : "minecraft:air";
    context.tool = MiningCalculator::toolKindFromItemName(itemName);
    context.tier = MiningCalculator::toolTierFromItemName(itemName);
    context.efficiency = held ? held->efficiencyLevel() : 0;

    const int hasteAmp = amplifierFor(player.effects, effects::Haste);
    const int fatigueAmp = amplifierFor(player.effects, effects::MiningFatigue);
    context.haste = hasteAmp >= 0 ? hasteAmp + 1 : 0;
    context.fatigue = fatigueAmp >= 0 ? fatigueAmp + 1 : 0;
    context.inWater = waterAtEyeLevel(server, player);
    if (context.inWater) {
        for (int slot = 5; slot <= 8; ++slot) {
            if (!player.inv[slot].empty() &&
                EnchantmentHelper::hasAquaAffinity(player.inv[slot])) {
                context.aquaAffinity = true;
                break;
            }
        }
    }
    context.onGround = player.onGround;
    return context;
}

bool sameInventoryStack(const ItemStack& lhs, const ItemStack& rhs) {
    return !lhs.empty() && !rhs.empty() &&
           lhs.itemId == rhs.itemId &&
           lhs.components == rhs.components &&
           lhs.removedComponents == rhs.removedComponents;
}

struct SessionMobSnapshot {
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    MobKind kind = MobKind::Pig;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool dead = false;
    int slimeSize = 0;
};

SessionMobSnapshot snapshotMobForSession(const MobEntity& mob) {
    std::lock_guard entityLock(*mob.stateMtx);
    return {mob.entityId, GameServer::canonicalDimension(mob.dimension),
            mob.kind, mob.x, mob.y, mob.z, mob.dead, mob.slimeSize};
}

struct SessionPlayerSnapshot {
    std::int8_t dimension = 0;
    std::uint8_t gamemode = 0;
    std::int32_t entityId = 0;
    std::int32_t heldSlot = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool sneaking = false;
    bool onGround = true;
    bool dead = false;
    bool inPlay = false;
};

SessionPlayerSnapshot snapshotPlayerForSession(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    return {GameServer::canonicalDimension(player.dimension), player.gamemode,
            player.entityId, player.heldSlot, player.x, player.y, player.z,
            player.yaw, player.pitch, player.isSneaking, player.onGround,
            player.dead, player.inPlay};
}

double squaredDistanceToBox(double x, double y, double z,
                            double minX, double minY, double minZ,
                            double maxX, double maxY, double maxZ) {
    const double dx = x < minX ? minX - x : (x > maxX ? x - maxX : 0.0);
    const double dy = y < minY ? minY - y : (y > maxY ? y - maxY : 0.0);
    const double dz = z < minZ ? minZ - z : (z > maxZ ? z - maxZ : 0.0);
    return dx * dx + dy * dy + dz * dz;
}

bool withinBlockInteractionRange(const SessionPlayerSnapshot& player,
                                 std::int32_t x, std::int32_t y,
                                 std::int32_t z) {
    if (!player.inPlay || player.dead ||
        !std::isfinite(player.x) || !std::isfinite(player.y) ||
        !std::isfinite(player.z))
        return false;
    // Player feet are stored in the same coordinate convention as vanilla.
    // Measuring from the eye to the closest point of the block preserves
    // legitimate edge clicks while keeping the survival/creative ranges
    // distinct (4.5/5.0 blocks).
    const double eyeX = player.x;
    const double eyeY = player.y + 1.62;
    const double eyeZ = player.z;
    const double range = player.gamemode == 1 ? 5.0 : 4.5;
    return squaredDistanceToBox(eyeX, eyeY, eyeZ,
                                static_cast<double>(x),
                                static_cast<double>(y),
                                static_cast<double>(z),
                                static_cast<double>(x) + 1.0,
                                static_cast<double>(y) + 1.0,
                                static_cast<double>(z) + 1.0) <=
           range * range + 1e-6;
}

double interactionEntityHalfWidth(MobKind kind, int slimeSize = 2) {
    if (MobEntity::isBoat(kind) || MobEntity::isMinecartKind(kind)) return 0.72;
    if (kind == MobKind::EnderDragon || kind == MobKind::Wither) return 1.0;
    if (kind == MobKind::Slime || kind == MobKind::MagmaCube)
        return std::max(0.26, static_cast<double>(slimeWidthForSize(slimeSize)) * 0.5);
    return 0.32;
}

double interactionEntityHeight(MobKind kind) {
    if (MobEntity::isBoat(kind) || MobEntity::isMinecartKind(kind)) return 0.7;
    if (kind == MobKind::EnderDragon) return 3.5;
    if (kind == MobKind::Wither) return 3.5;
    if (kind == MobKind::Enderman || kind == MobKind::IronGolem ||
        kind == MobKind::Ravager || kind == MobKind::Warden) return 2.9;
    return 1.8;
}

bool withinEntityInteractionRange(const SessionPlayerSnapshot& player,
                                  double targetX, double targetY, double targetZ,
                                  MobKind targetKind = MobKind::Pig,
                                  int slimeSize = 2) {
    if (!player.inPlay || player.dead ||
        !std::isfinite(player.x) || !std::isfinite(player.y) ||
        !std::isfinite(player.z) || !std::isfinite(targetX) ||
        !std::isfinite(targetY) || !std::isfinite(targetZ))
        return false;
    const double range = player.gamemode == 1 ? 5.0 : 3.0;
    const double halfWidth = interactionEntityHalfWidth(targetKind, slimeSize);
    const double height = interactionEntityHeight(targetKind);
    return squaredDistanceToBox(player.x, player.y + 1.62, player.z,
                                targetX - halfWidth, targetY, targetZ - halfWidth,
                                targetX + halfWidth, targetY + height,
                                targetZ + halfWidth) <= range * range + 1e-6;
}

bool withinEntityInteractionRange(double playerX, double playerY, double playerZ,
                                  std::uint8_t gamemode,
                                  double targetX, double targetY, double targetZ,
                                  MobKind targetKind = MobKind::Pig,
                                  int slimeSize = 2) {
    if (!std::isfinite(playerX) || !std::isfinite(playerY) ||
        !std::isfinite(playerZ) || !std::isfinite(targetX) ||
        !std::isfinite(targetY) || !std::isfinite(targetZ))
        return false;
    const double range = gamemode == 1 ? 5.0 : 3.0;
    const double halfWidth = interactionEntityHalfWidth(targetKind, slimeSize);
    const double height = interactionEntityHeight(targetKind);
    return squaredDistanceToBox(playerX, playerY + 1.62, playerZ,
                                targetX - halfWidth, targetY, targetZ - halfWidth,
                                targetX + halfWidth, targetY + height,
                                targetZ + halfWidth) <= range * range + 1e-6;
}

bool collisionSolidForPlayer(std::uint16_t state) {
    if (state == 0) return false;
    const auto* def = gen::blockByState(state);
    if (!def) return false;
    const std::string_view name = def->name;
    // Fluids are enterable and do not form a full player collision box.
    if (name == "minecraft:water" || name == "minecraft:lava" ||
        name == "minecraft:bubble_column")
        return false;
    return isMotionBlocking(state);
}

bool intersectsPlayerCollision(const World& world, double x, double y, double z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return true;
    constexpr double kHalfWidth = 0.3;
    constexpr double kHeight = 1.8;
    constexpr double kEpsilon = 1e-7;
    const int minX = static_cast<int>(std::floor(x - kHalfWidth + kEpsilon));
    const int maxX = static_cast<int>(std::floor(x + kHalfWidth - kEpsilon));
    const int minY = static_cast<int>(std::floor(y + kEpsilon));
    const int maxY = static_cast<int>(std::floor(y + kHeight - kEpsilon));
    const int minZ = static_cast<int>(std::floor(z - kHalfWidth + kEpsilon));
    const int maxZ = static_cast<int>(std::floor(z + kHalfWidth - kEpsilon));
    for (int by = minY; by <= maxY; ++by)
        for (int bz = minZ; bz <= maxZ; ++bz)
            for (int bx = minX; bx <= maxX; ++bx)
                if (collisionSolidForPlayer(world.getBlock(bx, by, bz))) return true;
    return false;
}

int effectiveViewDistance(const ServerConfig& config) {
    // Chunk streaming currently keeps a bounded 12-chunk radius.  Advertise
    // the same value that tickChunksAround can actually satisfy so the client
    // does not request a view larger than the server's authoritative stream.
    return std::clamp(std::min(config.viewDistance, 12),
                      constants::kViewDistanceMin, 12);
}

int effectiveViewDistance(const ServerConfig& config, int clientViewDistance) {
    return std::clamp(std::min({config.viewDistance, clientViewDistance, 12}),
                      constants::kViewDistanceMin, 12);
}

// Add a complete stack to a trial inventory.  Returning false leaves the
// caller's inventory untouched when the whole stack cannot fit; menu close
// and result-slot paths rely on that property to avoid partial-add + drop
// duplication.
bool insertCompleteInventoryStack(std::array<InvSlot, 46>& inventory,
                                  const ItemStack& source) {
    if (source.empty()) return true;
    int remaining = source.count;
    const int limit = maxStackFor(source);
    static constexpr int kHotbar[] = {36, 37, 38, 39, 40, 41, 42, 43, 44};
    static constexpr int kMain[] = {
        9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};

    const auto mergeInto = [&](const int* slots, std::size_t count) {
        for (std::size_t i = 0; i < count && remaining > 0; ++i) {
            ItemStack& destination = inventory[slots[i]];
            if (!sameInventoryStack(destination, source) ||
                destination.count >= limit) continue;
            const int moved = std::min(remaining, limit - destination.count);
            destination.count = static_cast<std::int16_t>(destination.count + moved);
            remaining -= moved;
        }
    };
    const auto fillEmpty = [&](const int* slots, std::size_t count) {
        for (std::size_t i = 0; i < count && remaining > 0; ++i) {
            ItemStack& destination = inventory[slots[i]];
            if (!destination.empty()) continue;
            destination = source;
            destination.count = static_cast<std::int16_t>(
                std::min(remaining, limit));
            remaining -= destination.count;
        }
    };

    mergeInto(kHotbar, std::size(kHotbar));
    mergeInto(kMain, std::size(kMain));
    fillEmpty(kHotbar, std::size(kHotbar));
    fillEmpty(kMain, std::size(kMain));
    return remaining == 0;
}
} // namespace

bool handleCakeBlockConsume(GameServer& srv, Player& p, std::int32_t x, std::int32_t y, std::int32_t z){
    return HungerManager::handleCakeBlockConsume(srv, p, x, y, z);
}
static WriteBuffer makeWorldState(const ServerConfig& c, const World& world,
                                  std::uint8_t gamemode) {
    const auto dim = GameServer::canonicalDimension(world.dimensionId());
    WriteBuffer w;
    w.varint(dim == -1 ? 3 : (dim == 1 ? 2 : 0));
    w.string(world.dimensionKey());
    w.i64(c.hashedSeed);
    w.u8(gamemode);
    w.i8(-1);
    w.boolean(false);
    w.boolean(world.isFlat());
    w.boolean(false);
    w.varint(0);
    w.varint(world.seaLevel());
    return w;
}
static WriteBuffer makeSpawnEntity(const Player& p) {
    WriteBuffer b;
    b.varint(p.entityId);
    b.uuid(p.uuid.data());
    b.varint(static_cast<std::int32_t>(gen::kPlayerEntityTypeId));
    b.f64(p.x); b.f64(p.y); b.f64(p.z);
    const auto toAngle = [](float deg) { return static_cast<std::uint8_t>(deg * constants::kAngleScaleNum / constants::kAngleScaleDen); };
    b.i8(static_cast<std::int8_t>(toAngle(p.pitch)));
    b.i8(static_cast<std::int8_t>(toAngle(p.yaw)));
    b.i8(static_cast<std::int8_t>(toAngle(p.yaw)));
    b.varint(0);
    b.i16(0); b.i16(0); b.i16(0);
    return b;
}
static void sendSkinMetadata(Player& to, std::int32_t entityId) {
    WriteBuffer md;
    md.varint(entityId);
    md.u8(17); md.u8(0);
    md.u8(0x7F);
    md.u8(255);
    to.conn->trySendPacket(pl::sc::SetEntityMetadata, md);
}
struct SessionMenuIo : MenuIo {
    Session& s;
    explicit SessionMenuIo(Session& ss) : s(ss) {}

    // MenuLogic is called while the session owns the player/container model
    // locks.  Its callbacks are deliberately queued so that world mutation,
    // persistence/advancement hooks, and other extension-facing work never
    // run from inside that critical section.  The caller flushes after it
    // has released all model locks.
    enum class ActionKind { Drop, BlockEntityChanged, ItemObtained };
    struct PendingAction {
        ActionKind kind = ActionKind::BlockEntityChanged;
        Player* player = nullptr;
        std::int8_t dimension = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        std::int64_t key = 0;
        ItemStack stack = ItemStack::air();
        std::string how;
    };
    std::vector<PendingAction> pending;

    void dropFromPlayer(Player& p, const ItemStack& stack, bool whole) override {
        ItemStack s2 = stack;
        if (!whole) s2.count = 1;
        PendingAction action;
        action.kind = ActionKind::Drop;
        action.player = &p;
        action.dimension = p.dimension;
        action.x = p.x;
        action.y = p.y + 1.2;
        action.z = p.z;
        action.stack = std::move(s2);
        pending.push_back(std::move(action));
    }
    void blockEntityChanged(std::int64_t key) override {
        PendingAction action;
        action.kind = ActionKind::BlockEntityChanged;
        action.dimension = s.dimension();
        action.key = key;
        pending.push_back(std::move(action));
    }
    void itemCrafted(Player& p, const ItemStack& result) override {
        PendingAction action;
        action.kind = ActionKind::ItemObtained;
        action.player = &p;
        action.stack = result;
        action.how = "crafted";
        pending.push_back(std::move(action));
    }
    void itemSmelted(Player& p, const ItemStack& result) override {
        PendingAction action;
        action.kind = ActionKind::ItemObtained;
        action.player = &p;
        action.stack = result;
        action.how = "smelted";
        pending.push_back(std::move(action));
    }
    void flush() {
        auto actions = std::move(pending);
        pending.clear();
        for (auto& action : actions) {
            switch (action.kind) {
            case ActionKind::Drop:
                s.server().spawnItemDropFor(action.dimension, action.x, action.y,
                                             action.z, action.stack, 0, 0.15, 0);
                break;
            case ActionKind::BlockEntityChanged:
                s.server().blockEntitiesFor(action.dimension).markDirty(action.key);
                break;
            case ActionKind::ItemObtained:
                if (action.player)
                    s.server().onItemObtained(*action.player, action.stack,
                                              action.how.c_str());
                break;
            }
        }
    }
};

static std::unique_lock<std::recursive_mutex> lockMenuBlockEntity(Menu& menu) {
    if (!menu.blockEntityOwner || !menu.blockEntityOwner->stateMtx)
        return {};
    return std::unique_lock<std::recursive_mutex>(*menu.blockEntityOwner->stateMtx);
}

void Session::run() {
    try {
        while (state_ != State::Done && srv_.running()) {
            switch (state_) {
            case State::Handshake: {
                if (conn_->peekFirstByte(50) == 0xFE) {
                    answerLegacyPing();
                    state_ = State::Done;
                    break;
                }
                auto frame = conn_->readFrame();
                ReadBuffer in(frame);
                const std::uint8_t pid = in.u8();
                if (pid != hb::cs::Intention)
                    throw std::runtime_error("expected handshake intention");
                handleHandshake(in);
                break;
            }
            case State::Status:
                handleStatus();
                state_ = State::Done;                 // vanilla closes after status
                break;
            case State::Login:
                handleLogin();
                break;
            case State::Configuration:
                handleConfiguration();
                break;
            case State::Play:
                handlePlay();
                break;
            default:
                return;
            }
        }
    } catch (const SocketClosedError&) {
    } catch (const PacketDecoder::OversizeError& e) {
        std::fprintf(stderr, "[cppfm] session %s oversize: %s\n",
                     conn_->peer().c_str(), e.what());
        if (state_ == State::Login || state_ == State::Configuration ||
            state_ == State::Play) {
            try { disconnectIn("{\"text\":\"Packet too large\"}"); } catch (...) {}
        }
        state_ = State::Done;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] session %s error: %s\n",
                     conn_->peer().c_str(), e.what());
        if ((state_ == State::Login || state_ == State::Configuration ||
             state_ == State::Play) && conn_->isOpen()) {
            try { disconnectIn("{\"text\":\"Internal server error\"}"); } catch (...) {}
        }
        state_ = State::Done;
    }
    if (registered_) {
        // Cleanup runs after the main session loop and must not leave a stale
        // player in the global roster when a hook or persistence operation
        // fails.  Each step is isolated so later removal/broadcast work still
        // happens, while failures remain visible in the server log.
        registered_ = false;
        // Stop using this player for simulation-distance decisions immediately.
        // Persistence and quit hooks may take longer than one tick, and keeping
        // a disconnected player active during that window expands random ticks
        // and entity work around a position nobody can observe anymore.
        self_->inPlay = false;
        const auto cleanupStep = [this](const char* name, auto&& step) {
            try {
                step();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[cppfm] session cleanup (%s) failed: %s\n",
                             name, e.what());
            } catch (...) {
                std::fprintf(stderr, "[cppfm] session cleanup (%s) failed\n", name);
            }
        };
        cleanupStep("JVM quit hook", [this] {
            if (srv_.jvmRuntime()) srv_.jvmRuntime()->onPlayerQuit(*self_);
        });
        cleanupStep("player quit event", [this] {
            api::PlayerQuitEvent qev;
            qev.player = self_.get();
            srv_.events().quit.fire(qev);
        });
        cleanupStep("progress save", [this] { srv_.savePlayerProgress(*self_); });
        cleanupStep("leave message", [this] {
            srv_.broadcastSystemText((msg::kYellow + self_->name + " left the game"), nullptr);
        });
        cleanupStep("player list removal", [this] {
            WriteBuffer rm;
            rm.varint(1);
            rm.uuid(self_->uuid.data());
            srv_.broadcastPacketExcept(nullptr, pl::sc::PlayerInfoRemove, rm);
        });
        cleanupStep("entity removal", [this] {
            WriteBuffer ent;
            ent.varint(1);
            ent.varint(self_->entityId);
            srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                                  pl::sc::RemoveEntities, ent);
        });
        cleanupStep("chat suggestions", [this] {
            srv_.broadcastChatSuggestions(1, std::vector<std::string>{self_->name}, nullptr);
        });
        cleanupStep("score reset", [this] {
            // D26: wildcard reset_score 0x49 for disconnecting holder to clear sidebar ghosts
            const auto affected = srv_.scoreboard.resetAllScores(self_->name);
            if (!affected.empty()) srv_.sendResetScoreAllWildcard(self_->name);
        });
        cleanupStep("player save", [this] {
            srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_);
        });
        cleanupStep("roster removal", [this] { srv_.removePlayer(self_.get()); });
    }
}

namespace {
constexpr std::size_t kMaxLegacyPingBytes = 0xFFFFu;
constexpr std::size_t kMaxServerIconBytes = 512u * 1024u;

std::string readServerIconBase64() {
    std::ifstream file("server-icon.png", std::ios::binary | std::ios::ate);
    if (!file) return {};

    const std::streampos end = file.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) > kMaxServerIconBytes) {
        std::fprintf(stderr, "[cppfm] ignoring server-icon.png larger than %zu bytes\n",
                     kMaxServerIconBytes);
        return {};
    }
    const auto size = static_cast<std::size_t>(end);
    file.seekg(0, std::ios::beg);
    std::string bytes(size, '\0');
    if (size != 0 && !file.read(bytes.data(), static_cast<std::streamsize>(size)))
        return {};

    static constexpr char kBase64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(21 + ((bytes.size() + 2) / 3) * 4);
    encoded = "data:image/png;base64,";
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t b0 = static_cast<std::uint8_t>(bytes[i]);
        const std::uint32_t b1 = i + 1 < bytes.size()
                                     ? static_cast<std::uint8_t>(bytes[i + 1]) : 0;
        const std::uint32_t b2 = i + 2 < bytes.size()
                                     ? static_cast<std::uint8_t>(bytes[i + 2]) : 0;
        encoded.push_back(kBase64[(b0 >> 2) & 0x3F]);
        encoded.push_back(kBase64[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
        encoded.push_back(i + 1 < bytes.size()
                             ? kBase64[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=');
        encoded.push_back(i + 2 < bytes.size() ? kBase64[b2 & 0x3F] : '=');
    }
    return encoded;
}

std::string makeStatusJson(const GameServer& server,
                           const std::vector<GameServer::PlayerRef>& players) {
    json::Value root = json::Value::object();

    json::Value version = json::Value::object();
    version.set("name", json::Value::ofString(kMinecraftVersion));
    version.set("protocol", json::Value::ofNumber(kProtocolVersion));
    root.set("version", std::move(version));

    json::Value playerInfo = json::Value::object();
    playerInfo.set("max", json::Value::ofNumber(server.config().maxPlayers));
    playerInfo.set("online", json::Value::ofNumber(
        static_cast<double>(players.size())));
    json::Value sample = json::Value::array();
    for (std::size_t i = 0; i < players.size() && i < 2; ++i) {
        if (!players[i]) continue;
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::ofString(players[i]->name));
        entry.set("id", json::Value::ofString(GameServer::uuidToDashed(players[i]->uuid)));
        sample.push(std::move(entry));
    }
    playerInfo.set("sample", std::move(sample));
    root.set("players", std::move(playerInfo));

    json::Value description = json::Value::object();
    description.set("text", json::Value::ofString(server.config().motd));
    root.set("description", std::move(description));
    root.set("enforcesSecureChat", json::Value::ofBool(false));

    const std::string favicon = readServerIconBase64();
    if (!favicon.empty()) root.set("favicon", json::Value::ofString(favicon));
    return root.dump();
}
} // namespace

void Session::answerLegacyPing() {
    const std::string body = std::string("\u00a71") + '\0' +
        std::to_string(kProtocolVersion) + '\0' + std::string(kMinecraftVersion) + '\0' +
        srv_.config().motd + '\0' + std::to_string(srv_.playerCount()) + '\0' +
        std::to_string(srv_.config().maxPlayers);
    if (body.size() > kMaxLegacyPingBytes) {
        std::fprintf(stderr, "[cppfm] legacy ping response exceeds UTF-16 length limit\n");
        return;
    }
    // UTF-16BE encode (all chars here are BMP; § is U+00A7).
    std::vector<std::uint8_t> out;
    out.reserve(3 + body.size() * 2);
    out.push_back(0xFF);
    out.push_back(0);
    out.push_back(0);
    for (char c : body) {
        const unsigned char uc = static_cast<unsigned char>(c);
        // § arrived as UTF-8 (0xC2 0xA7) — emit the single U+00A7 unit instead.
        if (uc == 0xC2) continue;
        const std::uint16_t unit = (uc == 0xA7) ? 0x00A7 : (uc < 0x80 ? uc : '?');
        out.push_back(static_cast<std::uint8_t>(unit >> 8));
        out.push_back(static_cast<std::uint8_t>(unit & 0xFF));
    }
    // fix the length prefix for the folded § byte
    const auto codeUnits = (out.size() - 3) / 2;
    if (codeUnits > kMaxLegacyPingBytes) {
        std::fprintf(stderr, "[cppfm] legacy ping response exceeds UTF-16 length limit\n");
        return;
    }
    const std::uint16_t real = static_cast<std::uint16_t>(codeUnits);
    out[1] = static_cast<std::uint8_t>(real >> 8);
    out[2] = static_cast<std::uint8_t>(real & 0xFF);
    conn_->trySendRaw(out.data(), out.size());
    std::fprintf(stderr, "[cppfm] legacy ping answered (0xFE)\n");
}
void Session::handleHandshake(ReadBuffer& in) {
    const std::int32_t protoVer = in.varint();
    const std::string address = in.string(1024);
    const std::uint16_t port = in.u16();
    const std::int32_t nextState = in.varint();
    (void)address; (void)port;
    if (nextState == 1) { state_ = State::Status; return; }
    if (nextState == 2) {
        if (protoVer != kProtocolVersion) {
            state_ = State::Login;
            disconnectIn("{\"text\":\"Outdated client! Please use 1.21.4\"}");
            state_ = State::Done;
            return;
        }
        state_ = State::Login;
        return;
    }
    throw std::runtime_error("bad handshake next state");
}
void Session::handleStatus() {
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        switch (in.u8()) {
        case st::cs::Request: {
            const auto players = srv_.playersSnapshot();
            const std::string json = makeStatusJson(srv_, players);
            WriteBuffer body;
            body.string(json);
            conn_->sendPacket(st::sc::Response, body);
            break;
        }
        case st::cs::Ping: {
            WriteBuffer body;
            body.i64(in.i64());
            conn_->sendPacket(st::sc::Pong, body);
            return;
        }
        default:
            throw std::runtime_error("unexpected status packet");
        }
    }
}
void Session::disconnectIn(const char* textJson) {
    WriteBuffer body;
    nbt::writeTextComponent(body, textJson);
    switch (state_) {
    case State::Play:          conn_->sendPacket(pl::sc::Disconnect, body); break;
    case State::Configuration: conn_->sendPacket(cf::sc::Disconnect, body); break;
    default:                   conn_->sendPacket(lo::sc::Disconnect, body); break;
    }
}
// peer reads the reason, then an abortive close (dead socket observable).
void Session::kickPlay(const char* jsonReason) {
    if (state_ == State::Done) return;
    try { disconnectIn(jsonReason); } catch (...) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    conn_->abort();
    state_ = State::Done;
}
void Session::handleLogin() {
    auto frame = conn_->readFrame();
    ReadBuffer in(frame);
    if (in.u8() != lo::cs::Hello) throw std::runtime_error("expected login hello");

    self_->name = [this, &in]{
        try {
            return in.string(16);
        } catch (const std::exception&) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "Invalid username");
            conn_->trySendPacket(proto::lo::sc::Disconnect, kick);
            state_ = State::Done;
            return std::string{};
        }
    }();
    if (state_ == State::Done) return;

    if (!GameServer::isValidPlayerName(self_->name)) {
        WriteBuffer kick;
        nbt::writeTextComponent(kick, "Invalid username");
        conn_->trySendPacket(proto::lo::sc::Disconnect, kick);
        state_ = State::Done;
        return;
    }

    auto uuidBytes = in.bytes(16);
    std::copy(uuidBytes.begin(), uuidBytes.end(), self_->uuid.begin());
    // ban check (banned-players.json)
    if (srv_.isBanned(self_->name)) {
        WriteBuffer kick;
        nbt::writeTextComponent(kick, "You are banned from this server");
        conn_->sendPacket(proto::lo::sc::Disconnect, kick);
        state_ = State::Done;
        return;
    }
    // ip ban check
    {
        std::string peer = conn_->peer();
        std::string ip = peer;
        auto colon = ip.find(':');
        if (colon != std::string::npos) ip = ip.substr(0, colon);
        if (!ip.empty() && ip != "?" && srv_.isIpBanned(ip)) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "You are IP banned from this server");
            conn_->sendPacket(proto::lo::sc::Disconnect, kick);
            state_ = State::Done;
            return;
        }
    }
    if (srv_.whitelist().enabled()) {
        // any registered-name match is impossible pre-join; check file-backed list
        bool ok = srv_.whitelist().contains(self_->name) || srv_.isOp(self_->name);
        if (!ok) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "You are not whitelisted on this server");
            conn_->sendPacket(proto::lo::sc::Disconnect, kick);
            state_ = State::Done;
            return;
        }
    }
    if (srv_.config().maxPlayers > 0 && (int)srv_.playerCount() >= srv_.config().maxPlayers) {
        WriteBuffer kick;
        nbt::writeTextComponent(kick, "Server is full");
        conn_->sendPacket(proto::lo::sc::Disconnect, kick);
        state_ = State::Done;
        return;
    }
    self_->entityId = 0; // set on play entry

    if (srv_.config().compressionThreshold >= 0) {
        WriteBuffer sc;
        sc.varint(srv_.config().compressionThreshold);
        conn_->sendPacket(lo::sc::SetCompression, sc);
        conn_->setCompression(srv_.config().compressionThreshold);
    }

    std::fprintf(stderr, "[cppfm] login hello: %s from %s\n",
                 self_->name.c_str(), conn_->peer().c_str());
    if (srv_.config().onlineMode) {
        std::fprintf(stderr, "[cppfm] ONLINE: sending encryption request to %s\n", self_->name.c_str());
        if (!srv_.loginVerifyToken_.size()) {
            srv_.loginKeys_.generate();
            srv_.loginVerifyToken_.resize(16);
            RAND_bytes(reinterpret_cast<unsigned char*>(srv_.loginVerifyToken_.data()), 16);
        }
        WriteBuffer er;
        er.string("");                                // serverId
        er.varint(static_cast<std::int32_t>(srv_.loginKeys_.publicDer.size()));
        er.raw(srv_.loginKeys_.publicDer.data(), srv_.loginKeys_.publicDer.size());
        er.varint(16);
        er.raw(srv_.loginVerifyToken_.data(), 16);
        er.boolean(true);                             // shouldAuthenticate (strict 1.21.4)
        conn_->sendPacket(proto::lo::sc::EncryptionRequest, er);

        auto pbody = conn_->readFrame();
        ReadBuffer rin(pbody);
        const auto respPid = rin.u8();
        if (respPid != proto::lo::cs::Key) throw std::runtime_error("expected encryption response");
        try {
        const auto slen = rin.varint();
        const auto secretCt = rin.bytes(slen);
        const auto tlen = rin.varint();
        const auto tokenCt = rin.bytes(tlen);

        auto secret = crypto::rsaDecryptP(srv_.loginKeys_.pkey, secretCt.data(), secretCt.size());
        auto tokenBack = crypto::rsaDecryptP(srv_.loginKeys_.pkey, tokenCt.data(), tokenCt.size());
        if (tokenBack != srv_.loginVerifyToken_)
            throw std::runtime_error("verify token mismatch");
        if (secret.size() != 16) throw std::runtime_error("bad shared secret size");
        // Mojang session-server authentication
        std::string hash = crypto::mcSha1Hex("", secret, srv_.loginKeys_.publicDer);
        bool authOk = false;
        std::string uuidHex;
        if (getenv("CPPFM_AUTH_STUB")) {
            // test mode: accept any session
            unsigned char md[16];
            unsigned int ml = 0;
            EVP_MD_CTX* mm = EVP_MD_CTX_new();
            EVP_DigestInit_ex(mm, EVP_sha1(), nullptr);
            EVP_DigestUpdate(mm, self_->name.data(), self_->name.size());
            EVP_DigestFinal_ex(mm, md, &ml);
            EVP_MD_CTX_free(mm);
            char hexbuf[33];
            for (int q = 0; q < 16; ++q) snprintf(hexbuf + q * 2, 3, "%02x", md[q]);
            uuidHex = std::string(hexbuf, 32);
            authOk = true;
        } else {
            try {
                const std::string url = "https://sessionserver.mojang.com/session/minecraft/hasJoined?username=" +
                    self_->name + "&serverId=" + hash;
                const std::string json = httpGet(url);
                HasJoinedResult r;
                authOk = parseHasJoined(json, r);
                if (authOk) uuidHex = r.uuidNoDashes;
                if (authOk) {
                    for (auto& pr : r.props) self_->loginProps.push_back({pr.name, pr.value, pr.signature});
                }
            } catch (const std::exception& e) {
                authOk = false;
            }
        }
        if (!authOk) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "Failed to verify your session (online mode)");
            conn_->sendPacket(proto::lo::sc::Disconnect, kick);
            state_ = State::Done;
            return;
        }
        for (int q = 0; q < 16; ++q)
            self_->uuid[q] = static_cast<std::uint8_t>(std::stoul(uuidHex.substr(q * 2, 2), nullptr, 16));

        std::fprintf(stderr, "[cppfm] %s online auth ok, enabling encryption\n", self_->name.c_str());
        conn_->enableEncryption(secret);
        if (srv_.config().compressionThreshold >= 0) {
            WriteBuffer scp;
            scp.varint(srv_.config().compressionThreshold);
            conn_->sendPacket(proto::lo::sc::SetCompression, scp);
            conn_->setCompression(srv_.config().compressionThreshold);
        }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[cppfm] ONLINE AUTH ERROR [%s]: %s\n",
                         self_->name.c_str(), e.what());
            throw;  // re-throw for session cleanup
        }
        std::fprintf(stderr, "[cppfm] %s sent compression+success\n", self_->name.c_str());
    }

    // login success: uuid, name, property list (verified against capture)
    WriteBuffer ok;
    ok.uuid(self_->uuid.data());
    ok.string(self_->name);
    ok.varint(static_cast<std::int32_t>(self_->loginProps.size()));
    for (const auto& pr : self_->loginProps) {
        ok.string(pr.name);
        ok.string(pr.value);
        ok.boolean(!pr.signature.empty());
        if (!pr.signature.empty()) ok.string(pr.signature);
    }
    conn_->sendPacket(lo::sc::GameProfile, ok);

    for (;;) {
        auto f2 = conn_->readFrame();
        ReadBuffer in2(f2);
        switch (in2.u8()) {
        case lo::cs::LoginAcknowledged:
            state_ = State::Configuration;
            return;
        case lo::cs::CustomQueryAnswer:                  // 0x02 plugin response
            (void)in2.varint();                          // message id
            if (in2.boolean()) {
                const auto n = in2.varint();
                if (n >= 0) in2.bytes(static_cast<std::size_t>(n));
            }
            break;                                       // tolerated, no server use
        case lo::cs::CookieResponse: {                   // 0x04 cookie
            const std::string key = in2.string(constants::kMaxStringLength);
            if (in2.boolean()) {
                const auto len = in2.varint();
                if (len >= 0 && len <= 4096)
                    self_->cookies[key] = in2.bytes(static_cast<std::size_t>(len));
            } else {
                self_->cookies.erase(key);
            }
            break;
        }
        case lo::cs::Hello:                              // 0x00 re-sent start
        case lo::cs::Key:                                // 0x01 late encryption
            in2.skipRest();                              // tolerated (already past)
            break;
        default:
            throw std::runtime_error("unexpected packet during login ack wait");
        }
    }
}
Session::ConfigWaitResult Session::handleOneConfigPacket(ReadBuffer& in) {
    const std::uint8_t kpid = in.u8();
    switch (kpid) {
    case cf::cs::FinishAcknowledgement:
        return ConfigWaitResult::FinishAck;
    case cf::cs::SelectKnownPacks: {
        const std::int32_t n = in.varint();
        for (std::int32_t i = 0; i < n; ++i) {
            (void)in.string();                      // namespace
            (void)in.string();                      // id
            (void)in.string();                      // version
        }
        return ConfigWaitResult::PacksDone;
    }
    case cf::cs::KeepAlive: {                       // echo
        WriteBuffer e; e.raw(in.p + in.off, in.remaining());
        conn_->sendPacket(cf::sc::KeepAlive, e);
        return ConfigWaitResult::Continue;
    }
    case cf::cs::ClientInformation: {               // settings resend: parse & ignore
        (void)in.string();                          // locale
        (void)in.i8();                              // view distance
        (void)in.varint();                          // chat mode
        (void)in.boolean();                         // chat colors
        (void)in.u8();                              // skin parts
        (void)in.varint();                          // main hand
        (void)in.boolean();                         // text filtering
        (void)in.boolean();                         // allow server listings
        return ConfigWaitResult::Continue;
    }
    case cf::cs::CustomPayload: {                   // plugin channels (config)
        const std::string channel = in.string(constants::kMaxStringLength);
        api::ChannelRegistry::Payload body(in.p + in.off, in.p + in.len);
        onPluginPayload(channel, body, 0);
        return ConfigWaitResult::Continue;
    }
    case cf::cs::CookieResponse: {
        const std::string key = in.string(constants::kMaxStringLength);
        if (in.boolean()) {
            const auto len = in.varint();
            self_->cookies[key] = in.bytes(static_cast<std::size_t>(len));
            srv_.storeCookie(self_->uuid, key, self_->cookies[key]);
        } else srv_.eraseCookie(self_->uuid, key);
        return ConfigWaitResult::Continue;
    }
    case cf::cs::ResourcePackResponse:
        (void)in.u8(); (void)in.varint();
        return ConfigWaitResult::Continue;
    case cf::cs::Pong:
        (void)in.i32();
        return ConfigWaitResult::Continue;
    case cf::cs::CustomReportDetails:
    case cf::cs::ServerLinks:
        in.skipRest();                              // tolerated, no server state
        return ConfigWaitResult::Continue;
    default:
        throw std::runtime_error("unexpected packet 0x" + [&]{
            char b[3]; snprintf(b,3,"%02x", kpid); return std::string(b); }() +
            " while awaiting configuration reply");
    }
}

void Session::handleConfiguration() {
    // 1.21.4: AddResourcePack = UUID + url + hash + forced + hasPrompt (no message) — UUID required (strict N13)
    if (!srv_.config().resourcePackUrl.empty()) {
        WriteBuffer b;
        auto packUuid = packUuidFromUrl(srv_.config().resourcePackUrl);
        b.uuid(packUuid.data());
        b.string(srv_.config().resourcePackUrl);
        b.string(srv_.config().resourcePackSha1);
        b.boolean(srv_.config().resourcePackForced);
        b.boolean(false);                              // no prompt message
        conn_->sendPacket(cf::sc::AddResourcePack, b);
    }
    // 1. brand
    {
        WriteBuffer b;
        b.string("minecraft:brand");               // channel
        WriteBuffer payload;
        payload.string("CppFabricMC");
        b.raw(payload.data.data(), payload.data.size());
        conn_->sendPacket(cf::sc::CustomPayload, b);
    }
    // 1b. FeatureFlags 0x0C — vanilla 1.21.4 sends ["minecraft:vanilla"] (docs/SPEC_WIRE.md configuration registry order + feature_flags)
    {
        WriteBuffer b;
        b.varint(1);
        b.string("minecraft:vanilla");
        conn_->sendPacket(cf::sc::FeatureFlags, b);
    }
    // 2. SelectKnownPacks 0x0E — vanilla advertises {minecraft:core 1.21.4} (not empty)
    {
        WriteBuffer b;
        b.varint(1);
        b.string("minecraft");
        b.string("core");
        b.string("1.21.4");
        conn_->sendPacket(cf::sc::SelectKnownPacks, b);
    }
    // 3. wait for the client's SelectKnownPacks answer (server hangs otherwise!)
    bool finishAckEarly = false;
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        auto r = handleOneConfigPacket(in);
        if (r == ConfigWaitResult::PacksDone) break;
        if (r == ConfigWaitResult::FinishAck) finishAckEarly = true;
    }
    if (finishAckEarly)
        std::fprintf(stderr, "[cppfm] %s: early finish-ack during packs wait (tolerated)\n",
                     self_->name.c_str());
    // 4. registry blobs, verbatim wire order — D10 lock: exactly 12 in docs/SPEC_WIRE.md order
    {
        const auto& regs = srv_.data().registries();
        if (regs.size() != EmbeddedData::kRegistrySpec.size()) {
            std::fprintf(stderr, "[Registry] expected %zu registries, got %zu\n",
                EmbeddedData::kRegistrySpec.size(), regs.size());
        }
        // runtime order/count check (EmbeddedData::verifyRegistrySpec already logged)
        srv_.data().verifyRegistrySpec();
        for (const auto& r : regs) {
            WriteBuffer pkt;
            pkt.u8(cf::sc::RegistryData);
            pkt.raw(r.body.data(), r.body.size());
            conn_->sendRawBody(pkt.data);
        }
    }
    // 5. tags (captured verbatim)
    {
        WriteBuffer pkt;
        pkt.u8(cf::sc::UpdateTags);
        pkt.raw(srv_.data().tags().data(), srv_.data().tags().size());
        conn_->sendRawBody(pkt.data);
    }
    conn_->sendPacket(cf::sc::FinishConfiguration, {});
    for (;;) {
        std::vector<std::uint8_t> frame;
        try {
            frame = conn_->readFrameWithTimeout(std::chrono::seconds(30));
        } catch (const SocketClosedError& e) {
            if (e.timedOut) {
                WriteBuffer kick;
                nbt::writeTextComponent(kick, "Took too long to acknowledge configuration");
                conn_->trySendPacket(cf::sc::Disconnect, kick);
            }
            throw;
        }
        ReadBuffer in(frame);
        auto r = handleOneConfigPacket(in);
        if (r == ConfigWaitResult::FinishAck) {
            std::fprintf(stderr, "[cppfm] %s: finish ack at %.2f\n", self_->name.c_str(),
                         std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
            state_ = State::Play;
            onEnterPlay();
            std::fprintf(stderr, "[cppfm] %s: onEnterPlay done\n", self_->name.c_str());
            return;
        }
        // PacksDone here = late select_known_packs retransmit: absorbed.
    }
}
void Session::onEnterPlay() {
    self_->conn = conn_;
    self_->entityId = srv_.nextEntityId();
    self_->lastSeenMs = nowMs();

    // Load the durable dimension/position before emitting Login and the
    // first chunk view.  Loading afterwards advertised the overworld and
    // then silently left the client at the hard-coded fresh-spawn point.
    srv_.loadPlayerData(GameServer::uuidToHex(self_->uuid), *self_);
    self_->dimension = GameServer::canonicalDimension(self_->dimension);
    sendJoinGame();
    srv_.sendServerData(*self_);
    srv_.sendWorldBorderTo(*self_);

    {   // brand again in play phase (vanilla does both)
        WriteBuffer b;
        b.string("minecraft:brand");
        WriteBuffer p;
        p.string("CppFabricMC");
        b.raw(p.data.data(), p.data.size());
        conn_->sendPacket(pl::sc::CustomPayload, b);
    }
    {   // held slot 0
        WriteBuffer b; b.i8(0);
        conn_->sendPacket(pl::sc::SetHeldSlot, b);
    }
    {   // default spawn point
        const auto spawn = srv_.worldFor(self_->dimension).spawnPoint();
        WriteBuffer b;
        b.position(spawn.x, spawn.y, spawn.z);
        b.f32(spawn.angle);
        conn_->sendPacket(pl::sc::SetDefaultSpawn, b);
    }
    sendTeleport(self_->x, self_->y, self_->z, self_->yaw, self_->pitch);

    sendPlayerInfoAddSelf();
    // tell everyone about us / tell us about everyone
    broadcastPlayerInfoAdd(self_.get());
    for (auto& other : srv_.playersSnapshot()) {
        if (other.get() == self_.get()) continue;
        WriteBuffer add;
        add.u8(0x01 | 0x08);                       // add_player | update_listed
        add.varint(1);
        add.uuid(other->uuid.data());
        add.string(other->name);
        add.varint(0);                             // props
        add.varint(1);                             // listed
        conn_->sendPacket(pl::sc::PlayerInfoUpdate, add);
    }

    registered_ = true;
    srv_.addPlayer(self_);
    self_->inPlay = true;
    self_->gamemode = 1;   // creative default for building comfort
    self_->health = 20; self_->food = 20; self_->saturation = 5;
    self_->exhaustion = 0; self_->fallDist = 0; self_->dead = false;
    if (self_->enchantmentSeed == 0) {
        self_->enchantmentSeed = static_cast<std::int32_t>(self_->entityId * 0x9e3779b9u ^ srv_.config().hashedSeed ^ 0x27d4eb2du);
        if (self_->enchantmentSeed == 0) self_->enchantmentSeed = 0x5a5a5a5a;
    }

    sendAbilities();
    self_->prevFeetY = self_->y;

    api::PlayerJoinEvent jev;
    jev.player = self_.get();
    srv_.events().join.fire(jev);
    if (srv_.jvmRuntime()) srv_.jvmRuntime()->onPlayerJoin(*self_);
    srv_.initPlayerProgress(*self_);
    srv_.sendAdvancementsTo(*self_, true);

    broadcastSpawnEntity(self_.get());
    sendDeclareCommands();
    sendRecipeBook();
    srv_.sendSetExperience(*self_);

    sendStarterInventory();
    {   // health (creative ignores but harmless)
        WriteBuffer b;
        b.f32(20.f); b.varint(20); b.f32(5.f);
        conn_->sendPacket(pl::sc::SetHealth, b);
    }

    srv_.broadcastSystemText((msg::kYellow + self_->name + " joined the game"), nullptr);
    {
        std::vector<std::string> all;
        for (auto& pl : srv_.playersSnapshot()) all.push_back(pl->name);
        // action 2 = set for self
        srv_.sendChatSuggestions(*self_, 2, all);
        // action 0 = add for others
        std::vector<std::string> one{self_->name};
        srv_.broadcastChatSuggestions(0, one, self_.get());
    }
    sendSystemText((msg::kGray + "Welcome to " + msg::kAqua + "CppFabricMC" + msg::kGray + "! Build with the hotbar, chat freely."));
    if (srv_.bossAI()) srv_.bossAI()->onPlayerJoin(*self_);
}
void Session::sendDeclareCommands() {
    WriteBuffer b;
    // Strict 1.21.4: serialize the full Brigadier dispatcher tree (not minimal 3-node stub). Commands.cpp builds 20+ commands via
    // initCommands(); dispatcher.writeDeclareCommands emits flattened nodes with parser ids 0-53 matching protocol.json (N9/N10).
    srv_.commands().writeDeclareCommands(b);
    conn_->sendPacket(pl::sc::DeclareCommands, b);
}
void Session::handleRespawnRequest() {
    const std::int8_t targetDimension = self_->hasRespawnPoint
                                            ? GameServer::canonicalDimension(
                                                  self_->respawnDimension)
                                            : 0;
    const auto& targetWorld = srv_.worldFor(targetDimension);
    const auto worldSpawn = targetWorld.spawnPoint();
    const std::int32_t spawnX = self_->hasRespawnPoint
                                    ? self_->respawnX : worldSpawn.x;
    const std::int32_t spawnY = self_->hasRespawnPoint
                                    ? self_->respawnY : worldSpawn.y;
    const std::int32_t spawnZ = self_->hasRespawnPoint
                                    ? self_->respawnZ : worldSpawn.z;
    const float spawnAngle = self_->hasRespawnPoint
                                 ? self_->respawnAngle : worldSpawn.angle;
    self_->dimension = targetDimension;
    self_->x = spawnX + 0.5;
    self_->y = spawnY;
    self_->z = spawnZ + 0.5;
    self_->yaw = spawnAngle;
    self_->pitch = 0.f;
    self_->dead = false;
    self_->health = 20; self_->food = 20; self_->saturation = 5;
    self_->fallDist = 0;
    self_->fireTicks = 0;
    self_->freezeTicks = 0;
    self_->sleeping = false;
    WriteBuffer ws = makeWorldState(srv_.config(), targetWorld, self_->gamemode);
    WriteBuffer b;
    b.raw(ws.data.data(), ws.data.size());
    b.u8(0x03);                                    // keep metadata + attributes
    conn_->sendPacket(pl::sc::Respawn, b);
    {   // re-sync position & vitals
        WriteBuffer hp;
        hp.f32(20.f); hp.varint(20); hp.f32(5.f);
        conn_->sendPacket(pl::sc::SetHealth, hp);
    }
    {
        WriteBuffer point;
        point.position(spawnX, spawnY, spawnZ);
        point.f32(spawnAngle);
        conn_->sendPacket(pl::sc::SetDefaultSpawn, point);
    }
    sendTeleport(self_->x, self_->y, self_->z, self_->yaw, self_->pitch);
}
void Session::sendJoinGame() {
    const ServerConfig& c = srv_.config();
    const World& world = srv_.worldFor(self_->dimension);
    WriteBuffer b;
    b.i32(self_->entityId);
    b.boolean(c.hardcore);                         // hardcore
    b.varint(3);                                   // worlds[]
    b.string("minecraft:overworld");
    b.string("minecraft:the_nether");
    b.string("minecraft:the_end");
    b.varint(c.maxPlayers);
    b.varint(effectiveViewDistance(c));
    b.varint(std::clamp(std::min(c.simulationDistance, 12), 2, 12));
    b.boolean(false);                              // reduced debug
    b.boolean(true);                               // respawn screen
    b.boolean(false);                              // do limited crafting
    // SpawnInfo
    {
        WriteBuffer ws = makeWorldState(c, world, self_->gamemode);
        b.raw(ws.data.data(), ws.data.size());
    }
    b.boolean(false);                              // enforces secure chat
    conn_->sendPacket(pl::sc::Login, b);
    WriteBuffer vd;
    vd.varint(effectiveViewDistance(c));
    conn_->sendPacket(pl::sc::UpdateViewDistance, vd);
}
void Session::sendAbilities() {
    std::uint8_t f = 0;
    if (self_->gamemode == 1) f |= 0x01 | 0x04 | 0x08;   // creative
    else if (self_->gamemode == 3) f |= 0x02 | 0x04;     // spectator flies
    if (self_->isFlying && !(f & 0x04)) self_->isFlying = false; // revoke unpermitted flight
    if (self_->isFlying) f |= 0x02;
    WriteBuffer b;
    b.i8(static_cast<std::int8_t>(f));
    b.f32(0.05f);                                       // flyingSpeed (vanilla default)
    b.f32(0.10f);                                       // walkingSpeed (vanilla default)
    conn_->sendPacket(pl::sc::Abilities, b);
}
void Session::sendSignBlockEntity(std::int32_t x, std::int32_t y, std::int32_t z) {
    bool hanging = false;
    {
        World& w = srv_.worldFor(self_->dimension);
        const auto* d = gen::blockByState(w.getBlock(x, y, z));
        if (!d || d->name.find("sign") == std::string::npos) {
            std::fprintf(stderr, "[cppfm] sign resend at %d,%d,%d ignored (not a sign block)\n",
                         x, y, z);
            return;
        }
        hanging = (d->name.find("hanging_sign") != std::string::npos);
    }
    auto beOwner = srv_.blockEntitiesFor(self_->dimension).getShared(posKey(x, y, z));
    const BlockEntity* be = beOwner.get();
    if (!be) return;
    std::lock_guard entityLock(*be->stateMtx);
    if (be->kind != BlockEntity::Kind::Sign) return;
    WriteBuffer b;
    b.position(x, y, z);
    b.varint(hanging ? 8 : 7);
    nbt::Writer w(b);
    w.rootCompound();
    w.namedString("id", hanging ? "minecraft:hanging_sign" : "minecraft:sign");
    w.namedInt("x", x); w.namedInt("y", y); w.namedInt("z", z);
    w.namedByte("is_waxed", 0);
    auto side = [&](const char* key, const std::string lines[4]) {
        w.beginCompound(key);
        w.beginList("messages", nbt::String, 4);
        for (int i = 0; i < 4; ++i) w.bareString(lines[i]);
        w.namedString("color", "black");
        w.namedByte("has_glowing_text", 0);
        w.endCompound();
    };
    side("front_text", be->sign.front);
    side("back_text", be->sign.back);
    w.endCompound();
    conn_->trySendPacket(pl::sc::BlockEntityData, b);
    srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
                                          pl::sc::BlockEntityData, b);
}
void Session::applyClientSettings(Player::ClientSettings s) {
    if (s.locale.empty()) s.locale = "en_us";
    self_->clientSettings = s;
    WriteBuffer b;
    b.varint(effectiveViewDistance(srv_.config(), s.viewDistance));
    conn_->trySendPacket(pl::sc::UpdateViewDistance, b);
}
bool Session::requireOp(int level, const char* what) {
    (void)level; // levels collapse to ops.json membership + creative (documented)
    if (srv_.isOp(self_->name) && self_->gamemode == 1) return true;
    std::fprintf(stderr, "[cppfm] %s: %s denied for %s (op/creative gate)\n",
                 self_->name.c_str(), what,
                 srv_.isOp(self_->name) ? "non-creative" : "non-op");
    return false;
}
void Session::sendTagQueryResponse(std::int32_t transactionId, const WriteBuffer& nbt) {
    WriteBuffer b;
    b.varint(transactionId);
    b.raw(nbt.data.data(), nbt.data.size());
    conn_->trySendPacket(pl::sc::TagQueryResponse, b);
}
void Session::answerBlockNbt(std::int32_t transactionId, std::int32_t x, std::int32_t y, std::int32_t z) {
    auto beOwner = srv_.blockEntitiesFor(self_->dimension).getShared(posKey(x, y, z));
    const BlockEntity* be = beOwner.get();
    WriteBuffer nbt;
    if (be) {
        std::lock_guard entityLock(*be->stateMtx);
        if (be->kind == BlockEntity::Kind::Sign) {
            // Real data path: signs carry their text (same shape as the 0x07 resend).
            World& w = srv_.worldFor(self_->dimension);
            const auto* d = gen::blockByState(w.getBlock(x, y, z));
            const bool hanging = d && d->name.find("hanging_sign") != std::string::npos;
            nbt::Writer wr(nbt);
            wr.rootCompound();
            wr.namedString("id", hanging ? "minecraft:hanging_sign" : "minecraft:sign");
            wr.namedInt("x", x); wr.namedInt("y", y); wr.namedInt("z", z);
            auto side = [&](const char* key, const std::string lines[4]) {
                wr.beginCompound(key);
                wr.beginList("messages", nbt::String, 4);
                for (int i = 0; i < 4; ++i) wr.bareString(lines[i]);
                wr.namedString("color", "black");
                wr.namedByte("has_glowing_text", 0);
                wr.endCompound();
            };
            side("front_text", be->sign.front);
            side("back_text", be->sign.back);
            wr.endCompound();
        }
    }
    if (nbt.data.empty()) {
        // Other block entities have no NBT serializer yet — echo the
        // transaction with an empty compound (documented stub, docs/SPEC_WIRE.md).
        nbt::Writer wr(nbt);
        wr.rootCompound();
        wr.endCompound();
    }
    sendTagQueryResponse(transactionId, nbt);
}
void Session::answerEntityNbt(std::int32_t transactionId, std::int32_t entityId) {
    (void)entityId; // entity NBT serialization deferred — echo empty (docs/SPEC_WIRE.md)
    WriteBuffer nbt;
    nbt::Writer wr(nbt);
    wr.rootCompound();
    wr.endCompound();
    sendTagQueryResponse(transactionId, nbt);
}
// and recomputes the output exactly like a grid click does (live sync).
void Session::onNameItem(const std::string& name) {
    const std::string boundedName =
        name.size() > constants::kMaxRenameLength
            ? name.substr(0, constants::kMaxRenameLength) : name;
    std::shared_ptr<Menu> menu;
    std::shared_ptr<BlockEntity> menuOwner;
    {
        std::lock_guard stateLock(self_->stateMtx);
        if (!openMenu_ || openMenu_->type != MenuType::Anvil) return;
        menu = openMenu_;
        menuOwner = menu->blockEntityOwner;
    }

    std::unique_lock<std::recursive_mutex> entityLock;
    if (menuOwner && menuOwner->stateMtx)
        entityLock = std::unique_lock<std::recursive_mutex>(*menuOwner->stateMtx);
    std::unique_lock playerLock(self_->stateMtx);
    if (openMenu_.get() != menu.get() || menu->type != MenuType::Anvil) return;

    menu->anvilRename = boundedName;
    if (auto* logic = getMenuLogic(menu->type))
        logic->onContentChanged(*menu, *self_);
    const std::string rename = menu->anvilRename;
    const int cost = CostCalculator::anvilCost(
        menu->extraSlots[0], menu->extraSlots[1], rename);
    const bool tooExpensive = CostCalculator::isTooExpensive(
        cost, self_->gamemode == 1);
    if (!menu->extraSlots[0].empty() && cost > 0 && !tooExpensive) {
        menu->extraSlots[2] = menu->extraSlots[0];
        menu->extraSlots[2].setRepairCost(CostCalculator::nextRepairCost(
            menu->extraSlots[0], menu->extraSlots[1]));
        if (!rename.empty()) menu->extraSlots[2].setCustomName(rename);
    } else {
        menu->extraSlots[2] = ItemStack::air();
    }

    WriteBuffer property;
    property.varint(menu->windowId);
    property.i16(0);
    property.i16(static_cast<std::int16_t>(cost < 0 ? 0 : cost));
    playerLock.unlock();
    if (entityLock.owns_lock()) entityLock.unlock();

    // Packet order remains property -> full content -> cursor, while all
    // packets are emitted outside the model locks.
    conn_->trySendPacket(pl::sc::ContainerSetData, property);
    sendMenuContent(*menu);
    syncCursorItem();
}
// jump_boost,strength} + secondary regeneration (or primary for tier II).
void Session::onBeaconEffect(std::optional<std::int32_t> primary,
                            std::optional<std::int32_t> secondary) {
    static const std::int32_t kPrimaries[] = {1, 3, 11, 8, 5};
    auto validPrimary = [](std::int32_t v) {
        for (auto p : kPrimaries) if (p == v) return true;
        return false;
    };
    if (primary && !validPrimary(*primary)) {
        std::fprintf(stderr, "[cppfm] beacon primary %d rejected (unknown effect)\n", *primary);
        return;
    }
    if (secondary && *secondary != 10 && (!primary || *secondary != *primary)) {
        std::fprintf(stderr, "[cppfm] beacon secondary %d rejected\n", *secondary);
        return;
    }
    self_->beaconPrimary = primary;
    self_->beaconSecondary = secondary;
    // Live effect: beacon buffs re-apply while in range; grant locally with a
    // 30s duration (refresh-on-confirm, same EntityEffect path as /effect).
    const std::int32_t eff = secondary.value_or(primary.value_or(-1));
    if (eff < 0) return;
    EffectInstance e;
    e.type = static_cast<std::uint8_t>(eff);
    e.amplifier = (secondary && primary && *secondary == *primary) ? 1 : 0;
    e.durationTicks = 30 * 20;
    self_->effects.erase(std::remove_if(self_->effects.begin(), self_->effects.end(),
        [&](const EffectInstance& x) { return x.type == e.type; }), self_->effects.end());
    self_->effects.push_back(e);
    WriteBuffer b;
    b.varint(self_->entityId);
    b.varint(e.type);
    b.varint(e.amplifier);
    b.varint(e.durationTicks);
    b.u8(effectFlags(e));
    self_->conn->trySendPacket(proto::pl::sc::EntityEffect, b);
}
void Session::onSpectate(const std::array<std::uint8_t,16>& target) {
    if (self_->gamemode != 3) {
        std::fprintf(stderr, "[cppfm] spectate from non-spectator %s ignored\n",
                     self_->name.c_str());
        return;
    }
    self_->spectateTarget = target;
    self_->hasSpectateTarget = true;
    for (auto& other : srv_.playersSnapshot()) {
        if (other->uuid != target) continue;
        sendTeleport(other->x, other->y, other->z, self_->yaw, self_->pitch);
        WriteBuffer cam;
        cam.varint(other->entityId);
        conn_->trySendPacket(pl::sc::Camera, cam);
        return;
    }
    std::fprintf(stderr, "[cppfm] spectate target not found (mob UUIDs unsupported)\n");
}
void Session::sendTeleport(double x, double y, double z, float yaw, float pitch) {
    self_->x = x; self_->y = y; self_->z = z;
    self_->yaw = yaw; self_->pitch = pitch;
    WriteBuffer b;
    b.varint(++teleportId_);
    b.f64(x); b.f64(y); b.f64(z);
    b.f64(0); b.f64(0); b.f64(0);                  // velocity
    b.f32(yaw); b.f32(pitch);
    b.u32(0);                                      // relatives flags: absolute all
    conn_->sendPacket(pl::sc::PlayerPosition, b);
}
void Session::broadcastSpawnEntity(Player* about) {
    WriteBuffer b = makeSpawnEntity(*about);
    srv_.broadcastPacketExceptInDimension(about->dimension, about,
                                          pl::sc::SpawnEntity, b);
    sendSkinMetadata(*about, about->entityId);
    // also tell the newcomer about everyone else
    for (auto& other : srv_.playersSnapshot()) {
        if (other.get() == about || !other->inPlay ||
            other->dimension != about->dimension) continue;
        WriteBuffer ob = makeSpawnEntity(*other);
        about->conn->trySendPacket(pl::sc::SpawnEntity, ob);
        sendSkinMetadata(*about, other->entityId);
    }
}
void Session::sendPlayerInfoAddSelf() {
    WriteBuffer add;
    add.u8(0x01 | 0x04 | 0x08);                    // add_player | update_game_mode | update_listed
    add.varint(1);
    add.uuid(self_->uuid.data());
    add.string(self_->name);
    add.varint(0);                                 // properties
    add.varint(self_->gamemode);
    add.varint(1);                                 // listed
    conn_->sendPacket(pl::sc::PlayerInfoUpdate, add);
}
void Session::broadcastPlayerInfoAdd(Player* about) {
    WriteBuffer add;
    add.u8(0x01 | 0x04 | 0x08);
    add.varint(1);
    add.uuid(about->uuid.data());
    add.string(about->name);
    add.varint(0);
    add.varint(about->gamemode);
    add.varint(1);
    srv_.broadcastPacketExcept(about, pl::sc::PlayerInfoUpdate, add);
}
void Session::sendStarterInventory() {
    WriteBuffer b;
    std::shared_ptr<Connection> connection = conn_;
    std::unique_lock playerLock(self_->stateMtx);
    std::unique_lock packetLock(self_->inventoryPacketMtx);
    // build inventory model from starter kit
    for (auto& s2 : self_->inv) { s2.itemId = 0; s2.count = 0; }
    {
        int hot = 36;
        for (auto& e : kKit) {
            auto ii = gen::itemIdByName().find(e.name);
            if (ii == gen::itemIdByName().end()) continue;
            if (hot < 45) { self_->inv[hot] = InvSlot::of(ii->second, static_cast<std::int16_t>(e.cnt)); ++hot; }
        }
    }
    b.varint(0);                                       // window id: player inventory
    b.varint(++self_->invStateId);
    b.varint(46);                                      // slots
    for (int i = 0; i < 46; ++i) self_->inv[i].write(b);
    ItemStack::air().write(b);                         // carried item
    playerLock.unlock();
    connection->sendPacket(pl::sc::ContainerSetContent, b);
}
void Session::onWindowClick(ReadBuffer& in) {
    // Strict 1.21.4 (protocol 769) : `window_click` 0x10 windowId VarInt + stateId VarInt (I12).
    int windowId = 0;
    int stateId = 0;
    size_t mark = in.off;
    try {
        windowId = in.varint();
        stateId = in.varint();
    } catch (...) {
        in.off = mark;
        try {
            windowId = in.u8();
            stateId = in.varint();
        } catch (...) {
            in.off = mark;
            try { windowId = in.varint(); } catch (...) { return; }
            try { stateId = in.varint(); } catch (...) { stateId = 0; }
        }
    }
    const auto slotIdx = in.i16();
    const auto button = in.i8();
    const auto mode = in.varint();

    // changed slots array (client prediction; we recompute server-side)
    const auto nChanged = in.varint();
    if (nChanged < 0 || nChanged > 1024)
        throw std::runtime_error("window click changed-slot count out of range");
    for (std::int32_t i = 0; i < nChanged; ++i) {
        (void)in.i16();
        ItemStack::read(in);
    }
    ItemStack::read(in); // discard client cursor (server-authoritative)

    // Only hold stateMtx while deciding which authoritative view applies.
    // Menu handling and every resend below take their own short model
    // snapshot and perform network I/O after releasing the lock.  Keeping
    // this dispatcher lock-free across those calls also prevents a stale
    // click from acquiring the player lock and then waiting on a socket.
    std::shared_ptr<Menu> menu;
    bool playerWindow = false;
    bool staleRevision = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        staleRevision = stateId != self_->invStateId;
        if (windowId != 0 && openMenu_ && openMenu_->windowId == windowId) {
            menu = openMenu_;
        } else if (windowId == 0) {
            playerWindow = true;
        }
    }

    // The revision is the server's optimistic-concurrency guard.  Applying a
    // click built from an older menu snapshot can duplicate/lose items when a
    // tick or another packet has already changed the inventory.  Send the
    // current authoritative view and leave the click unapplied.
    if (staleRevision) {
        if (menu) sendMenuContent(*menu);
        else if (playerWindow) srv_.resendInventory(*self_);
        syncCursorItem();
        return;
    }

    if (menu) {
        handleMenuClick(*menu, slotIdx, button, mode);
        srv_.syncEquipmentOnChange(*self_);
        return;
    }
    if (playerWindow) {
        handlePlayerInventoryClick(slotIdx, button, mode);
        srv_.syncEquipmentOnChange(*self_);
    }
}

void Session::handlePlayerInventoryClick(int slot, int button, int mode) {
    // Player inventory is represented by Player::inv for persistence and all
    // non-screen callers.  The screen adapter owns only the 2x2 recipe grid
    // and cached result, then copies those five protocol slots back before the
    // authoritative ContainerSetContent snapshot is sent.
    std::unique_lock playerLock(self_->stateMtx);
    Menu& menu = playerInventoryMenu_;
    menu.type = MenuType::Crafting;
    menu.playerInventory = true;
    menu.blockKey = -1;
    menu.container = nullptr;
    menu.containerCount = 0;
    menu.blockEntity = nullptr;
    menu.blockEntityOwner.reset();
    for (auto& stack : menu.craftGrid) stack = ItemStack::air();
    for (int i = 0; i < 4; ++i)
        menu.craftGrid[menu.craftGridIndex(i + 1)] =
            self_->inv[static_cast<std::size_t>(i + 1)];
    menu.craftResult = self_->inv[0];
    menu.refreshCraftResult(srv_.recipes());

    SessionMenuIo io(*this);
    (void)ClickLogic::apply(menu, *self_, srv_.recipes(), slot, button, mode,
                            cursorItem_, io);
    for (int i = 0; i < 4; ++i)
        self_->inv[static_cast<std::size_t>(i + 1)] =
            menu.craftGrid[menu.craftGridIndex(i + 1)];
    self_->inv[0] = menu.craftResult;
    playerLock.unlock();

    io.flush();
    srv_.resendInventory(*self_);
    syncCursorItem();
}
void Session::onEnchantItem(ReadBuffer& in) {
    // Packet `enchant_item` 0x0F: `windowId` VarInt (protocol.json 1.21.4, strict) + `button` VarInt.
    // Yarn `EnchantmentScreenHandler` uses VarInt for windowId; retain u8 fallback for leniency (vanilla client sends VarInt, some proxies u8).
    int windowId = 0;
    int button = 0;
    const std::size_t mark = in.off;
    try {
        windowId = in.varint();
        if (in.remaining() > 0) button = in.varint();
        else button = 0;
    } catch (...) {
        in.off = mark;
        try { windowId = in.u8(); button = in.u8(); } catch(...) { return; }
    }
    std::shared_ptr<Menu> menu;
    std::shared_ptr<BlockEntity> menuOwner;
    std::int64_t blockKey = -1;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_ || openMenu_->windowId != windowId ||
            openMenu_->type != MenuType::Enchantment)
            return;
        menu = openMenu_;
        menuOwner = menu->blockEntityOwner;
        blockKey = menu->blockKey;
        dimension = self_->dimension;
    }

    // The bookshelf scan only needs the immutable menu position and world;
    // do it before acquiring the player lock so a world/chunk read cannot
    // block a concurrent player mutation while holding stateMtx.
    int bookshelves = 0;
    if (blockKey != -1) {
        const int bx = posKeyUnpackX(blockKey);
        const int by = posKeyUnpackY(blockKey);
        const int bz = posKeyUnpackZ(blockKey);
        bookshelves = CostCalculator::countBookshelves(
            srv_.worldFor(dimension), bx, by, bz);
    }

    std::unique_lock<std::recursive_mutex> entityLock;
    if (menuOwner && menuOwner->stateMtx)
        entityLock = std::unique_lock<std::recursive_mutex>(*menuOwner->stateMtx);
    std::unique_lock playerLock(self_->stateMtx);
    if (openMenu_.get() != menu.get() || menu->windowId != windowId ||
        menu->type != MenuType::Enchantment)
        return;
    auto* logic = getMenuLogic(MenuType::Enchantment);
    auto* ench = logic ? dynamic_cast<EnchantmentMenuLogic*>(logic) : nullptr;
    if (!ench) return;

    ItemStack* input = menu->container ? &menu->container[0]
                                       : &menu->extraSlots[0];
    ItemStack* lapis = menu->container ? &menu->container[1]
                                       : &menu->extraSlots[1];
    const auto lapisId = gen::itemIdByName().find("minecraft:lapis_lazuli");
    if (button < 0 || button >= 3 || input->empty() || lapis->empty() ||
        lapisId == gen::itemIdByName().end() || lapis->itemId != lapisId->second)
        return;

    const auto offers = ench->offers(*menu, *self_, bookshelves);
    const EnchantmentOffer& choice =
        offers[static_cast<std::size_t>(button)];
    if (!choice.selectable() || lapis->count < choice.lapisCost)
        return;
    const int requiredLevel = button + 1;
    if (self_->gamemode == 0 &&
        (self_->xp.level < requiredLevel || self_->xp.level < choice.levelCost))
        return;

    // Apply the presented choice while the menu and player model are
    // serialized.  This is intentionally equivalent to
    // EnchantmentMenuLogic::onEnchantButton, but keeps its experience packet
    // out of the critical section.
    const std::string enchantedItemName = input->name();
    const int consumedLapis = choice.lapisCost;
    *input = choice.result;
    lapis->count -= choice.lapisCost;
    if (lapis->count <= 0) *lapis = ItemStack::air();
    const bool experienceChanged = self_->gamemode == 0;
    if (experienceChanged)
        self_->xp.level = std::max(0, self_->xp.level - choice.levelCost);
    std::uint32_t nextSeed = CostCalculator::splitmix32(
        static_cast<std::uint32_t>(self_->enchantmentSeed) +
        0x9e3779b9u + static_cast<std::uint32_t>(button));
    if (nextSeed == 0) nextSeed = 0x5a5a5a5au;
    self_->enchantmentSeed = static_cast<std::int32_t>(nextSeed);
    const std::int32_t enchantedWindowId = menu->windowId;

    SessionMenuIo io(*this);
    io.blockEntityChanged(menu->blockKey);
    playerLock.unlock();
    if (entityLock.owns_lock()) entityLock.unlock();

    if (experienceChanged) GameServer::sendSetExperience(*self_);
    io.flush();
    // Advancement/JVM hooks may synchronously close or replace the menu.
    if (!enchantedItemName.empty())
        srv_.onItemEnchanted(*self_, enchantedItemName, consumedLapis);
    bool sameMenu = false;
    {
        std::lock_guard stateLock(self_->stateMtx);
        sameMenu = openMenu_.get() == menu.get() &&
                   openMenu_->windowId == enchantedWindowId;
    }
    if (sameMenu) {
        sendMenuContent(*menu);
        syncCursorItem();
    }
}
void Session::onTabComplete(ReadBuffer& in) {
    const auto transactionId = in.varint();
    const std::string text = in.string(65536);

    brigadier::CommandSource src;
    src.player = self_.get();
    src.name = self_->name;
    src.console = false;
    src.srcX = self_->x; src.srcY = self_->y; src.srcZ = self_->z;
    srv_.bindCommandSelector(src);

    std::string query = text;
    if (!query.empty() && query[0] == '/') query = query.substr(1);
    const auto suggestions = srv_.commands().suggest(query, std::move(src));

    // Strict token start: replace only the current token, not whole line.
    // Vanilla CommandSuggestions range is [start, start+length) covering the token being completed.
    std::int32_t start = 0;
    if (!text.empty()) {
        // find last space — token starts after it
        std::size_t lastSpace = text.rfind(' ');
        if (lastSpace != std::string::npos) {
            if (lastSpace + 1 >= text.size()) start = static_cast<std::int32_t>(text.size());
            else start = static_cast<std::int32_t>(lastSpace + 1);
        } else {
            // no space: for "/" prefixed commands, token starts after '/'
            if (text[0] == '/') start = 1;
            else start = 0;
        }
        // also handle trailing spaces already covered; for quoted or colon-separated
        // resource locations we keep the space-based token (vanilla includes "minecraft:" prefix).
    }
    std::int32_t length = static_cast<std::int32_t>(text.size()) - start;
    if (length < 0) length = 0;
    WriteBuffer b;
    b.varint(transactionId);
    b.varint(start);
    b.varint(length);
    b.varint(static_cast<std::int32_t>(suggestions.size()));
    for (auto& [match, tooltip] : suggestions) {
        b.string(match);
        b.boolean(false);
    }
    conn_->trySendPacket(pl::sc::CommandSuggestions, b);
}
void Session::sendSetSlot(std::int32_t windowId, std::int32_t stateId,
                          std::int16_t slot, const ItemStack& s) {
    WriteBuffer b;
    b.varint(windowId);
    b.varint(stateId);
    b.i16(slot);
    s.write(b);
    std::lock_guard packetLock(self_->inventoryPacketMtx);
    conn_->trySendPacket(pl::sc::ContainerSetSlot, b);
}
void Session::syncCursorItem() {
    WriteBuffer b;
    cursorItem_.write(b);
    std::lock_guard packetLock(self_->inventoryPacketMtx);
    conn_->trySendPacket(pl::sc::SetCursorItem, b);
}
void Session::handleMenuClick(Menu& m, int slot, int button, int mode) {
    Menu* const menuPtr = &m;
    const std::int32_t menuWindowId = m.windowId;
    // A menu backed by a block entity must be updated atomically with the
    // player's inventory, but the entity lock is never held during packet
    // I/O or deferred callbacks.  Keep one order everywhere in this file:
    // block entity -> player.
    auto entityLock = lockMenuBlockEntity(m);
    std::unique_lock playerLock(self_->stateMtx);
    SessionMenuIo io(*this);
    std::vector<WriteBuffer> extraPackets;
    bool experienceChanged = false;

    const auto menuStillOpen = [&]() {
        std::lock_guard playerStateLock(self_->stateMtx);
        return openMenu_ && openMenu_.get() == menuPtr &&
               openMenu_->windowId == menuWindowId;
    };
    const auto releaseModelLocks = [&]() {
        if (playerLock.owns_lock()) playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
    };
    const auto finish = [&]() {
        releaseModelLocks();
        // MenuIo callbacks are extension/world work and must run after both
        // model locks have been released.  They may also close or replace the
        // menu, so all subsequent packet emission is guarded by
        // menuStillOpen().
        if (experienceChanged) GameServer::sendSetExperience(*self_);
        io.flush();
        if (!menuStillOpen()) return;
        sendMenuContent(*menuPtr);
        if (!menuStillOpen()) return;
        syncCursorItem();
        if (!menuStillOpen()) return;
        for (const auto& packet : extraPackets)
            conn_->trySendPacket(pl::sc::ContainerSetData, packet);
    };

    const auto giveOutput = [&](const ItemStack& output) {
        if (output.empty()) return false;
        if (cursorItem_.empty()) {
            cursorItem_ = output;
            return true;
        }
        if (sameInventoryStack(cursorItem_, output)) {
            const int limit = maxStackFor(output);
            if (cursorItem_.count <= limit - output.count) {
                cursorItem_.count = static_cast<std::int16_t>(
                    cursorItem_.count + output.count);
                return true;
            }
        }
        auto trial = self_->inv;
        if (!insertCompleteInventoryStack(trial, output)) return false;
        self_->inv = std::move(trial);
        return true;
    };

    // Stonecutter output take (slot 1) - consume input, give result
    if (m.type == MenuType::Stonecutter && slot == 1 && mode == 0 && button == 0) {
        ItemStack* inp = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* out = m.container ? &m.container[1] : &m.extraSlots[1];
        if (!out->empty() && !inp->empty()) {
            if (!giveOutput(*out)) return;
            if (--inp->count <= 0) *inp = ItemStack::air();
            if (!inp->empty()) {
                const Recipe* r = srv_.recipes().findStonecutting(inp->itemId);
                if (r) *out = r->result;
                else *out = ItemStack::air();
            } else *out = ItemStack::air();
            finish();
            return;
        }
    }
    if (m.type == MenuType::Anvil && slot == 2 && mode == 0 && button == 0) {
        ItemStack* out = &m.extraSlots[2];
        if (!out->empty()) {
            std::string rename = m.anvilRename;
            int cost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], rename);
            if (cost < 0) cost = 0;
            bool tooExp = CostCalculator::isTooExpensive(cost, self_->gamemode==1);
            if ((self_->xp.level >= cost || self_->gamemode == 1) && cost > 0 && !tooExp) {
                if (!giveOutput(*out)) return;
                if (self_->gamemode == 0) {
                    self_->xp.level -= cost;
                    experienceChanged = true;
                }
                if (--m.extraSlots[0].count <= 0) m.extraSlots[0] = ItemStack::air();
                if (!m.extraSlots[1].empty() && --m.extraSlots[1].count <= 0) m.extraSlots[1] = ItemStack::air();
                *out = ItemStack::air();
                // refresh cost
                int newCost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], m.anvilRename);
                WriteBuffer pb;
                pb.varint(m.windowId);
                pb.i16(0);
                pb.i16(static_cast<std::int16_t>(newCost < 0 ? 0 : newCost));
                extraPackets.push_back(std::move(pb));
                finish();
                return;
            }
        }
    }
    // Yarn `CartographyTableScreenHandler` slots: 0 map, 1 paper, 2 result (filled_map clone). Take result consumes paper.
    if (m.type == MenuType::CartographyTable && slot == 2 && mode == 0 && button == 0) {
        ItemStack* mapIn = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* paperIn = m.container ? &m.container[1] : &m.extraSlots[1];
        ItemStack* out = m.container ? &m.container[2] : &m.extraSlots[2];
        if (!out->empty() && !mapIn->empty() && !paperIn->empty()) {
            // Validate map duplication recipe: filled_map + paper -> filled_map clone
            bool isFilledMap = out->name() == "minecraft:filled_map" || mapIn->name() == "minecraft:filled_map";
            bool isPaper = paperIn->name() == "minecraft:paper";
            if (isFilledMap && isPaper) {
                if (!giveOutput(*out)) return;
                if (--paperIn->count <= 0) *paperIn = ItemStack::air();
                // map slot is not consumed (vanilla duplicates map, not consumes); vanilla keeps map and only consumes paper
                // Duplicate output is single map copy already given; clear output and recompute
                *out = ItemStack::air();
                if (!mapIn->empty() && !paperIn->empty()) {
                    // recompute output: clone map (preserve components like map_id)
                    *out = *mapIn;
                    out->count = 1;
                }
                finish();
                return;
            }
        }
    }
    if (auto* logic = getMenuLogic(m.type)) {
        // Check if click is within container region; let logic handle it, fallback to generic for player inv
        int cont = m.totalSlots() - 36;
        if (slot >=0 && slot < cont) {
            bool handled = logic->onSlotClick(m, *self_, slot, button, mode, cursorItem_, io, srv_.recipes());
            if (handled) {
                logic->onContentChanged(m, *self_);
                if (m.type == MenuType::Crafting) m.refreshCraftResult(srv_.recipes());
                finish();
                return;
            }
        }
    }
    // crafting result refresh before interaction
    m.refreshCraftResult(srv_.recipes());
    (void)ClickLogic::apply(m, *self_, srv_.recipes(),
                            slot, button, mode, cursorItem_, io);
    if (m.type == MenuType::Crafting) m.refreshCraftResult(srv_.recipes());
    // Also notify MenuLogic of content change for result recomputation (e.g., Anvil)
    if (auto* logic2 = getMenuLogic(m.type)) logic2->onContentChanged(m, *self_);
    // Recompute dependent result slots before taking the authoritative menu
    // snapshot.  The previous order emitted a full snapshot and then a
    // separate slot packet, which allowed a newer revision to overtake that
    // slot packet on a concurrent send path.
    if (m.type == MenuType::Stonecutter) {
        ItemStack* inp = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* out = m.container ? &m.container[1] : &m.extraSlots[1];
        if (!inp->empty()) {
            const Recipe* r = srv_.recipes().findStonecutting(inp->itemId);
            if (r) *out = r->result;
            else *out = ItemStack::air();
        } else {
            *out = ItemStack::air();
        }
    }
    // vanilla: filled_map + paper -> filled_map copy (count 1); paper consumed on take, map preserved
    if (m.type == MenuType::CartographyTable) {
        ItemStack* mapIn = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* paperIn = m.container ? &m.container[1] : &m.extraSlots[1];
        ItemStack* out = m.container ? &m.container[2] : &m.extraSlots[2];
        bool canClone = false;
        if (!mapIn->empty() && !paperIn->empty()) {
            std::string mn = mapIn->name();
            std::string pn = paperIn->name();
            // allow filled_map + paper -> filled_map copy, also map + paper
            bool isMap = (mn == "minecraft:filled_map" || mn == "minecraft:map");
            bool isPaper = (pn == "minecraft:paper");
            canClone = isMap && isPaper;
        }
        if (canClone) {
            *out = *mapIn;
            out->count = 1;
            // preserve map_id etc via components already copied
        } else {
            *out = ItemStack::air();
        }
    }
    if (m.type == MenuType::Anvil) {
        std::string rename = m.anvilRename;
        int cost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], rename);
        WriteBuffer pb;
        pb.varint(m.windowId);
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cost < 0 ? 0 : cost));
        extraPackets.push_back(std::move(pb));
        bool tooExp = CostCalculator::isTooExpensive(cost, self_->gamemode==1);
        if (!m.extraSlots[0].empty() && cost > 0 && !tooExp) {
            m.extraSlots[2] = m.extraSlots[0];
            int nextCost = CostCalculator::nextRepairCost(m.extraSlots[0], m.extraSlots[1]);
            m.extraSlots[2].setRepairCost(nextCost);
            if(!rename.empty()) m.extraSlots[2].setCustomName(rename);
            if(!m.extraSlots[1].empty() && (m.extraSlots[1].hasEnchant("minecraft:protection") || m.extraSlots[1].hasEnchant("protection"))){
                int lvl = m.extraSlots[1].enchantLevel("protection");
                if(lvl==0) lvl = m.extraSlots[1].enchantLevel("minecraft:protection");
                if(lvl>0) ItemStack::addEnchant(m.extraSlots[2], "minecraft:protection", lvl);
            }
        } else {
            m.extraSlots[2] = ItemStack::air();
        }
    }
    if (m.type == MenuType::Brewing) {
        if (m.blockEntity && m.blockEntity->kind == BlockEntity::Kind::Brewing) {
            auto& b = m.blockEntity->brewing;
            for (int prop = 0; prop < 2; ++prop) {
                WriteBuffer pb;
                pb.varint(m.windowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(prop == 0 ? b.brewTime : b.fuel);
                extraPackets.push_back(std::move(pb));
            }
        }
    }
    if (m.type == MenuType::Furnace) {
        if (m.blockEntity && m.blockEntity->kind == BlockEntity::Kind::Furnace) {
            auto& f = m.blockEntity->furnace;
            const int props[4] = {f.cookProgress, f.cookTotal, f.burnTicks, f.burnDuration};
            for (int prop = 0; prop < 4; ++prop) {
                WriteBuffer pb;
                pb.varint(m.windowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(props[prop]));
                extraPackets.push_back(std::move(pb));
            }
        }
    }
    finish();
}
void Session::sendMenuContent(Menu& m) {
    WriteBuffer b;
    std::vector<WriteBuffer> propertyPackets;
    std::shared_ptr<Connection> connection = conn_;
    auto entityLock = lockMenuBlockEntity(m);
    // Keep the lock order consistent with handleMenuClick/onEnchantItem:
    // container first, then player.  The locks are used only to snapshot the
    // model; packet I/O happens after both are released.
    std::unique_lock playerLock(self_->stateMtx);
    std::unique_lock packetLock(self_->inventoryPacketMtx);
    // The caller may have released stateMtx before requesting this snapshot.
    // A close/reopen can therefore leave the owning handle alive while the
    // object is no longer the session's active screen.  Do not emit a stale
    // snapshot for that old screen.
    if (openMenu_.get() != &m)
        return;
    b.varint(m.windowId);
    b.varint(++self_->invStateId);
    b.varint(m.totalSlots());
    for (int i = 0; i < m.totalSlots(); ++i) {
        ItemStack* s = m.slotAt(i, self_->inv.data());
        if (s) s->write(b);
        else ItemStack::air().write(b);
    }
    cursorItem_.write(b);
    appendEnchantmentProperties(m, propertyPackets);
    playerLock.unlock();
    if (entityLock.owns_lock()) entityLock.unlock();
    connection->trySendPacket(pl::sc::ContainerSetContent, b);
    for (const auto& packet : propertyPackets)
        connection->trySendPacket(pl::sc::ContainerSetData, packet);
}
void Session::appendEnchantmentProperties(
    Menu& m, std::vector<WriteBuffer>& packets) {
    if (m.type != MenuType::Enchantment || !self_) return;

    const std::int8_t dimension = self_->dimension;
    int bookshelves = 0;
    if (m.blockKey != -1) {
        const int bx = posKeyUnpackX(m.blockKey);
        const int by = posKeyUnpackY(m.blockKey);
        const int bz = posKeyUnpackZ(m.blockKey);
        bookshelves = CostCalculator::countBookshelves(
            srv_.worldFor(dimension), bx, by, bz);
    }

    std::array<EnchantmentOffer, 3> offers{};
    std::int32_t enchantmentSeed = 0;
    if (auto* logic = getMenuLogic(MenuType::Enchantment)) {
        if (auto* enchantment = dynamic_cast<EnchantmentMenuLogic*>(logic)) {
            offers = enchantment->offers(m, *self_, bookshelves);
            enchantmentSeed = self_->enchantmentSeed;
        }
    }

    // EnchantmentScreenHandler's PropertyDelegate order in 1.21.4 is:
    // power[0..2], seed, enchantmentId[0..2], enchantmentLevel[0..2].
    auto appendProperty = [&](int property, int value) {
        WriteBuffer pb;
        pb.varint(m.windowId);
        pb.i16(static_cast<std::int16_t>(property));
        pb.i16(static_cast<std::int16_t>(value));
        packets.push_back(std::move(pb));
    };
    for (int i = 0; i < 3; ++i)
        appendProperty(i, offers[static_cast<std::size_t>(i)].levelCost);
    appendProperty(3, enchantmentSeed);
    for (int i = 0; i < 3; ++i)
        appendProperty(4 + i, offers[static_cast<std::size_t>(i)].enchantmentId);
    for (int i = 0; i < 3; ++i)
        appendProperty(7 + i, offers[static_cast<std::size_t>(i)].enchantmentLevel);
}
void Session::openMenuAt(std::int32_t x, std::int32_t y, std::int32_t z,
                         std::uint16_t stateOfBlock) {
    // A UseItemOn packet carries the state observed by the client.  The
    // block may have changed before this handler runs (redstone, a tick, or
    // another player), so never construct a menu from a stale state.  Keep
    // the player lock out of the world/store lookup: the normal menu lock
    // order is block-entity -> player.
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!self_->inPlay || self_->dead) return;
        dimension = GameServer::canonicalDimension(self_->dimension);
    }
    const std::uint16_t currentState = srv_.worldFor(dimension).getBlock(x, y, z);
    if (currentState != stateOfBlock) return;
    const gen::BlockDef* def = gen::blockByState(stateOfBlock);
    if (!def) return;
    const std::string name(def->name);

    auto menu = std::make_shared<Menu>();
    menu->windowId = ++menuWindowCounter_;
    menu->blockKey = posKey(x, y, z);
    auto& blockEntityStore = srv_.blockEntitiesFor(dimension);
    const auto openBlockEntity = [&](BlockEntity::Kind kind) {
        auto owner = blockEntityStore.getShared(menu->blockKey);
        bool wrongKind = !owner;
        if (owner) {
            std::lock_guard entityLock(*owner->stateMtx);
            wrongKind = owner->kind != kind;
        }
        if (wrongKind)
            owner = blockEntityStore.createShared(menu->blockKey, kind);
        menu->blockEntityOwner = owner;
        return owner.get();
    };

    if (name == "minecraft:ender_chest") {
        // B-14 EnderItems: per-player 27 slots, not per-block (vanilla EnderChest)
        menu->type = MenuType::Chest;
        menu->container = self_->enderItems.data();
        menu->containerCount = 27;
        menu->blockEntity = nullptr;
    } else if (name.find("chest") != std::string::npos &&
        name.find("ender") == std::string::npos) {
        auto* be = openBlockEntity(BlockEntity::Kind::Chest);
        menu->type = MenuType::Chest;
        menu->container = be->chest.slots;
        menu->containerCount = ChestData::kSlots;
        menu->blockEntity = be;
    } else if (name == "minecraft:hopper" || name == "minecraft:dispenser" ||
               name == "minecraft:dropper") {
        const bool hopper = name == "minecraft:hopper";
        const auto kind = hopper ? BlockEntity::Kind::Hopper
            : (name == "minecraft:dropper" ? BlockEntity::Kind::Dropper
                                             : BlockEntity::Kind::Dispenser);
        auto* be = openBlockEntity(kind);
        menu->type = hopper ? MenuType::Hopper : MenuType::Dispenser;
        menu->container = be->generic.slots;
        menu->containerCount = hopper ? 5 : 9;
        menu->blockEntity = be;
    } else if (name == "minecraft:furnace") {
        auto* be = openBlockEntity(BlockEntity::Kind::Furnace);
        menu->type = MenuType::Furnace;
        menu->container = be->furnace.slots;
        menu->containerCount = 3;
        menu->blockEntity = be;
    } else if (name == "minecraft:blast_furnace") {
        auto* be = openBlockEntity(BlockEntity::Kind::Furnace);
        menu->type = MenuType::BlastFurnace;
        menu->container = be->furnace.slots;
        menu->containerCount = 3;
        menu->blockEntity = be;
    } else if (name == "minecraft:smoker") {
        auto* be = openBlockEntity(BlockEntity::Kind::Furnace);
        menu->type = MenuType::Smoker;
        menu->container = be->furnace.slots;
        menu->containerCount = 3;
        menu->blockEntity = be;
    } else if (name == "minecraft:crafting_table") {
        menu->type = MenuType::Crafting;
    } else if (name == "minecraft:enchanting_table") {
        menu->type = MenuType::Enchantment;
        menu->container = menu->extraSlots;
        menu->containerCount = 2;
    } else if (name == "minecraft:anvil" || name == "minecraft:chipped_anvil" ||
               name == "minecraft:damaged_anvil") {
        menu->type = MenuType::Anvil;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
    } else if (name == "minecraft:brewing_stand") {
        auto* be = openBlockEntity(BlockEntity::Kind::Brewing);
        menu->type = MenuType::Brewing;
        menu->container = be->brewing.slots;
        menu->containerCount = 5;
        menu->blockEntity = be;
    } else if (name == "minecraft:stonecutter") {
        menu->type = MenuType::Stonecutter;
        menu->container = menu->extraSlots;
        menu->containerCount = 2;
    } else if (name == "minecraft:grindstone") {
        menu->type = MenuType::Grindstone;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
    } else if (name == "minecraft:smithing_table") {
        menu->type = MenuType::Smithing;
        menu->container = menu->extraSlots;
        menu->containerCount = 4;
    } else if (name == "minecraft:beacon") {
        menu->type = MenuType::Beacon;
        menu->container = menu->extraSlots;
        menu->containerCount = 1;
    } else if (name == "minecraft:loom") {
        menu->type = MenuType::Loom;
        menu->container = menu->extraSlots;
        menu->containerCount = 4;
    } else if (name == "minecraft:barrel") {
        auto* be = openBlockEntity(BlockEntity::Kind::Barrel);
        menu->type = MenuType::Barrel;
        menu->container = be->chest.slots;
        menu->containerCount = 27;
        menu->blockEntity = be;
    } else if (name.find("shulker_box") != std::string::npos) {
        auto* be = openBlockEntity(BlockEntity::Kind::ShulkerBox);
        menu->type = MenuType::ShulkerBox;
        menu->container = be->chest.slots;
        menu->containerCount = ChestData::kSlots;
        menu->blockEntity = be;
    } else if (name == "minecraft:crafter") {
        auto* be = openBlockEntity(BlockEntity::Kind::Crafter);
        menu->type = MenuType::Crafter;
        menu->container = be->crafter.slots;
        menu->containerCount = 9;
        menu->crafterDisabledSlots = &be->crafter.disabledSlots;
        menu->blockEntity = be;
    } else if (name == "minecraft:cartography_table") {
        menu->type = MenuType::CartographyTable;
        menu->container = menu->extraSlots;
        menu->containerCount = 3;
    } else if (name == "minecraft:lectern") {
        menu->type = MenuType::Lectern;
        menu->container = menu->extraSlots;
        menu->containerCount = 1;
    } else return;

    // Opening another screen first closes the previous handler.  Apart from
    // matching vanilla's screen lifecycle, this is important because the
    // previous handler may own a cursor item or transient input slots.  Do
    // this before publishing the new pointer, so no re-entrant packet can
    // observe two active menus.
    closeOpenMenu(true);
    menu->refreshCraftResult(srv_.recipes());

    std::shared_ptr<Menu> activeMenu;
    {
        std::lock_guard playerLock(self_->stateMtx);
        openMenu_ = std::move(menu);
        activeMenu = openMenu_;
    }

    {
        WriteBuffer b;
        b.varint(activeMenu->windowId);
        b.varint(activeMenu->openScreenTypeId());
        const char* title = "Container";
        switch(activeMenu->type) {
            case MenuType::Chest: title="Chest"; break;
            case MenuType::Furnace: title="Furnace"; break;
            case MenuType::BlastFurnace: title="Blast Furnace"; break;
            case MenuType::Smoker: title="Smoker"; break;
            case MenuType::Crafting: title="Crafting"; break;
            case MenuType::Enchantment: title="Enchanting Table"; break;
            case MenuType::Anvil: title="Anvil"; break;
            case MenuType::Brewing: title="Brewing Stand"; break;
            case MenuType::Stonecutter: title="Stonecutter"; break;
            case MenuType::Grindstone: title="Grindstone"; break;
            case MenuType::Smithing: title="Smithing Table"; break;
            case MenuType::Beacon: title="Beacon"; break;
            case MenuType::Loom: title="Loom"; break;
            case MenuType::Barrel: title="Barrel"; break;
            case MenuType::ShulkerBox: title="Shulker Box"; break;
            case MenuType::Hopper: title="Hopper"; break;
            case MenuType::Dispenser: title="Dispenser"; break;
            case MenuType::Crafter: title="Crafter"; break;
            case MenuType::CartographyTable: title="Cartography Table"; break;
            case MenuType::Lectern: title="Lectern"; break;
            case MenuType::Merchant: title="Villager"; break;
            default: title="Container"; break;
        }
        nbt::writeTextComponent(b, title);
        conn_->sendPacket(pl::sc::OpenScreen, b);
    }

    // sendMenuContent takes its own short model snapshot.  The active menu
    // is published before the first packet so a re-entrant client/JVM path
    // cannot click a screen that the session does not yet know about.
    sendMenuContent(*activeMenu);

    MenuType activeType = MenuType::Chest;
    std::int32_t activeWindowId = 0;
    ItemStack anvilLeft = ItemStack::air();
    ItemStack anvilRight = ItemStack::air();
    std::shared_ptr<BlockEntity> propertyOwner;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (openMenu_.get() != activeMenu.get()) return;
        activeType = activeMenu->type;
        activeWindowId = activeMenu->windowId;
        propertyOwner = activeMenu->blockEntityOwner;
        if (activeType == MenuType::Anvil) {
            anvilLeft = activeMenu->extraSlots[0];
            anvilRight = activeMenu->extraSlots[1];
        }
    }
    if (activeType == MenuType::Anvil) {
        ItemStack left = anvilLeft;
        ItemStack right = anvilRight;
        int cost = CostCalculator::anvilCost(left, right, "");
        WriteBuffer pb;
        pb.varint(activeWindowId);
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cost < 0 ? 0 : cost));
        conn_->trySendPacket(pl::sc::ContainerSetData, pb);
    } else if (activeType == MenuType::Brewing) {
        int brewTime = 0;
        int fuel = 0;
        bool valid = false;
        if (propertyOwner && propertyOwner->stateMtx) {
            std::lock_guard entityLock(*propertyOwner->stateMtx);
            if (propertyOwner->kind == BlockEntity::Kind::Brewing) {
                brewTime = propertyOwner->brewing.brewTime;
                fuel = propertyOwner->brewing.fuel;
                valid = true;
            }
        }
        if (valid) {
            for (int prop = 0; prop < 2; ++prop) {
                WriteBuffer pb;
                pb.varint(activeWindowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(prop == 0 ? brewTime : fuel));
                conn_->trySendPacket(pl::sc::ContainerSetData, pb);
            }
        }
    } else if (activeType == MenuType::Furnace ||
               activeType == MenuType::BlastFurnace ||
               activeType == MenuType::Smoker) {
        std::array<int, 4> props{};
        bool valid = false;
        if (propertyOwner && propertyOwner->stateMtx) {
            std::lock_guard entityLock(*propertyOwner->stateMtx);
            if (propertyOwner->kind == BlockEntity::Kind::Furnace) {
                const auto& f = propertyOwner->furnace;
                props = {f.cookProgress, f.cookTotal, f.burnTicks,
                         f.burnDuration};
                valid = true;
            }
        }
        if (valid) {
            for (int prop = 0; prop < 4; ++prop) {
                WriteBuffer pb;
                pb.varint(activeWindowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(props[prop]));
                conn_->trySendPacket(pl::sc::ContainerSetData, pb);
            }
        }
    }
}
void Session::closeOpenMenu(bool sendPacketToClient) {
    struct PendingDrop {
        std::int8_t dimension = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        ItemStack stack = ItemStack::air();
    };
    std::int32_t windowId = 0;
    std::vector<PendingDrop> drops;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_) {
            tradingVillager_ = -1;
            return;
        }
        windowId = openMenu_->windowId;
        // Keep the original return-to-inventory transaction, but record full
        // inventory drops separately so world/entity mutation cannot happen
        // while stateMtx is held.
        const auto returnToPlayerOrQueueDrop = [this, &drops](const ItemStack& stack) {
            if (stack.empty()) return;
            auto trial = self_->inv;
            if (insertCompleteInventoryStack(trial, stack)) {
                self_->inv = std::move(trial);
                return;
            }
            PendingDrop drop;
            drop.dimension = self_->dimension;
            drop.x = self_->x;
            drop.y = self_->y + 0.5;
            drop.z = self_->z;
            drop.stack = stack;
            drops.push_back(std::move(drop));
        };

        if (openMenu_->type == MenuType::Crafting) {
            for (auto& s : openMenu_->craftGrid) {
                if (s.empty()) continue;
                returnToPlayerOrQueueDrop(s);
                s = ItemStack::air();
            }
            openMenu_->craftResult = ItemStack::air();
        } else if (openMenu_->container == openMenu_->extraSlots) {
            // These handlers use Menu::extraSlots as a per-screen temporary
            // inventory.  Their result slot is derived, never a real item to
            // return, but every input must survive an explicit close or a
            // replacement by another screen.
            int inputSlots = 0;
            switch (openMenu_->type) {
            case MenuType::Enchantment: inputSlots = 2; break;
            case MenuType::Anvil: inputSlots = 2; break;
            case MenuType::Stonecutter: inputSlots = 1; break;
            case MenuType::Grindstone: inputSlots = 2; break;
            case MenuType::Smithing: inputSlots = 3; break;
            case MenuType::Beacon: inputSlots = 1; break;
            case MenuType::Loom: inputSlots = 3; break;
            case MenuType::CartographyTable: inputSlots = 2; break;
            case MenuType::Lectern: inputSlots = 1; break;
            default: break;
            }
            inputSlots = std::min(inputSlots, openMenu_->containerCount);
            for (int i = 0; i < inputSlots; ++i) {
                ItemStack& s = openMenu_->extraSlots[i];
                if (s.empty()) continue;
                returnToPlayerOrQueueDrop(s);
                s = ItemStack::air();
            }
            // Result slots are previews.  Clear all remaining temporary
            // slots so a stale result cannot be carried into a later screen.
            for (int i = inputSlots; i < openMenu_->containerCount; ++i)
                openMenu_->extraSlots[i] = ItemStack::air();
        }
        if (!cursorItem_.empty()) {
            returnToPlayerOrQueueDrop(cursorItem_);
            cursorItem_ = ItemStack::air();
        }
        openMenu_.reset();
        tradingVillager_ = -1;
    }
    for (const auto& drop : drops) {
        srv_.spawnItemDropFor(drop.dimension, drop.x, drop.y, drop.z,
                              drop.stack, 0, 0.1, 0);
    }
    if (sendPacketToClient) {
        WriteBuffer b;
        b.varint(windowId);
        conn_->trySendPacket(pl::sc::CloseContainer, b);
    }
}
void Session::onCloseContainer() {
    bool hadMenu = false;
    {
        std::lock_guard stateLock(self_->stateMtx);
        hadMenu = openMenu_ != nullptr;
    }
    closeOpenMenu(false);
    if (hadMenu) srv_.resendInventory(*self_);
    syncCursorItem();
}
void Session::sendRecipeBook() {
    // settings: 8 booleans (gui open / filtering per station)
    {
        WriteBuffer b;
        for (int i = 0; i < 8; ++i) b.boolean(false);
        conn_->sendPacket(pl::sc::RecipeBookSettings, b);
    }
    const auto& all = srv_.recipes().all();
    WriteBuffer b;
    b.varint(static_cast<std::int32_t>(all.size()));
    std::int32_t displayId = 0;
    const auto tableItem = gen::itemIdByName().at("minecraft:crafting_table");
    const auto furnaceItem = gen::itemIdByName().at("minecraft:furnace");
    for (const auto& r : all) {
        // entry: {recipe:{displayId,display,group,category,requirements?},flags}
        b.varint(displayId);
        switch (r.kind) {
        case Recipe::Kind::Shaped:
            b.varint(1);                       // crafting_shaped
            b.varint(r.width);
            b.varint(r.height);
            b.varint(static_cast<std::int32_t>(r.cells.size()));
            for (auto& ing : r.cells)
                writeSlotDisplayItem(b, ing.items.empty()
                                         ? 0 : *ing.items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);   // craftingStation
            break;
        case Recipe::Kind::Shapeless: {
            b.varint(0);                       // crafting_shapeless
            b.varint(static_cast<std::int32_t>(r.ingredients.size()));
            for (auto& ing : r.ingredients)
                writeSlotDisplayItem(b, ing.items.empty()
                                         ? 0 : *ing.items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);
            break;
        }
        case Recipe::Kind::Smelting: {
            b.varint(2);                       // furnace
            writeSlotDisplayItem(b, r.cells.front().items.empty()
                                     ? 0 : *r.cells.front().items.begin());
            writeSlotDisplayItem(b,
                gen::itemIdByName().at("minecraft:coal"));   // fuel
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, furnaceItem); // station
            b.varint(r.cookingTicks);
            b.f32(r.experience);
            break;
        }
        case Recipe::Kind::Stonecutting: {
            b.varint(3);                       // stonecutter
            writeSlotDisplayItem(b, r.cells.front().items.empty()
                                     ? 0 : *r.cells.front().items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, furnaceItem);
            break;
        }
        case Recipe::Kind::Smithing: {
            b.varint(0);                       // smithing as shapeless for book display
            b.varint(static_cast<std::int32_t>(r.ingredients.size()));
            for (auto& ing : r.ingredients)
                writeSlotDisplayItem(b, ing.items.empty() ? 0 : *ing.items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);
            break;
        }
        case Recipe::Kind::Special: {
            b.varint(0);                       // special as shapeless placeholder
            b.varint(0);
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);
            break;
        }
        }
        b.varint(0);                           // group: none (could use r.group hash but 0 for parity)
        b.varint(r.category);                  // use JSON-derived category
        b.boolean(false);                      // craftingRequirements absent
        b.u8(0x03);                            // notification | highlight
        ++displayId;
    }
    b.boolean(true);                           // replace=true
    conn_->trySendPacket(pl::sc::RecipeBookAdd, b);
}
void Session::handlePlaceRecipe(std::int32_t recipeId, bool makeAll) {
    (void)makeAll;
    std::shared_ptr<Menu> menu;
    std::shared_ptr<BlockEntity> menuOwner;
    MenuType menuType = MenuType::Chest;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_) return;
        menu = openMenu_;
        menuOwner = menu->blockEntityOwner;
        menuType = menu->type;
    }
    const auto& all = srv_.recipes().all();
    if (recipeId < 0 || static_cast<std::size_t>(recipeId) >= all.size()) return;
    const Recipe& r = all[static_cast<std::size_t>(recipeId)];

    // A recipe placement is one inventory/menu transaction.  In particular,
    // do not empty an existing grid and then call addToInventory(): a full
    // inventory used to make that path silently delete the old inputs.
    std::unique_lock<std::recursive_mutex> entityLock;
    if (menuOwner && menuOwner->stateMtx)
        entityLock = std::unique_lock<std::recursive_mutex>(*menuOwner->stateMtx);
    std::unique_lock playerLock(self_->stateMtx);
    if (openMenu_.get() != menu.get() || menu->type != menuType) return;
    Menu& m = *menu;

    auto take = [&](std::array<InvSlot, 46>& inventory,
                    const Ingredient& ing) -> ItemStack {
        // RecipeBook placement consumes the player's main inventory and
        // hotbar, not armor or off-hand slots.
        for (int i = 9; i <= 44; ++i) {
            auto& s = inventory[static_cast<std::size_t>(i)];
            if (!s.empty() && ing.accepts(s.itemId)) {
                ItemStack one = s;
                one.count = 1;
                if (--s.count <= 0) s = ItemStack::air();
                return one;
            }
        }
        return ItemStack::air();
    };

    if (m.type == MenuType::Crafting) {
        auto trial = self_->inv;
        for (auto& s : m.craftGrid) {
            if (!s.empty()) {
                if (!insertCompleteInventoryStack(trial, s)) return;
            }
        }
        std::array<ItemStack, 9> placed{};
        for (auto& s : placed) s = ItemStack::air();
        bool complete = true;
        if (r.kind == Recipe::Kind::Shaped) {
            if (r.width <= 0 || r.width > 3 || r.height <= 0 || r.height > 3 ||
                r.cells.size() < static_cast<std::size_t>(r.width * r.height))
                complete = false;
            for (int y = 0; y < r.height && complete; ++y)
                for (int x = 0; x < r.width && complete; ++x) {
                    const auto& ing = r.cells[static_cast<std::size_t>(y) *
                                              r.width + x];
                    if (ing.empty()) continue;
                    ItemStack it2 = take(trial, ing);
                    if (it2.empty()) { complete = false; break; }
                    placed[static_cast<std::size_t>(y) * 3 + x] = it2;
                }
        } else if (r.kind == Recipe::Kind::Shapeless) {
            int i = 0;
            for (const auto& ing : r.ingredients) {
                if (i >= 9) break;
                ItemStack it2 = take(trial, ing);
                if (it2.empty()) { complete = false; break; }
                placed[static_cast<std::size_t>(i++)] = it2;
            }
        } else complete = false;
        if (!complete) {
            // `trial` is discarded, so both the original grid and inventory
            // remain exactly as they were when a required ingredient is
            // missing.
            return;
        }
        self_->inv = std::move(trial);
        for (std::size_t i = 0; i < placed.size(); ++i)
            m.craftGrid[i] = placed[i];
        m.refreshCraftResult(srv_.recipes());
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        srv_.resendInventory(*self_);
        sendMenuContent(m);
        syncCursorItem();
        return;
    } else if (m.type == MenuType::Furnace ||
               m.type == MenuType::BlastFurnace ||
               m.type == MenuType::Smoker) {
        if (r.kind != Recipe::Kind::Smelting) return;
        if (!m.container || m.containerCount < 2 || r.cells.empty()) return;
        // Place ingredient into input slot 0, and if needed fuel into slot 1
        auto trial = self_->inv;
        if (!m.container[0].empty() &&
            !insertCompleteInventoryStack(trial, m.container[0])) return;
        const Ingredient& ing = r.cells.front();
        ItemStack got = take(trial, ing);
        if (got.empty()) return;
        ItemStack newFuel = m.container[1];
        // try to place fuel if empty and makeAll is true or slot empty
        if (newFuel.empty()) {
            // find any fuel item in inventory
            for (int i = 9; i <= 44; ++i) {
                auto& s = trial[static_cast<std::size_t>(i)];
                if (!s.empty() && isFuelItem(s.itemId)) {
                    ItemStack one = s;
                    one.count = 1;
                    if (--s.count <= 0) s = ItemStack::air();
                    newFuel = one;
                    break;
                }
            }
        }
        self_->inv = std::move(trial);
        m.container[0] = got;
        m.container[1] = newFuel;
        const std::int32_t windowId = m.windowId;
        const int cookProgress = m.blockEntity &&
            m.blockEntity->kind == BlockEntity::Kind::Furnace
                ? m.blockEntity->furnace.cookProgress : 0;
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        sendMenuContent(m);
        srv_.resendInventory(*self_);
        syncCursorItem();
        WriteBuffer pb;
        pb.varint(windowId);
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cookProgress));
        conn_->trySendPacket(pl::sc::ContainerSetData, pb);
        return;
    } else if (m.type == MenuType::Stonecutter) {
        if (r.kind != Recipe::Kind::Stonecutting) return;
        if (r.cells.empty()) return;
        ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* output = m.container ? &m.container[1] : &m.extraSlots[1];
        auto trial = self_->inv;
        if (!input->empty() && !insertCompleteInventoryStack(trial, *input)) return;
        const Ingredient& ing = r.cells.front();
        ItemStack got = take(trial, ing);
        if (got.empty()) return;
        self_->inv = std::move(trial);
        *input = got;
        *output = r.result;
        const std::int32_t windowId = m.windowId;
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        // The full menu snapshot below advances the revision atomically; an
        // additional `invStateId + 1` SetSlot used to advertise a revision
        // that was not actually committed.
        WriteBuffer b;
        b.varint(windowId);
        b.varint(recipeId);
        conn_->trySendPacket(pl::sc::PlaceGhostRecipe, b);
        sendMenuContent(m);
        srv_.resendInventory(*self_);
        syncCursorItem();
        return;
    }
    // For other containers (Enchantment, Anvil, Brewing, etc.), PlaceRecipe is no-op but we still ack
}
void Session::handlePlaceGhostRecipe(std::int32_t recipeId) {
    std::shared_ptr<Menu> menu;
    std::shared_ptr<BlockEntity> menuOwner;
    std::int32_t playerEntityId = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_ || openMenu_->type != MenuType::Stonecutter) return;
        menu = openMenu_;
        menuOwner = menu->blockEntityOwner;
        playerEntityId = self_->entityId;
    }
    // throttle 0x39: limit to 1 per 5 ticks per player
    {
        // ghostThrottle_ is shared by all session threads, whereas the
        // throttle entry is not part of Player::stateMtx.  Protect the map
        // separately and hold it only for the lookup/update.
        static std::mutex ghostThrottleMutex;
        std::lock_guard throttleLock(ghostThrottleMutex);
        auto& thr = srv_.ghostThrottle_;
        std::int64_t now = srv_.tickNo_;
        auto it = thr.find(playerEntityId);
        if (it != thr.end() && now - it->second < 5) return;
        thr[playerEntityId] = now;
    }
    const auto& all = srv_.recipes().all();
    if (recipeId < 0 || static_cast<std::size_t>(recipeId) >= all.size()) return;
    const Recipe& r = all[static_cast<std::size_t>(recipeId)];
    if (r.kind != Recipe::Kind::Stonecutting) return;

    std::unique_lock<std::recursive_mutex> entityLock;
    if (menuOwner && menuOwner->stateMtx)
        entityLock = std::unique_lock<std::recursive_mutex>(*menuOwner->stateMtx);
    std::unique_lock playerLock(self_->stateMtx);
    if (openMenu_.get() != menu.get() || menu->type != MenuType::Stonecutter) return;
    Menu& m = *menu;
    if (r.cells.empty()) return;
    ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
    ItemStack* output = m.container ? &m.container[1] : &m.extraSlots[1];
    if (input->empty() || !r.cells.front().accepts(input->itemId)) return;
    *output = r.result;
    const std::int32_t windowId = m.windowId;
    const ItemStack outputSnapshot = *output;
    ++self_->invStateId;
    const std::int32_t stateId = self_->invStateId;
    playerLock.unlock();
    if (entityLock.owns_lock()) entityLock.unlock();
    // Send the committed revision, not a fabricated `current + 1` value.
    sendSetSlot(windowId, stateId, 1, outputSnapshot);
    // echo PlaceGhostRecipe back to client
    WriteBuffer b;
    b.varint(windowId);
    b.varint(recipeId);
    conn_->trySendPacket(pl::sc::PlaceGhostRecipe, b);
}
void Session::onPluginPayload(const std::string& channel,
                              const api::ChannelRegistry::Payload& body,
                              int phase) {
    // Forward every payload, including register/unregister, before the
    // built-in channel bookkeeping.  The bounded JVM hook is guarded and
    // reentrant-safe; a failed hook does not make the native path unavailable.
    if (srv_.jvmRuntime())
        srv_.jvmRuntime()->onPluginMessage(*self_, phase, channel, body);
    if (channel == "minecraft:register") {
        // NUL-separated channel list
        std::string joined(body.begin(), body.end());
        std::size_t start = 0;
        while (start <= joined.size()) {
            auto end = joined.find('\0', start);
            if (end == std::string::npos) end = joined.size();
            if (end > start) {
                std::lock_guard stateLock(self_->stateMtx);
                self_->clientChannels.insert(joined.substr(start, end - start));
            }
            start = end + 1;
        }
        return;
    }
    if (channel == "minecraft:unregister") {
        std::string joined(body.begin(), body.end());
        std::lock_guard stateLock(self_->stateMtx);
        self_->clientChannels.erase(joined);
        return;
    }
    if ((channel == "MC|ItemName" || channel == "minecraft:item_name") && phase == 1) {
        std::string rename;
        try {
            if (!body.empty()) {
                ReadBuffer rb(body.data(), body.size());
                rename = rb.string(constants::kMaxStringLength);
                if (rb.remaining() > 0) {
                    ReadBuffer rb2(body.data(), body.size());
                    (void)rb2.varint();
                    if (rb2.remaining() > 0)
                        rename = rb2.string(constants::kMaxStringLength);
                }
            }
        } catch (...) { rename.clear(); }
        onNameItem(rename);
        return;
    }
    api::ChannelRegistry::get().dispatch(phase, channel, body);
}
void Session::sendPluginPayload(int phase, const std::string& channel,
                                const std::vector<std::uint8_t>& body) {
    WriteBuffer b;
    b.string(channel);
    b.raw(body.data(), body.size());
    const std::uint8_t id = phase == 0 ? cf::sc::CustomPayload
                                       : pl::sc::CustomPayload;
    conn_->trySendPacket(id, b);
}
void Session::sendSystemText(const std::string& text) {
    WriteBuffer body;
    nbt::writeTextComponent(body, text);
    body.boolean(false);
    conn_->sendPacket(pl::sc::SystemChat, body);
}
void Session::sendChunk(std::int32_t cx, std::int32_t cz) {
    const std::int8_t dimension = GameServer::canonicalDimension(self_->dimension);
    World& world = srv_.worldFor(dimension);
    const std::uint32_t biomeIdx = srv_.data().biomeIndex(world.biomeKey());
    srv_.demandChunkAsyncFor(dimension, cx, cz);
    GameServer::ChunkBodyRef body;
    if (!srv_.getCachedChunkFor(dimension, cx, cz, biomeIdx, body)) {
        auto fresh = std::make_shared<const std::vector<std::uint8_t>>([&]{
            WriteBuffer wb;
            world.generateChunkIfMissing(cx, cz);
            world.withChunk(cx, cz, [&](const Chunk& c) {
                serializeLevelChunkBody(wb, cx, cz, c, biomeIdx);
            });
            return wb.data;
        }());
        srv_.storeChunkFor(dimension, cx, cz, 0, fresh);
        body = fresh;
    }
    conn_->sendPacketBuf(pl::sc::LevelChunkWithLight, *body);
    srv_.blockEntitiesFor(dimension).forEach([&](std::int64_t k, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Sign) return;
        const std::int32_t bx = posKeyUnpackX(k), by = posKeyUnpackY(k), bz = posKeyUnpackZ(k);
        if ((bx >> 4) != cx || (bz >> 4) != cz) return;
        sendSignBlockEntity(bx, by, bz);
    });
    sentChunks_.insert(chunkKey(cx, cz));
}
void Session::streamInitialChunks() {
    std::fprintf(stderr, "[cppfm] %s: streaming initial chunks\n", self_->name.c_str());
    chunksStreamed_ = true;
    tickChunksAround(self_->x, self_->z);
}
void Session::tickChunksAround(double px, double pz) {
    const int vd = effectiveViewDistance(srv_.config(), self_->clientSettings.viewDistance);
    const std::int32_t pcx = static_cast<std::int32_t>(std::floor(px)) >> 4;
    const std::int32_t pcz = static_cast<std::int32_t>(std::floor(pz)) >> 4;

    if (pcx != lastCx_ || pcz != lastCz_) {
        WriteBuffer center;
        center.varint(pcx);
        center.varint(pcz);
        conn_->trySendPacket(pl::sc::SetCenterChunk, center);
        lastCx_ = pcx; lastCz_ = pcz;
    }

    // collect missing chunks in view, sorted by distance to player chunk
    std::vector<std::pair<std::int64_t, std::pair<std::int32_t,std::int32_t>>> todo;
    for (std::int32_t dz = -vd; dz <= vd; ++dz)
        for (std::int32_t dx = -vd; dx <= vd; ++dx) {
            const std::int32_t cx = pcx + dx, cz = pcz + dz;
            const std::int64_t k = chunkKey(cx, cz);
            if (!sentChunks_.count(k)) todo.emplace_back(
                static_cast<std::int64_t>(dx) * dx + static_cast<std::int64_t>(dz) * dz,
                std::make_pair(cx, cz));
        }
    std::sort(todo.begin(), todo.end());

    if (!todo.empty()) {
        try {
            conn_->sendPacket(pl::sc::ChunkBatchStart, {});
            for (auto& t : todo) sendChunk(t.second.first, t.second.second);
            WriteBuffer fin;
            fin.varint(static_cast<std::int32_t>(todo.size()));
            conn_->sendPacket(pl::sc::ChunkBatchFinished, fin);
        } catch (...) {}
    }

    std::vector<std::int64_t> forget;
    for (auto k : sentChunks_) {
        auto [cx, cz] = chunkKeyDecode(k);
        if (std::abs(cx - pcx) > vd + 1 || std::abs(cz - pcz) > vd + 1)
            forget.push_back(k);
    }
    if (!forget.empty()) {
        for (auto k : forget) {
            auto [fcx, fcz] = chunkKeyDecode(k);
            WriteBuffer f;
            f.i32(fcz);   // z first per schema!
            f.i32(fcx);
            conn_->trySendPacket(pl::sc::ForgetLevelChunk, f);
            sentChunks_.erase(k);
        }
    }
}
void Session::ack(std::int32_t sequence) {
    WriteBuffer b;
    b.varint(sequence);
    conn_->sendPacket(pl::sc::AckBlockChange, b);
}
void Session::handlePlay() {
    for (;;) {
        try {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        self_->lastSeenMs = nowMs();
        switch (in.u8()) {
        case pl::cs::AcceptTeleportation: onAcceptTeleportation(in); break;
        case pl::cs::MovePlayerPos:       onMovement(in, true, false); break;
        case pl::cs::MovePlayerPosRot:    onMovement(in, true, true);  break;
        case pl::cs::MovePlayerRot:       onMovement(in, false, true); break;
        case pl::cs::MovePlayerStatusOnly:onMovement(in, false, false);break;
        case pl::cs::KeepAlive: onKeepAlivePacket(in); break;
        case pl::cs::ChatMessage:         onChatMessage(in); break;
        case pl::cs::ChatCommandSigned: if (onChatCommandSignedPacket(in)) return; break; // signed command (spec shape)
        case pl::cs::ChatSessionUpdate: onChatSessionUpdate(in); break; // plan3 Chat signing
        case pl::cs::MessageAck: in.skipRest(); break;
        case pl::cs::CookieResponse: onCookieResponse(in); break; // plan3 Cookie
        case pl::cs::CustomPayload: onCustomPayload(in); break; // plugin messaging API
        case pl::cs::UseEntity:           onUseEntity(in); break;
        case pl::cs::ChatCommand:         onChatCommand(in); break;
        case pl::cs::PlayerAction:        onPlayerAction(in); break;
        case pl::cs::EnchantItem:         onEnchantItem(in); break;   // 0x0F plan7
        case pl::cs::UseItemOn:           onUseItemOn(in); break;
        case pl::cs::UseItem:             onUseItem(in); break;
        case pl::cs::HeldItemSlot:        onHeldSlot(in); break;
        case pl::cs::WindowClick:         onWindowClick(in); break;   // 0x10
        case pl::cs::CloseContainer:      onCloseContainer(); break;  // 0x11
        case pl::cs::PlaceRecipe: onPlaceRecipePacket(in); break; // 0x25
        case pl::cs::TabComplete:         onTabComplete(in); break;
        case pl::cs::SelectTrade: onSelectTrade(in); break; // 0x31
        case pl::cs::ChunkBatchReceived:  in.f32(); break;
        case pl::cs::PingRequest: onPingRequest(in); break;
        case pl::cs::ClientTickEnd: break;
        case pl::cs::Abilities: onPlayerAbilities(in); break; // 0x26 serverbound {flags i8}
        case pl::cs::PlayerLoaded: onPlayerLoadedPacket(); break; // 0x2a
        case pl::cs::Swing: break;
        case pl::cs::SetCreativeModeSlot: onSetCreativeModeSlot(in); break;
        case pl::cs::SetDifficulty: (void)in.u8(); break;
        case pl::cs::ClientCommand: onClientCommand(in); break;
        case pl::cs::PlayerInput: onPlayerInput(in); break;
        case pl::cs::MoveVehicle: onMoveVehicle(in); break;
        case pl::cs::SignUpdate: onSignUpdate(in); break; // 0x39 — always a sign edit
        case pl::cs::EntityAction: onEntityAction(in); break;
        // (2026-09-04). Each case is self-guarded: a malformed body is logged + skipped so one bad packet never kills the session (W-16
        // per-packet policy).
        case pl::cs::Settings: onClientSettings(in); break; // 0x0C W-05
        case pl::cs::NameItem: onNameItemPacket(in); break; // 0x2E W-08
        case pl::cs::SetBeaconEffect: onBeaconEffectPacket(in); break; // 0x32 W-08
        case pl::cs::PickItemFromBlock: onPickItemFromBlock(in); break; // 0x22 W-08
        case pl::cs::PickItemFromEntity: onPickItemFromEntity(in); break; // 0x23 W-08
        case pl::cs::RecipeBook: onRecipeBookPacket(in); break; // 0x2C W-08
        case pl::cs::DisplayedRecipe: onDisplayedRecipe(in); break; // 0x2D W-08
        case pl::cs::SteerBoat: onSteerBoat(in); break; // 0x21 W-10(a)
        case pl::cs::ResourcePackReceive: if (onResourcePackReceive(in)) return; break; // 0x2F W-10(b)
        case pl::cs::Pong: onPong(in); break; // 0x2B W-10(c)
        case pl::cs::AdvancementTab: onAdvancementTab(in); break; // 0x30 W-10(d)
        case pl::cs::SelectBundleItem: onSelectBundleItem(in); break; // 0x02 W-10(d)
        case pl::cs::SetSlotState: onSetSlotState(in); break; // 0x12 W-10(d)
        case pl::cs::DebugSampleSubscription: onDebugSampleSubscription(in); break; // 0x15 W-10(d)
        case pl::cs::QueryBlockEntityTag: onQueryBlockEntityTag(in); break; // 0x01 W-10(d)
        case pl::cs::QueryEntityNbt: onQueryEntityNbt(in); break; // 0x17 W-10(d)
        case pl::cs::LockDifficulty: onLockDifficulty(in); break; // 0x1B W-10(d)
        case pl::cs::ConfigurationAcknowledged:                     // 0x0E W-10(d)
            // Play-phase ack of a play→config reversal (transfer). No config
            // stack exists yet — parsed (empty body) and kept as the future hook.
            break;
        case pl::cs::EditBook: onEditBook(in); break; // 0x16 W-09
        case pl::cs::GenerateStructure: onGenerateStructure(in); break; // 0x19 W-09
        case pl::cs::UpdateCommandBlock: onUpdateCommandBlock(in); break; // 0x34 W-09
        case pl::cs::UpdateCommandBlockMinecart: onUpdateCommandBlockMinecart(in); break; // 0x35 W-09
        case pl::cs::UpdateJigsaw: onUpdateJigsaw(in); break; // 0x37 W-09
        case pl::cs::UpdateStructureBlock: onUpdateStructureBlock(in); break; // 0x38 W-09
        case pl::cs::Spectate: onSpectatePacket(in); break; // 0x3B W-09
        default:
            // unknown login/config = kick + Disconnect (thrown by their handlers, kicked by Session::run). Policy: docs/SPEC_OPS.md#rate-limits-and-disconnect-policy.
            if (playLogGate_.shouldLog(nowMs()))
                std::fprintf(stderr, "[cppfm] unknown play packet from %s\n",
                             conn_->peer().c_str());
            in.skipRest();
            break;
        }
        } catch (const SocketClosedError&) {
            throw;
        } catch (const PacketDecoder::OversizeError& e) {
            if (playLogGate_.shouldLog(nowMs()))
                std::fprintf(stderr, "[cppfm] %s oversize kick: %s\n",
                             conn_->peer().c_str(), e.what());
            kickPlay("{\"text\":\"Packet too large\"}");
            return;
        } catch (const std::exception& e) {
            if (playLogGate_.shouldLog(nowMs()))
                std::fprintf(stderr, "[cppfm] play packet ignored (%s): %s\n",
                             conn_->peer().c_str(), e.what());
            continue;
        }
    }
}

void Session::onAcceptTeleportation(ReadBuffer& in) {
    in.varint();
    self_->spawned = true;
    if (!chunksStreamed_) streamInitialChunks();
}
void Session::onKeepAlivePacket(ReadBuffer& in) {
    // Client's response: just clear the pending flag. Sending anything here creates an infinite keepalive ping-pong.
    const std::int64_t id = in.i64();
    if (self_->pendingKeepAlive == 0 || id == self_->pendingKeepAlive)
        self_->pendingKeepAlive = 0;
}
bool Session::onChatCommandSignedPacket(ReadBuffer& in) {
    if (spam_.onChat(srv_.tickNow())) {
        kickPlay("{\"translate\":\"disconnect.spam\"}");
        return true;
    }
    try {
        const std::string cmd = in.string(constants::kMaxStringLength);
        (void)in.i64(); (void)in.i64();        // timestamp, salt
        const auto n = in.varint();            // argumentSignatures count
        if (n < 0 || n > 16) return false;            // absurd count: ignore, stay connected
        for (std::int32_t q = 0; q < n; ++q) {
            (void)in.string(32767);            // argumentName
            in.bytes(constants::kChatSignatureBytes);  // signature: fixed 256B
        }
        (void)in.varint();                     // messageCount
        in.bytes(3);                           // acknowledged[3] (was 60B over-read)
        dispatchCommand(cmd);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] signed-cmd parse ignored: %s\n", e.what());
    }
    return false;
}
void Session::onChatSessionUpdate(ReadBuffer& in) {
    self_->chatPubKey.clear();
    std::array<std::uint8_t, 16> sid{};
    auto sb = in.bytes(16);
    std::copy(sb.begin(), sb.end(), sid.begin());
    self_->chatSessionExpiry = in.i64();
    const auto pkLen = in.varint();
    self_->chatPubKey = in.bytes(static_cast<std::size_t>(pkLen));
    const auto sigLen = in.varint();
    in.bytes(static_cast<std::size_t>(sigLen));
    self_->hasChatSession = pkLen > 0;
}
void Session::onCookieResponse(ReadBuffer& in) {
    const std::string key = in.string(constants::kMaxStringLength);
    if (in.boolean()) {
        const auto len = in.varint();
        self_->cookies[key] =
            in.bytes(static_cast<std::size_t>(len));
        srv_.storeCookie(self_->uuid, key, self_->cookies[key]);
    } else {
        srv_.eraseCookie(self_->uuid, key);
    }
}
void Session::onCustomPayload(ReadBuffer& in) {
    const std::string channel = in.string(constants::kMaxStringLength);
    api::ChannelRegistry::Payload body(
        in.p + in.off, in.p + in.len);
    onPluginPayload(channel, body, 1);
}
void Session::onPlaceRecipePacket(ReadBuffer& in) {
    [[maybe_unused]] int windowId = 0;
    {
        const std::size_t mark = in.off;
        try { windowId = in.varint(); }
        catch (...) { in.off = mark; windowId = in.u8(); }
    }
    const auto recipeId = in.varint();
    const auto makeAll = in.boolean();
    handlePlaceRecipe(recipeId, makeAll);
}
void Session::onSelectTrade(ReadBuffer& in) {
    const auto idx = in.varint();
    if (tradingVillager_ >= 0 && !srv_.selectTrade(*self_, idx, tradingVillager_))
        srv_.resendInventory(*self_);
}
void Session::onPingRequest(ReadBuffer& in) {
    const std::int64_t id = in.i64();
    WriteBuffer b; b.i64(id);
    conn_->sendPacket(0x38 /*ping response*/, b);
}
void Session::onPlayerAbilities(ReadBuffer& in) {
    const std::int8_t f = in.i8();
    const bool wantFly = (f & 0x02) != 0;
    const bool canFly = (self_->gamemode == 1 || self_->gamemode == 3);
    self_->isFlying = wantFly && canFly;
    sendAbilities();
}
void Session::onSetCreativeModeSlot(ReadBuffer& in) {
    const std::int16_t slot = in.i16();
    const auto stack = ItemStack::read(in);
    if (slot >= 0 && slot < 46) {
        if ((slot==5||slot==6||slot==7||slot==8) && !self_->inv[slot].empty() && stack.empty()) {
            if (EnchantmentHelper::hasBindingCurse(self_->inv[slot])) {
                srv_.resendInventory(*self_);
                return;
            }
        }
        self_->inv[slot] = stack;
        if (slot==5||slot==6||slot==7||slot==8||slot==45||(slot>=36&&slot<=44)) {
            srv_.syncEquipmentOnChange(*self_);
        }
    }
}
void Session::onClientCommand(ReadBuffer& in) {
    const std::int32_t action = in.varint();
    if (action == 0) handleRespawnRequest();
}
void Session::onPlayerInput(ReadBuffer& in) {
    try{
        float sideways=0, forward=0;
        uint8_t flags=0;
        if(in.remaining()>=9){ sideways=in.f32(); forward=in.f32(); flags=in.u8(); }
        else if(in.remaining()>=1){ flags=in.u8(); }
        else { in.skipRest(); return; }
        bool wantSneak = (flags & 0x02) !=0;
        bool wantJump = (flags & 0x01) !=0;
        std::int32_t vehicleId = -1;
        std::int8_t dimension = 0;
        bool dismounted = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            if (wantSneak && self_->vehicleId != -1) {
                vehicleId = self_->vehicleId;
                dimension = GameServer::canonicalDimension(self_->dimension);
                self_->vehicleId = -1;
                dismounted = true;
            }
        }
        if (dismounted) {
            for (const auto& m : srv_.mobsSnapshot()) {
                if (!m) continue;
                std::lock_guard entityLock(*m->stateMtx);
                if (m->entityId == vehicleId &&
                    m->riderEntityId == self_->entityId) {
                    m->riderEntityId = -1;
                    break;
                }
            }
            srv_.broadcastSetPassengersEmptyFor(dimension, vehicleId);
        }
        bool riding = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            riding = self_->vehicleId != -1;
        }
        if(wantJump && riding){
            srv_.handleHorseJump(*self_, 80);
        }
        (void)sideways;(void)forward;
    }catch(...){ in.skipRest(); }
}
void Session::onMoveVehicle(ReadBuffer& in) {
    try{
        double x=in.f64(), y=in.f64(), z=in.f64();
        float yaw=in.f32(), pitch=in.f32();
        srv_.handleMoveVehicle(*self_, x,y,z,yaw,pitch);
    }catch(...){ in.skipRest(); }
}
void Session::onSignUpdate(ReadBuffer& in) {
    try {
        std::int32_t sx, sy, sz;
        in.position(sx, sy, sz);
        const bool front = in.boolean();
        std::string lines[4];
        for (int i = 0; i < 4; ++i) lines[i] = in.string(384);
        const std::int64_t key = posKey(sx, sy, sz);
        auto& blockEntityStore = srv_.blockEntitiesFor(self_->dimension);
        auto beOwner = blockEntityStore.getShared(key);
        BlockEntity* bep = beOwner.get();
        if (!bep) {
            beOwner = blockEntityStore.createShared(key, BlockEntity::Kind::Sign);
            bep = beOwner.get();
        }
        {
            std::lock_guard entityLock(*bep->stateMtx);
            if (bep->kind != BlockEntity::Kind::Sign) {
                std::fprintf(stderr, "[cppfm] sign update at %d,%d,%d ignored (not a sign block entity)\n",
                             sx, sy, sz);
                return;
            }
            std::string* dst = front ? bep->sign.front : bep->sign.back;
            for (int i = 0; i < 4; ++i) dst[i] = lines[i];
            if (front) bep->sign.hasFront = true; else bep->sign.hasBack = true;
        }
        sendSignBlockEntity(sx, sy, sz);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] sign update ignored: %s\n", e.what());
    }
}
void Session::onEntityAction(ReadBuffer& in) {
    const std::int32_t eid = in.varint();
    const std::int32_t action = in.varint();
    const std::int32_t jumpBoost = in.varint();
    // Entity Action is a client command for this player only.  More
    // importantly, the packet handler runs concurrently with the tick loop;
    // never read or mutate Player fields outside the same state boundary used
    // by movement and inventory handlers.
    bool stateChanged = false;
    bool sneaking = false;
    bool sprinting = false;
    bool attributeChanged = false;
    bool dismounted = false;
    std::int32_t playerEntityId = 0;
    std::int32_t vehicleId = -1;
    std::int8_t playerDimension = 0;
    WriteBuffer poseMetadata;
    WriteBuffer flagsMetadata;
    WriteBuffer attributeUpdate;

    {
        std::lock_guard playerLock(self_->stateMtx);
        if (eid != self_->entityId) return;

        const bool wasSneaking = self_->isSneaking;
        const bool wasSprinting = self_->isSprinting;
        if (action == 0) self_->isSneaking = true;
        else if (action == 1) self_->isSneaking = false;
        else if (action == 3) self_->isSprinting = true;
        else if (action == 4) self_->isSprinting = false;

        playerEntityId = self_->entityId;
        playerDimension = GameServer::canonicalDimension(self_->dimension);
        sneaking = self_->isSneaking;
        sprinting = self_->isSprinting;
        stateChanged = wasSneaking != sneaking || wasSprinting != sprinting;

        // START_SNEAKING dismounts the current vehicle.  Clear the player
        // link while holding the player lock, then repair the mob link after
        // releasing it so no packet/broadcast runs under a model lock.
        if (action == 0 && self_->vehicleId != -1) {
            vehicleId = self_->vehicleId;
            self_->vehicleId = -1;
            dismounted = true;
        }

        if (stateChanged) {
            if (wasSneaking != sneaking) {
                // pose metadata index 6 varint: 5 crouching, 0 standing
                poseMetadata.varint(playerEntityId);
                poseMetadata.u8(6); poseMetadata.varint(1);
                poseMetadata.varint(sneaking ? 5 : 0);
                poseMetadata.u8(255);
            }

            // flags byte index 0: 0x02 sneak + 0x08 sprint
            flagsMetadata.varint(playerEntityId);
            flagsMetadata.u8(0); flagsMetadata.varint(0);
            std::uint8_t flags = 0;
            if (sneaking) flags |= 0x02;
            if (sprinting) flags |= 0x08;
            flagsMetadata.u8(flags);
            flagsMetadata.u8(255);

            int swiftLevel = 0;
            for (int slot = 5; slot <= 8; ++slot) {
                if (self_->inv[slot].empty()) continue;
                const std::string name = self_->inv[slot].name();
                if (name.find("leggings") != std::string::npos)
                    swiftLevel = std::max(swiftLevel,
                                          EnchantmentHelper::swiftSneakLevel(self_->inv[slot]));
            }
            if (swiftLevel == 0) {
                for (int slot = 5; slot <= 8; ++slot) {
                    if (!self_->inv[slot].empty())
                        swiftLevel = std::max(swiftLevel,
                                              EnchantmentHelper::swiftSneakLevel(self_->inv[slot]));
                }
            }
            const double before = self_->attributes.getValue(Attribute::MOVEMENT_SPEED);
            if (sneaking && swiftLevel > 0)
                self_->attributes.applySwiftSneak(swiftLevel);
            else
                self_->attributes.removeModifier(Attribute::MOVEMENT_SPEED, "swift_sneak");
            const double after = self_->attributes.getValue(Attribute::MOVEMENT_SPEED);
            attributeChanged = std::abs(before - after) > 1e-9;
            if (attributeChanged)
                self_->attributes.writeUpdate(attributeUpdate, playerEntityId);
        }
    }

    if (dismounted) {
        for (const auto& mob : srv_.mobsSnapshot()) {
            if (!mob) continue;
            std::lock_guard entityLock(*mob->stateMtx);
            if (mob->entityId == vehicleId && mob->riderEntityId == playerEntityId) {
                mob->riderEntityId = -1;
                break;
            }
        }
        srv_.broadcastSetPassengersEmptyFor(playerDimension, vehicleId);
    }

    const auto sendMetadata = [&](const WriteBuffer& body) {
        // The tracking broadcast excludes the source player, but the source
        // client also needs the authoritative pose/flags update.
        conn_->trySendPacket(pl::sc::SetEntityMetadata, body);
        srv_.broadcastPacketExceptInDimension(playerDimension, self_.get(),
                                              pl::sc::SetEntityMetadata, body);
    };
    if (!poseMetadata.data.empty()) sendMetadata(poseMetadata);
    if (!flagsMetadata.data.empty()) sendMetadata(flagsMetadata);
    if (attributeChanged) {
        conn_->trySendPacket(proto::pl::sc::UpdateAttributes, attributeUpdate);
        srv_.broadcastPacketExceptInDimension(playerDimension, self_.get(),
                                              proto::pl::sc::UpdateAttributes,
                                              attributeUpdate);
    }

    // Actions 5/6 only communicate horse-jump charging state to the server;
    // they are not a serverbound request to emit SetEntityMetadata.  Action 7
    // is OPEN_VEHICLE_INVENTORY, not a jump (the old code treated it as one).
    // Vehicle movement/jump simulation remains owned by handleHorseJump and
    // is invoked by the explicit jump-power path.
    (void)jumpBoost;
}
void Session::onClientSettings(ReadBuffer& in) {
    try {
        Player::ClientSettings s;
        s.locale = in.string(64);
        s.viewDistance = GameServer::clampClientViewDistance(in.i8());
        s.chatFlags = in.varint();
        s.chatColors = in.boolean();
        s.skinParts = in.u8();
        s.mainHand = in.varint();
        s.textFiltering = in.boolean();
        s.serverListing = in.boolean();
        s.particleStatus = in.varint();
        if (s.particleStatus < 0 || s.particleStatus > 2) s.particleStatus = 0;
        applyClientSettings(s);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] settings from %s ignored: %s\n",
                     self_->name.c_str(), e.what());
        in.skipRest();
    }
}
void Session::onNameItemPacket(ReadBuffer& in) {
    try { onNameItem(in.string(50)); }
    catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] name_item ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onBeaconEffectPacket(ReadBuffer& in) {
    try {
        std::optional<std::int32_t> prim, sec;
        if (in.boolean()) prim = in.varint();
        if (in.boolean()) sec = in.varint();
        onBeaconEffect(prim, sec);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] beacon effect ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onPickItemFromBlock(ReadBuffer& in) {
    try {
        std::int32_t bx, by, bz;
        in.position(bx, by, bz);
        const bool includeData = in.boolean();
        (void)includeData; // BE-copy detail deferred (docs/SPEC_WIRE.md)
        World& w = srv_.worldFor(self_->dimension);
        const std::uint16_t st = w.getBlock(bx, by, bz);
        if (st == 0) return;
        const auto* bd = gen::blockByState(st);
        if (!bd) return;
        const auto it = gen::itemIdByName().find(std::string(bd->name));
        if (it == gen::itemIdByName().end()) return;
        if (!srv_.addToInventory(*self_, it->second, 1)) return;
        srv_.resendInventory(*self_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] pick-block ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onPickItemFromEntity(ReadBuffer& in) {
    try {
        const std::int32_t eid = in.varint();
        const bool includeData = in.boolean();
        (void)includeData;
        std::string egg;
        {
            for (const auto& m : srv_.mobsSnapshot()) {
                if (!m) continue;
                if (m->entityId != eid) continue;
                egg = std::string(MobEntity::kindName(m->kind)) + "_spawn_egg";
                break;
            }
        }
        if (egg.empty()) return;
        const auto it = gen::itemIdByName().find(egg);
        if (it == gen::itemIdByName().end()) return;
        if (!srv_.addToInventory(*self_, it->second, 1)) return;
        srv_.resendInventory(*self_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] pick-entity ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onRecipeBookPacket(ReadBuffer& in) {
    try {
        self_->recipeBookId = in.varint();
        self_->recipeBookOpen = in.boolean();
        self_->recipeBookFilter = in.boolean();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] recipe_book ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onDisplayedRecipe(ReadBuffer& in) {
    try { self_->displayedRecipe = in.varint(); }
    catch (...) { in.skipRest(); }
}
void Session::onSteerBoat(ReadBuffer& in) {
    try {
        self_->boatLeftPaddle = in.boolean();
        self_->boatRightPaddle = in.boolean();
    } catch (...) { in.skipRest(); }
}
bool Session::onResourcePackReceive(ReadBuffer& in) {
    try {
        std::array<std::uint8_t,16> uuid{};
        auto ub = in.bytes(16);
        std::copy(ub.begin(), ub.end(), uuid.begin());
        const std::int32_t result = in.varint();
        // vanilla PackResult: 0 loaded, 1 declined, 2 failed_download, 3 accepted, 4 downloaded... A forced pack that is declined/failed
        // must kick.
        if (srv_.config().resourcePackForced && (result == 1 || result == 2)) {
            disconnectIn("{\"text\":\"Server resource pack declined\"}");
            state_ = State::Done;
            return true;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] resource_pack_receive ignored: %s\n", e.what());
        in.skipRest();
    }
    return false;
}
void Session::onPong(ReadBuffer& in) {
    try {
        self_->lastPlayPongId = in.i32();
        self_->lastPlayPongMs = nowMs();
    } catch (...) { in.skipRest(); }
}
void Session::onAdvancementTab(ReadBuffer& in) {
    try {
        const std::int32_t action = in.varint();
        self_->advancementTabAction = action;
        self_->advancementTabId.clear();
        if (action == 0) self_->advancementTabId = in.string(512);
        if (action == 0) srv_.sendSelectAdvancementTab(*self_, self_->advancementTabId);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] advancement_tab ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onSelectBundleItem(ReadBuffer& in) {
    try {
        const std::int32_t slotId = in.varint();
        const std::int32_t idx = in.varint();
        if (slotId >= 0 && idx >= 0) self_->bundleSelectedIndex = idx;
    } catch (...) { in.skipRest(); }
}
void Session::onSetSlotState(ReadBuffer& in) {
    try { (void)in.varint(); (void)in.varint(); (void)in.boolean(); }
    catch (...) { in.skipRest(); }
    // No server-side slot-enable state exists (client-side crafting
    // ghost slot hint) — parsed and intentionally ignored (docs/SPEC_WIRE.md).
}
void Session::onDebugSampleSubscription(ReadBuffer& in) {
    try { (void)in.varint(); } catch (...) { in.skipRest(); }
    return; // debug profiler subscription — no server effect (docs/SPEC_WIRE.md).
}
void Session::onQueryBlockEntityTag(ReadBuffer& in) {
    try {
        const std::int32_t tx = in.varint();
        std::int32_t bx, by, bz;
        in.position(bx, by, bz);
        answerBlockNbt(tx, bx, by, bz);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] query_block_nbt ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onQueryEntityNbt(ReadBuffer& in) {
    try {
        const std::int32_t tx = in.varint();
        const std::int32_t eid = in.varint();
        answerEntityNbt(tx, eid);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] query_entity_nbt ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onLockDifficulty(ReadBuffer& in) {
    try {
        const bool locked = in.boolean();
        if (srv_.isOp(self_->name)) self_->difficultyLocked = locked;
        else std::fprintf(stderr, "[cppfm] lock_difficulty from non-op %s denied\n",
                          self_->name.c_str());
    } catch (...) { in.skipRest(); }
}
void Session::onEditBook(ReadBuffer& in) {
    try {
        const std::int32_t hand = in.varint();
        const std::int32_t n = in.varint();
        if (n < 0 || n > 100) { in.skipRest(); return; } // W-14 page budget
        std::vector<std::string> pages;
        pages.reserve(static_cast<std::size_t>(n));
        std::size_t total = 0;
        for (std::int32_t i = 0; i < n; ++i) {
            pages.push_back(in.string(32767));
            total += pages.back().size();
            if (total > 2 * 1024 * 1024) { in.skipRest(); pages.clear(); break; }
        }
        if (pages.empty() && n > 0) return;
        const bool hasTitle = in.boolean();
        const std::string title = hasTitle ? in.string(128) : std::string{};
        self_->lastBookEdit = {hand, pages, title, hasTitle};
        // Real effect: signing (title present) converts a held writable book into a written book carrying the title.
        if (hasTitle && (hand == 0 || hand == 1)) {
            ItemStack* held = (hand == 0) ? &self_->inv[36 + self_->heldSlot]
                                          : &self_->inv[45];
            if (held && (held->name() == "minecraft:writable_book" ||
                         held->name() == "minecraft:written_book")) {
                auto it = gen::itemIdByName().find("minecraft:written_book");
                if (it != gen::itemIdByName().end()) {
                    held->itemId = it->second;
                    if (!title.empty()) held->setCustomName(title);
                    srv_.resendInventory(*self_);
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] edit_book ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onGenerateStructure(ReadBuffer& in) {
    try {
        std::int32_t gx, gy, gz;
        in.position(gx, gy, gz);
        (void)in.varint(); (void)in.boolean();
        if (!requireOp(2, "generate_structure")) return;
        std::fprintf(stderr, "[cppfm] generate_structure at %d,%d,%d denied-by-deferral (no structure gen API yet)\n",
                     gx, gy, gz);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] generate_structure ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onUpdateCommandBlock(ReadBuffer& in) {
    try {
        std::int32_t cx, cy, cz;
        in.position(cx, cy, cz);
        (void)in.string(32767); (void)in.varint(); (void)in.u8();
        if (!requireOp(2, "update_command_block")) return;
        std::fprintf(stderr, "[cppfm] update_command_block at %d,%d,%d denied-by-deferral (no command-block BE yet)\n",
                     cx, cy, cz);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] update_command_block ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onUpdateCommandBlockMinecart(ReadBuffer& in) {
    try {
        const std::int32_t eid = in.varint();
        (void)in.string(32767); (void)in.boolean();
        if (!requireOp(2, "update_command_block_minecart")) return;
        std::fprintf(stderr, "[cppfm] update_command_block_minecart eid=%d denied-by-deferral\n", eid);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] update_command_block_minecart ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onUpdateJigsaw(ReadBuffer& in) {
    try {
        std::int32_t jx, jy, jz;
        in.position(jx, jy, jz);
        for (int i = 0; i < 5; ++i) (void)in.string(512);
        (void)in.varint(); (void)in.varint();
        if (!requireOp(2, "update_jigsaw")) return;
        std::fprintf(stderr, "[cppfm] update_jigsaw at %d,%d,%d denied-by-deferral\n", jx, jy, jz);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] update_jigsaw ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onUpdateStructureBlock(ReadBuffer& in) {
    try {
        std::int32_t ux, uy, uz;
        in.position(ux, uy, uz);
        (void)in.varint(); (void)in.varint(); (void)in.string(512);
        (void)in.i8(); (void)in.i8(); (void)in.i8();
        (void)in.i8(); (void)in.i8(); (void)in.i8();
        (void)in.varint(); (void)in.varint(); (void)in.string(512);
        (void)in.f32(); (void)in.varint(); (void)in.u8();
        if (!requireOp(2, "update_structure_block")) return;
        std::fprintf(stderr, "[cppfm] update_structure_block at %d,%d,%d denied-by-deferral\n", ux, uy, uz);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] update_structure_block ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onSpectatePacket(ReadBuffer& in) {
    try {
        std::array<std::uint8_t,16> target{};
        auto tb = in.bytes(16);
        std::copy(tb.begin(), tb.end(), target.begin());
        onSpectate(target);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] spectate ignored: %s\n", e.what());
        in.skipRest();
    }
}
void Session::onPlayerLoadedPacket() {
    if (!chunksStreamed_) streamInitialChunks();
}
void Session::onMovement(ReadBuffer& in, bool hasPos, bool hasRot) {
    const SessionPlayerSnapshot movementState = snapshotPlayerForSession(*self_);
    const double oldX = movementState.x, oldY = movementState.y,
                 oldZ = movementState.z;
    const bool wasOnGround = movementState.onGround;
    const double nx = hasPos ? in.f64() : movementState.x;
    const double ny = hasPos ? in.f64() : movementState.y;
    const double nz = hasPos ? in.f64() : movementState.z;
    const float newYaw = hasRot ? in.f32() : movementState.yaw;
    const float newPitch = hasRot ? in.f32() : movementState.pitch;
    const std::uint8_t moveFlags = in.u8();
    const bool nowGround = (moveFlags & 0x01) != 0;

    if (hasPos) {
        const double dx = nx - oldX;
        const double dy = ny - oldY;
        const double dz = nz - oldZ;
        const bool finitePosition = std::isfinite(nx) && std::isfinite(ny) &&
                                     std::isfinite(nz);
        // Vanilla's moved-too-quickly guard is approximately a ten-block
        // per-packet displacement before velocity allowances.  Keeping the
        // same conservative envelope blocks forged coordinates without
        // rejecting ordinary lag-spike movement packets.
        const bool movedTooQuickly = dx * dx + dy * dy + dz * dz > 100.0;
        const World& movementWorld = srv_.worldFor(movementState.dimension);
        const bool enteredCollision = movementState.gamemode != 3 &&
                                      intersectsPlayerCollision(
                                          movementWorld, nx, ny, nz);
        if (!finitePosition || !std::isfinite(newYaw) ||
            !std::isfinite(newPitch) || ny < -2048.0 || ny > 2048.0 ||
            movedTooQuickly || !srv_.isInsideBorder(nx, nz) ||
            enteredCollision) {
            // Correct the client to the last accepted authoritative pose.  A
            // malformed movement packet must not partially update position,
            // rotation, fall distance, or chunk streaming state.
            sendTeleport(oldX, oldY, oldZ, movementState.yaw,
                         movementState.pitch);
            return;
        }
    } else if (!std::isfinite(newYaw) || !std::isfinite(newPitch)) {
        sendTeleport(oldX, oldY, oldZ, movementState.yaw, movementState.pitch);
        return;
    }

    if (hasPos) {
        if (!self_->onGround && ny < self_->y && self_->gamemode == 0)
            self_->fallDist += self_->y - ny;
        self_->x = nx; self_->y = ny; self_->z = nz;
    }
    if (hasRot) {
        self_->yaw = newYaw;
        self_->pitch = newPitch;
    }
    // bit1 hasHorizontalCollision: no consumer yet (future wall-kick etc.).
    if (hasPos) {
        if (self_->y < -2048.0 || self_->y > 2048.0)
            throw std::runtime_error("player moved out of world bounds");
        // landing — fall mitigation (water/slime/honey/hay/powder_snow+slowfalling)
        auto isFallMitigated = [&]() -> bool {
            if (self_->gamemode == 1 || self_->gamemode == 3) return true;
            int bx = (int)std::floor(self_->x);
            int by = (int)std::floor(self_->y - 0.2);
            int bz = (int)std::floor(self_->z);
            uint16_t st = srv_.worldFor(self_->dimension).getBlock(bx,by,bz);
            auto *d = gen::blockByState(st);
            if (!d) return false;
            if (d->name == "minecraft:water") return true;
            if (d->name == "minecraft:slime_block") return true;
            if (d->name == "minecraft:honey_block") return true;
            if (d->name == "minecraft:hay_block") return true;
            if (d->name == "minecraft:powder_snow") {
                for (auto &e : self_->effects) if (e.type == effects::SlowFalling) return true;
                return false;
            }
            // also check if landing block is waterlogged? simplified
            return false;
        };
        if (nowGround && !self_->onGround) {
            if (self_->fallDist > 0.5 && !self_->isSneaking) {
                // Players always pass the vanilla 0.512 volume and mobGriefing
                // checks; those branches only apply to mob entities.
                int bx = static_cast<int>(std::floor(self_->x));
                int by = static_cast<int>(std::floor(self_->y - 0.2));
                int bz = static_cast<int>(std::floor(self_->z));
                World& w = srv_.worldFor(self_->dimension);
                std::uint16_t st = w.getBlock(bx, by, bz);
                const gen::BlockDef* bd = gen::blockByState(st);
                if (bd && std::string(bd->name) == "minecraft:farmland") {
                    float prob = std::clamp((float)(self_->fallDist - 0.5), 0.f, 1.f);
                    bool doTrample = (prob >= 1.0f) || ((nextRandom()/(float)RAND_MAX) < prob);
                    if (doTrample) {
                        bool hasMoisture = false;
                        for (auto& [k,v] : gen::propsOf(st)) if (k=="moisture") hasMoisture=true;
                        // drop crop above if any
                        auto above = w.getBlock(bx, by+1, bz);
                        if (above != 0) {
                            auto* ad = gen::blockByState(above);
                            if (ad && (std::string(ad->name).find("wheat")!=std::string::npos ||
                                       std::string(ad->name).find("carrots")!=std::string::npos ||
                                       std::string(ad->name).find("potatoes")!=std::string::npos ||
                                       std::string(ad->name).find("beetroots")!=std::string::npos)) {
                                // drop one item?
                                auto it = gen::itemIdByName().find(ad->name);
                                if (it != gen::itemIdByName().end()) {
                                    srv_.spawnItemDropFor(self_->dimension, bx+0.5,
                                                          by+1.2, bz+0.5,
                                                          it->second, 1, 0,
                                                          0.1, 0);
                                }
                                w.setBlock(bx, by+1, bz, 0);
                                srv_.broadcastBlockChangeFor(self_->dimension, bx, by+1, bz, 0);
                            }
                        }
                        if (hasMoisture) {
                            const gen::BlockDef* d = bd;
                            std::vector<std::pair<std::string_view,std::string_view>> props;
                            for (auto& [k,v] : gen::propsOf(st)) if (k!="moisture") props.emplace_back(k,v);
                            props.emplace_back("moisture", "0");
                            std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
                            w.setBlock(bx, by, bz, ns);
                            srv_.broadcastBlockChangeFor(self_->dimension, bx, by, bz, ns);
                        }
                        // revert to dirt
                        auto it = gen::blockNameToState().find("minecraft:dirt");
                        if (it != gen::blockNameToState().end()) {
                            std::uint16_t dirt = static_cast<std::uint16_t>(it->second);
                            w.setBlock(bx, by, bz, dirt);
                            srv_.broadcastBlockChangeFor(self_->dimension, bx, by, bz, dirt);
                        }
                        // LevelEvent 2001: block break particles (strict B11)
                    srv_.broadcastWorldEventFor(self_->dimension, 2001, bx,
                                                    by, bz,
                                                    static_cast<std::int32_t>(st),
                                                    false);
                }
            }
            }
            {
                int lbx = static_cast<int>(std::floor(self_->x));
                int lby = static_cast<int>(std::floor(self_->y - 0.2));
                int lbz = static_cast<int>(std::floor(self_->z));
                std::uint16_t lst = srv_.worldFor(self_->dimension).getBlock(lbx, lby, lbz);
                blockEventDispatcher().onEntityLand(self_.get(), lbx, lby, lbz, lst, self_->fallDist);
                api::EntityLandEvent lev; lev.entity=self_.get(); lev.x=lbx; lev.y=lby; lev.z=lbz; lev.blockState=lst; lev.fallDistance=self_->fallDist;
                api::events().entityLand.fire(lev);
            }
            bool mitigated = false;
            if (self_->fallDist > 3.0) mitigated = isFallMitigated();
            if (mitigated) {
                self_->fallDist = 0;
            } else if (self_->fallDist > 3.0) {
                if (srv_.gamerules_.getBool("fallDamage"))
                    srv_.applyDamage(*self_, static_cast<float>(std::floor(self_->fallDist - 3.0)),
                                "fall");
                self_->fallDist = 0;
            } else {
                self_->fallDist = 0;
            }
        }
        if (nowGround) self_->fallDist = 0;
        if (self_->gamemode == 0) {
            const double hdx = self_->x - oldX, hdz = self_->z - oldZ;
            double hDist = std::sqrt(hdx*hdx + hdz*hdz);
            if (hDist > 0.001) {
                float mult = self_->isSwimming ? 0.01f : (self_->isSprinting ? 0.10f : 0.0f);
                self_->exhaustion += (float)hDist * mult;
            }
            // jump exhaustion: leaving ground with upward motion
            double dy = self_->y - oldY;
            if (hasPos && wasOnGround && !nowGround && dy > 0.05) {
                float jumpCost = self_->isSprinting ? 0.2f : 0.05f;
                // apply jump boost reduction? ignore
                srv_.addHungerExhaustion(*self_, jumpCost);
            }
        }
        self_->spawned = true;
        if (!chunksStreamed_) streamInitialChunks();
        else tickChunksAround(self_->x, self_->z);
    }
    self_->onGround = nowGround;
    {
        if (!self_->onGround && self_->gamemode == 0) {
            // Use vertical drift check: not falling and not in water/lava
            if (self_->fallDist < 0.5) {
                self_->flyingTicks++;
            } else {
                self_->flyingTicks = 0;
            }
            self_->isFlying = self_->flyingTicks > 10;
            if (!srv_.config().allowFlight && self_->flyingTicks > 80) {
                WriteBuffer kick;
                nbt::writeTextComponent(kick, "Flying is not enabled on this server");
                self_->conn->trySendPacket(proto::pl::sc::Disconnect, kick);
                self_->conn->close();
            }
        } else {
            self_->flyingTicks = 0;
            self_->isFlying = false;
        }
    }
    if (self_->onGround && hasPos) {
        bool hasFrost = false;
        for (int i=5;i<=8;++i) if (!self_->inv[i].empty() && EnchantmentHelper::hasFrostWalker(self_->inv[i])) { hasFrost=true; break; }
        if (hasFrost) {
            World& w = srv_.worldFor(self_->dimension);
            int bx = (int)std::floor(self_->x);
            int by = (int)std::floor(self_->y - 0.5);
            int bz = (int)std::floor(self_->z);
            int lvl = 0;
            for (int i=5;i<=8;++i) if (!self_->inv[i].empty()) lvl = std::max(lvl, EnchantmentHelper::frostWalkerLevel(self_->inv[i]));
            if(lvl==0) for(int i=5;i<=8;++i) if(!self_->inv[i].empty()) lvl = std::max(lvl, self_->inv[i].enchantLevel("frost_walker"));
            int radius = 2 + lvl;
            auto frostIt = gen::blockNameToState().find("minecraft:frosted_ice");
            if (frostIt != gen::blockNameToState().end()) {
                std::uint16_t frosted = (std::uint16_t)frostIt->second;
                for (int dx=-radius; dx<=radius; ++dx) for (int dz=-radius; dz<=radius; ++dz) {
                    if (dx*dx+dz*dz > radius*radius) continue;
                    int wx = bx+dx, wz = bz+dz;
                    std::uint16_t st = w.getBlock(wx, by, wz);
                    const gen::BlockDef* bd = gen::blockByState(st);
                    if (bd && std::string(bd->name)=="minecraft:water") {
                        bool isSource=false;
                        for (auto& [k,v]: gen::propsOf(st)) if (k=="level" && v=="0") isSource=true;
                        if (!isSource) continue;
                        if (w.getBlock(wx, by+1, wz) != 0) continue;
                        w.setBlock(wx, by, wz, frosted);
                        srv_.broadcastBlockChangeFor(self_->dimension, wx, by, wz, frosted);
                    }
                }
            }
        }
    }
    {
        World& w = srv_.worldFor(self_->dimension);
        int bx = (int)std::floor(self_->x);
        int by = (int)std::floor(self_->y - 0.2);
        int bz = (int)std::floor(self_->z);
        std::uint16_t below = w.getBlock(bx, by, bz);
        const gen::BlockDef* bd = gen::blockByState(below);
        bool onSoul = bd && (std::string(bd->name)=="minecraft:soul_sand" || std::string(bd->name)=="minecraft:soul_soil");
        int soulLvl = 0;
        for(int i=5;i<=8;++i) if(!self_->inv[i].empty()) soulLvl = std::max(soulLvl, EnchantmentHelper::soulSpeedLevel(self_->inv[i]));
        int swiftLvl = 0;
        for(int i=5;i<=8;++i) if(!self_->inv[i].empty()){
            std::string n=self_->inv[i].name();
            if(n.find("leggings")!=std::string::npos) swiftLvl = std::max(swiftLvl, EnchantmentHelper::swiftSneakLevel(self_->inv[i]));
        }
        if(swiftLvl==0) for(int i=5;i<=8;++i) if(!self_->inv[i].empty()) swiftLvl = std::max(swiftLvl, EnchantmentHelper::swiftSneakLevel(self_->inv[i]));
        double before = self_->attributes.getValue(Attribute::MOVEMENT_SPEED);
        self_->attributes.syncEnchantSpeed(soulLvl, swiftLvl, self_->isSneaking, onSoul);
        double after = self_->attributes.getValue(Attribute::MOVEMENT_SPEED);
        if(std::abs(before-after) > 1e-9){
            WriteBuffer ab;
            self_->attributes.writeUpdate(ab, self_->entityId);
            self_->conn->trySendPacket(proto::pl::sc::UpdateAttributes, ab);
            srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
                                                  proto::pl::sc::UpdateAttributes, ab);
        }
        if(onSoul && soulLvl>0 && !self_->isSneaking){
            if(nextRandom()%60==0){
                for(int i=5;i<=8;++i) if(!self_->inv[i].empty() && self_->inv[i].isArmor() && EnchantmentHelper::soulSpeedLevel(self_->inv[i])>0){
                    if(DamageComponent::applyDamage(self_->inv[i], 1)){
                        self_->inv[i]=ItemStack::air();
                    }
                    srv_.resendInventory(*self_);
                    break;
                }
            }
        }
    }
    broadcastMovement();
    {
        if (srv_.tickNow() > self_->portalCooldownUntilTick) {
            World& curW = srv_.worldFor(self_->dimension);
            std::int32_t bx = static_cast<std::int32_t>(std::floor(self_->x));
            std::int32_t by = static_cast<std::int32_t>(std::floor(self_->y));
            std::int32_t bz = static_cast<std::int32_t>(std::floor(self_->z));
            bool inNether = false, inEnd = false;
            for (int dy = 0; dy <= 1; ++dy) {
                std::int32_t yy = by + dy;
                std::uint16_t st = curW.getBlock(bx, yy, bz);
                const gen::BlockDef* d = gen::blockByState(st);
                if (d) {
                    if (std::string_view(d->name) == "minecraft:nether_portal") inNether = true;
                    if (std::string_view(d->name) == "minecraft:end_portal") inEnd = true;
                }
            }
            std::int8_t target = 127;
            if (inNether) {
                if (self_->dimension == 0) target = -1;
                else if (self_->dimension == -1) target = 0;
                else if (self_->dimension == 1) target = 0;
            } else if (inEnd) {
                if (self_->dimension == 0) target = 1;
                else if (self_->dimension == 1) target = 0;
                else if (self_->dimension == -1) target = 0;
            }
            if (target != 127) {
                bool ok = PortalHandler::tryTeleport(srv_, *self_, target);
                if (ok) {
                    // Portal transfer changes the world used by tickDigs. Do
                    // not let an old-dimension dig continue at the same XYZ.
                    if (self_->digActive) srv_.broadcastDigStage(*self_, -1);
                    self_->digActive = false;
                    self_->digTotalTicks = 0;
                    self_->digLastStage = 255;
                    sentChunks_.clear();
                    lastCx_ = INT32_MAX; lastCz_ = INT32_MAX;
                    try { tickChunksAround(self_->x, self_->z); } catch (...) {}
                }
            }
        }
    }
}
void Session::broadcastMovement() {
    if (!self_->spawned) return;
    const bool first = !hasSent_;
    if (first) {                                   // initial absolute pose
        WriteBuffer b;
        b.varint(self_->entityId);
        b.f64(self_->x); b.f64(self_->y); b.f64(self_->z);
        b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
        b.boolean(self_->onGround);
        srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                              pl::sc::EntityTeleport, b);
        sentX_ = self_->x; sentY_ = self_->y; sentZ_ = self_->z;
        sentYaw_ = self_->yaw; sentPitch_ = self_->pitch;
        hasSent_ = true;
        return;
    }
    const double dx = self_->x - sentX_;
    const double dy = first ? 0 : self_->y - sentY_;
    const double dz = first ? 0 : self_->z - sentZ_;
    const bool rotated = first || self_->yaw != sentYaw_ || self_->pitch != sentPitch_;

    constexpr double kMaxRel = 7.999;              // i16 fixed point range /4096
    if (!first && dx*dx + dy*dy + dz*dz > 0.0001) {
        if (std::abs(dx) < kMaxRel && std::abs(dy) < kMaxRel && std::abs(dz) < kMaxRel) {
            if (rotated) {
                WriteBuffer b;
                b.varint(self_->entityId);
                b.i16(static_cast<std::int16_t>(dx * 4096));
                b.i16(static_cast<std::int16_t>(dy * 4096));
                b.i16(static_cast<std::int16_t>(dz * 4096));
                b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
                b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
                b.boolean(self_->onGround);
                srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                                      pl::sc::MoveEntityPosRot, b);
            } else {
                WriteBuffer b;
                b.varint(self_->entityId);
                b.i16(static_cast<std::int16_t>(dx * 4096));
                b.i16(static_cast<std::int16_t>(dy * 4096));
                b.i16(static_cast<std::int16_t>(dz * 4096));
                b.boolean(self_->onGround);
                srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                                      pl::sc::MoveEntityPos, b);
            }
            WriteBuffer h;
            h.varint(self_->entityId);
            h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
            srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                                  pl::sc::RotateHead, h);
        } else {                                    // teleport-class delta
            WriteBuffer b;
            b.varint(self_->entityId);
            b.f64(self_->x); b.f64(self_->y); b.f64(self_->z);
            b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
            b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
            b.boolean(self_->onGround);
            srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                                  pl::sc::EntityTeleport, b);
        }
    } else if (rotated) {                           // pure rotation
        WriteBuffer b;
        b.varint(self_->entityId);
        b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
        b.boolean(self_->onGround);
        srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                              pl::sc::EntityLook, b);
        WriteBuffer h;
        h.varint(self_->entityId);
        h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        srv_.broadcastPacketExceptInDimension(self_->dimension, nullptr,
                                              pl::sc::RotateHead, h);
    }

    sentX_ = self_->x; sentY_ = self_->y; sentZ_ = self_->z;
    sentYaw_ = self_->yaw; sentPitch_ = self_->pitch;
    hasSent_ = true;
}
void Session::onChatMessage(ReadBuffer& in) {
    if (spam_.onChat(srv_.tickNow())) {
        kickPlay("{\"translate\":\"disconnect.spam\"}");
        return;
    }
    const std::string msg = in.string(constants::kMaxStringLength);
    std::int64_t timestamp = in.i64();
    std::int64_t salt = in.i64();
    std::vector<std::uint8_t> signature;
    if (in.boolean()) signature = in.bytes(256);
    (void)in.varint();                               // offset
    in.bytes(3);                                     // acknowledged
    if (srv_.config().onlineMode && srv_.config().enforcesSecureChat) {
        if (signature.empty()) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "Chat message signature required (enforce-secure-profile)");
            conn_->trySendPacket(proto::pl::sc::Disconnect, kick);
            conn_->close();
            return;
        }
    }

    // events: PlayerChat (cancellable)
    api::PlayerChatEvent ev;
    ev.player = self_.get();
    ev.message = msg;
    if (!srv_.events().chat.fire(ev)) return;
    if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onChat(*self_, ev.message)) return;

    if (!ev.message.empty() && ev.message[0] == '/')
        return dispatchCommand(ev.message.substr(1));
    // Strict N6: verify RSA-SHA256 when hasChatSession; fallback to SystemChat
    bool usePlayerChat = false;
    if (self_->hasChatSession) {
        usePlayerChat = ChatMessageProcessor::verify(*self_, ev.message, timestamp, salt, signature);
        // record salt for replay soft-check (keep last 20)
        self_->lastSeenSignatures.push_back(static_cast<std::uint8_t>(salt & 0xFF));
        if (self_->lastSeenSignatures.size() > 20) self_->lastSeenSignatures.erase(self_->lastSeenSignatures.begin());
    }
    if (usePlayerChat && ChatMessageProcessor::shouldUsePlayerChat(*self_)) {
        srv_.broadcastPlayerChat(*self_, ev.message, timestamp);
    } else {
        const std::string line = "<" + self_->name + "> " + ev.message;
        srv_.broadcastSystemText(line, nullptr);
    }
}
void Session::onChatCommand(ReadBuffer& in) {
    if (spam_.onChat(srv_.tickNow())) {
        kickPlay("{\"translate\":\"disconnect.spam\"}");
        return;
    }
    const std::string cmd = in.string(constants::kMaxStringLength);
    dispatchCommand(cmd);
}
void Session::dispatchCommand(const std::string& line) {
    std::string command = line;
    if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onCommand(self_.get(), command)) return;
    brigadier::CommandSource src;
    src.player = self_.get();
    src.name = self_->name;
    src.console = false;
    src.hasOp = srv_.isOp(self_->name); // plan42 R3 (E-19): truthful op flag for kick/whitelist permission control
    src.srcX = self_->x; src.srcY = self_->y; src.srcZ = self_->z;
    src.srcYaw = self_->yaw; src.srcPitch = self_->pitch;
    srv_.bindCommandSelector(src);

    const auto res = [&]{
        try {
            return srv_.commands().execute(command, std::move(src));
        } catch (const std::exception& e) {
            // (or the process via a non-std exception); report as error chat.
            brigadier::ExecutionResult r;
            r.ok = false;
            r.errorText = std::string("Error: ") + e.what();
            return r;
        } catch (...) {
            brigadier::ExecutionResult r;
            r.ok = false;
            r.errorText = "Error: internal command failure";
            return r;
        }
    }();
    if (!res.ok)
        sendSystemText((msg::kRed + (res.errorText.empty()
                          ? "Incorrect argument for command"
                          : res.errorText)));
}
void Session::onHeldSlot(ReadBuffer& in) {
    const std::int16_t slot = in.i16();
    if (slot >= 0 && slot < 9) self_->heldSlot = slot;
}
void Session::onPlayerAction(ReadBuffer& in) {
    const std::int32_t status = in.varint();
    std::int32_t x, y, z;
    in.position(x, y, z);
    (void)in.i8();                                    // face
    const std::int32_t sequence = in.varint();

    const SessionPlayerSnapshot playerState = snapshotPlayerForSession(*self_);
    World& world = srv_.worldFor(playerState.dimension);
    auto sendAuthoritativeBlock = [&](std::int32_t bx, std::int32_t by,
                                      std::int32_t bz, std::uint16_t state) {
        WriteBuffer rb;
        rb.position(bx, by, bz);
        rb.varint(state);
            conn_->trySendPacket(proto::pl::sc::BlockUpdate, rb);
    };
    auto cancelDig = [&]() {
        if (self_->digActive) srv_.broadcastDigStage(*self_, -1);
        self_->digActive = false;
        self_->digTotalTicks = 0;
        self_->digLastStage = 255;
    };

    // BlockDig status 1 is an independent abort action. It must be handled
    // before the start/finish path so it cannot be hidden by that condition.
    if (status == 1) {
        cancelDig();
        ack(sequence);
        return;
    }

    if ((status == 0 || status == 2) &&
        !withinBlockInteractionRange(playerState, x, y, z)) {
        // A client is allowed to abort an existing dig from anywhere, but a
        // start/finish must be close enough to the block.  Canceling and
        // returning the authoritative state prevents a forged far-away
        // packet from starting or completing a server-side dig.
        cancelDig();
        sendAuthoritativeBlock(x, y, z, world.getBlock(x, y, z));
        ack(sequence);
        return;
    }

    if ((status==0 || status==2) && self_->dimension==0 && srv_.isSpawnProtected(x, z) && !srv_.isOp(self_->name)) {
        // cancel: re-send block and ack
        const std::uint16_t cur = world.getBlock(x, y, z);
        cancelDig();
        sendAuthoritativeBlock(x, y, z, cur);
        sendSystemText((msg::kRed + "Spawn protection prevents building here"));
        ack(sequence);
        return;
    }

    if (status == 0) {                                // start dig
        const std::uint16_t oldState = world.getBlock(x, y, z);
        if (self_->digActive) cancelDig();

        if (self_->gamemode != 0) {                   // creative/adventure: existing behavior
            if (oldState != 0) {
                api::BlockBreakEvent ev;
                ev.player = self_.get();
                ev.x = x; ev.y = y; ev.z = z;
                ev.oldState = oldState;
                if (!srv_.events().blockBreak.fire(ev)) {
                    ack(sequence);
                    return;
                }
                if (srv_.jvmRuntime() &&
                    !srv_.jvmRuntime()->onBlockBreak(*self_, x, y, z, oldState)) {
                    ack(sequence);
                    return;
                }
                world.setBlock(x, y, z, 0);
                srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, 0);
                if (const auto* broken = gen::blockByState(oldState);
                    broken && std::string(broken->name).find("_bed") != std::string::npos) {
                    srv_.invalidateRespawnPointsAt(self_->dimension, x, y, z);
                }
                world.scheduleNeighborUpdates(x, y, z);
            }
        } else if (oldState != 0) {
            const gen::BlockDef* def = gen::blockByState(oldState);
            if (def && def->hardness >= 0.f) {
                const MiningContext context = miningContextFor(srv_, *self_);
                const MiningResult result = MiningCalculator::calculate(*def, context);
                if (result.ticks != MiningCalculator::kUnbreakable) {
                    self_->digActive = true;
                    self_->digX = x; self_->digY = y; self_->digZ = z;
                    self_->digStartTick = MiningCalculator::packDigStartTick(
                        srv_.tickNoForTest(), oldState);
                    // Survival blocks with a zero-tick result still complete in
                    // the authoritative tick loop, never in the packet handler.
                    self_->digTotalTicks = std::max(1, result.ticks);
                    self_->digLastStage = MiningCalculator::packDigStage(0, result.harvest);
                    srv_.broadcastDigStage(*self_, 0);
                }
            }
        }
    } else if (status == 2) {                         // finish (client-side timing)
        const std::uint16_t oldState = world.getBlock(x, y, z);
        const gen::BlockDef* def = gen::blockByState(oldState);
        const bool sameTarget = self_->digActive &&
            self_->digX == x && self_->digY == y && self_->digZ == z;
        const bool sameState = self_->digActive &&
            MiningCalculator::digStartingState(self_->digStartTick) == oldState;

        if (self_->gamemode != 0) {
            // A gamemode change must not let a survival dig complete later.
            cancelDig();
        } else if (!sameTarget || !sameState || oldState == 0 || !def || def->hardness < 0.f) {
            // Finish is never an implicit start. Reject untracked/wrong-target
            // packets and re-synchronise the requested block.
            cancelDig();
            sendAuthoritativeBlock(x, y, z, oldState);
        } else {
            const std::int64_t elapsed = srv_.tickNoForTest() -
                MiningCalculator::unpackDigStartTick(self_->digStartTick);
            if (elapsed < self_->digTotalTicks) {
                // Do not shorten the stored server timing, even for a finish
                // packet that is only a few ticks early.
                cancelDig();
                sendAuthoritativeBlock(x, y, z, oldState);
            }
            // At/after the authoritative deadline, tickDigs owns mutation,
            // event dispatch, durability, and drops.
        }
    }
    ack(sequence);                                      // ALWAYS ack sequences
}
struct Session::UseItemOnRequest {
    std::int32_t x, y, z, tx, ty, tz;
    int face;
    bool survival;
    ItemUseContext context;
};

bool Session::handleUseItemOnInteractions(const UseItemOnRequest& request) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto& ctx = request.context;
    if (self_->dimension==0 && srv_.isSpawnProtected(tx, tz) && !srv_.isOp(self_->name)) {
        // check if placing a block (held is block item) – cancel
        const bool isBlockPlace = (self_->heldSlot>=0 && self_->heldSlot<9 && !self_->inv[36+self_->heldSlot].empty()
            && gen::blockByName(self_->inv[36+self_->heldSlot].name()) != nullptr);
        if (isBlockPlace) {
            sendSystemText((msg::kRed + "Spawn protection prevents building here"));
            return true;
        }
    }

    {
        const std::uint16_t _clickedSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        blockEventDispatcher().onBlockClicked(x, y, z, _clickedSt, d, self_.get());
        api::BlockClickedEvent _bcev; _bcev.player=self_.get(); _bcev.x=x; _bcev.y=y; _bcev.z=z; _bcev.state=_clickedSt; _bcev.face=d;
        if (!api::events().blockClicked.fire(_bcev)) return true;
        if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onBlockClicked(*self_, x, y, z, _clickedSt, d)) return true;
    }
    // right-click on interactive blocks opens menus (vanilla behaviour)
    {
        const std::uint16_t clickedState = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        const gen::BlockDef* bdef = gen::blockByState(clickedState);
        if (bdef) {
            const std::string bn(bdef->name);
            bool isMenuBlock = bn.find("chest") != std::string::npos ||
                bn == "minecraft:furnace" || bn == "minecraft:blast_furnace" ||
                bn == "minecraft:smoker" ||
                bn == "minecraft:hopper" || bn == "minecraft:dispenser" ||
                bn == "minecraft:dropper" ||
                bn == "minecraft:crafting_table" ||
                bn == "minecraft:enchanting_table" ||
                bn.find("anvil") != std::string::npos ||
                bn == "minecraft:brewing_stand" ||
                bn == "minecraft:stonecutter" ||
                bn == "minecraft:grindstone" ||
                bn.find("smithing") != std::string::npos ||
                bn == "minecraft:beacon" ||
                bn == "minecraft:loom" ||
                bn == "minecraft:barrel" ||
                bn.find("shulker_box") != std::string::npos ||
                bn == "minecraft:crafter" ||
                bn == "minecraft:cartography_table" ||
                bn == "minecraft:lectern";
            if (isMenuBlock) {
                // Allow opening from any face if not sneaking; ensure sneaking bypass
                if (ctx.isSneaking && !bn.empty()) {
                    // sneaking still places block, so skip menu
                } else {
                    openMenuAt(x, y, z, clickedState);
                    return true;
                }
            }
            if (d == 1) {
            // redstone interactables (lever / button / comparator) consume the click
            if (bn == "minecraft:lever" ||
                bn.find("_button") != std::string::npos ||
                bn.find("comparator") != std::string::npos) {
                srv_.redstoneFor(self_->dimension).onInteract(
                    x, y, z, srv_.tickNoForTest());
                return true;
            }
            if (bn.find("_bed") != std::string::npos &&
                bn.rfind("minecraft:", 0) == 0 && bn != "minecraft:bedrock") {
                const bool night = srv_.isNight();
                if (!night) {
                    sendSystemText((msg::kGray + "You can only sleep at night"));
                    return true;
                }
                self_->sleeping = true;
                self_->bedX = x; self_->bedY = y; self_->bedZ = z;
                self_->hasRespawnPoint = true;
                self_->respawnX = x; self_->respawnY = y; self_->respawnZ = z;
                self_->respawnDimension = GameServer::canonicalDimension(self_->dimension);
                self_->respawnAngle = self_->yaw;
                srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_);
                WriteBuffer sp;
                sp.position(x, y, z);
                sp.f32(self_->respawnAngle);
                conn_->trySendPacket(proto::pl::sc::SetDefaultSpawn, sp);
                int sleepingCount = 0, survivalCount = 0;
                for (auto& p : srv_.playersSnapshot()) {
                    if (!p->inPlay || p->gamemode != 0) continue;
                    ++survivalCount;
                    if (p->sleeping) ++sleepingCount;
                }
                if (sleepingCount >= survivalCount) {
                    srv_.setTimeOfDay(0);              // morning
                    if (srv_.raining()) srv_.forceWeatherClear();
                    for (auto& p : srv_.playersSnapshot())
                        if (p->sleeping) {
                            p->sleeping = false;
                            double wx = p->bedX + 1.5, wz = p->bedZ + 0.5;
                            WriteBuffer tb;
                            tb.varint(++teleportId_);
                            tb.f64(wx); tb.f64(p->bedY + 0.5); tb.f64(wz);
                            tb.f64(0); tb.f64(0); tb.f64(0);
                            tb.f32(p->yaw); tb.f32(0);
                            tb.u32(0);
                            p->conn->trySendPacket(proto::pl::sc::PlayerPosition, tb);
                        }
                    srv_.broadcastSystemText((msg::kGray + "Good morning!"));
                } else {
                    sendSystemText((msg::kGray + "Sleeping... (" +
                                   std::to_string(sleepingCount) + "/" +
                                   std::to_string(survivalCount) + ")"));
                }
                return true;
            }
        }
    }

    {
        World& ww = srv_.worldFor(self_->dimension);
        uint16_t cst = ww.getBlock(x,y,z);
        auto* cdef = gen::blockByState(cst);
        if (cdef && std::string(cdef->name)=="minecraft:cake") {
            if (handleCakeBlockConsume(srv_, *self_, x,y,z)) { return true; }
        }
    }
    }

    return false;
}
bool Session::handleUseItemOnToolActions(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto survival = request.survival;
    {
        InvSlot heldCopy = (self_->heldSlot >= 0 && self_->heldSlot < 9) ? self_->inv[36 + self_->heldSlot] : InvSlot::air();
        const std::string heldNameForPortal = heldCopy.empty() ? std::string() : heldCopy.name();
        bool isFlint = heldNameForPortal == "minecraft:flint_and_steel";
        bool isFireCharge = heldNameForPortal == "minecraft:fire_charge";
        if ((isFlint || isFireCharge) && !heldCopy.empty()) {
            World& w = srv_.worldFor(self_->dimension);
            std::uint16_t clickedSt = w.getBlock(x, y, z);
            const gen::BlockDef* cd = gen::blockByState(clickedSt);
            bool clickedIsObsidian = cd && std::string(cd->name) == "minecraft:obsidian";
            if (clickedIsObsidian) {
                const auto& mp = gen::blockNameToState();
                auto obsIt = mp.find("minecraft:obsidian");
                std::uint16_t obsidian = obsIt != mp.end() ? static_cast<std::uint16_t>(obsIt->second) : 2397;
                const gen::BlockDef* portalDef = gen::blockByName("minecraft:nether_portal");
                bool ignited = false;
                auto fillInterior = [&](int orient, int ox, int oy, int oz) {
                    std::uint16_t portalState = 6033;
                    if (portalDef) {
                        if (orient == 0) portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","x"}}));
                        else portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","z"}}));
                    } else {
                        auto it2 = mp.find("minecraft:nether_portal");
                        if (it2 != mp.end()) portalState = static_cast<std::uint16_t>(it2->second);
                    }
                    for (int dy=1; dy<=3; ++dy) for (int dx=1; dx<=2; ++dx) {
                        int32_t wx, wz;
                        if (orient==0) { wx = ox+dx; wz = oz; }
                        else { wx = ox; wz = oz+dx; }
                        int32_t wy = oy+dy;
                        w.setBlock(wx, wy, wz, portalState);
                        srv_.broadcastBlockChangeFor(self_->dimension, wx, wy, wz, portalState);
                        if (srv_.blockTicks()) srv_.blockTicks()->schedule(wx, wy, wz, srv_.tickNow() + 1 + (nextRandom()%20));
                    }
                    int32_t cxp = ox+1 + (orient==0?1:0);
                    int32_t czp = oz + (orient==1?1:0);
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:block.portal.ambient",
                                           cxp+0.5, oy+2, czp+0.5, 0.8f, 1.0f,
                                           "block");
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:item.flintandsteel.use",
                                           x+0.5, y+0.5, z+0.5, 1.f, 1.f,
                                           "block");
                };
                for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                    for (int ox = x - 3; ox <= x && !ignited; ++ox) {
                        if (oy < kMinY || oy+4 >= kMaxY) continue;
                        bool valid = true;
                        for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                            int32_t wx = ox+dx; int32_t wy = oy+dy; int32_t wz = z;
                            w.generateChunkIfMissing(wx>>4, wz>>4);
                            std::uint16_t st = w.getBlock(wx, wy, wz);
                            bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                            if (isBorder) { if (st != obsidian) valid=false; }
                            else { if (st != 0) valid=false; }
                        }
                        if (!valid) continue;
                        fillInterior(0, ox, oy, z);
                        ignited = true;
                    }
                }
                if (!ignited) {
                    for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                        for (int oz = z - 3; oz <= z && !ignited; ++oz) {
                            if (oy < kMinY || oy+4 >= kMaxY) continue;
                            bool valid = true;
                            for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                                int32_t wx = x; int32_t wy = oy+dy; int32_t wz = oz+dx;
                                w.generateChunkIfMissing(wx>>4, wz>>4);
                                std::uint16_t st = w.getBlock(wx, wy, wz);
                                bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                                if (isBorder) { if (st != obsidian) valid=false; }
                                else { if (st != 0) valid=false; }
                            }
                            if (!valid) continue;
                            fillInterior(1, x, oy, oz);
                            ignited = true;
                        }
                    }
                }
                if (ignited) {
                    if (self_->gamemode == 0) {
                        if (isFlint) {
                            auto* slot = &self_->inv[36 + self_->heldSlot];
                            bool broken = slot->applyDamage(1);
                            if (broken) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        } else if (isFireCharge) {
                            auto* slot = &self_->inv[36 + self_->heldSlot];
                            if (--slot->count <= 0) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        }
                    }
                    return true;
                }
            }
        }
    }

    if (!heldItem.empty() && (heldItem.name()=="minecraft:flint_and_steel" || heldItem.name()=="minecraft:fire_charge")) {
        std::uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x,y,z);
        const gen::BlockDef* cbd = gen::blockByState(clickedSt);
        if (cbd && std::string(cbd->name)=="minecraft:tnt") {
            srv_.worldFor(self_->dimension).setBlock(x,y,z,0);
            srv_.broadcastBlockChangeFor(self_->dimension, x,y,z,0);
            srv_.spawnPrimedTntFor(self_->dimension, x+0.5, y+0.5, z+0.5,
                                   0, 0.2, 0, 80);
            srv_.broadcastSoundFor(self_->dimension,
                                   "minecraft:entity.tnt.primed", x+0.5,
                                   y+0.5, z+0.5, 1.f, 1.f, "block");
            if (survival) {
                auto& mh = self_->inv[36 + self_->heldSlot];
                if (heldItem.name()=="minecraft:flint_and_steel") { if (mh.applyDamage(1)) mh = ItemStack::air(); }
                else { if (--mh.count <= 0) mh = ItemStack::air(); }
                srv_.resendInventory(*self_);
            }
            return true;
        }
    }

    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName == "minecraft:water_bucket" || heldName == "minecraft:lava_bucket") {
            std::uint16_t target = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            bool replaceable = (target == 0);
            // also consider replaceable plants? treat only air for now
            if (replaceable) {
                std::string fluidName = (heldName == "minecraft:water_bucket") ? "minecraft:water" : "minecraft:lava";
                std::uint16_t fluidState = static_cast<std::uint16_t>(gen::stateWithPropsList(fluidName, {{"level","0"}}));
                if (fluidState==0) {
                    auto it = gen::blockNameToState().find(fluidName);
                    if (it != gen::blockNameToState().end()) fluidState = static_cast<std::uint16_t>(it->second);
                }
                srv_.worldFor(self_->dimension).setBlock(tx, ty, tz, fluidState);
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, fluidState);
                if (survival) {
                    auto* mh = &self_->inv[36 + self_->heldSlot];
                    *mh = ItemStack::ofName("minecraft:bucket", 1);
                    srv_.resendInventory(*self_);
                }
                srv_.broadcastSoundFor(self_->dimension,
                                       "minecraft:item.bucket.empty", tx+0.5,
                                       ty+0.5, tz+0.5, 1.f, 1.f, "block");
                return true;
            }
        } else if (heldName == "minecraft:bucket") {
            auto tryPick = [&](std::int32_t px,std::int32_t py,std::int32_t pz)->bool{
                std::uint16_t bs = srv_.worldFor(self_->dimension).getBlock(px,py,pz);
                const gen::BlockDef* bd = gen::blockByState(bs);
                if (!bd) return false;
                bool isWater=false,isLava=false;
                if (bd->name=="minecraft:water") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isWater=true;
                } else if (bd->name=="minecraft:lava") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isLava=true;
                }
                if (!isWater && !isLava) return false;
                srv_.worldFor(self_->dimension).setBlock(px,py,pz, 0);
                srv_.broadcastBlockChangeFor(self_->dimension, px,py,pz, 0);
                if (survival) {
                    auto* mh = &self_->inv[36 + self_->heldSlot];
                    std::string newName = isWater ? "minecraft:water_bucket" : "minecraft:lava_bucket";
                    *mh = ItemStack::ofName(newName, 1);
                    srv_.resendInventory(*self_);
                    srv_.onBucketFilled(*self_, newName);
                } else {
                    std::string newName = isWater ? "minecraft:water_bucket" : "minecraft:lava_bucket";
                    srv_.onBucketFilled(*self_, newName);
                }
                srv_.broadcastSoundFor(self_->dimension,
                                       "minecraft:item.bucket.fill", px+0.5,
                                       py+0.5, pz+0.5, 1.f, 1.f, "block");
                return true;
            };
            if (tryPick(x,y,z) || tryPick(tx,ty,tz)) {
                return true;
            }
        } else if (heldName == "minecraft:flint_and_steel" || heldName == "minecraft:fire_charge") {
            std::uint16_t target = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            if (target == 0) {
                bool canPlace = true;
                if (srv_.gameRules().contains("doFireTick") && !srv_.gameRules().getBool("doFireTick")) canPlace = false;
                if (canPlace) {
                    std::uint16_t belowSt = srv_.worldFor(self_->dimension).getBlock(tx, ty-1, tz);
                    const gen::BlockDef* belowDef = gen::blockByState(belowSt);
                    bool soulBase = false;
                    if (belowDef) {
                        auto &tags = srv_.tagManager_.blockTags;
                        auto it = tags.find("minecraft:soul_fire_base_blocks");
                        if (it != tags.end()) {
                            auto nit = gen::blockNameToState().find(std::string(belowDef->name));
                            if (nit != gen::blockNameToState().end()) {
                                uint32_t defId = static_cast<uint32_t>(nit->second);
                                soulBase = it->second.count(defId) > 0;
                            }
                        }
                        if (!soulBase) {
                            soulBase = std::string(belowDef->name)=="minecraft:soul_sand" || std::string(belowDef->name)=="minecraft:soul_soil";
                        }
                    }
                    std::string fireName = soulBase ? "minecraft:soul_fire" : "minecraft:fire";
                    auto it = gen::blockNameToState().find(fireName);
                    if (it == gen::blockNameToState().end()) it = gen::blockNameToState().find("minecraft:fire");
                    if (it != gen::blockNameToState().end()) {
                        std::uint16_t fireState = static_cast<std::uint16_t>(it->second);
                        srv_.worldFor(self_->dimension).setBlock(tx, ty, tz, fireState);
                        srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, fireState);
                        if (survival) {
                            auto* mh = &self_->inv[36 + self_->heldSlot];
                            if (heldName=="minecraft:flint_and_steel") {
                                if (mh->applyDamage(1)) *mh = ItemStack::air();
                            } else {
                                if (--mh->count <=0) *mh = ItemStack::air();
                            }
                            srv_.resendInventory(*self_);
                        }
                        srv_.broadcastSoundFor(self_->dimension,
                                               "minecraft:item.flintandsteel.use",
                                               tx+0.5, ty+0.5, tz+0.5, 1.f,
                                               1.f, "block");
                    }
                }
                return true;
            }
        }
    }

    // ---- bone meal fertilize hook ----
    if (!heldItem.empty() && heldItem.name() == "minecraft:bone_meal") {
        const std::uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        if (clickedSt != 0) {
            const gen::BlockDef* cb = gen::blockByState(clickedSt);
            if (cb) {
                const std::string bn(cb->name);
                auto* beh = srv_.blockTicksFor(self_->dimension).behaviorFor(bn);
                if (beh && beh->fertilize(srv_.worldFor(self_->dimension), x, y, z, clickedSt, &srv_)) {
                    const std::uint16_t newSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
                    srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, newSt);
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:item.bone_meal.use",
                                           x + 0.5, y + 0.5, z + 0.5);
                    if (survival) {
                        auto* mh = &self_->inv[36 + self_->heldSlot];
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
                    return true;
                }
            }
        }
    }

    return false;
}
bool Session::handleUseItemOnDoorAndSlab(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto dir = request.face;
    const auto survival = request.survival;
    const auto& ctx = request.context;
    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName.size() > 5 && heldName.rfind("_door", heldName.size() - 5) != std::string::npos) {
            const gen::BlockDef* ddef = gen::blockByName(heldName);
            if (ddef && srv_.worldFor(self_->dimension).getBlock(tx, ty, tz) == 0 &&
                srv_.worldFor(self_->dimension).getBlock(tx, ty + 1, tz) == 0) {
                float yaw = self_->yaw;
                const char* facing = "north";
                if (yaw >= 45.f && yaw < 135.f) facing = "west";
                else if (yaw >= 135.f && yaw < 225.f) facing = "south";
                else if (yaw >= 225.f && yaw < 315.f) facing = "east";
                // hinge logic via solid faces and neighboring doors (vanilla DoorBlock per Yarn 1.21.4: isFullCube count + hitPos tie - strict B5)
                std::string hingeStr = "left";
                {
                    auto isFullCubeAt = [&](int nx,int ny,int nz)->bool{
                        uint16_t s2 = srv_.worldFor(self_->dimension).getBlock(nx,ny,nz);
                        if(s2==0) return false;
                        auto* bd2 = gen::blockByState(s2);
                        if(!bd2) return false;
                        std::string n(bd2->name);
                        if(n.find("_slab")!=std::string::npos){
                            for(auto& [k,v]: gen::propsOf(s2)) if(k=="type" && v!="double") return false;
                        }
                        if(n.find("stairs")!=std::string::npos) return false;
                        return !bd2->transparent;
                    };
                    auto isDoorLowerAt = [&](int nx,int ny,int nz)->bool{
                        uint16_t s2 = srv_.worldFor(self_->dimension).getBlock(nx,ny,nz);
                        if(s2==0) return false;
                        auto* bd2 = gen::blockByState(s2);
                        if(!bd2 || std::string(bd2->name).find("_door")==std::string::npos) return false;
                        for(auto& [k,v]: gen::propsOf(s2)) if(k=="half" && v=="lower") return true;
                        return false;
                    };
                    int dxL=0, dzL=0, dxR=0, dzR=0;
                    std::string fs(facing);
                    if(fs=="north"){ dxL=-1; dzL=0; dxR=1; dzR=0; }
                    else if(fs=="south"){ dxL=1; dzL=0; dxR=-1; dzR=0; }
                    else if(fs=="west"){ dxL=0; dzL=1; dxR=0; dzR=-1; }
                    else if(fs=="east"){ dxL=0; dzL=-1; dxR=0; dzR=1; }
                    else { dxL=-1; dzL=0; dxR=1; dzR=0; }
                    int i = 0;
                    if(isFullCubeAt(tx+dxL, ty, tz+dzL)) i += -1;
                    if(isFullCubeAt(tx+dxL, ty+1, tz+dzL)) i += -1;
                    if(isFullCubeAt(tx+dxR, ty, tz+dzR)) i += 1;
                    if(isFullCubeAt(tx+dxR, ty+1, tz+dzR)) i += 1;
                    bool leftDoor = isDoorLowerAt(tx+dxL, ty, tz+dzL);
                    bool rightDoor = isDoorLowerAt(tx+dxR, ty, tz+dzR);
                    if((!leftDoor || rightDoor) && i <= 0){
                        if((!rightDoor || leftDoor) && i >= 0){
                            int j = (fs=="east"?1: fs=="west"?-1:0);
                            int k = (fs=="south"?1: fs=="north"?-1:0);
                            double d = ctx.cursor.x;
                            double e = ctx.cursor.z;
                            bool chooseLeft = (j >= 0 || !(e < 0.5)) && (j <= 0 || !(e > 0.5)) && (k >= 0 || !(d > 0.5)) && (k <= 0 || !(d < 0.5));
                            hingeStr = chooseLeft ? "left" : "right";
                        } else {
                            hingeStr = "left";
                        }
                    } else {
                        hingeStr = "right";
                    }
                }
                bool powered = false;
                powered = srv_.redstoneFor(self_->dimension).isPoweredHere(tx,ty,tz) ||
                          srv_.redstoneFor(self_->dimension).isPoweredHere(tx,ty+1,tz);
                std::string openStr = powered ? "true" : "false";
                std::string poweredStr = powered ? "true" : "false";
                const auto lower =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","lower"},{"facing",facing},{"open",openStr},{"hinge",hingeStr},{"powered",poweredStr}}));
                const auto upper =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","upper"},{"facing",facing},{"open",openStr},{"hinge",hingeStr},{"powered",poweredStr}}));
                srv_.worldFor(self_->dimension).setBlock(tx, ty, tz, lower);
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, lower);
                srv_.worldFor(self_->dimension).setBlock(tx, ty + 1, tz, upper);
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty + 1, tz, upper);
                if (survival) {
                    auto mh = &self_->inv[36 + self_->heldSlot];
                    if (ItemStack::maxDamageFor(mh->itemId) > 0) {
                        if (mh->applyDamage(1)) *mh = ItemStack::air();
                        srv_.resendInventory(*self_);
                    } else {
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
                }
                return true;
            }
        }
    }

    if (!heldItem.empty()) {
        std::string hName = heldItem.name();
        if (hName.find("_slab") != std::string::npos) {
            uint16_t existing = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            const gen::BlockDef* ed = gen::blockByState(existing);
            if (ed && std::string(ed->name) == hName) {
                std::string curType = getPropStr(existing, "type");
                if (curType != "double") {
                    bool isBottom = curType=="bottom";
                    bool hittingOpposite = false;
                    if(isBottom) hittingOpposite = (d==1 || ctx.cursor.y > 0.5);
                    else /* top */ hittingOpposite = (d==0 || ctx.cursor.y < 0.5);
                    if(hittingOpposite){
                        std::vector<std::pair<std::string_view,std::string_view>> p;
                        for(auto&[k,v]: gen::propsOf(existing)) if(k!="type" && k!="waterlogged") p.emplace_back(k,v);
                        p.emplace_back("type","double");
                        // double slab must be waterlogged false
                        bool hasWl=false; for(int i=0;i<ed->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[ed->propsOff+i]]; if(pd.name=="waterlogged") hasWl=true; }
                        if(hasWl) p.emplace_back("waterlogged","false");
                        uint16_t dbl = static_cast<uint16_t>(gen::stateWithProps(*ed, p));
                        api::BlockPlaceEvent ev2; ev2.player=self_.get(); ev2.x=tx; ev2.y=ty; ev2.z=tz; ev2.newState=dbl;
                        if (srv_.events().blockPlace.fire(ev2)) {
                            srv_.worldFor(self_->dimension).setBlock(tx,ty,tz,dbl);
                            srv_.broadcastBlockChangeFor(self_->dimension, tx,ty,tz,dbl);
                            if (survival) {
                                auto* mh=&self_->inv[36 + self_->heldSlot];
                                if(--mh->count<=0) *mh=ItemStack::air();
                                srv_.resendInventory(*self_);
                            }
                            return true;
                        }
                    } else {
                        // same half → try adjacent placement (vanilla places single slab at offset)
                        const int adjX = tx + kBlockFaceOffsetX[d];
                        const int adjY = ty + kBlockFaceOffsetY[d];
                        const int adjZ = tz + kBlockFaceOffsetZ[d];
                        if (srv_.worldFor(self_->dimension).getBlock(adjX,adjY,adjZ)==0) {
                            const gen::BlockDef* sdef = gen::blockByName(hName);
                            if(sdef){
                                const char* newType;
                                if(d==1) newType="bottom";
                                else if(d==0) newType="top";
                                else newType = (ctx.cursor.y > 0.5 ? "top" : "bottom");
                                auto adjFs = FluidSim::getFluidState(srv_.worldFor(self_->dimension), adjX, adjY, adjZ);
                                bool wl = adjFs.isStillWater();
                                bool hasWlAdj=false; for(int i=0;i<sdef->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[sdef->propsOff+i]]; if(pd.name=="waterlogged") hasWlAdj=true; }
                                std::vector<std::pair<std::string_view,std::string_view>> ap;
                                ap.emplace_back("type", newType);
                                if(hasWlAdj) ap.emplace_back("waterlogged", wl?"true":"false");
                                uint16_t adjSt = static_cast<uint16_t>(gen::stateWithProps(*sdef, ap));
                                api::BlockPlaceEvent evA; evA.player=self_.get(); evA.x=adjX; evA.y=adjY; evA.z=adjZ; evA.newState=adjSt;
                                if(srv_.events().blockPlace.fire(evA)){
                                    srv_.worldFor(self_->dimension).setBlock(adjX,adjY,adjZ,adjSt);
                                    srv_.broadcastBlockChangeFor(self_->dimension, adjX,adjY,adjZ,adjSt);
                                    if(wl) srv_.fluidsFor(self_->dimension).touch(adjX,adjY,adjZ);
                                    if(survival){
                                        auto* mh=&self_->inv[36 + self_->heldSlot];
                                        if(--mh->count<=0) *mh=ItemStack::air();
                                        srv_.resendInventory(*self_);
                                    }
                                    return true;
                                }
                            }
                        }
                        // same half but adjacent blocked → do not make double, fall through to normal handling (will ack without placing)
                        return true;
                    }
                }
            }
        }
        // also check placing slab onto existing slab at click position? vanilla allows placing slab on top of clicked slab to make double.
        // If tx is offset, also check clicked pos if it is slab and face is up/down
        if (hName.find("_slab") != std::string::npos) {
            uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x,y,z);
            const gen::BlockDef* cd = gen::blockByState(clickedSt);
            if (cd && std::string(cd->name) == hName) {
                std::string curType = getPropStr(clickedSt, "type");
                if (curType != "double") {
                    // Only double if clicking top of bottom slab or bottom of top slab
                    std::string chalf = getPropStr(clickedSt, "type");
                    bool canDouble = false;
                    if (chalf=="bottom" && dir==1) canDouble=true;
                    if (chalf=="top" && dir==0) canDouble=true;
                    if (canDouble) {
                        std::vector<std::pair<std::string_view,std::string_view>> p;
                        for(auto&[k,v]: gen::propsOf(clickedSt)) if(k!="type" && k!="waterlogged") p.emplace_back(k,v);
                        p.emplace_back("type","double");
                        bool hasWl=false; for(int i=0;i<cd->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[cd->propsOff+i]]; if(pd.name=="waterlogged") hasWl=true; }
                        if(hasWl) p.emplace_back("waterlogged","false");
                        uint16_t dbl = static_cast<uint16_t>(gen::stateWithProps(*cd, p));
                        srv_.worldFor(self_->dimension).setBlock(x,y,z,dbl);
                        srv_.broadcastBlockChangeFor(self_->dimension, x,y,z,dbl);
                        if (survival) {
                            auto* mh=&self_->inv[36 + self_->heldSlot];
                            if(--mh->count<=0) *mh=ItemStack::air();
                            srv_.resendInventory(*self_);
                        }
                        return true;
                    }
                }
            }
        }
    }

    return false;
}
bool Session::handleUseItemOnOccupied(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    if (srv_.worldFor(self_->dimension).getBlock(tx, ty, tz) != 0 || heldItem.empty()) {
        const std::uint16_t clickedState = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        const gen::BlockDef* cdef = gen::blockByState(clickedState);
        if (cdef && cdef->name.size() > 5 &&
            cdef->name.rfind("_door", cdef->name.size() - 5) != std::string::npos) {
            bool isIron = std::string(cdef->name) == "minecraft:iron_door";
            if(isIron){
                return true;
            }
            bool open = false, upperHalf = false;
            bool powered = false;
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "open") open = v == "true";
                if (k == "half") upperHalf = v == "upper";
                if (k == "powered") powered = v == "true";
            }
            std::string facing;
            std::string hinge = "left";
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "facing") facing = std::string(v);
                if (k == "hinge") hinge = std::string(v);
            }
            const std::uint16_t st1 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "upper" : "lower"},
                     {"facing", facing}, {"hinge", hinge}, {"powered", powered?"true":"false"}}));
            const std::int32_t oy = upperHalf ? y - 1 : y + 1;
            const std::uint16_t st2 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "lower" : "upper"},
                     {"facing", facing}, {"hinge", hinge}, {"powered", powered?"true":"false"}}));
            srv_.worldFor(self_->dimension).setBlock(x, y, z, st1);
            srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, st1);
            srv_.worldFor(self_->dimension).setBlock(x, oy, z, st2);
            srv_.broadcastBlockChangeFor(self_->dimension, x, oy, z, st2);
            srv_.broadcastSoundFor(self_->dimension,
                                   "minecraft:block.wooden_door.toggle",
                                   x + .5, y + .5, z + .5, 1.f,
                                   open ? 0.7f : 0.9f);
        }
        return true;
    }
    return false;
}
bool Session::handleUseItemOnEntityItems(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto survival = request.survival;
    // item id -> block name (block items share the name)
    std::string itemName = heldItem.name();
    {
        BlockPos hitPos{x, y, z};
        if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
            ItemStack& stk = self_->inv[36 + self_->heldSlot];
            if (!stk.empty() && stk.name().ends_with("_spawn_egg")) {
                if (srv_.trySpawnEgg(*self_, stk, hitPos, d)) return true;
            }
        }
        // keep itemName endsWith check for tooling/grep
        if (itemName.ends_with("_spawn_egg")) {
            // handled via trySpawnEgg above
        }
    }
    {
        if (!heldItem.empty()) {
            std::string hName = heldItem.name();
            bool isBoatItem = hName.ends_with("_boat") || hName.ends_with("_raft");
            if (isBoatItem) {
                auto kindOpt = [&]() -> std::optional<MobKind> {
                    if (hName=="minecraft:oak_boat") return MobKind::OakBoat;
                    if (hName=="minecraft:spruce_boat") return MobKind::SpruceBoat;
                    if (hName=="minecraft:birch_boat") return MobKind::BirchBoat;
                    if (hName=="minecraft:jungle_boat") return MobKind::JungleBoat;
                    if (hName=="minecraft:acacia_boat") return MobKind::AcaciaBoat;
                    if (hName=="minecraft:dark_oak_boat") return MobKind::DarkOakBoat;
                    if (hName=="minecraft:mangrove_boat") return MobKind::MangroveBoat;
                    if (hName=="minecraft:cherry_boat") return MobKind::CherryBoat;
                    if (hName=="minecraft:pale_oak_boat") return MobKind::PaleOakBoat;
                    if (hName=="minecraft:bamboo_raft") return MobKind::BambooRaft;
                    if (hName=="minecraft:oak_chest_boat") return MobKind::OakChestBoat;
                    if (hName=="minecraft:spruce_chest_boat") return MobKind::SpruceChestBoat;
                    if (hName=="minecraft:birch_chest_boat") return MobKind::BirchChestBoat;
                    if (hName=="minecraft:jungle_chest_boat") return MobKind::JungleChestBoat;
                    if (hName=="minecraft:acacia_chest_boat") return MobKind::AcaciaChestBoat;
                    if (hName=="minecraft:dark_oak_chest_boat") return MobKind::DarkOakChestBoat;
                    if (hName=="minecraft:mangrove_chest_boat") return MobKind::MangroveChestBoat;
                    if (hName=="minecraft:cherry_chest_boat") return MobKind::CherryChestBoat;
                    if (hName=="minecraft:pale_oak_chest_boat") return MobKind::PaleOakChestBoat;
                    if (hName=="minecraft:bamboo_chest_raft") return MobKind::BambooChestRaft;
                    return std::nullopt;
                }();
                if (kindOpt) {
                    double sx = tx + 0.5, sy = ty + 0.1, sz = tz + 0.5;
                    srv_.spawnMobFor(self_->dimension, *kindOpt, sx, sy, sz);
                    if (survival) {
                        auto* mh = &self_->inv[36 + self_->heldSlot];
                        if (--mh->count <= 0) *mh = ItemStack::air();
                        srv_.resendInventory(*self_);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}
void Session::placeUseItemOnBlock(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto survival = request.survival;
    const auto& ctx = request.context;
    const std::string itemName = heldItem.name();
    std::uint16_t newState = 0;
    const gen::BlockDef* bdef2 = gen::blockByName(itemName);
    if (!bdef2) {                                          // not a placeable block
        // special items handled elsewhere (food via UseItem); nothing to do

        return;
    }
    std::vector<std::pair<std::string_view, std::string_view>> props;
    {
        float yaw = ctx.yaw;
        const char* facing = "north";
        if (yaw >= 45.f && yaw < 135.f) facing = "east";
        else if (yaw >= 135.f && yaw < 225.f) facing = "south";
        else if (yaw >= 225.f && yaw < 315.f) facing = "west";
        bool hasFacing = false;
        bool hasHalf = false, hasShape = false, hasSnowy = false, hasWaterlogged = false, hasAxis = false;
        bool hasOrientation = false;
        for (int i = 0; i < bdef2->propCount; ++i) {
            const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[bdef2->propsOff + i]];
            if (pd.name == "facing") hasFacing = true;
            if (pd.name == "half") hasHalf = true;
            if (pd.name == "shape") hasShape = true;
            if (pd.name == "snowy") hasSnowy = true;
            if (pd.name == "waterlogged") hasWaterlogged = true;
            if (pd.name == "axis") hasAxis = true;
            if (pd.name == "orientation") hasOrientation = true;
        }
        if (hasFacing) props.emplace_back("facing", facing);
        if (hasHalf) {
            const char* half = "bottom";
            if (ctx.face == 0) half = "top";
            else if (ctx.face == 1) half = "bottom";
            else {
                half = (ctx.cursor.y > 0.5 ? "top" : "bottom");
            }
            props.emplace_back("half", half);
        }
        if (hasShape) {
            std::string facingStr = std::string(facing);
            std::string halfStr = "bottom";
            for(auto& pr: props) if(pr.first=="half") halfStr=std::string(pr.second);
            std::string shape = computeStairsShape(*ctx.world, ctx.placePos.x, ctx.placePos.y, ctx.placePos.z, facingStr, halfStr);
            props.emplace_back("shape", shape);
        }
        if (hasWaterlogged) {
            bool waterlogged = false;
            auto fluid = FluidSim::getFluidState(*ctx.world, ctx.placePos.x, ctx.placePos.y, ctx.placePos.z);
            if (fluid.isStillWater()) waterlogged = true;
            // For double slab, force false (handled earlier)
            bool isDoubleSlab = false;
            for(auto& pr: props) if(pr.first=="type" && pr.second=="double") isDoubleSlab=true;
            if(isDoubleSlab) waterlogged=false;
            props.emplace_back("waterlogged", waterlogged ? "true" : "false");
        }
        if (hasSnowy) {
            bool snowy = false;
            std::uint16_t above = ctx.world->getBlock(ctx.placePos.x, ctx.placePos.y + 1, ctx.placePos.z);
            const gen::BlockDef* ad = gen::blockByState(above);
            if (ad && (std::string(ad->name) == "minecraft:snow" || std::string(ad->name) == "minecraft:snow_block" || std::string(ad->name) == "minecraft:powder_snow")) snowy = true;
            props.emplace_back("snowy", snowy ? "true" : "false");
        }
        if (hasAxis) {
            const char* axis = "y";
            if (ctx.face == 4 || ctx.face == 5) axis = "x";
            else if (ctx.face == 2 || ctx.face == 3) axis = "z";
            props.emplace_back("axis", axis);
        }
        if (hasOrientation) {
            // Crafter's `orientation` is the ordered pair front_top.  A
            // vertical placement uses the player's horizontal direction for
            // the top; a horizontal placement keeps the top upright.  The
            // first token is also the ejection/front direction used by the
            // redstone craft path.
            std::string orientation;
            if (ctx.face == 0 || ctx.face == 1) {
                orientation = (ctx.face == 0 ? "down_" : "up_");
                orientation += facing;
            } else {
                orientation = facing;
                orientation += "_up";
            }
            props.emplace_back("orientation", orientation);
        }
        // slab type handling: reuse half logic as type
        bool hasTypeSlab = false;
        for (int i = 0; i < bdef2->propCount; ++i) {
            const auto& pd = gen::kPropDefs[gen::kBlockPropsRun[bdef2->propsOff + i]];
            if (pd.name == "type") { hasTypeSlab = true; break; }
        }
        if (hasTypeSlab && std::string(bdef2->name).find("_slab") != std::string::npos) {
            const char* type = "bottom";
            if (ctx.face == 0) type = "top";
            else if (ctx.face == 1) type = "bottom";
            else type = (ctx.cursor.y > 0.5 ? "top" : "bottom");
            // remove previous if any, then add
            props.emplace_back("type", type);
        }
        newState = static_cast<std::uint16_t>(gen::stateWithProps(*bdef2, props));
        if (std::string(bdef2->name)=="minecraft:creaking_heart") {
            std::string axis="y"; for(auto& pr: props) if(pr.first=="axis") axis=std::string(pr.second);
            std::vector<std::pair<std::string_view,std::string_view>> cprops;
            cprops.emplace_back("axis", axis);
            cprops.emplace_back("natural", "false");
            cprops.emplace_back("active", "false");
            newState = static_cast<std::uint16_t>(gen::stateWithProps(*bdef2, cprops));
        }
    }

    api::BlockPlaceEvent ev;
    ev.player = self_.get();
    ev.x = tx; ev.y = ty; ev.z = tz;
    ev.newState = newState;
    if (!srv_.events().blockPlace.fire(ev)) {  return; }
    if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onBlockPlace(*self_, tx, ty, tz, newState)) return;

    srv_.worldFor(self_->dimension).setBlock(tx, ty, tz, newState);
    if (bdef2->name == "minecraft:crafter") {
        auto& store = srv_.blockEntitiesFor(self_->dimension);
        auto* be = store.getAt(tx, ty, tz);
        if (!be || be->kind != BlockEntity::Kind::Crafter)
            store.create(posKey(tx, ty, tz), BlockEntity::Kind::Crafter);
    }
    srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, newState);
    if (std::string(bdef2->name)=="minecraft:bamboo") {
        auto* bambooDef = gen::blockByName("minecraft:bamboo");
        if (bambooDef) {
            bool needsFix=false;
            std::string curLeaves, curStage, curAge;
            for(auto&[k,v]: gen::propsOf(newState)) {
                if(k=="leaves") curLeaves=std::string(v);
                if(k=="stage") curStage=std::string(v);
                if(k=="age") curAge=std::string(v);
            }
            if(curLeaves!="none" || curStage!="0" || curAge!="0") needsFix=true;
            if (needsFix) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                props.emplace_back("leaves","none");
                props.emplace_back("stage","0");
                props.emplace_back("age","0");
                std::uint16_t fixed = static_cast<std::uint16_t>(gen::stateWithProps(*bambooDef, props));
                srv_.worldFor(self_->dimension).setBlock(tx,ty,tz, fixed);
                srv_.broadcastBlockChangeFor(self_->dimension, tx,ty,tz, fixed);
                newState = fixed;
            }
            int by = ty;
            while (by > kMinY) {
                std::uint16_t bs = srv_.worldFor(self_->dimension).getBlock(tx, by-1, tz);
                if (bs==0) break;
                auto* bd = gen::blockByState(bs);
                if(!bd || std::string(bd->name)!="minecraft:bamboo") break;
                --by;
            }
            int h=0;
            for(int yy=by; yy<kMaxY; ++yy) {
                std::uint16_t bs = srv_.worldFor(self_->dimension).getBlock(tx, yy, tz);
                if (bs==0) break;
                auto* bd = gen::blockByState(bs);
                if(!bd || std::string(bd->name)!="minecraft:bamboo") break;
                ++h;
            }
            auto bambooLeavesFor = [](int h, int dist)->std::string {
                if(dist==0) { if(h==1) return "none"; if(h==2) return "small"; return "large"; }
                if(dist==1) { if(h==2) return "none"; if(h==3) return "small"; if(h>=4) return "large"; return "none"; }
                if(dist==2) { if(h>=5) return "small"; return "none"; }
                return "none";
            };
            bool thick = h>=4;
            for(int i=0;i<h;++i){
                int yy = by + i;
                int dist = h-1 - i;
                std::string want = bambooLeavesFor(h, dist);
                std::uint16_t st = srv_.worldFor(self_->dimension).getBlock(tx, yy, tz);
                auto* d = gen::blockByState(st);
                if(!d) continue;
                std::string curL;
                for(auto&[k,v]: gen::propsOf(st)) if(k=="leaves") curL=std::string(v);
                int curA=0;
                for(auto&[k,v]: gen::propsOf(st)) if(k=="age") curA=std::atoi(std::string(v).c_str());
                int wantA = thick?1:curA;
                if(curL!=want || (thick && curA!=1)) {
                    std::vector<std::pair<std::string_view,std::string_view>> props;
                    for(auto&[k,v]: gen::propsOf(st)) if(k!="leaves" && k!="age") props.emplace_back(k,v);
                    props.emplace_back("leaves", want);
                    const std::string ageString = std::to_string(wantA);
                    props.emplace_back("age", ageString);
                    std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
                    srv_.worldFor(self_->dimension).setBlock(tx, yy, tz, ns);
                    srv_.broadcastBlockChangeFor(self_->dimension, tx, yy, tz, ns);
                }
            }
        }
    }
    srv_.worldFor(self_->dimension).scheduleNeighborUpdates(tx, ty, tz);
    if (std::string(bdef2->name).find("_stairs") != std::string::npos) {
        updateNeighborStairsShapes(srv_.worldFor(self_->dimension), srv_, tx, ty, tz);
        // schedule fluid tick if waterlogged
        std::string wl = getPropStr(newState, "waterlogged");
        if (wl=="true") {
            srv_.fluidsFor(self_->dimension).touch(tx,ty,tz);
        }
    } else {
        std::string wl = getPropStr(newState, "waterlogged");
        if (wl=="true") srv_.fluidsFor(self_->dimension).touch(tx,ty,tz);
    }
    {
        std::uint16_t oldSt = 0; // air before
        blockEventDispatcher().onBlockPlace(tx, ty, tz, oldSt, newState, self_.get());
    }
    srv_.onPlacedBlock(*self_, tx, ty, tz, newState);
    if (survival) {
        auto mutableHeld = &self_->inv[36 + self_->heldSlot];
        if (ItemStack::maxDamageFor(mutableHeld->itemId) > 0) {
            if (mutableHeld->applyDamage(1)) *mutableHeld = ItemStack::air();
        } else {
            if (--mutableHeld->count <= 0) *mutableHeld = ItemStack::air();
        }
        srv_.resendInventory(*self_);
    }
}
void Session::onUseItemOn(ReadBuffer& in) {
    (void)in.varint();                                  // hand
    std::int32_t x, y, z;
    in.position(x, y, z);
    const std::int32_t dir = in.varint();
    const float cursorX = in.f32();
    const float cursorY = in.f32();
    const float cursorZ = in.f32();
    (void)cursorX; (void)cursorZ;
    (void)in.boolean();                                 // inside block
    (void)in.boolean();                                 // world border hit
    const std::int32_t sequence = in.varint();

    const SessionPlayerSnapshot playerState = snapshotPlayerForSession(*self_);
    if (!withinBlockInteractionRange(playerState, x, y, z)) {
        // The sequence still needs an acknowledgement, but no interaction,
        // callback, item consumption, or placement may be driven by a block
        // outside the server-side interaction range.
        ack(sequence);
        return;
    }

    const int d = (dir >= 0 && dir < 6) ? dir : 0;
    const std::int32_t tx = x + kBlockFaceOffsetX[d], ty = y + kBlockFaceOffsetY[d], tz = z + kBlockFaceOffsetZ[d];

    ItemUseContext ctx;
    ctx.player = self_.get();
    ctx.world = &srv_.worldFor(playerState.dimension);
    ctx.hitPos = {x, y, z};
    ctx.placePos = {tx, ty, tz};
    ctx.face = d;
    ctx.cursor = {static_cast<double>(cursorX), static_cast<double>(cursorY), static_cast<double>(cursorZ)};
    ctx.yaw = playerState.yaw;
    ctx.isSneaking = playerState.sneaking;

    const UseItemOnRequest request{x, y, z, tx, ty, tz, d,
                                  playerState.gamemode == 0, ctx};
    if (handleUseItemOnInteractions(request)) {
        ack(sequence);
        return;
    }

    const InvSlot airSlot = InvSlot::air();
    const InvSlot heldItem =
        (playerState.heldSlot >= 0 && playerState.heldSlot < 9)
            ? [&]() {
                  std::lock_guard playerLock(self_->stateMtx);
                  return self_->inv[36 + playerState.heldSlot];
              }()
            : airSlot;
    if (!heldItem.empty()) {
        srv_.onItemUsedOnBlock(self_.get(), x, y, z, heldItem);
    }
    if (handleUseItemOnToolActions(request, heldItem) ||
        handleUseItemOnDoorAndSlab(request, heldItem) ||
        handleUseItemOnOccupied(request, heldItem) ||
        handleUseItemOnEntityItems(request, heldItem)) {
        ack(sequence);
        return;
    }

    placeUseItemOnBlock(request, heldItem);
    ack(sequence);
}
void Session::onUseItem(ReadBuffer& in) {
    (void)in.varint();
    const std::int32_t sequence = in.varint();
    (void)in.f32(); (void)in.f32();
    if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
        auto& sl = self_->inv[36 + self_->heldSlot];
        {
            bool shieldHeld = (!sl.empty() && sl.name().find("shield") != std::string::npos) ||
                              (!self_->inv[45].empty() && self_->inv[45].name().find("shield") != std::string::npos);
            if (shieldHeld && self_->shieldDisableTicks <= 0) self_->isBlocking = true;
        }
        if (!sl.empty() && sl.name().find("trident")!=std::string::npos) {
            int riptideLvl = EnchantmentHelper::getRiptide(sl);
            bool isInWater = false;
            {
                // check block at feet is water
                uint16_t bst = srv_.worldFor(self_->dimension).getBlock((int)std::floor(self_->x), (int)std::floor(self_->y), (int)std::floor(self_->z));
                if (auto* bd = gen::blockByState(bst)) {
                    std::string bn(bd->name);
                    if (bn.find("water")!=std::string::npos) isInWater = true;
                }
                // also check block at eye height
                if (!isInWater) {
                    uint16_t bst2 = srv_.worldFor(self_->dimension).getBlock((int)std::floor(self_->x), (int)std::floor(self_->y+1), (int)std::floor(self_->z));
                    if (auto* bd2 = gen::blockByState(bst2)) {
                        std::string bn2(bd2->name);
                        if (bn2.find("water")!=std::string::npos) isInWater = true;
                    }
                }
            }
            bool raining = srv_.raining() || srv_.thundering();
            if (riptideLvl>0 && (raining || isInWater)) {
                // riptide: propel player in look direction, damage trident, no projectile
                double yawRad = self_->yaw * 3.14159265/180.0;
                double pitchRad = self_->pitch * 3.14159265/180.0;
                double vx = -std::sin(yawRad)*std::cos(pitchRad)*(1.2f * riptideLvl);
                double vy = -std::sin(pitchRad)*(1.2f * riptideLvl);
                double vz =  std::cos(yawRad)*std::cos(pitchRad)*(1.2f * riptideLvl);
                // apply velocity via EntityVelocity packet
                WriteBuffer vel;
                vel.varint(self_->entityId);
                vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(vy*8000)); vel.i16((int16_t)(vz*8000));
                self_->conn->trySendPacket(proto::pl::sc::EntityVelocity, vel);
                // also broadcast to others
                srv_.broadcastPacketExceptInDimension(self_->dimension,
                                                      self_.get(),
                                                      proto::pl::sc::EntityVelocity, vel);
                if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                    if (sl.applyDamage(1)) sl = ItemStack::air();
                    srv_.resendInventory(*self_);
                }
                ack(sequence);
                return;
            }
            // normal trident throw (channeling handled on hit in GameServer_items.cpp)
            double yawRad = self_->yaw * 3.14159265/180.0;
            double pitchRad = self_->pitch * 3.14159265/180.0;
            double vx = -std::sin(yawRad)*std::cos(pitchRad)*1.5;
            double vy = -std::sin(pitchRad)*1.5;
            double vz =  std::cos(yawRad)*std::cos(pitchRad)*1.5;
            auto trident = srv_.spawnProjectileFor(self_->dimension,
                                                   ProjectileKind::Trident,
                                                   self_->x, self_->y+1.6,
                                                   self_->z, vx, vy, vz,
                                                   self_->entityId, true);
            if (trident) trident->loyaltyLevel = EnchantmentHelper::getLoyalty(sl); // plan44 G-09: loyalty return
            if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                if (sl.applyDamage(1)) sl = ItemStack::air();
                srv_.resendInventory(*self_);
            }
            ack(sequence);
            return;
        }
        if (!sl.empty() && sl.name()=="minecraft:bow") {
            bool hasInfinity = EnchantmentHelper::hasInfinity(sl);
            bool isCreative = self_->gamemode==1;
            // find arrow in inventory (any arrow type)
            int arrowSlot = -1;
            for(int i=9;i<=44;++i){
                if(self_->inv[i].empty()) continue;
                std::string an = self_->inv[i].name();
                if(an.find("arrow")!=std::string::npos){ arrowSlot=i; break; }
            }
            // also check offhand
            if(arrowSlot==-1 && !self_->inv[45].empty() && self_->inv[45].name().find("arrow")!=std::string::npos) arrowSlot=45;
            bool hasArrow = arrowSlot!=-1 && self_->inv[arrowSlot].count>0;
            if(!hasArrow && !isCreative && !hasInfinity){
                ack(sequence); return;
            }
            // spawn arrow projectile
            double yawRad = self_->yaw * 3.14159265/180.0;
            double pitchRad = self_->pitch * 3.14159265/180.0;
            double vx = -std::sin(yawRad)*std::cos(pitchRad)*2.0;
            double vy = -std::sin(pitchRad)*2.0 + 0.15;
            double vz =  std::cos(yawRad)*std::cos(pitchRad)*2.0;
            srv_.spawnProjectileFor(self_->dimension, ProjectileKind::Arrow,
                                    self_->x, self_->y+1.6, self_->z, vx, vy,
                                    vz, self_->entityId, true);
            // damage bow
            if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                if (sl.applyDamage(1)) sl = ItemStack::air();
            }
            // consume arrow unless infinity or creative
            if(!isCreative){
                if(hasInfinity && hasArrow){
                    // infinity: consume 0 (require at least 1 arrow stays)
                } else if(hasArrow){
                    auto &arr = self_->inv[arrowSlot];
                    if(--arr.count<=0) arr = ItemStack::air();
                }
            }
            srv_.resendInventory(*self_);
            ack(sequence);
            return;
        }
        // durability cost of 1 — quickcharge has no live load timer here, see quickChargeLoadTime)
        if (!sl.empty() && sl.name()=="minecraft:crossbow") {
            bool isCreative = self_->gamemode==1;
            int arrowSlot = -1;
            for(int i=9;i<=44;++i){
                if(self_->inv[i].empty()) continue;
                std::string an = self_->inv[i].name();
                if(an.find("arrow")!=std::string::npos){ arrowSlot=i; break; }
            }
            if(arrowSlot==-1 && !self_->inv[45].empty() && self_->inv[45].name().find("arrow")!=std::string::npos) arrowSlot=45;
            bool hasArrow = arrowSlot!=-1 && self_->inv[arrowSlot].count>0;
            if(!hasArrow && !isCreative){
                ack(sequence); return;
            }
            int shots = EnchantmentHelper::multishotShots(sl);
            int pierce = EnchantmentHelper::getPiercing(sl);
            double yawRad = self_->yaw * 3.14159265/180.0;
            double pitchRad = self_->pitch * 3.14159265/180.0;
            for (int s = 0; s < shots; ++s) {
                double spread = (shots == 1) ? 0.0 : (s == 0 ? 0.0 : (s == 1 ? 0.17 : -0.17)); // ±10deg
                double yr = yawRad + spread;
                double vx = -std::sin(yr)*std::cos(pitchRad)*2.0;
                double vy = -std::sin(pitchRad)*2.0 + 0.15;
                double vz =  std::cos(yr)*std::cos(pitchRad)*2.0;
                auto pr = srv_.spawnProjectileFor(self_->dimension,
                                                  ProjectileKind::Arrow,
                                                  self_->x, self_->y+1.6,
                                                  self_->z, vx, vy, vz,
                                                  self_->entityId, true);
                if (pr) pr->piercingLevel = pierce;
            }
            if (self_->gamemode==0 && ItemStack::maxDamageFor(sl.itemId)>0) {
                if (DamageComponent::applyDamage(sl, 1)) sl = ItemStack::air(); // multishot costs 1 durability
            }
            if(!isCreative && hasArrow){
                auto &arr = self_->inv[arrowSlot];
                if(--arr.count<=0) arr = ItemStack::air();
            }
            srv_.resendInventory(*self_);
            ack(sequence);
            return;
        }
        if (!sl.empty()) {
            std::string n = sl.name();
            bool isPearl = n.find("ender_pearl")!=std::string::npos;
            bool isSnow  = n.find("snowball")!=std::string::npos;
            bool isEgg   = n=="minecraft:egg";
            if (isPearl || isSnow || isEgg) {
                if (isPearl && srv_.tickNow() - self_->lastEnderPearlTick < 20) {
                    // still on cooldown — notify
                    auto pidIt = gen::itemIdByName().find("minecraft:ender_pearl");
                    if (pidIt!=gen::itemIdByName().end() && self_->conn) {
                        WriteBuffer cd; cd.varint((int32_t)pidIt->second); cd.varint(20 - (int)(srv_.tickNow() - self_->lastEnderPearlTick));
                        self_->conn->trySendPacket(proto::pl::sc::SetCooldown, cd);
                    }
                    ack(sequence); return;
                }
                double yawRad = self_->yaw * 3.14159265/180.0;
                double pitchRad = self_->pitch * 3.14159265/180.0;
                double vx = -std::sin(yawRad)*std::cos(pitchRad)*1.5;
                double vy = -std::sin(pitchRad)*1.5;
                double vz =  std::cos(yawRad)*std::cos(pitchRad)*1.5;
                ProjectileKind pk = isPearl? ProjectileKind::EnderPearl : (isSnow? ProjectileKind::Snowball : ProjectileKind::Egg);
                srv_.spawnProjectileFor(self_->dimension, pk, self_->x,
                                        self_->y+1.6, self_->z, vx, vy, vz,
                                        self_->entityId, true);
                if (isPearl) {
                    self_->lastEnderPearlTick = srv_.tickNow();
                    if (self_->conn) {
                        auto pidIt = gen::itemIdByName().find("minecraft:ender_pearl");
                        if (pidIt!=gen::itemIdByName().end()){
                            WriteBuffer cd; cd.varint((int32_t)pidIt->second); cd.varint(20);
                            self_->conn->trySendPacket(proto::pl::sc::SetCooldown, cd);
                        }
                    }
                }
                if (self_->gamemode!=1) {
                    if (--sl.count <=0) sl = ItemStack::air();
                    srv_.resendInventory(*self_);
                }
                ack(sequence); return;
            }
        }
        if (!sl.empty() && self_->food < 20) {
            std::string iname = sl.name();
            bool isFood = false;
            int beforeFood = self_->food;
            float beforeSat = self_->saturation;
            srv_.handleFoodConsume(*self_, iname);
            if (self_->food != beforeFood || self_->saturation != beforeSat) isFood = true;
            else {
                // generic fallback for unknown food names that handleFoodConsume might not have matched (e.g., modded)
                if (iname.find("stew")!=std::string::npos||iname.find("soup")!=std::string::npos||iname.find("cake")!=std::string::npos) isFood=true;
            }
            if (isFood) {
                // exhaustion for eating: 0.05? vanilla 0.005 per food?
                srv_.addHungerExhaustion(*self_, 0.005f);
                {
                    ItemStack consumed = sl;
                    consumed.count = 1;
                    srv_.onConsumeItem(*self_, consumed);
                }
                // consume item (stew leaves bowl already handled inside handleFoodConsume via addToInventory)
                bool isStew = iname.find("stew")!=std::string::npos || iname.find("soup")!=std::string::npos;
                bool isCake = iname.find("cake")!=std::string::npos;
                if (!isStew && !isCake) {
                    if (--sl.count <= 0) sl = InvSlot::air();
                } else if (isStew) {
                    // stew consumed: bowl already added, just decrement stew
                    auto tmp = sl;
                    if (--tmp.count <=0) sl = InvSlot::air(); else sl = tmp;
                } else if (isCake) {
                    if (--sl.count <=0) sl = InvSlot::air();
                }
                srv_.resendInventory(*self_);
            } else {
                // revert if not food (handleFoodConsume might have clamped without change)
                self_->food = beforeFood; self_->saturation = beforeSat;
            }
        }
    }
    ack(sequence);
}
void Session::onUseEntity(ReadBuffer& in) {
    const std::int32_t target = in.varint();
    const std::int32_t mouse = in.varint();
    if (mouse == 2) { (void)in.f32(); (void)in.f32(); (void)in.f32(); }
    if (mouse != 1) {
        // INTERACT (0) / INTERACT_AT (2)
        if (mouse == 0 || mouse == 2) {
            const int hand = in.varint();
            (void)hand;
            const bool sneaking = in.boolean();
            // stack, so hand is wire-consumed here and hand-specific item resolution stays a behavior refinement. check shear and riding
            const SessionPlayerSnapshot playerState = snapshotPlayerForSession(*self_);
            const std::int8_t playerDimension = playerState.dimension;
            std::shared_ptr<MobEntity> targetMob;
            for (const auto& m : srv_.mobsSnapshot()) {
                if (!m) continue;
                std::lock_guard entityLock(*m->stateMtx);
                if (m->entityId == target && !m->dead &&
                    GameServer::canonicalDimension(m->dimension) == playerDimension) {
                    targetMob = m;
                    break;
                }
            }
            if (targetMob) {
                const SessionMobSnapshot targetState = snapshotMobForSession(*targetMob);
                if (!withinEntityInteractionRange(
                        playerState, targetState.x, targetState.y, targetState.z,
                        targetState.kind, targetState.slimeSize))
                    return;
                // Shearing is one transaction across the player's held item
                // and the sheep state.  Capture the packet/drop data while
                // both model locks are held, then do extension/transport work
                // after releasing them.
                bool sheared = false;
                std::int8_t sheepDimension = 0;
                std::int32_t sheepEntityId = 0;
                int sheepColor = 0;
                double sheepX = 0.0, sheepY = 0.0, sheepZ = 0.0;
                {
                    std::scoped_lock stateLock(self_->stateMtx,
                                               *targetMob->stateMtx);
                    if (targetMob->kind == MobKind::Sheep && !targetMob->sheared &&
                        self_->heldSlot >= 0 && self_->heldSlot < 9) {
                        auto& held = self_->inv[36 + self_->heldSlot];
                        const auto shearsIdIt = gen::itemIdByName().find("minecraft:shears");
                        if (shearsIdIt != gen::itemIdByName().end() &&
                            held.itemId == shearsIdIt->second) {
                            targetMob->sheared = true;
                            if (held.applyDamage(1)) held = ItemStack::air();
                            sheared = true;
                            sheepDimension = targetMob->dimension;
                            sheepEntityId = targetMob->entityId;
                            sheepColor = targetMob->woolColor % 16;
                            sheepX = targetMob->x;
                            sheepY = targetMob->y;
                            sheepZ = targetMob->z;
                        }
                    }
                }
                if (sheared) {
                    static const char* woolNames[] = {
                        "minecraft:white_wool","minecraft:orange_wool","minecraft:magenta_wool","minecraft:light_blue_wool",
                        "minecraft:yellow_wool","minecraft:lime_wool","minecraft:pink_wool","minecraft:gray_wool",
                        "minecraft:light_gray_wool","minecraft:cyan_wool","minecraft:purple_wool","minecraft:blue_wool",
                        "minecraft:brown_wool","minecraft:green_wool","minecraft:red_wool","minecraft:black_wool"
                    };
                    const auto wit = gen::itemIdByName().find(woolNames[sheepColor]);
                    if (wit != gen::itemIdByName().end()) {
                        const int count = 1 + (nextRandom() % 3);
                        srv_.spawnItemDropFor(
                            sheepDimension, sheepX, sheepY + 0.8, sheepZ,
                            wit->second, static_cast<std::uint8_t>(count),
                            (nextRandom() / (double)RAND_MAX - .5) * 0.12,
                            0.12,
                            (nextRandom() / (double)RAND_MAX - .5) * 0.12);
                    }
                    WriteBuffer md;
                    md.varint(sheepEntityId);
                    md.u8(17); md.u8(8); md.u8(1);
                    md.u8(255);
                    srv_.broadcastPacketExceptInDimension(
                        sheepDimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                    srv_.resendInventory(*self_);
                    return;
                }
                // Take a value snapshot before any transport or extension
                // call.  `openTrading` and `tryBreedFeed` can send packets or
                // re-enter the server; neither is allowed to run while the
                // live MobEntity lock is held.
                MobEntity interactionSnapshot;
                {
                    std::lock_guard entityLock(*targetMob->stateMtx);
                    if (targetMob->dead ||
                        GameServer::canonicalDimension(targetMob->dimension) !=
                            playerDimension)
                        return;
                    interactionSnapshot = *targetMob;
                }
                const auto isHorseLike = [](MobKind kind) {
                    return kind == MobKind::Horse || kind == MobKind::Donkey ||
                           kind == MobKind::Mule || kind == MobKind::Llama ||
                           kind == MobKind::TraderLlama || kind == MobKind::Camel ||
                           kind == MobKind::SkeletonHorse ||
                           kind == MobKind::ZombieHorse;
                };

                bool openHorseWindow = false;
                {
                    std::lock_guard playerLock(self_->stateMtx);
                    openHorseWindow = isHorseLike(interactionSnapshot.kind) &&
                        (self_->vehicleId == interactionSnapshot.entityId || sneaking);
                }
                if (openHorseWindow) {
                    closeOpenMenu(true);
                    constexpr int slotCount = 15;
                    int windowId = ++menuWindowCounter_;
                    if (windowId == 0 || windowId > 100) {
                        menuWindowCounter_ = 1;
                        windowId = 1;
                    }
                    std::int32_t stateId = 0;
                    {
                        std::lock_guard playerLock(self_->stateMtx);
                        stateId = ++self_->invStateId;
                    }
                    WriteBuffer ow;
                    ow.varint(windowId);
                    ow.varint(slotCount);
                    ow.varint(interactionSnapshot.entityId);
                    conn_->trySendPacket(proto::pl::sc::OpenHorseWindow, ow);
                    // Horse inventory storage is not modelled yet, but keep
                    // the wire snapshot internally consistent and emit it
                    // only after the MobEntity lock has been released.
                    WriteBuffer cc;
                    cc.varint(windowId);
                    cc.varint(stateId);
                    cc.varint(slotCount);
                    for (int i = 0; i < slotCount; ++i) ItemStack::air().write(cc);
                    ItemStack::air().write(cc);
                    conn_->trySendPacket(proto::pl::sc::ContainerSetContent, cc);
                    return;
                }

                bool mounted = false;
                std::int32_t mountedEntityId = -1;
                {
                    // The dead/dimension/rider checks and the two-way vehicle
                    // link must be one transaction.  scoped_lock provides a
                    // deadlock-safe order against the server's Player->Mob
                    // paths while no packet is sent in this scope.
                    std::scoped_lock stateLock(self_->stateMtx,
                                               *targetMob->stateMtx);
                    const bool currentTarget =
                        !targetMob->dead && targetMob->entityId == target &&
                        GameServer::canonicalDimension(targetMob->dimension) ==
                            GameServer::canonicalDimension(self_->dimension);
                    const bool mountable =
                        targetMob->kind == MobKind::Horse ||
                        targetMob->kind == MobKind::Llama ||
                        targetMob->kind == MobKind::Pig ||
                        MobEntity::isBoat(targetMob->kind) ||
                        targetMob->kind == MobKind::Minecart;
                    if (currentTarget && mountable && self_->vehicleId == -1 &&
                        targetMob->riderEntityId == -1) {
                        self_->vehicleId = targetMob->entityId;
                        targetMob->riderEntityId = self_->entityId;
                        mounted = true;
                        mountedEntityId = targetMob->entityId;
                    }
                }
                if (mounted) {
                    srv_.broadcastSetPassengers(mountedEntityId);
                    return;
                }

                // tryBreedFeed has its own deadlock-safe Player/Mob
                // transaction and releases both locks before its packets.
                if (srv_.tryBreedFeed(*self_, *targetMob)) return;
                if (interactionSnapshot.kind == MobKind::Villager) {
                    MobEntity villagerSnapshot;
                    bool stillValid = false;
                    {
                        std::lock_guard entityLock(*targetMob->stateMtx);
                        stillValid = !targetMob->dead &&
                            targetMob->entityId == target &&
                            GameServer::canonicalDimension(targetMob->dimension) ==
                                playerDimension;
                        if (stillValid) villagerSnapshot = *targetMob;
                    }
                    if (stillValid) {
                        // Preserve inputs/cursor from an already-open screen
                        // before the merchant screen is emitted.  Do not
                        // discard openMenu_ by assigning nullptr.
                        closeOpenMenu(true);
                        if (srv_.openTrading(*self_, villagerSnapshot)) {
                            std::lock_guard playerLock(self_->stateMtx);
                            if (self_->inPlay && !self_->dead &&
                                GameServer::canonicalDimension(self_->dimension) ==
                                    playerDimension)
                                tradingVillager_ = target;
                        }
                    }
                }
                }
        } else {
            in.skipRest();
        }
        return;
    }
    (void)in.boolean();

    struct AttackState {
        std::int32_t entityId = 0;
        std::int8_t dimension = 0;
        std::uint8_t gamemode = 0;
        std::int32_t heldSlot = 0;
        std::shared_ptr<Connection> connection;
        std::string name;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double fallDistance = 0.0;
        bool onGround = false;
        bool sprinting = false;
        bool riding = false;
        std::int32_t attackCooldownTicks = 0;
        std::vector<EffectInstance> effects;
        ItemStack weapon;
    } attacker;
    {
        std::lock_guard playerLock(self_->stateMtx);
        attacker.entityId = self_->entityId;
        attacker.dimension = GameServer::canonicalDimension(self_->dimension);
        attacker.gamemode = self_->gamemode;
        attacker.heldSlot = self_->heldSlot;
        attacker.connection = self_->conn;
        attacker.name = self_->name;
        attacker.x = self_->x;
        attacker.y = self_->y;
        attacker.z = self_->z;
        attacker.fallDistance = self_->fallDist;
        attacker.onGround = self_->onGround;
        attacker.sprinting = self_->isSprinting;
        attacker.riding = self_->vehicleId != -1;
        attacker.attackCooldownTicks = self_->attackCooldownTicks;
        attacker.effects = self_->effects;
        if (attacker.heldSlot >= 0 && attacker.heldSlot < 9)
            attacker.weapon = self_->inv[36 + attacker.heldSlot];
        // A swing consumes exhaustion and resets the attack/shield state as a
        // single player-state transaction.  No callback or packet is reached
        // while this lock is held.
        srv_.addHungerExhaustion(*self_, 0.1f);
        self_->attackCooldownTicks = 0;
        self_->isBlocking = false;
        self_->blockingTicks = 0;
    }

    float baseDmg = 1.f;
    ItemStack weaponStack = attacker.weapon;
    if (!weaponStack.empty()) {
            std::string iname = weaponStack.name();
            if (iname.find("sword") != std::string::npos) baseDmg = 6.f;
            else if (iname.find("axe") != std::string::npos) baseDmg = 7.f;
            else if (iname.find("_sword") != std::string::npos) baseDmg = 5.f;
            if (weaponStack.itemId == gen::itemIdByName().at("minecraft:iron_sword")) baseDmg = 6.f;
            // base enchant sharpness kept for pvp (player victims) — smite/bane for mobs applied per victim
            baseDmg = EnchantmentHelper::meleeDamageWithEnchant(baseDmg, weaponStack);
    }
    float dmg = baseDmg + meleeDamageBonusFor(attacker.effects);
    // E-06 wire compat: base mapping above is untouched; crit/sweep only scale the result.
    bool charged = isChargedAttack(attacker.attackCooldownTicks);
    bool falling = !attacker.onGround && attacker.fallDistance > 0;
    bool weaponIsSword = !weaponStack.empty() && isSwordItem(weaponStack.name());
    bool weaponIsAxe = !weaponStack.empty() && isAxeItem(weaponStack.name());
    bool weaponIsMace = !weaponStack.empty() && isMaceItem(weaponStack.name());
    bool attackerInWater = false;
    {
        uint16_t bst = srv_.worldFor(attacker.dimension).getBlock(
            static_cast<int>(std::floor(attacker.x)),
            static_cast<int>(std::floor(attacker.y)),
            static_cast<int>(std::floor(attacker.z)));
        if (auto* bd = gen::blockByState(bst)) attackerInWater = std::string(bd->name).find("water") != std::string::npos;
    }
    bool crit = isCritAttack(attacker.onGround, falling, attacker.sprinting, charged,
                             attackerInWater, false, attacker.riding, false);
    bool doSweep = isSweepAttack(weaponIsSword, attacker.onGround,
                                 attacker.sprinting, charged);
    int breachLv = weaponStack.empty() ? 0 : EnchantmentHelper::getBreach(weaponStack);
    int fireAspectLv = weaponStack.empty() ? 0 : EnchantmentHelper::getFireAspect(weaponStack);
    auto broadcastCritParticles = [&](double x, double y, double z) {
        if (!crit) return;
        WriteBuffer pb = makeWorldParticlesBody(x, y + 1.0, z, 0.2f, 0.2f, 0.2f, 0.3f, 8,
                                                ParticleId::crit, {}, false, false);
        srv_.broadcastPacketExceptInDimension(attacker.dimension, nullptr,
                                              pl::sc::WorldParticles, pb);
    };

    // ---- PVP: check player victims first (items 76-80 combat)
    for (auto &pp : srv_.playersSnapshot()) {
        auto *victimP = pp.get();
        if (!victimP) continue;
        std::int8_t victimDimension = 0;
        double victimTargetX = 0.0, victimTargetY = 0.0, victimTargetZ = 0.0;
        {
            std::lock_guard victimLock(victimP->stateMtx);
            if (victimP->entityId != target || victimP->dead ||
                GameServer::canonicalDimension(victimP->dimension) !=
                    attacker.dimension)
                continue;
            victimDimension = GameServer::canonicalDimension(victimP->dimension);
            victimTargetX = victimP->x;
            victimTargetY = victimP->y;
            victimTargetZ = victimP->z;
        }
        if (victimP == self_.get()) return; // self-hit ignore
        if (!withinEntityInteractionRange(
                attacker.x, attacker.y, attacker.z, attacker.gamemode,
                victimTargetX, victimTargetY, victimTargetZ))
            return;
        if (!srv_.config().pvp) {
            // PvP disabled: suppress damage, optionally notify
            return;
        }
        {
            DamageSource psrc("player");
            if (CombatManager::tryShieldBlock(srv_, *victimP, psrc,
                                              attacker.x, attacker.z,
                                              weaponIsAxe))
                return;
        }
        float pvpDmg = dmg;
        if (crit) pvpDmg = applyCrit(pvpDmg); // plan44 G-07: falling x1.5
        if (weaponIsMace) // plan44 G-09: density smash bonus also applies in PVP
            pvpDmg += EnchantmentHelper::densityBonus(
                static_cast<int>(std::floor(attacker.fallDistance)), weaponStack);
        float before = 0.0f;
        DamageSource psrc2("player");
        {
            std::lock_guard victimLock(victimP->stateMtx);
            if (victimP->dead || GameServer::canonicalDimension(victimP->dimension) !=
                                    attacker.dimension)
                return;
            before = victimP->health;
        }
        srv_.applyDamage(*victimP, pvpDmg, psrc2, breachLv);
        double victimX = 0.0, victimY = 0.0, victimZ = 0.0;
        std::shared_ptr<Connection> victimConnection;
        std::int32_t victimEntityId = target;
        bool damaged = false;
        {
            std::lock_guard victimLock(victimP->stateMtx);
            if (GameServer::canonicalDimension(victimP->dimension) !=
                    attacker.dimension)
                return;
            damaged = victimP->health < before;
            if (damaged && fireAspectLv > 0 && !victimP->dead)
                victimP->fireTicks = 100; // fire aspect ignites PVP victims
            victimX = victimP->x;
            victimY = victimP->y;
            victimZ = victimP->z;
            victimConnection = victimP->conn;
            victimEntityId = victimP->entityId;
        }
        broadcastCritParticles(victimX, victimY, victimZ);
        if (damaged)
            CombatManager::applyThornsReflection(srv_, *victimP, nullptr,
                                                  self_.get());
        // knockback impulse
        double dx = victimX - attacker.x;
        double dz = victimZ - attacker.z;
        double len = std::sqrt(dx*dx + dz*dz);
        if (len < 0.01) { dx = (nextRandom()/(double)RAND_MAX - 0.5); dz = (nextRandom()/(double)RAND_MAX - 0.5); len = std::sqrt(dx*dx+dz*dz); }
        double nx = dx / len;
        double nz = dz / len;
        WriteBuffer vel;
        vel.varint(victimEntityId);
        vel.i16(static_cast<std::int16_t>(nx * 400));
        vel.i16(static_cast<std::int16_t>(300));
        vel.i16(static_cast<std::int16_t>(nz * 400));
        if (victimConnection)
            victimConnection->trySendPacket(pl::sc::EntityVelocity, vel);
        srv_.broadcastPacketExceptInDimension(victimDimension, victimP,
                                              pl::sc::EntityVelocity, vel);
        return;
    }

    bool killed = false;
    std::shared_ptr<MobEntity> victim;
    std::shared_ptr<MobEntity> hitTarget;
    bool hitMob = false;
    SessionMobSnapshot hitSnapshot;
    float attackDmgNoCrit = dmg; // plan44 G-07: sweep uses enchanted AD without crit
    int flameLvl = 0; int punchLvl = 0; int kbLvl = 0;
    if (!weaponStack.empty()) {
        flameLvl = EnchantmentHelper::getFlame(weaponStack);
        punchLvl = EnchantmentHelper::getPunch(weaponStack);
        kbLvl = EnchantmentHelper::getKnockback(weaponStack);
    }
    for (const auto& m : srv_.mobsSnapshot()) {
        if (!m) continue;
        const SessionMobSnapshot before = snapshotMobForSession(*m);
        if (before.entityId != target || before.dead ||
            before.dimension != attacker.dimension)
            continue;
        if (!withinEntityInteractionRange(
                attacker.x, attacker.y, attacker.z, attacker.gamemode,
                before.x, before.y, before.z, before.kind, before.slimeSize))
            return;
        hitTarget = m;
        float mobDmg = dmg;
        if (!weaponStack.empty()) {
            // Recompute with the target kind for smite/bane without retaining
            // the Mob lock across damage callbacks.
            mobDmg = EnchantmentHelper::meleeDamageWithEnchant(
                [&] {
                    float b = 1.f;
                    const std::string iname = weaponStack.name();
                    if (iname.find("sword") != std::string::npos) b = 6.f;
                    else if (iname.find("axe") != std::string::npos) b = 7.f;
                    else if (iname.find("_sword") != std::string::npos) b = 5.f;
                    if (weaponStack.itemId ==
                        gen::itemIdByName().at("minecraft:iron_sword"))
                        b = 6.f;
                    return b;
                }(), weaponStack, before.kind);
            mobDmg += meleeDamageBonusFor(attacker.effects);
        }
        if (!weaponStack.empty()) {
            mobDmg += EnchantmentHelper::impalingBonusFor(weaponStack, before.kind);
            if (weaponIsMace)
                mobDmg += EnchantmentHelper::densityBonus(
                    static_cast<int>(std::floor(attacker.fallDistance)), weaponStack);
        }
        attackDmgNoCrit = mobDmg; // sweep uses enchanted AD without crit (vanilla)
        if (crit) mobDmg = applyCrit(mobDmg); // plan44 G-07: falling x1.5 (main target only)
        DamageSource msrc("player");
        srv_.applyDamageToMob(*m, mobDmg, msrc, breachLv);
        SessionMobSnapshot after = snapshotMobForSession(*m);
        if (after.entityId != target || after.dimension != attacker.dimension)
            continue;
        if (flameLvl > 0 && !after.dead) {
            {
                std::lock_guard entityLock(*m->stateMtx);
                if (m->entityId == target && !m->dead)
                    m->onFireTicks = 100;
            }
            after = snapshotMobForSession(*m);
            srv_.broadcastSoundFor(after.dimension,
                                   "minecraft:item.firecharge.use",
                                   after.x, after.y, after.z, 1.f, 1.f, "block");
        }
        // AI hurt memory → panic/anger
        srv_.noteMobHurt(after.entityId, attacker.entityId);
        hitMob = true;
        hitSnapshot = after;
        if (after.dead) { killed = true; victim = m; }
        break;
    }
    if (hitMob && hitTarget) {
        if (doSweep) {
            int sweepLv = weaponStack.empty() ? 0 : EnchantmentHelper::getSweepingEdge(weaponStack);
            float sweepDmg = sweepingEdgeDamage(attackDmgNoCrit, sweepLv);
            struct SplashTarget {
                std::shared_ptr<MobEntity> mob;
                SessionMobSnapshot state;
            };
            std::vector<SplashTarget> splash;
            for (const auto& m : srv_.mobsSnapshot()) {
                if (!m) continue;
                const SessionMobSnapshot state = snapshotMobForSession(*m);
                if (state.entityId == target || state.dead ||
                    state.dimension != attacker.dimension)
                    continue;
                if (!withinEntityInteractionRange(
                        attacker.x, attacker.y, attacker.z, attacker.gamemode,
                        state.x, state.y, state.z, state.kind, state.slimeSize))
                    continue;
                if (inSweepRange(hitSnapshot.x, hitSnapshot.y, hitSnapshot.z,
                                 state.x, state.y, state.z))
                    splash.push_back({m, state});
            }
            DamageSource ssrc("player");
            for (auto& candidate : splash) {
                srv_.applyDamageToMob(*candidate.mob, sweepDmg, ssrc, breachLv);
                SessionMobSnapshot state = snapshotMobForSession(*candidate.mob);
                if (state.entityId != candidate.state.entityId ||
                    state.dimension != attacker.dimension || state.dead)
                    continue;
                if (fireAspectLv > 0 || flameLvl > 0) {
                    std::lock_guard entityLock(*candidate.mob->stateMtx);
                    if (!candidate.mob->dead)
                        candidate.mob->onFireTicks = 100; // MC-93669: sweep ignites
                    state = snapshotMobForSession(*candidate.mob);
                }
                double dx = state.x - attacker.x, dz = state.z - attacker.z;
                double len = std::sqrt(dx*dx + dz*dz);
                if (len < 0.01) { dx = 0.5; dz = 0.0; len = 0.5; }
                WriteBuffer vel;
                vel.varint(state.entityId);
                vel.i16(static_cast<std::int16_t>(dx / len * 400));
                vel.i16(static_cast<std::int16_t>(300));
                vel.i16(static_cast<std::int16_t>(dz / len * 400));
                srv_.broadcastPacketExceptInDimension(state.dimension, nullptr,
                                                      pl::sc::EntityVelocity, vel);
            }
            if (!splash.empty()) {
                WriteBuffer pb = makeWorldParticlesBody(hitSnapshot.x,
                                                        hitSnapshot.y + 1.0,
                                                        hitSnapshot.z,
                                                        0.3f, 0.2f, 0.3f, 0.2f, 6,
                                                        ParticleId::sweep_attack, {}, false, false);
                srv_.broadcastPacketExceptInDimension(hitSnapshot.dimension, nullptr,
                                                      pl::sc::WorldParticles, pb);
                srv_.broadcastSoundFor(hitSnapshot.dimension,
                                       "minecraft:entity.player.attack.sweep",
                                       hitSnapshot.x, hitSnapshot.y, hitSnapshot.z,
                                       1.f, 1.f, "player");
            }
        }
        if (weaponIsMace && attacker.fallDistance > 1.5) {
            int wb = weaponStack.empty() ? 0 : EnchantmentHelper::getWindBurst(weaponStack);
            if (wb > 0) {
                WriteBuffer wv;
                wv.varint(attacker.entityId);
                wv.i16(0);
                wv.i16(static_cast<std::int16_t>(windBurstLaunchVy(wb) * 8000));
                wv.i16(0);
                if (attacker.connection)
                    attacker.connection->trySendPacket(pl::sc::EntityVelocity, wv);
                srv_.broadcastPacketExceptInDimension(attacker.dimension,
                                                      self_.get(),
                                                      pl::sc::EntityVelocity, wv);
            }
        }
    }
    bool inventoryChanged = false;
    if (attacker.heldSlot >= 0 && attacker.heldSlot < 9) {
        std::lock_guard playerLock(self_->stateMtx);
        auto& held = self_->inv[36 + attacker.heldSlot];
        if (self_->heldSlot == attacker.heldSlot && !held.empty() &&
            ItemStack::maxDamageFor(held.itemId) > 0) {
            if (DamageComponent::applyDamage(held, 1)) held = ItemStack::air();
            inventoryChanged = true;
        }
    }
    if (inventoryChanged) srv_.resendInventory(*self_);
    if (hitMob && hitTarget) {
        const SessionMobSnapshot state = snapshotMobForSession(*hitTarget);
        double dx = state.x - attacker.x;
        double dz = state.z - attacker.z;
        double len = std::sqrt(dx*dx + dz*dz);
        if (len < 0.01) { dx = (nextRandom()/(double)RAND_MAX - 0.5); dz = (nextRandom()/(double)RAND_MAX - 0.5); len = std::sqrt(dx*dx+dz*dz); }
        double nx = dx / len;
        double nz = dz / len;
        float kbForce = 0.4f * (punchLvl + kbLvl) + (punchLvl>0 ? 0.5f : 0.f);
        // base knockback preserved as 400, add scaled extra (kbForce*8000)
        int horiz = 400 + (int)(kbForce * 8000 * 0.05); // small add to keep compat, vanilla punch force is separate
        // For clear vanilla: velocity = nx * (400 + kbForce*400) ; here we add directly
        if (kbForce>0) {
            horiz = (int)(400 + kbForce * 400);
        }
        WriteBuffer vel;
        vel.varint(state.entityId);
        vel.i16(static_cast<std::int16_t>(nx * horiz));
        vel.i16(static_cast<std::int16_t>(300 + (kbForce>0 ? 100 : 0)));
        vel.i16(static_cast<std::int16_t>(nz * horiz));
        srv_.broadcastPacketExceptInDimension(state.dimension, nullptr,
                                              pl::sc::EntityVelocity, vel);
        // apply to mob's vel for server-side sync
        if (kbForce>0) {
            std::lock_guard entityLock(*hitTarget->stateMtx);
            if (hitTarget->entityId == state.entityId && !hitTarget->dead) {
                hitTarget->velX += nx * kbForce * 0.3;
                hitTarget->velZ += nz * kbForce * 0.3;
                hitTarget->velY += 0.1;
            }
        }
        // hurt animation + sound already via applyDamageToMob
    }
    if (killed && victim) {
        const SessionMobSnapshot death = snapshotMobForSession(*victim);
        if (death.entityId != target || !death.dead) return;
        WriteBuffer rm;
        rm.varint(1); rm.varint(target);
        srv_.broadcastPacketExceptInDimension(death.dimension, nullptr,
                                              pl::sc::RemoveEntities, rm);
        srv_.onMobKilledBy(*self_, death.kind);
        srv_.scoreboard.addScore("kills", attacker.name, 1);
        srv_.sendScoreAll("kills", attacker.name,
                          srv_.scoreboard.getScore("kills", attacker.name));
        const auto drop = MobEntity::dropFor(death.kind);
        if (drop.itemId) {
            int lootLv = weaponStack.empty() ? 0 : EnchantmentHelper::getLooting(weaponStack);
            int cnt = drop.count;
            if (lootLv > 0) cnt = std::min(64, cnt + (nextRandom() % (lootLv + 1)));
            srv_.spawnItemDropFor(death.dimension, death.x, death.y + 0.4,
                                  death.z, drop.itemId, (std::uint8_t)cnt,
                                  (nextRandom()/(double)RAND_MAX-.5)*.15, .1,
                                  (nextRandom()/(double)RAND_MAX-.5)*.15);
        }
        srv_.spawnXpOrbsFor(death.dimension, death.x, death.y + 0.5,
                            death.z, mobStats(death.kind).xpDrop,
                            self_.get());
        // slime split on player kill
        if ((death.kind == MobKind::Slime || death.kind == MobKind::MagmaCube) &&
            death.slimeSize > 0) {
            std::vector<std::shared_ptr<MobEntity>> babies;
            int n = 2 + (nextRandom() % 3);
            for (int s=0; s<n; ++s) {
                auto baby = std::make_shared<MobEntity>();
                baby->entityId = srv_.nextEntityId();
                baby->kind = death.kind;
                baby->dimension = death.dimension;
                baby->slimeSize = death.slimeSize - 1;
                baby->health = MobEntity::slimeHealthForSize(baby->slimeSize);
                if (baby->health < 1.f) baby->health = 1.f;
                baby->x = death.x + (nextRandom()/(double)RAND_MAX - 0.5) * 0.5;
                baby->y = death.y;
                baby->z = death.z + (nextRandom()/(double)RAND_MAX - 0.5) * 0.5;
                baby->lastSeenMs = 0;
                if (srv_.jvmRuntime() &&
                    !srv_.jvmRuntime()->onMobSpawn(*baby, baby->x, baby->y, baby->z))
                    continue;
                babies.push_back(std::move(baby));
            }
            for (auto& baby : babies) srv_.addMob(baby);
            for (auto& baby : babies) srv_.broadcastMobSpawn(*baby);
        }
        srv_.eraseMobAi(target);
        srv_.removeMob(victim);
        srv_.invalidateJvmMob(victim);
        bool wasVehicle = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            if (self_->vehicleId == target) {
                self_->vehicleId = -1;
                wasVehicle = true;
            }
        }
        if (wasVehicle)
            srv_.broadcastSetPassengersEmptyFor(death.dimension, target);
    }
}
} // namespace cppfm
