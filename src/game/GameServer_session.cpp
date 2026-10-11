#include "GameServer.hpp"
#include "SessionInteractionSupport.hpp"
#include "SecureChatPolicy.hpp"
#include "Messages.hpp"
#include "MetadataTypes.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "CombatManager.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
#include "Particles.hpp"
#include "PositionPackets.hpp"
#include "../net/CookieResponse.hpp"

namespace cppfm {
using namespace proto;

namespace {
int effectiveViewDistance(const ServerConfig& config) {
    return std::clamp(config.viewDistance, constants::kViewDistanceMin,
                      constants::kViewDistanceMax);
}

int effectiveViewDistance(const ServerConfig& config, int clientViewDistance) {
    return std::clamp(std::min(config.viewDistance, clientViewDistance),
                      constants::kViewDistanceMin, constants::kViewDistanceMax);
}


} // namespace

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
struct PlayerSpawnSnapshot {
    std::shared_ptr<Connection> connection;
    std::array<std::uint8_t, 16> uuid{};
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool inPlay = false;
    bool trackingSpawnPublished = false;
};

static PlayerSpawnSnapshot snapshotPlayerSpawn(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    return {player.conn, player.uuid, player.entityId,
            GameServer::canonicalDimension(player.dimension),
            player.x, player.y, player.z, player.yaw, player.pitch,
            player.inPlay, player.trackingSpawnPublished};
}

static WriteBuffer makeSpawnEntity(const PlayerSpawnSnapshot& p) {
    WriteBuffer b;
    b.varint(p.entityId);
    b.uuid(p.uuid.data());
    b.varint(static_cast<std::int32_t>(gen::kPlayerEntityTypeId));
    b.f64(p.x); b.f64(p.y); b.f64(p.z);
    b.i8(constants::protocolSignedAngleByte(p.pitch));
    b.i8(constants::protocolSignedAngleByte(p.yaw));
    b.i8(constants::protocolSignedAngleByte(p.yaw));
    b.varint(0);
    b.i16(0); b.i16(0); b.i16(0);
    return b;
}
static WriteBuffer makeSkinMetadata(std::int32_t entityId) {
    WriteBuffer md;
    md.varint(entityId);
    md.u8(17); md.u8(0);
    md.u8(0x7F);
    md.u8(255);
    return md;
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
                if (state_ == State::Status && statusAdmission_ &&
                    !statusAdmission_())
                    state_ = State::Done;
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
                if (state_ == State::Play && releaseAdmission_) {
                    releaseAdmission_();
                    releaseAdmission_ = {};
                }
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
        // happens, while failures remain visible in the server log. The shared
        // completion is published only after roster removal; same-UUID logins
        // wait on it before loading any saved state.
        registered_ = false;
        bool handoffReady = true;
        const auto cleanupStep = [this, &handoffReady](
            const char* name, bool requiredForHandoff, auto&& step) {
            try {
                step();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[cppfm] session cleanup (%s) failed: %s\n",
                             name, e.what());
                if (requiredForHandoff) handoffReady = false;
            } catch (...) {
                std::fprintf(stderr, "[cppfm] session cleanup (%s) failed\n", name);
                if (requiredForHandoff) handoffReady = false;
            }
        };
        cleanupStep("session registry removal", true, [this] {
            srv_.unregisterSession(this);
        });
        cleanupStep("inactive transition", true, [this] {
            // Stop using this player for simulation-distance decisions
            // immediately, before hooks or persistence can take longer.
            std::lock_guard playerLock(self_->stateMtx);
            self_->inPlay = false;
        });
        cleanupStep("JVM quit hook", true, [this] {
            if (srv_.jvmRuntime()) srv_.jvmRuntime()->onPlayerQuit(*self_);
        });
        cleanupStep("player quit event", true, [this] {
            api::PlayerQuitEvent qev;
            qev.player = self_.get();
            srv_.events().quit.fire(qev);
        });
        cleanupStep("progress save", true, [this, &handoffReady] {
            if (!srv_.savePlayerProgress(*self_)) handoffReady = false;
        });
        cleanupStep("leave message", false, [this] {
            srv_.broadcastSystemText((msg::kYellow + self_->name + " left the game"), nullptr);
        });
        cleanupStep("player list removal", true, [this] {
            srv_.serializePlayerTrackingPublication([this] {
                // A duplicate login may already have replaced this session
                // in the roster. Its PlayerInfo Add was published inside the
                // same sequencer, so the old session must not Remove that
                // replacement's entry when its socket cleanup finally runs.
                for (const auto& current : srv_.playersSnapshot()) {
                    if (!current || current.get() == self_.get()) continue;
                    std::lock_guard playerLock(current->stateMtx);
                    if (current->inPlay && current->uuid == self_->uuid)
                        return;
                }
                WriteBuffer remove;
                remove.varint(1);
                remove.uuid(self_->uuid.data());
                srv_.broadcastPacketExcept(
                    nullptr, pl::sc::PlayerInfoRemove, remove);
            });
        });
        cleanupStep("entity removal", true, [this] {
            srv_.serializePlayerTrackingPublication([this] {
                std::int8_t dimension = 0;
                {
                    std::lock_guard playerLock(self_->stateMtx);
                    dimension = self_->dimension;
                    self_->trackingSpawnPublished = false;
                }
                srv_.broadcastPlayerTrackingRemovalUnsequenced(
                    *self_, dimension);
            });
        });
        cleanupStep("chat suggestions", false, [this] {
            srv_.broadcastChatSuggestions(1, std::vector<std::string>{self_->name}, nullptr);
        });
        cleanupStep("score reset", false, [this] {
            // D26: wildcard reset_score 0x49 for disconnecting holder to clear sidebar ghosts
            const auto affected = srv_.scoreboard.resetAllScores(self_->name);
            if (!affected.empty()) srv_.sendResetScoreAllWildcard(self_->name);
        });
        cleanupStep("player save", true, [this, &handoffReady] {
            if (preservePlayerDataOnDisconnect_ ||
                !srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_))
                handoffReady = false;
        });
        cleanupStep("roster removal", true, [this] {
            srv_.removePlayer(self_.get());
        });
        if (!handoffReady) {
            try {
                srv_.blockPlayerLoginHandoff(
                    GameServer::uuidToHex(self_->uuid));
            } catch (...) {
                std::fprintf(stderr,
                             "[cppfm] could not fence failed player handoff for %s\n",
                             self_->name.c_str());
            }
        }
        try {
            cleanupCompletionPromise_.set_value(handoffReady);
        } catch (...) {
            std::fprintf(stderr,
                         "[cppfm] could not publish cleanup completion for %s\n",
                         self_->name.c_str());
        }
    }
}

