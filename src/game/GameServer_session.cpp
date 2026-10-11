#include "GameServer.hpp"
#include "SessionInteractionSupport.hpp"
#include "SecureChatPolicy.hpp"
#include "Messages.hpp"
#include "BlockEvent.hpp"
#include "MetadataTypes.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "PositionPackets.hpp"
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
#include "../net/CookieResponse.hpp"

namespace cppfm {
using namespace proto;

namespace session_lifecycle_detail {
WriteBuffer makeWorldState(const ServerConfig& config, const World& world,
                           std::uint8_t gamemode);
}

namespace {
struct SessionMobSnapshot {
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    MobKind kind = MobKind::Pig;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool dead = false;
    int slimeSize = 0;
    bool entityOnFire = false;
};

SessionMobSnapshot snapshotMobForSession(const MobEntity& mob) {
    std::lock_guard entityLock(*mob.stateMtx);
    return {mob.entityId, GameServer::canonicalDimension(mob.dimension),
            mob.kind, mob.x, mob.y, mob.z, mob.dead, mob.slimeSize,
            mob.onFireTicks > 0};
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
    return session_detail::squaredDistanceToBox(playerX, playerY + 1.62, playerZ,
                                targetX - halfWidth, targetY, targetZ - halfWidth,
                                targetX + halfWidth, targetY + height,
                                targetZ + halfWidth) <= range * range + 1e-6;
}

bool withinEntityInteractionRange(const session_detail::SessionPlayerSnapshot& player,
                                  double targetX, double targetY, double targetZ,
                                  MobKind targetKind = MobKind::Pig,
                                  int slimeSize = 2) {
    if (!player.inPlay || player.dead) return false;
    return withinEntityInteractionRange(player.x, player.y, player.z,
                                        player.gamemode, targetX, targetY,
                                        targetZ, targetKind, slimeSize);
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




int effectiveViewDistance(const ServerConfig& config, int clientViewDistance) {
    return std::clamp(std::min(config.viewDistance, clientViewDistance),
                      constants::kViewDistanceMin, constants::kViewDistanceMax);
}

} // namespace


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




















void Session::sendSignBlockEntity(std::int8_t dimension, std::int32_t x,
                                 std::int32_t y, std::int32_t z,
                                 bool broadcast) {
    bool hanging = false;
    {
        World& w = srv_.worldFor(dimension);
        const auto* d = gen::blockByState(w.getBlock(x, y, z));
        if (!d || d->name.find("sign") == std::string::npos) {
            std::fprintf(stderr, "[cppfm] sign resend at %d,%d,%d ignored (not a sign block)\n",
                         x, y, z);
            return;
        }
        hanging = (d->name.find("hanging_sign") != std::string::npos);
    }
    auto beOwner = srv_.blockEntitiesFor(dimension).getShared(posKey(x, y, z));
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
    conn_->trySendPacketLowPriority(pl::sc::BlockEntityData, b);
    if (broadcast)
        srv_.broadcastPacketExceptInDimension(dimension, self_.get(),
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
    auto menu = openMenu_;
    if (!menu || menu->type != MenuType::Beacon || !menu->blockKey ||
        !menu->container || menu->containerCount < 1) {
        std::fprintf(stderr, "[cppfm] beacon effect without an open beacon menu rejected\n");
        return;
    }
    const auto blockKey = *menu->blockKey;
    const auto bx = posKeyUnpackX(blockKey);
    const auto by = posKeyUnpackY(blockKey);
    const auto bz = posKeyUnpackZ(blockKey);
    const double dx = self_->x - (static_cast<double>(bx) + 0.5);
    const double dy = self_->y - (static_cast<double>(by) + 0.5);
    const double dz = self_->z - (static_cast<double>(bz) + 0.5);
    if (dx * dx + dy * dy + dz * dz > 64.0) return;
    const auto beaconState = srv_.worldFor(self_->dimension).getBlock(bx, by, bz);
    const auto* beaconBlock = gen::blockByState(beaconState);
    if (!beaconBlock || std::string(beaconBlock->name).find("beacon") == std::string::npos) return;
    const auto payment = menu->container[0].itemId;
    const auto& itemIds = gen::itemIdByName();
    const bool paymentAllowed = [&] {
        for (const char* name : {"minecraft:iron_ingot", "minecraft:gold_ingot",
                                 "minecraft:diamond", "minecraft:emerald",
                                 "minecraft:netherite_ingot"}) {
            const auto it = itemIds.find(name);
            if (it != itemIds.end() && it->second == payment) return true;
        }
        return false;
    }();
    if (menu->container[0].empty() || !paymentAllowed) return;

    static const std::int32_t kPrimaries[] = {1, 3, 11, 8, 5};
    auto validPrimary = [](std::int32_t v) {
        for (auto p : kPrimaries) if (p == v) return true;
        return false;
    };
    if (!primary || !validPrimary(*primary)) {
        std::fprintf(stderr, "[cppfm] beacon primary missing or invalid\n");
        return;
    }
    if (secondary && *secondary != 10 && *secondary != *primary) {
        std::fprintf(stderr, "[cppfm] beacon secondary %d rejected\n", *secondary);
        return;
    }
    --menu->container[0].count;
    if (menu->container[0].count <= 0) menu->container[0] = ItemStack::air();
    sendMenuContent(*menu);
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
    b.u8(static_cast<std::uint8_t>(effectFlags(e)));
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
    srv_.teleportPlayer(*self_, x, y, z, std::array<float, 2>{yaw, pitch});
}

bool GameServer::teleportPlayer(
    Player& player, double x, double y, double z,
    std::optional<std::array<float, 2>> rotation) {
    std::lock_guard teleportLock(player.teleportMtx);
    std::lock_guard playerLock(player.stateMtx);
    const std::int32_t nextTeleportId = player.lastTeleportId == INT32_MAX
        ? 0 : player.lastTeleportId + 1;
    const float nextYaw = rotation ? (*rotation)[0] : player.yaw;
    const float nextPitch = rotation ? (*rotation)[1] : player.pitch;
    const auto connection = player.conn;
    if (!connection) return false;
    const WriteBuffer packet = makePlayerPositionLookBody(
        nextTeleportId, x, y, z, 0.0, 0.0, 0.0, nextYaw, nextPitch);
    // A position correction is a session state transition. Keep older
    // low-priority chunk frames ahead of it so the client cannot observe the
    // teleport before the output already queued for its previous location.
    if (!connection->trySendPacketBarrier(
            proto::pl::sc::PlayerPosition, packet)) {
        connection->close();
        return false;
    }

    // Publish the matching server-side state only after the correction was
    // admitted to the connection's ordered output queue. Otherwise a failed
    // send would leave the client at the old pose and the server at the new.
    player.lastTeleportId = nextTeleportId;
    player.pendingTeleportId = nextTeleportId;
    player.x = x;
    player.y = y;
    player.z = z;
    player.fallDist = 0;
    player.yaw = nextYaw;
    player.pitch = nextPitch;
    return true;
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
bool Session::sendChunk(std::int8_t dimension, std::int32_t cx,
                        std::int32_t cz,
                        SimulationDispatchGuard& simulationLock) {
    dimension = GameServer::canonicalDimension(dimension);
    World& world = srv_.worldFor(dimension);
    const std::uint32_t biomeIdx = srv_.data().biomeIndex(world.biomeKey());
    srv_.demandChunkAsyncFor(dimension, cx, cz);
    // A first synchronous chunk load publishes its persisted block entities.
    // Resolve that load before taking the initial-NBT snapshot; otherwise the
    // block array can come from disk while its sign/container records are
    // absent from the snapshot. sendNextChunkBatch releases the simulation
    // gate around this call, so disk I/O does not stall server dispatch.
    {
        auto loadLease = world.lockLoadedChunkMutationGate(cx, cz);
        if (!loadLease) return false;
        loadLease->unlock();
    }
    constexpr int kSnapshotAttempts = 3;
    for (int attempt = 0; attempt < kSnapshotAttempts; ++attempt) {
        simulationLock.lock();
        if (GameServer::canonicalDimension(self_->dimension) != dimension) {
            simulationLock.unlock();
            return false;
        }
        auto snapshotTicket =
            srv_.beginChunkCacheSnapshot(dimension, cx, cz);
        const bool cacheHit = srv_.publishCachedChunkForSnapshot(
            snapshotTicket,
            [this](const GameServer::ChunkBodyRef& body) {
                conn_->sendPacketBufLowPriority(
                    pl::sc::LevelChunkWithLight, *body);
            });
        if (cacheHit) {
            simulationLock.unlock();
            sentChunks_.insert(chunkKey(cx, cz));
            return true;
        }

        // Snapshot the BE projection while authoritative packet/tick/command
        // mutations are serialized. Do not hold the World mutation lease while
        // taking owner locks: moving pistons can take those locks before World.
        nbt::Value blockEntities = nbt::Value::makeList(nbt::Compound);
        if (!srv_.blockEntitiesFor(dimension).writeChunkInitialNbt(
                cx, cz, blockEntities)) {
            simulationLock.unlock();
            return false;
        }
        simulationLock.unlock();

        // Build from a detached chunk snapshot outside the simulation gate so
        // a cache miss cannot stall every player's tick while it is encoded.
        auto mutationLease = world.lockLoadedChunkMutationGate(cx, cz);
        if (!mutationLease) return false;
        auto chunkSnapshot = world.tryGetCopy(cx, cz);
        mutationLease->unlock();
        if (!chunkSnapshot) return false;
        const auto worldRevision = chunkSnapshot->revision;

        auto fresh = std::make_shared<const std::vector<std::uint8_t>>([&] {
            WriteBuffer body;
            serializeLevelChunkBody(body, cx, cz, *chunkSnapshot, biomeIdx,
                                    world.verticalGeometry(), &blockEntities);
            return body.data;
        }());
        if (fresh->empty()) return false;

        // Revalidate both independent projections at publication. The World
        // revision catches block mutations, while the snapshot ticket catches
        // BE-only edits to this exact chunk. Cache validation + FIFO insertion
        // is one operation with respect to invalidation, so later deltas cannot
        // overtake this older snapshot.
        simulationLock.lock();
        if (GameServer::canonicalDimension(self_->dimension) != dimension) {
            simulationLock.unlock();
            return false;
        }
        auto publishLease = world.lockMutationGate();
        if (!world.hasChunk(cx, cz) ||
            world.revisionAt(cx, cz) != worldRevision) {
            publishLease.unlock();
            simulationLock.unlock();
            continue;
        }
        const bool published = srv_.publishChunkForSnapshot(
            snapshotTicket, worldRevision, fresh,
            [this](const GameServer::ChunkBodyRef& body) {
                conn_->sendPacketBufLowPriority(
                    pl::sc::LevelChunkWithLight, *body);
            });
        publishLease.unlock();
        simulationLock.unlock();
        if (!published) continue;

        sentChunks_.insert(chunkKey(cx, cz));
        return true;
    }
    // Sustained edits can invalidate every optimistic snapshot. Leave the key
    // unsent; the next chunk-batch pump retries it without publishing stale NBT.
    return false;
}
void Session::streamInitialChunks(SimulationDispatchGuard& simulationLock) {
    std::fprintf(stderr, "[cppfm] %s: streaming initial chunks\n", self_->name.c_str());
    chunksStreamed_ = true;
    tickChunksAround(self_->x, self_->z, simulationLock);
}
void Session::tickChunksAround(double px, double pz,
                               SimulationDispatchGuard& simulationLock) {
    const std::int8_t dimension =
        GameServer::canonicalDimension(self_->dimension);
    const int vd = effectiveViewDistance(
        srv_.config(), self_->clientSettings.viewDistance);
    const std::int32_t pcx = static_cast<std::int32_t>(std::floor(px)) >> 4;
    const std::int32_t pcz = static_cast<std::int32_t>(std::floor(pz)) >> 4;

    if (chunkStreamDimension_ && *chunkStreamDimension_ != dimension) {
        sentChunks_.clear();
        pendingChunkStream_.clear();
        lastCx_ = INT32_MAX;
        lastCz_ = INT32_MAX;
    }
    chunkStreamDimension_ = dimension;

    if (pcx != lastCx_ || pcz != lastCz_) {
        WriteBuffer center;
        center.varint(pcx);
        center.varint(pcz);
        conn_->trySendPacketLowPriority(pl::sc::SetCenterChunk, center);
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
    pendingChunkStream_.clear();
    pendingChunkStream_.reserve(todo.size());
    for (const auto& item : todo)
        pendingChunkStream_.push_back(item.second);

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
            conn_->trySendPacketLowPriority(pl::sc::ForgetLevelChunk, f);
            sentChunks_.erase(k);
        }
    }

    sendNextChunkBatch(dimension, simulationLock);
}

void Session::sendNextChunkBatch(
    std::int8_t dimension, SimulationDispatchGuard& simulationLock) {
    if (chunkBatchAwaitingAck_ || pendingChunkStream_.empty()) return;

    const auto attemptCount = std::min(chunkBatchSize_, pendingChunkStream_.size());
    std::vector<std::pair<std::int32_t, std::int32_t>> batch(
        pendingChunkStream_.begin(),
        pendingChunkStream_.begin() + static_cast<std::ptrdiff_t>(attemptCount));
    chunkBatchAwaitingAck_ = true;

    std::size_t sent = 0;
    bool completed = false;
    simulationLock.unlock();
    try {
        conn_->sendPacketLowPriority(pl::sc::ChunkBatchStart, {});
        for (const auto& [cx, cz] : batch)
            if (sendChunk(dimension, cx, cz, simulationLock)) ++sent;
        WriteBuffer finished;
        finished.varint(static_cast<std::int32_t>(sent));
        conn_->sendPacketLowPriority(pl::sc::ChunkBatchFinished, finished);
        completed = true;
    } catch (...) {
        // The packet loop will observe the closed/failed connection. Keep the
        // simulation gate out of chunk generation and packet queueing either
        // way; the guard is reacquired before this dispatch continues.
    }
    simulationLock.lock();

    pendingChunkStream_.erase(
        pendingChunkStream_.begin(),
        pendingChunkStream_.begin() + static_cast<std::ptrdiff_t>(attemptCount));
    chunkBatchAwaitingAck_ = completed;
    const auto currentDimension =
        GameServer::canonicalDimension(self_->dimension);
    if (dimension != currentDimension) {
        // A command may transfer the player while chunk generation runs
        // outside the simulation gate. Chunk keys omit dimension, so discard
        // the old world's sent-set before the next acknowledgement pumps the
        // newly authoritative view.
        sentChunks_.clear();
        pendingChunkStream_.clear();
        chunkStreamDimension_ = currentDimension;
        lastCx_ = INT32_MAX;
        lastCz_ = INT32_MAX;
    }
}
void Session::ack(std::int32_t sequence) {
    WriteBuffer b;
    b.varint(sequence);
    conn_->sendPacket(pl::sc::AckBlockChange, b);
}
void Session::handlePlay() {
    for (;;) {
        if (state_ != State::Play) return;
        try {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        SimulationDispatchGuard simulationLock(srv_.simulationDispatchMtx_);
        self_->lastSeenMs = nowMs();
        const auto packetId = in.u8();

        // Every client packet that can operate on a block-backed menu passes
        // this owner check before dispatch, including specialized handlers
        // such as enchant, rename, recipe placement, and property updates.
        // A chunk can be evicted while the menu retains a shared owner; that
        // owner keeps its memory alive but is no longer part of the persisted
        // block-entity store and must never accept another mutation.
        std::shared_ptr<Menu> activeMenu;
        std::int8_t activeDimension = 0;
        {
            std::lock_guard playerLock(self_->stateMtx);
            activeMenu = openMenu_;
            activeDimension = GameServer::canonicalDimension(self_->dimension);
        }
        const bool menuMutationPacket = [&] {
            switch (packetId) {
            case pl::cs::EnchantItem:
            case pl::cs::WindowClick:
            case pl::cs::PlaceRecipe:
            case pl::cs::NameItem:
            case pl::cs::SetBeaconEffect:
                return true;
            default:
                return false;
            }
        }();
        if (activeMenu && activeMenu->blockKey &&
            activeMenu->blockEntityOwner &&
            srv_.blockEntitiesFor(activeDimension).getShared(
                *activeMenu->blockKey) != activeMenu->blockEntityOwner) {
            bool stillOpen = false;
            {
                std::lock_guard playerLock(self_->stateMtx);
                stillOpen = openMenu_ == activeMenu;
            }
            if (stillOpen) {
                closeOpenMenu(true);
                srv_.resendInventory(*self_);
                syncCursorItem();
            }
            // Reject menu operations that first observe the detached owner;
            // closing the screen and then dispatching a click could still use
            // its stale handler snapshot. Unrelated movement, chat, and
            // keepalive packets continue through normal dispatch.
            if (menuMutationPacket) continue;
        }

        switch (packetId) {
        case pl::cs::AcceptTeleportation:
            onAcceptTeleportation(in, simulationLock); break;
        case pl::cs::MovePlayerPos:
            onMovement(in, true, false, simulationLock); break;
        case pl::cs::MovePlayerPosRot:
            onMovement(in, true, true, simulationLock); break;
        case pl::cs::MovePlayerRot:
            onMovement(in, false, true, simulationLock); break;
        case pl::cs::MovePlayerStatusOnly:
            onMovement(in, false, false, simulationLock); break;
        case pl::cs::KeepAlive: onKeepAlivePacket(in); break;
        case pl::cs::ChatMessage:         onChatMessage(in); break;
        case pl::cs::ChatCommandSigned: if (onChatCommandSignedPacket(in)) return; break; // signed command (spec shape)
        case pl::cs::ChatSessionUpdate:
            onChatSessionUpdate(in);
            if (state_ != State::Play) return;
            break; // plan3 Chat signing
        case pl::cs::MessageAck: in.skipRest(); break;
        case pl::cs::CookieResponse:
            net::consumeCookieResponseBody(in);
            break; // client-owned CookieResponse, no server cache
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
        case pl::cs::ChunkBatchReceived: {
            const float requested = in.f32();
            if (std::isfinite(requested) && requested > 0.0f) {
                const float bounded = std::clamp(
                    requested, 1.0f,
                    static_cast<float>(kMaxChunkBatchSize));
                chunkBatchSize_ = static_cast<std::size_t>(std::ceil(bounded));
            }
            chunkBatchAwaitingAck_ = false;
            if (chunksStreamed_)
                tickChunksAround(self_->x, self_->z, simulationLock);
            break;
        }
        case pl::cs::PingRequest: onPingRequest(in); break;
        case pl::cs::ClientTickEnd: break;
        case pl::cs::Abilities: onPlayerAbilities(in); break; // 0x26 serverbound {flags i8}
        case pl::cs::PlayerLoaded:
            onPlayerLoadedPacket(simulationLock); break; // 0x2a
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

void Session::onAcceptTeleportation(
    ReadBuffer& in, SimulationDispatchGuard& simulationLock) {
    const std::int32_t teleportId = in.varint();
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!self_->pendingTeleportId ||
            *self_->pendingTeleportId != teleportId)
            return;
        self_->pendingTeleportId.reset();
        self_->spawned = true;
    }
    if (!chunksStreamed_)
        streamInitialChunks(simulationLock);
    else
        tickChunksAround(self_->x, self_->z, simulationLock);
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
        const auto lastSeenOffset = in.varint();
        if (lastSeenOffset < 0) return false;   // LastSeenMessages.Update offset, not a count
        const std::uint32_t acknowledgedMask =
            static_cast<std::uint32_t>(in.u8()) |
            (static_cast<std::uint32_t>(in.u8()) << 8) |
            (static_cast<std::uint32_t>(in.u8()) << 16);
        if ((acknowledgedMask & 0xFFF00000u) != 0) return false;
        if (srv_.config().enforcesSecureChat) {
            // Argument signatures are distinct from chat-message signatures:
            // dispatching this packet without reconstructing the command
            // argument transcript would turn an unsigned argument into an
            // authenticated command.  Fail closed until that protocol is
            // implemented instead of executing attacker-controlled text.
            kickPlay("{\"text\":\"Signed commands are unavailable on this server\"}");
            return true;
        }
        dispatchCommand(cmd);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] signed-cmd parse ignored: %s\n", e.what());
    }
    return false;
}
void Session::onChatSessionUpdate(ReadBuffer& in) {
    self_->hasChatSession = false;
    self_->chatPubKey.clear();
    self_->chatSessionSignature.clear();
    try {
    const auto sessionId = in.bytes(16);
    const auto expiry = in.i64();
    const auto pkLen = in.varint();
    if (pkLen <= 0 || pkLen > 512)
        throw std::runtime_error("invalid chat public key length");
    auto publicKey = in.bytes(static_cast<std::size_t>(pkLen));
    const auto sigLen = in.varint();
    if (sigLen <= 0 || sigLen > 4096)
        throw std::runtime_error("invalid chat session signature length");
    auto certificate = in.bytes(static_cast<std::size_t>(sigLen));

    std::vector<std::uint8_t> signedData;
    signedData.reserve(16 + sizeof(std::int64_t) + publicKey.size());
    signedData.insert(signedData.end(), self_->uuid.begin(), self_->uuid.end());
    const auto expiryBits = static_cast<std::uint64_t>(expiry);
    for (int shift = 56; shift >= 0; shift -= 8)
        signedData.push_back(static_cast<std::uint8_t>((expiryBits >> shift) & 0xFF));
    signedData.insert(signedData.end(), publicKey.begin(), publicKey.end());

    bool trusted = false;
    if (self_->onlineAuthenticated && !self_->mojangProfileKeys.empty() && expiry > epochMs()) {
        for (const auto& key : self_->mojangProfileKeys) {
            // Mojang playerCertificateKeys sign the profile certificate with
            // SHA1withRSA; the chat-message transcript below uses SHA-256.
            if (crypto::verifyRsaSha1(key, signedData.data(),
                                      signedData.size(), certificate)) {
                trusted = true;
                break;
            }
        }
    }
    if (!trusted) {
        std::fprintf(stderr, "[cppfm] rejected untrusted chat session for %s\n",
                     self_->name.c_str());
        if (srv_.config().enforceSecureProfile || srv_.config().enforcesSecureChat) {
            // A rejected profile is terminal.  Abort the transport after the
            // disconnect packet so the client cannot continue sending play
            // mutations while ignoring the kick.
            kickPlay("{\"text\":\"Invalid secure profile session\"}");
        }
        return;
    }

    std::copy(sessionId.begin(), sessionId.end(), self_->chatSessionId.begin());
    self_->chatMessageIndex = 0;
    self_->chatSessionExpiry = expiry;
    self_->chatPubKey = std::move(publicKey);
    self_->chatSessionSignature = std::move(certificate);
    self_->lastSeenChatSalts.clear();
    self_->hasChatSession = true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] malformed chat session update for %s: %s\n",
                     self_->name.c_str(), e.what());
        if (srv_.config().enforceSecureProfile || srv_.config().enforcesSecureChat)
            kickPlay("{\"text\":\"Invalid secure profile session\"}");
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
    bool creative = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        creative = self_->gamemode == 1;
    }
    if (!creative) {
        in.skipRest();
        srv_.resendInventory(*self_);
        return;
    }
    const std::int16_t slot = in.i16();
    auto stack = ItemStack::read(in);
    bool resendInventory = false;
    bool syncEquipment = false;
    if (slot >= 0 && slot < 46) {
        {
            std::lock_guard playerLock(self_->stateMtx);
            if (self_->gamemode != 1) {
                resendInventory = true;
            } else if ((slot == 5 || slot == 6 || slot == 7 || slot == 8) &&
                       !self_->inv[slot].empty() && stack.empty() &&
                       EnchantmentHelper::hasBindingCurse(self_->inv[slot])) {
                resendInventory = true;
            } else {
                // The client packet is authoritative. Do not copy opaque NBT
                // from the old slot into a same-ID replacement: the wire
                // format cannot distinguish that replacement from an echo of
                // the old stack. Unsupported item data remains a declared
                // creative-slot round-trip limitation.
                self_->inv[slot] = std::move(stack);
                syncEquipment = slot == 5 || slot == 6 || slot == 7 ||
                                slot == 8 || slot == 45 ||
                                (slot >= 36 && slot <= 44);
            }
        }
    }
    if (resendInventory) srv_.resendInventory(*self_);
    else if (syncEquipment) srv_.syncEquipmentOnChange(*self_);
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
    // Chunk cache keys are local coordinates, not (dimension, coordinates).
    // A respawn can switch dimensions while retaining the same chunk coords;
    // invalidate the session view so its confirmation streams the destination
    // dimension instead of treating source-dimension chunks as already sent.
    sentChunks_.clear();
    lastCx_ = INT32_MAX;
    lastCz_ = INT32_MAX;
    chunksStreamed_ = false;
    hasSent_ = false;
    WriteBuffer ws = session_lifecycle_detail::makeWorldState(
        srv_.config(), targetWorld, self_->gamemode);
    WriteBuffer b;
    b.raw(ws.data.data(), ws.data.size());
    b.u8(0x03);                                    // keep metadata + attributes
    conn_->sendPacketBarrier(pl::sc::Respawn, b);
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
        const double dx = static_cast<double>(sx) + 0.5 - self_->x;
        const double dy = static_cast<double>(sy) + 0.5 - self_->y;
        const double dz = static_cast<double>(sz) + 0.5 - self_->z;
        if (dx * dx + dy * dy + dz * dz > 36.0) return;
        const auto dimension = self_->dimension;
        if (!srv_.isOp(self_->name) && dimension == 0 &&
            srv_.isSpawnProtected(sx, sz)) return;
        auto& signWorld = srv_.worldFor(dimension);
        // The world mutation gate serializes this check and publication with
        // block replacement/removal. Without it, an edit admitted for a sign
        // could recreate an orphan sign entity after another session breaks
        // that block.
        auto mutationGate = signWorld.lockMutationGate();
        const auto signState = signWorld.getBlock(sx, sy, sz);
        const auto* signBlock = gen::blockByState(signState);
        if (!signBlock || std::string(signBlock->name).find("sign") == std::string::npos) return;
        const std::int64_t key = posKey(sx, sy, sz);
        auto& blockEntityStore = srv_.blockEntitiesFor(dimension);
        auto beOwner = blockEntityStore.getShared(key);
        const auto admitCandidate = [this, dimension](
            const BlockEntityStore::SnapshotOverride& candidate) {
            return srv_.blockEntityCandidatesFitAnvilEntry(
                dimension,
                std::vector<BlockEntityStore::SnapshotOverride>{candidate});
        };
        const auto applySignText = [&lines, front](BlockEntity& candidate) {
            if (candidate.kind != BlockEntity::Kind::Sign) return false;
            std::string* destination = front ? candidate.sign.front
                                             : candidate.sign.back;
            for (int i = 0; i < 4; ++i) destination[i] = lines[i];
            if (front) candidate.sign.hasFront = true;
            else candidate.sign.hasBack = true;
            return true;
        };

