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

namespace {
int effectiveViewDistance(const ServerConfig& config) {
    return std::clamp(config.viewDistance, constants::kViewDistanceMin,
                      constants::kViewDistanceMax);
}
} // namespace

namespace session_lifecycle_detail {
WriteBuffer makeWorldState(const ServerConfig& c, const World& world,
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
} // namespace session_lifecycle_detail

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
        // happens, while failures remain visible in the server log.
        registered_ = false;
        srv_.unregisterSession(this);
        // Stop using this player for simulation-distance decisions immediately.
        // Persistence and quit hooks may take longer than one tick, and keeping
        // a disconnected player active during that window expands random ticks
        // and entity work around a position nobody can observe anymore.
        {
            std::lock_guard playerLock(self_->stateMtx);
            self_->inPlay = false;
        }
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
            if (preservePlayerDataOnDisconnect_) return;
            srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_);
        });
        cleanupStep("roster removal", [this] { srv_.removePlayer(self_.get()); });
    }
}

Session::~Session() {
    if (playerAdmissionReserved_) srv_.releasePlayerAdmission();
}

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
    // Do not let an unauthenticated online-mode peer reserve a player slot
    // while it stalls on the encryption/session-server exchange.
    if (!srv_.reservePlayerAdmission()) {
        rejectLogin("Server is full", false);
        return;
    }
    playerAdmissionReserved_ = true;
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
    bool unsupportedItemData = false;
    bool unsafeExistingPlayerData = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        self_->gamemode = srv_.defaultGameMode();
    }
    (void)srv_.loadPlayerData(
        GameServer::uuidToHex(self_->uuid), *self_,
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
    srv_.addPlayer(self_, true);
    srv_.registerSession(this);
    playerAdmissionReserved_ = false;
    self_->inPlay = true;
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

    sendInitialInventory();
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
        WriteBuffer ws = session_lifecycle_detail::makeWorldState(
            c, world, self_->gamemode);
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

} // namespace cppfm