Session::~Session() {
    if (playerAdmissionReserved_) srv_.releasePlayerAdmission();
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
    root.set("enforcesSecureChat",
             json::Value::ofBool(secure_chat_policy::isEnforced(
                 server.config().enforceSecureProfile,
                 server.config().enforcesSecureChat)));

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
// Queue the reason, then use the bounded graceful writer drain so the client
// receives the protocol Disconnect even when the simulation writer is busy.
void Session::kickPlay(const char* jsonReason) {
    if (state_ == State::Done) return;
    try { disconnectIn(jsonReason); } catch (...) {}
    conn_->close();
    state_ = State::Done;
}
void Session::handleLogin() {
    auto frame = conn_->readFrame();
    SimulationDispatchGuard simulationLock(srv_.simulationDispatchMtx_);
    ReadBuffer in(frame);
    if (in.u8() != lo::cs::Hello) throw std::runtime_error("expected login hello");

    const auto rejectLogin = [this](const char* reason, bool bestEffort) {
        WriteBuffer kick;
        nbt::writeTextComponent(kick, reason);
        if (bestEffort) conn_->trySendPacket(proto::lo::sc::Disconnect, kick);
        else conn_->sendPacket(proto::lo::sc::Disconnect, kick);
        state_ = State::Done;
    };

    self_->name = [&in, &rejectLogin]{
        try {
            return in.string(16);
        } catch (const std::exception&) {
            rejectLogin("Invalid username", true);
            return std::string{};
        }
    }();
    if (state_ == State::Done) return;

    if (!GameServer::isValidPlayerName(self_->name)) {
        rejectLogin("Invalid username", true);
        return;
    }

    auto uuidBytes = in.bytes(16);
    std::copy(uuidBytes.begin(), uuidBytes.end(), self_->uuid.begin());
    // ban check (banned-players.json)
    if (srv_.isBanned(self_->name)) {
        rejectLogin("You are banned from this server", false);
        return;
    }
    // ip ban check
    {
        std::string peer = conn_->peer();
        std::string ip = peer;
        auto colon = ip.find(':');
        if (colon != std::string::npos) ip = ip.substr(0, colon);
        if (!ip.empty() && ip != "?" && srv_.isIpBanned(ip)) {
            rejectLogin("You are IP banned from this server", false);
            return;
        }
    }
    if (srv_.whitelist().enabled()) {
        // any registered-name match is impossible pre-join; check file-backed list
        bool ok = srv_.whitelist().contains(self_->name) || srv_.isOp(self_->name);
        if (!ok) {
            rejectLogin("You are not whitelisted on this server", false);
            return;
        }
    }
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

        // Do not hold the world gate while waiting for the client key response.
        simulationLock.unlock();
        auto pbody = conn_->readFrame();
        simulationLock.lock();
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
        // Mojang session authentication is external I/O; let the simulation
        // tick and other sessions proceed while it waits.
        simulationLock.unlock();
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
        if (authOk && !std::getenv("CPPFM_AUTH_STUB")) {
            try {
                self_->mojangProfileKeys = fetchMojangProfilePublicKeys();
            } catch (const std::exception&) {
                self_->mojangProfileKeys.clear();
            }
        }
        simulationLock.lock();
        if (!authOk) {
            rejectLogin("Failed to verify your session (online mode)", false);
            return;
        }
        if (srv_.config().enforceSecureProfile &&
            (std::getenv("CPPFM_AUTH_STUB") || self_->mojangProfileKeys.empty())) {
            rejectLogin("Secure profile authentication is unavailable", false);
            return;
        }
        self_->onlineAuthenticated = !std::getenv("CPPFM_AUTH_STUB") &&
                                     !self_->mojangProfileKeys.empty();
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
    if (srv_.config().enforceSecureProfile && !self_->onlineAuthenticated) {
        rejectLogin("Secure profile authentication requires online mode", false);
        return;
    }
    // Capacity is reserved at FinishAck, after same-UUID replacement has
    // retired the old session. Reserving here would reject a valid takeover
    // when maxPlayers is already occupied by that identity.
    self_->entityId = 0; // set on play entry

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

    simulationLock.unlock();
    for (;;) {
        auto f2 = conn_->readFrame();
        simulationLock.lock();
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
            simulationLock.unlock();
            break;                                       // tolerated, no server use
        case lo::cs::CookieResponse: {                   // 0x04 cookie
            net::consumeCookieResponseBody(in2);
            simulationLock.unlock();
            break;
        }
        case lo::cs::Hello:                              // 0x00 re-sent start
        case lo::cs::Key:                                // 0x01 late encryption
            in2.skipRest();                              // tolerated (already past)
            simulationLock.unlock();
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
        net::consumeCookieResponseBody(in);
        return ConfigWaitResult::Continue;
    }
    case cf::cs::ResourcePackResponse: {
        const auto uuid = in.bytes(16);
        const bool uuidMatches = uuid.size() == resourcePackUuid_.size() &&
            std::equal(uuid.begin(), uuid.end(), resourcePackUuid_.begin());
        const std::int32_t result = in.varint();
        const bool packFailure = result != 0 && result != 3 && result != 4;
        if (resourcePackRequired_ && (!uuidMatches || packFailure)) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick,
                                    "Required resource pack was not accepted");
            conn_->trySendPacket(cf::sc::Disconnect, kick);
            state_ = State::Done;
            return ConfigWaitResult::Abort;
        }
        if (uuidMatches && result == 0) resourcePackLoaded_ = true;
        return ConfigWaitResult::Continue;
    }
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
        std::copy(packUuid.begin(), packUuid.end(), resourcePackUuid_.begin());
        resourcePackRequired_ = srv_.config().resourcePackForced;
        resourcePackLoaded_ = false;
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
        SimulationDispatchGuard simulationLock(srv_.simulationDispatchMtx_);
        auto r = handleOneConfigPacket(in);
        if (r == ConfigWaitResult::Abort) return;
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
        SimulationDispatchGuard simulationLock(srv_.simulationDispatchMtx_);
        auto r = handleOneConfigPacket(in);
        if (r == ConfigWaitResult::Abort) return;
        if (r == ConfigWaitResult::FinishAck) {
            if (resourcePackRequired_ && !resourcePackLoaded_) {
                WriteBuffer kick;
                nbt::writeTextComponent(kick,
                                        "Required resource pack was not loaded");
                conn_->trySendPacket(cf::sc::Disconnect, kick);
                state_ = State::Done;
                return;
            }
            std::fprintf(stderr, "[cppfm] %s: finish ack at %.2f\n", self_->name.c_str(),
                         std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
            state_ = State::Play;
            onEnterPlay(simulationLock);
            std::fprintf(stderr, "[cppfm] %s: onEnterPlay done\n", self_->name.c_str());
            return;
        }
        // PacksDone here = late select_known_packs retransmit: absorbed.
    }
}
void Session::onEnterPlay(SimulationDispatchGuard& simulationLock) {
    const std::string uuidHex = GameServer::uuidToHex(self_->uuid);

    // A duplicate login cannot wait for the old worker while holding the
    // simulation gate: that worker needs the same gate to unregister before
    // running its quit hooks and persistence cleanup.
    simulationLock.unlock();
    auto handoffLease = srv_.enterPlayerLoginHandoff(uuidHex);
    const auto rejectConfiguration = [this](const char* reason) {
        WriteBuffer disconnect;
        nbt::writeTextComponent(disconnect, reason);
        try { conn_->sendPacket(cf::sc::Disconnect, disconnect); } catch (...) {}
        conn_->close();
        state_ = State::Done;
    };

    if (srv_.playerLoginHandoffBlocked(uuidHex)) {
        rejectConfiguration(
            "Previous session could not be safely saved; login denied to protect player data.");
        return;
    }
    if (!srv_.reservePlayerAdmissionReplacing(self_->uuid)) {
        rejectConfiguration("Server is full");
        return;
    }
    playerAdmissionReserved_ = true;

    std::vector<std::shared_future<bool>> retiringSessions;
    std::vector<std::shared_ptr<Connection>> retiringConnections;
    bool missingCleanupCompletion = false;
    for (const auto& player : srv_.playersSnapshot()) {
        if (!player || player.get() == self_.get()) continue;
        std::lock_guard playerLock(player->stateMtx);
        if (player->uuid != self_->uuid) continue;
        player->inPlay = false;
        if (player->cleanupCompletion.valid())
            retiringSessions.push_back(player->cleanupCompletion);
        else
            missingCleanupCompletion = true;
        if (player->conn) retiringConnections.push_back(player->conn);
    }
    if (missingCleanupCompletion) {
        srv_.blockPlayerLoginHandoff(uuidHex);
        rejectConfiguration(
            "Previous session state cannot be safely handed off; login denied.");
        return;
    }

    if (!retiringConnections.empty()) {
        WriteBuffer kick;
        nbt::writeTextComponent(kick,
                                "You logged in from another location");
        for (const auto& connection : retiringConnections) {
            if (!connection) continue;
            connection->trySendPacket(pl::sc::Disconnect, kick);
            connection->close();
        }
    }

    bool retirementSucceeded = true;
    for (const auto& completion : retiringSessions) {
        try {
            if (!completion.get()) retirementSucceeded = false;
        } catch (...) {
            retirementSucceeded = false;
        }
    }
    if (!retirementSucceeded || srv_.playerLoginHandoffBlocked(uuidHex)) {
        rejectConfiguration(
            "Previous session could not be safely saved; login denied to protect player data.");
        return;
    }
    if (!conn_->isOpen()) {
        state_ = State::Done;
        return;
    }

    // Only after the previous worker has completed every required quit hook,
    // save, tracking removal, and roster removal may this worker load state or
    // enqueue its own PlayerInfo Add. Re-entering here is non-blocking; the
    // per-identity gate remains held through publication and join setup.
    simulationLock.lock();

    self_->conn = conn_;
    self_->entityId = srv_.nextEntityId();
    self_->lastSeenMs = nowMs();

    // Load the durable dimension/position before emitting Login and the
    // first chunk view.  Loading afterwards advertised the overworld and
    // then silently left the client at the hard-coded fresh-spawn point.
    bool unsupportedItemData = false;
    bool unsafeExistingPlayerData = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        self_->gamemode = srv_.defaultGameMode();
    }
    (void)srv_.loadPlayerData(
        uuidHex, *self_,
        nullptr, &unsupportedItemData,
        &unsafeExistingPlayerData);
    if (unsupportedItemData || unsafeExistingPlayerData) {
        preservePlayerDataOnDisconnect_ = true;
        WriteBuffer kick;
        nbt::writeTextComponent(
            kick,
            unsupportedItemData
                ? "Saved player data contains an item this server cannot load; install compatible mods and retry. Player data was preserved."
                : "Saved player data could not be safely loaded or isolated. The original data was preserved; check server file access and retry.");
        conn_->trySendPacket(pl::sc::Disconnect, kick);
        state_ = State::Done;
        return;
    }
    if (srv_.config().forceGameMode) {
        std::lock_guard playerLock(self_->stateMtx);
        if (self_->gamemode != srv_.defaultGameMode()) {
            self_->previousGamemode = self_->gamemode;
            self_->gamemode = srv_.defaultGameMode();
        }
    }
    bool inventoryCanBeSent = true;
    {
        std::lock_guard playerLock(self_->stateMtx);
        for (const auto& stack : self_->inv) {
            if (!stack.canWriteToProtocol()) {
                inventoryCanBeSent = false;
                break;
            }
        }
        if (inventoryCanBeSent) {
            for (const auto& stack : self_->enderItems) {
                if (!stack.canWriteToProtocol()) {
                    inventoryCanBeSent = false;
                    break;
                }
            }
        }
    }
    if (!inventoryCanBeSent) {
        WriteBuffer kick;
        nbt::writeTextComponent(
            kick,
            "Saved player inventory data contains items this server cannot encode; player data was preserved.");
        conn_->trySendPacket(pl::sc::Disconnect, kick);
        state_ = State::Done;
        return;
    }
    self_->dimension = GameServer::canonicalDimension(self_->dimension);
    sendJoinGame();
    {
        // Join Game establishes the world, but clients also need the current
        // clock immediately; waiting for the next periodic sync leaves a new
        // session with stale/no day and time state during its first second.
        WriteBuffer time;
        time.i64(srv_.tickNow());
        time.i64(srv_.dayTime());
        time.boolean(!srv_.gameRules().contains("doDaylightCycle") ||
                     srv_.gameRules().getBool("doDaylightCycle"));
        conn_->sendPacket(pl::sc::UpdateTime, time);
    }
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

    srv_.serializePlayerTrackingPublication([&] {
        sendPlayerInfoAddSelf();
        // Publish the existing roster and this player's list entry as one
        // ordered operation. The player is admitted before releasing the
        // sequencer so a concurrent leave cannot remove its entry before this
        // session joins the recipients of the removal packet.
        broadcastPlayerInfoAdd(self_.get());
        for (const auto& other : srv_.playersSnapshot()) {
            if (!other || other.get() == self_.get()) continue;
            std::array<std::uint8_t, 16> uuid{};
            std::string name;
            bool inPlay = false;
            {
                std::lock_guard playerLock(other->stateMtx);
                inPlay = other->inPlay;
                if (inPlay) {
                    uuid = other->uuid;
                    name = other->name;
                }
            }
            if (!inPlay) continue;
            WriteBuffer add;
            add.u8(0x01 | 0x08); // add_player | update_listed
            add.varint(1);
            add.uuid(uuid.data());
            add.string(name);
            add.varint(0); // properties
            add.varint(1); // listed
            if (!conn_->trySendPacket(pl::sc::PlayerInfoUpdate, add)) {
                conn_->close();
                break;
            }
        }

        {
            std::lock_guard playerLock(self_->stateMtx);
            self_->cleanupCompletion = cleanupCompletionFuture_;
        }
        registered_ = true;
        srv_.addPlayer(self_, true);
        playerAdmissionReserved_ = false;
        {
            std::lock_guard playerLock(self_->stateMtx);
            self_->inPlay = true;
            self_->health = 20;
            self_->food = 20;
            self_->saturation = 5;
            self_->exhaustion = 0;
            self_->fallDist = 0;
            self_->dead = false;
            if (self_->enchantmentSeed == 0) {
                self_->enchantmentSeed = static_cast<std::int32_t>(
                    self_->entityId * 0x9e3779b9u ^ srv_.config().hashedSeed ^
                    0x27d4eb2du);
                if (self_->enchantmentSeed == 0)
                    self_->enchantmentSeed = 0x5a5a5a5a;
            }
        }
    });
    // This helper enters the simulation-dispatch gate. Keep it outside the
    // player-publication sequencer because portal/respawn paths acquire those
    // locks in the opposite (simulation, then publication) order.
    srv_.registerSession(this);

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

    sendInitialInventory();
    // Spawn packets establish the tracked player entities.  Publish equipment
    // only after both directions have been sent, and force the new player's
    // post-spawn snapshot in case an update raced the earlier roster insertion.
    synchronizeEquipmentForTrackedPlayers();
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
    std::int8_t sourceDimension = 0;
    std::int8_t targetDimension = 0;
    std::uint8_t gamemode = 0;
    bool hasRespawnPoint = false;
    std::int32_t respawnX = 0;
    std::int32_t respawnY = 0;
    std::int32_t respawnZ = 0;
    float respawnAngle = 0.0f;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!self_->dead) return;
        sourceDimension = GameServer::canonicalDimension(self_->dimension);
        hasRespawnPoint = self_->hasRespawnPoint;
        targetDimension = hasRespawnPoint
            ? GameServer::canonicalDimension(self_->respawnDimension) : 0;
        respawnX = self_->respawnX;
        respawnY = self_->respawnY;
        respawnZ = self_->respawnZ;
        respawnAngle = self_->respawnAngle;
        gamemode = self_->gamemode;
    }
    // A death may be followed by a respawn packet before the next server tick
    // observes the dead state. Do not allow a pre-death item use to continue.
    stopAllItemUse();
    const auto& targetWorld = srv_.worldFor(targetDimension);
    const auto worldSpawn = targetWorld.spawnPoint();
    const std::int32_t spawnX = hasRespawnPoint ? respawnX : worldSpawn.x;
    const std::int32_t spawnY = hasRespawnPoint ? respawnY : worldSpawn.y;
    const std::int32_t spawnZ = hasRespawnPoint ? respawnZ : worldSpawn.z;
    const float spawnAngle = hasRespawnPoint ? respawnAngle : worldSpawn.angle;
    srv_.serializePlayerTrackingPublication([&] {
        {
            std::lock_guard playerLock(self_->stateMtx);
            // Keep other joining clients from publishing this entity in the
            // destination until the respawn and position packets have queued.
            self_->inPlay = false;
            self_->trackingSpawnPublished = false;
            self_->dimension = targetDimension;
            self_->x = spawnX + 0.5;
            self_->y = spawnY;
            self_->z = spawnZ + 0.5;
            self_->yaw = spawnAngle;
            self_->pitch = 0.f;
            self_->dead = false;
            self_->health = 20;
            self_->food = 20;
            self_->saturation = 5;
            self_->fallDist = 0;
            self_->fireTicks = 0;
            self_->freezeTicks = 0;
            self_->sleeping = false;
        }
        srv_.broadcastPlayerTrackingRemovalUnsequenced(
            *self_, sourceDimension);
    });
    // Chunk cache keys are local coordinates, not (dimension, coordinates).
    // A respawn can switch dimensions while retaining the same chunk coords;
    // invalidate the session view so its confirmation streams the destination
    // dimension instead of treating source-dimension chunks as already sent.
    sentChunks_.clear();
    lastCx_ = INT32_MAX;
    lastCz_ = INT32_MAX;
    chunksStreamed_ = false;
    hasSent_ = false;
    WriteBuffer ws = makeWorldState(srv_.config(), targetWorld, gamemode);
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
    if (!srv_.teleportPlayer(
            *self_, spawnX + 0.5, spawnY, spawnZ + 0.5,
            std::array<float, 2>{spawnAngle, 0.f}))
        return;
    publishPlayerTrackingAfterDimensionTransfer();
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
    b.varint(std::clamp(c.simulationDistance, 2, 32));
    b.boolean(false);                              // reduced debug
    b.boolean(true);                               // respawn screen
    b.boolean(false);                              // do limited crafting
    // SpawnInfo
    {
        WriteBuffer ws = makeWorldState(c, world, self_->gamemode);
        b.raw(ws.data.data(), ws.data.size());
    }
    b.boolean(secure_chat_policy::isEnforced(c.enforceSecureProfile,
                                             c.enforcesSecureChat)); // enforces secure chat
    conn_->sendPacket(pl::sc::Login, b);
    WriteBuffer difficulty;
    int difficultyId = 2;
    if (srv_.difficulty_ == "peaceful") difficultyId = 0;
    else if (srv_.difficulty_ == "easy") difficultyId = 1;
    else if (srv_.difficulty_ == "hard") difficultyId = 3;
    difficulty.u8(static_cast<std::uint8_t>(difficultyId));
    difficulty.boolean(false);                     // difficulty locked
    conn_->sendPacket(pl::sc::ChangeDifficulty, difficulty);
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
    srv_.serializePlayerTrackingPublication([this, about] {
        broadcastSpawnEntityUnderTrackingSequencer(about);
    });
}