        if (!beOwner) {
            BlockEntity candidate;
            candidate.kind = BlockEntity::Kind::Sign;
            if (!applySignText(candidate)) return;
            const auto installed =
                blockEntityStore.tryCreateSharedCandidateIfAdmitted(
                    key, candidate, admitCandidate);
            if (!installed) {
                sendSystemText(msg::kRed +
                    "Sign update rejected because the updated block entity cannot be safely saved");
                sendSignBlockEntity(dimension, sx, sy, sz);
                return;
            }
            if (installed.inserted) {
                // The complete candidate was published only after admission;
                // insertion's dirty callback accounts for this update.
                sendSignBlockEntity(dimension, sx, sy, sz);
                return;
            }
            beOwner = installed.owner;
        }

        if (!blockEntityStore.applyCandidateIfAdmitted(
                key, beOwner, applySignText, admitCandidate)) {
            sendSystemText(msg::kRed +
                "Sign update rejected because the updated block entity cannot be safely saved");
            sendSignBlockEntity(dimension, sx, sy, sz);
            return;
        }
        sendSignBlockEntity(dimension, sx, sy, sz);
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
        if (self_->gamemode != 1) { in.skipRest(); return; }
        std::int32_t bx, by, bz;
        in.position(bx, by, bz);
        const bool includeData = in.boolean();
        (void)includeData; // BE-copy detail deferred (docs/SPEC_WIRE.md)
        const double dx = static_cast<double>(bx) + 0.5 - self_->x;
        const double dy = static_cast<double>(by) + 0.5 - self_->y;
        const double dz = static_cast<double>(bz) + 0.5 - self_->z;
        if (dx * dx + dy * dy + dz * dz > 36.0) return;
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
        if (self_->gamemode != 1) { in.skipRest(); return; }
        const std::int32_t eid = in.varint();
        const bool includeData = in.boolean();
        (void)includeData;
        std::string egg;
        {
            for (const auto& m : srv_.mobsSnapshot()) {
                if (!m) continue;
                if (m->entityId != eid) continue;
                const double dx = m->x - self_->x;
                const double dy = (m->y + 0.5) - self_->y;
                const double dz = m->z - self_->z;
                if (dx * dx + dy * dy + dz * dz > 36.0) return;
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
        const bool uuidMatches = uuid == resourcePackUuid_;
        // A required pack is complete only after SUCCESSFULLY_LOADED (0).
        // ACCEPTED/DOWNLOADED are valid intermediate responses; failures and
        // an unrelated UUID are not.  FinishAcknowledgement enforces loaded.
        const bool packFailure = result != 0 && result != 3 && result != 4;
        if (resourcePackRequired_ && (!uuidMatches || packFailure)) {
            disconnectIn("{\"text\":\"Required resource pack was not loaded\"}");
            state_ = State::Done;
            return true;
        }
        if (uuidMatches && result == 0) resourcePackLoaded_ = true;
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
void Session::onPlayerLoadedPacket(
    SimulationDispatchGuard& simulationLock) {
    if (!chunksStreamed_) streamInitialChunks(simulationLock);
}
void Session::onMovement(ReadBuffer& in, bool hasPos, bool hasRot,
                         SimulationDispatchGuard& simulationLock) {
    const session_detail::SessionPlayerSnapshot movementState = session_detail::snapshotPlayerForSession(*self_);
    // A client may already have queued movement for its old position when a
    // server teleport is sent. Do not let that stale frame overwrite the
    // authoritative destination before the matching teleport acknowledgement.
    if (movementState.awaitingTeleportConfirmation) return;
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
                    const bool doTrample = (prob >= 1.0F) ||
                        (static_cast<float>(nextRandom()) /
                         static_cast<float>(RAND_MAX) < prob);
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
                std::int8_t sourceDimension = 0;
                {
                    std::lock_guard playerLock(self_->stateMtx);
                    sourceDimension = GameServer::canonicalDimension(
                        self_->dimension);
                }
                // tryTeleport temporarily releases the simulation gate while
                // preparing destination chunks. Never hold either dig mutex
                // across that call: the tick needs the same gate before it can
                // observe the portal transition and would otherwise deadlock.
                const bool ok = PortalHandler::tryTeleport(
                    srv_, *self_, target, simulationLock);
                if (ok) {
                    // tryTeleport returns with the simulation gate reacquired.
                    // Clear the old target before the tick can run again.
                    std::lock_guard transitionLock(self_->digTransitionMtx);
                    std::int32_t digX = 0, digY = 0, digZ = 0;
                    bool wasDigging = false;
                    {
                        std::lock_guard digLock(self_->digMtx);
                        wasDigging = self_->digActive;
                        digX = self_->digX;
                        digY = self_->digY;
                        digZ = self_->digZ;
                        self_->digActive = false;
                        self_->digTotalTicks = 0;
                        self_->digLastStage = 255;
                        ++self_->digRevision;
                    }
                    if (wasDigging)
                        srv_.broadcastDigStageAt(
                            *self_, sourceDimension, digX, digY, digZ, -1);
                    sentChunks_.clear();
                    lastCx_ = INT32_MAX; lastCz_ = INT32_MAX;
                }
            }
        }
    }
    if (self_->spawned) {
        try {
            if (!chunksStreamed_) streamInitialChunks(simulationLock);
            else tickChunksAround(self_->x, self_->z, simulationLock);
        } catch (...) {}
    }
}
void Session::broadcastMovement() {
    if (!self_->spawned) return;
    const bool first = !hasSent_;
    if (first) {                                   // initial absolute pose
        const WriteBuffer b = makeEntityTeleportBody(
            self_->entityId, self_->x, self_->y, self_->z,
            0.0, 0.0, 0.0, self_->yaw, self_->pitch, 0, self_->onGround);
        srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
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
                srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
                                                      pl::sc::MoveEntityPosRot, b);
            } else {
                WriteBuffer b;
                b.varint(self_->entityId);
                b.i16(static_cast<std::int16_t>(dx * 4096));
                b.i16(static_cast<std::int16_t>(dy * 4096));
                b.i16(static_cast<std::int16_t>(dz * 4096));
                b.boolean(self_->onGround);
                srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
                                                      pl::sc::MoveEntityPos, b);
            }
            WriteBuffer h;
            h.varint(self_->entityId);
            h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
            srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
                                                  pl::sc::RotateHead, h);
        } else {                                    // teleport-class delta
            const WriteBuffer b = makeEntityTeleportBody(
                self_->entityId, self_->x, self_->y, self_->z,
                0.0, 0.0, 0.0, self_->yaw, self_->pitch, 0,
                self_->onGround);
            srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
                                                  pl::sc::EntityTeleport, b);
        }
    } else if (rotated) {                           // pure rotation
        WriteBuffer b;
        b.varint(self_->entityId);
        b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
        b.boolean(self_->onGround);
        srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
                                              pl::sc::EntityLook, b);
        WriteBuffer h;
        h.varint(self_->entityId);
        h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        srv_.broadcastPacketExceptInDimension(self_->dimension, self_.get(),
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
    const auto lastSeenOffset = in.varint();         // LastSeenMessages.Update offset, not a count
    if (lastSeenOffset < 0)
        throw std::runtime_error("invalid chat last-seen offset");
    const std::uint32_t acknowledgedMask =
        static_cast<std::uint32_t>(in.u8()) |
        (static_cast<std::uint32_t>(in.u8()) << 8) |
        (static_cast<std::uint32_t>(in.u8()) << 16);
    if ((acknowledgedMask & 0xFFF00000u) != 0)
        throw std::runtime_error("invalid chat acknowledged bitset");
    const bool secureChatEnforced = secure_chat_policy::isEnforced(
        srv_.config().enforceSecureProfile, srv_.config().enforcesSecureChat);

    // Verify signed messages whenever a player session is available. The
    // profile-enforcement setting (or cppfm's explicit extension) controls
    // whether an unsigned message is rejected; online-mode by itself does
    // not suppress vanilla's unsigned-chat path.
    bool usePlayerChat = false;
    if (self_->hasChatSession && !signature.empty()) {
        const bool duplicate = std::find(self_->lastSeenChatSalts.begin(),
                                         self_->lastSeenChatSalts.end(), salt) !=
                               self_->lastSeenChatSalts.end();
        if (duplicate) {
            if (secureChatEnforced) {
                WriteBuffer kick;
                nbt::writeTextComponent(kick, "Duplicate chat message");
                conn_->trySendPacket(proto::pl::sc::Disconnect, kick);
                conn_->close();
            }
            return;
        }
        usePlayerChat = ChatMessageProcessor::verify(*self_, msg, timestamp, salt,
                                                      lastSeenOffset, acknowledgedMask, signature);
        if (usePlayerChat) {
            ++self_->chatMessageIndex;
            self_->lastSeenChatSalts.push_back(salt);
            if (self_->lastSeenChatSalts.size() > 20)
                self_->lastSeenChatSalts.erase(self_->lastSeenChatSalts.begin());
        }
    }
    const auto disposition = secure_chat_policy::classifyMessage(
        srv_.config().enforceSecureProfile, srv_.config().enforcesSecureChat,
        self_->hasChatSession, !signature.empty(), usePlayerChat);
    if (disposition == secure_chat_policy::MessageDisposition::Reject) {
        WriteBuffer kick;
        nbt::writeTextComponent(kick, "Chat message signature invalid or missing");
        conn_->trySendPacket(proto::pl::sc::Disconnect, kick);
        conn_->close();
        return;
    }

    if (disposition == secure_chat_policy::MessageDisposition::DropSessionUnsigned)
        return;

    // events: PlayerChat (cancellable), after authentication and replay checks
    api::PlayerChatEvent ev;
    ev.player = self_.get();
    ev.message = msg;
    if (!srv_.events().chat.fire(ev)) return;
    if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onChat(*self_, ev.message)) return;

    if (!ev.message.empty() && ev.message[0] == '/')
        return dispatchCommand(ev.message.substr(1));
    if (usePlayerChat && !srv_.config().enforcesSecureChat &&
        ChatMessageProcessor::shouldUsePlayerChat(*self_)) {
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
    // JVM callbacks may re-enter nativeExecuteCommand, which queues work for
    // the simulation thread.  Invoke them before taking either dispatch lock.
    if (srv_.jvmRuntime() && !srv_.jvmRuntime()->onCommand(self_.get(), command)) return;
    SimulationDispatchGuard simulationLock(srv_.simulationDispatchMtx_);
    std::lock_guard commandLock(srv_.commandDispatchMtx_);
    brigadier::CommandSource src;
    src.player = self_.get();
    src.name = self_->name;
    src.console = false;
    src.hasOp = srv_.isOp(self_->name); // plan42 R3 (E-19): truthful op flag for kick/whitelist permission control
    src.srcX = self_->x; src.srcY = self_->y; src.srcZ = self_->z;
    src.srcYaw = self_->yaw; src.srcPitch = self_->pitch;
    srv_.bindCommandSelector(src);

    const auto rootEnd = command.find_first_of(" \t\r\n");
    std::string root = command.substr(0, rootEnd);
    std::transform(root.begin(), root.end(), root.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const bool unrestricted = root == "help" || root == "list" || root == "me" ||
                              root == "msg" || root == "tell" || root == "w" ||
                              root == "whisper" || root == "teammsg" ||
                              root == "trigger";
    if (!src.hasOp && !unrestricted) {
        sendSystemText(msg::kRed + "You do not have permission to use /" + root);
        return;
    }

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
            const session_detail::SessionPlayerSnapshot playerState = session_detail::snapshotPlayerForSession(*self_);
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
        const bool damageApplied =
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
        if (damageApplied)
            srv_.noteMobHurt(after.entityId, attacker.entityId,
                             EnchantmentHelper::getLooting(weaponStack));
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
                const bool damageApplied = srv_.applyDamageToMob(
                    *candidate.mob, sweepDmg, ssrc, breachLv);
                SessionMobSnapshot state = snapshotMobForSession(*candidate.mob);
                if (damageApplied)
                    srv_.noteMobHurt(
                        state.entityId, attacker.entityId,
                        EnchantmentHelper::getLooting(weaponStack));
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
        const float kbForce = 0.4F * static_cast<float>(punchLvl + kbLvl) +
                              (punchLvl > 0 ? 0.5F : 0.0F);
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
        if (!victim->tryClaimDeathHandling()) return;
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
        auto lootContext = srv_.mobLootContextForDeath(
            death.entityId, death.entityOnFire);
        // A successful direct attack is the killing blow, even if an entity
        // has not yet acquired an AI entry and therefore has no hurt memory.
        lootContext.wasKilledByPlayer = true;
        lootContext.lootingLevel = EnchantmentHelper::getLooting(weaponStack);
        srv_.spawnMobLootFor(death.kind, death.dimension, death.x,
                             death.y + 0.4, death.z, lootContext);
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