void Session::broadcastSpawnEntityUnderTrackingSequencer(Player* about) {
    if (!about) return;
    const auto subject = snapshotPlayerSpawn(*about);
    if (!subject.inPlay || !subject.connection) return;

    const WriteBuffer spawn = makeSpawnEntity(subject);
    srv_.broadcastPlayerTrackingPacketInDimension(
        subject.dimension, about, pl::sc::SpawnEntity, spawn);
    const WriteBuffer skin = makeSkinMetadata(subject.entityId);
    srv_.broadcastPlayerTrackingPacketInDimension(
        subject.dimension, about, pl::sc::SetEntityMetadata, skin);
    if (!subject.connection->trySendPacket(pl::sc::SetEntityMetadata, skin)) {
        subject.connection->close();
        return;
    }

    // also tell the newcomer about everyone else
    for (const auto& other : srv_.playersSnapshot()) {
        if (!other || other.get() == about) continue;
        const auto existing = snapshotPlayerSpawn(*other);
        if (!existing.inPlay || !existing.connection ||
            !existing.trackingSpawnPublished ||
            existing.dimension != subject.dimension)
            continue;
        const WriteBuffer otherSpawn = makeSpawnEntity(existing);
        if (!subject.connection->trySendPacket(
                pl::sc::SpawnEntity, otherSpawn)) {
            subject.connection->close();
            return;
        }
        const WriteBuffer otherSkin = makeSkinMetadata(existing.entityId);
        if (!subject.connection->trySendPacket(
                pl::sc::SetEntityMetadata, otherSkin)) {
            subject.connection->close();
            return;
        }
    }
    {
        std::lock_guard playerLock(about->stateMtx);
        about->trackingSpawnPublished = true;
    }
}

void Session::synchronizeEquipmentForTrackedPlayers() {
    srv_.broadcastPlayerEquipment(*self_, true);
    const auto subject = snapshotPlayerSpawn(*self_);
    if (!subject.inPlay || !subject.trackingSpawnPublished) return;
    for (const auto& other : srv_.playersSnapshot()) {
        if (!other || other.get() == self_.get()) continue;
        const auto viewer = snapshotPlayerSpawn(*other);
        if (!viewer.inPlay || !viewer.trackingSpawnPublished ||
            viewer.dimension != subject.dimension)
            continue;
        srv_.sendPlayerEquipmentTo(*other, *self_);
    }
}

void Session::publishPlayerTrackingAfterDimensionTransfer() {
    srv_.serializePlayerTrackingPublication([&] {
        // The transfer/respawn path already enqueued the source removal while
        // hiding this player. Publish the destination only after its Respawn
        // and matching position correction have been enqueued.
        {
            std::lock_guard playerLock(self_->stateMtx);
            self_->inPlay = true;
        }
        broadcastSpawnEntityUnderTrackingSequencer(self_.get());
    });
    synchronizeEquipmentForTrackedPlayers();
    hasSent_ = false;
}
void Session::sendPlayerInfoAddSelf() {
    std::array<std::uint8_t, 16> uuid{};
    std::string name;
    std::uint8_t gamemode = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        uuid = self_->uuid;
        name = self_->name;
        gamemode = self_->gamemode;
    }
    WriteBuffer add;
    add.u8(0x01 | 0x04 | 0x08);                    // add_player | update_game_mode | update_listed
    add.varint(1);
    add.uuid(uuid.data());
    add.string(name);
    add.varint(0);                                 // properties
    add.varint(gamemode);
    add.varint(1);                                 // listed
    conn_->sendPacket(pl::sc::PlayerInfoUpdate, add);
}
void Session::broadcastPlayerInfoAdd(Player* about) {
    if (!about) return;
    std::array<std::uint8_t, 16> uuid{};
    std::string name;
    std::uint8_t gamemode = 0;
    {
        std::lock_guard playerLock(about->stateMtx);
        uuid = about->uuid;
        name = about->name;
        gamemode = about->gamemode;
    }
    WriteBuffer add;
    add.u8(0x01 | 0x04 | 0x08);
    add.varint(1);
    add.uuid(uuid.data());
    add.string(name);
    add.varint(0);
    add.varint(gamemode);
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
    if (slot >= 0 &&
        slot < static_cast<std::int32_t>(kPlayerInventorySlotCount)) {
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
} // namespace cppfm
