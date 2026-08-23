#include "GameServer.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include <cerrno>

namespace cppfm {

using namespace proto;

static std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// hotbar: block name -> (itemId, stateId) resolved at startup
struct HotbarEntry { std::uint32_t itemId; std::uint16_t stateId; };
static const char* kHotbarNames[] = {
    "minecraft:grass_block", "minecraft:dirt", "minecraft:stone",
    "minecraft:cobblestone", "minecraft:oak_planks", "minecraft:glass",
    "minecraft:sand", "minecraft:oak_log", "minecraft:glowstone",
};
static std::vector<HotbarEntry> resolveHotbar() {
    std::vector<HotbarEntry> v;
    const auto& items = gen::itemIdByName();
    const auto& blocks = gen::blockNameToState();
    for (auto* n : kHotbarNames) {
        auto ii = items.find(n);
        auto bi = blocks.find(n);
        if (ii == items.end() || bi == blocks.end()) continue;
        v.push_back({ii->second, static_cast<std::uint16_t>(bi->second)});
    }
    return v;
}
static std::vector<HotbarEntry> g_hotbar = resolveHotbar();

// ================================================================== GameServer

void GameServer::runForever() {
    std::thread janitor([this] {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const auto now = nowMs();
            static std::int64_t lastTime = 0;
            if (now - lastTime >= 1000) {
                lastTime = now;
                WriteBuffer t;
                t.i64(0);                    // age
                t.i64(now / 50 % 24000);     // day time from wall clock
                t.boolean(true);             // tick daylight
                broadcastPacketExcept(nullptr, pl::sc::SetTime, t);
            }
            for (auto& p : playersSnapshot()) {
                if (!p->spawned) continue;
                if (now - p->lastSeenMs > 60000) {           // hard idle sweep
                    try { p->conn->close(); } catch (...) {}
                    continue;
                }
                if (p->pendingKeepAlive != 0 && now - p->lastSeenMs > 30000) {
                    WriteBuffer reason;
                    nbt::writeTextComponent(reason, "Timed out");
                    try { p->conn->sendPacket(pl::sc::Disconnect, reason); } catch (...) {}
                    try { p->conn->close(); } catch (...) {}
                    continue;
                }
                if (now - p->lastKeepAliveSentMs >= 10000) {
                    const std::int64_t id = ++p->keepAliveCounter;
                    p->pendingKeepAlive = id;
                    p->lastKeepAliveSentMs = now;
                    WriteBuffer b;
                    b.i64(id);
                    try { p->conn->sendPacket(pl::sc::KeepAlive, b); } catch (...) {}
                }
            }
        }
    });
    janitor.detach();

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) throw std::runtime_error("socket() failed");
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg_.port);
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));
    if (::listen(listenFd_, 64) != 0)
        throw std::runtime_error("listen() failed");
    running_ = true;

    acceptLoop();
}

void GameServer::acceptLoop() {
    while (running_) {
        sockaddr_in cli{}; socklen_t cl = sizeof(cli);
        int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&cli), &cl);
        if (fd < 0) { if (running_) continue; break; }
        std::thread([this, fd] {
            auto conn = std::make_unique<Connection>(fd);
            conn->setNoDelay();
            conn->setSendTimeout(15);
            Session s(*this, std::move(conn));
            s.run();
        }).detach();
    }
}

// ===================================================================== Session

void Session::run() {
    try {
        while (state_ != State::Done && srv_.running()) {
            switch (state_) {
            case State::Handshake: {
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
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] session %s error: %s\n",
                     conn_->peer().c_str(), e.what());
    }
    if (registered_) {
        srv_.broadcastSystemText("\u00a7e" + self_->name + " left the game", nullptr);
        WriteBuffer rm;
        rm.varint(1);
        rm.uuid(self_->uuid.data());
        srv_.broadcastPacketExcept(nullptr, pl::sc::PlayerInfoRemove, rm);
        WriteBuffer ent;
        ent.varint(1);
        ent.varint(self_->entityId);
        srv_.broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, ent);
        srv_.removePlayer(self_.get());
        registered_ = false;
    }
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
            const std::string json =
                "{\"version\":{\"name\":\"" + std::string(kMinecraftVersion) +
                "\",\"protocol\":" + std::to_string(kProtocolVersion) +
                "},\"players\":{\"max\":" + std::to_string(srv_.config().maxPlayers) +
                ",\"online\":" + std::to_string(srv_.playerCount() + 0) +
                ",\"sample\":[]},\"description\":{\"text\":\"" + srv_.config().motd +
                "\"},\"enforcesSecureChat\":false}";
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

void Session::handleLogin() {
    auto frame = conn_->readFrame();
    ReadBuffer in(frame);
    if (in.u8() != lo::cs::Hello) throw std::runtime_error("expected login hello");

    self_->name = in.string(16);
    auto uuidBytes = in.bytes(16);
    std::copy(uuidBytes.begin(), uuidBytes.end(), self_->uuid.begin());
    self_->entityId = 0; // set on play entry

    if (srv_.config().compressionThreshold >= 0) {
        WriteBuffer sc;
        sc.varint(srv_.config().compressionThreshold);
        conn_->sendPacket(lo::sc::SetCompression, sc);
        conn_->setCompression(srv_.config().compressionThreshold);
    }

    // login success: uuid, name, empty property list (verified against capture)
    WriteBuffer ok;
    ok.uuid(self_->uuid.data());
    ok.string(self_->name);
    ok.varint(0);                                   // properties count
    conn_->sendPacket(lo::sc::GameProfile, ok);

    // wait for LoginAcknowledged (tolerate compression request even though we never send it)
    for (;;) {
        auto f2 = conn_->readFrame();
        ReadBuffer in2(f2);
        switch (in2.u8()) {
        case lo::cs::LoginAcknowledged:
            state_ = State::Configuration;
            return;
        default:
            throw std::runtime_error("unexpected packet during login ack wait");
        }
    }
}

void Session::handleConfiguration() {
    // 1. brand
    {
        WriteBuffer b;
        b.string("minecraft:brand");               // channel
        WriteBuffer payload;
        payload.string("CppFabricMC");
        b.raw(payload.data.data(), payload.data.size());
        conn_->sendPacket(cf::sc::CustomPayload, b);
    }
    // 2. known packs: we advertise none -> client expects full registry data
    {
        WriteBuffer b;
        b.varint(0);
        conn_->sendPacket(cf::sc::SelectKnownPacks, b);
    }
    // 3. wait for the client's SelectKnownPacks answer (server hangs otherwise!)
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        switch (in.u8()) {
        case cf::cs::SelectKnownPacks: {
            const std::int32_t n = in.varint();
            for (std::int32_t i = 0; i < n; ++i) {
                (void)in.string();                  // namespace
                (void)in.string();                  // id
                (void)in.string();                  // version
            }
            goto packsDone;
        }
        case cf::cs::KeepAlive: {                   // echo
            WriteBuffer e; e.raw(in.p + in.off, in.remaining());
            conn_->sendPacket(cf::sc::KeepAlive, e);
            break;
        }
        case cf::cs::ClientInformation: {           // settings: parse & ignore
            (void)in.string();                      // locale
            (void)in.i8();                          // view distance
            (void)in.varint();                      // chat mode
            (void)in.boolean();                     // chat colors
            (void)in.u8();                          // skin parts
            (void)in.varint();                      // main hand
            (void)in.boolean();                     // text filtering
            (void)in.boolean();                     // allow server listings
            break;
        }
        case cf::cs::CustomPayload:                 // channel+rest: ignore
            (void)in.string();
            in.skipRest();
            break;
        case cf::cs::ResourcePackResponse:
            (void)in.u8(); (void)in.varint();
            break;
        default:
            throw std::runtime_error("unexpected packet while awaiting known-packs reply");
        }
    }
packsDone:
    // 4. registry blobs, verbatim wire order
    for (const auto& r : srv_.data().registries()) {
        WriteBuffer pkt;
        pkt.u8(cf::sc::RegistryData);
        pkt.raw(r.body.data(), r.body.size());
        conn_->sendRawBody(pkt.data);
    }
    // 5. tags (captured verbatim)
    {
        WriteBuffer pkt;
        pkt.u8(cf::sc::UpdateTags);
        pkt.raw(srv_.data().tags().data(), srv_.data().tags().size());
        conn_->sendRawBody(pkt.data);
    }
    // 6. finish & await acknowledgement
    conn_->sendPacket(cf::sc::FinishConfiguration, {});
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        switch (in.u8()) {
        case cf::cs::FinishAcknowledgement:
            state_ = State::Play;
            onEnterPlay();
            return;
        case cf::cs::KeepAlive: {
            WriteBuffer e; e.raw(in.p + in.off, in.remaining());
            conn_->sendPacket(cf::sc::KeepAlive, e);
            break;
        }
        case cf::cs::CustomPayload:
            (void)in.string(); in.skipRest();
            break;
        default:
            throw std::runtime_error("unexpected packet during finish-ack wait");
        }
    }
}

// ------------------------------------------------------------------ play join

void Session::onEnterPlay() {
    self_->conn = conn_.get();
    self_->entityId = srv_.nextEntityId();
    self_->lastSeenMs = nowMs();

    sendJoinGame();
    sendAbilities();

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
        WriteBuffer b;
        b.position(0, -60, 0);
        b.f32(0.f);
        conn_->sendPacket(pl::sc::SetDefaultSpawn, b);
    }
    sendTeleport(0.5, -60.0, 0.5, 0.f, 0.f);

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

    broadcastSpawnEntity(self_.get());
    sendDeclareCommands();

    sendStarterInventory();
    {   // health (creative ignores but harmless)
        WriteBuffer b;
        b.f32(20.f); b.varint(20); b.f32(5.f);
        conn_->sendPacket(pl::sc::SetHealth, b);
    }

    registered_ = true;
    srv_.addPlayer(self_);

    srv_.broadcastSystemText("\u00a7e" + self_->name + " joined the game", nullptr);
    sendSystemText("\u00a77Welcome to \u00a7bCppFabricMC\u00a77! Build with the hotbar, chat freely.");
}

static WriteBuffer makeWorldState(const ServerConfig& c) {
    WriteBuffer w;
    w.varint(0);                                   // dimension type index
    w.string("minecraft:overworld");
    w.i64(c.hashedSeed);
    w.i8(1);                                       // gamemode creative
    w.u8(255);                                     // previous gamemode: none
    w.boolean(false);                              // is debug
    w.boolean(true);                               // is flat
    w.boolean(false);                              // has death location
    w.varint(0);                                   // portal cooldown
    w.varint(kSeaLevelFlat);
    return w;
}

// Minimal command tree: root -> /help, /ping  (literals only)
static void writeDeclareCommands(WriteBuffer& b) {
    const char* literals[] = {"help", "ping"};
    b.varint(3);                       // node count
    // node0: root, children = {1,2}
    b.u8(0x00);
    b.varint(2); b.varint(1); b.varint(2);
    for (const char* name : literals) {
        b.u8(0x01 | 0x04);             // literal | executable
        b.varint(0);                   // no children
        b.string(name);
    }
    b.varint(0);                       // root index
}

void Session::sendDeclareCommands() {
    WriteBuffer b;
    writeDeclareCommands(b);
    conn_->sendPacket(pl::sc::DeclareCommands, b);
}

void Session::handleRespawnRequest() {
    WriteBuffer ws = makeWorldState(srv_.config());
    WriteBuffer b;
    b.raw(ws.data.data(), ws.data.size());
    b.u8(0x03);                                    // keep metadata + attributes
    conn_->sendPacket(pl::sc::Respawn, b);
    {   // re-sync position & vitals
        WriteBuffer hp;
        hp.f32(20.f); hp.varint(20); hp.f32(5.f);
        conn_->sendPacket(pl::sc::SetHealth, hp);
    }
    sendTeleport(self_->x, -60.0, self_->z, self_->yaw, self_->pitch);
}

void Session::sendJoinGame() {
    const ServerConfig& c = srv_.config();
    WriteBuffer b;
    b.i32(self_->entityId);
    b.boolean(false);                              // hardcore
    b.varint(1);                                   // worlds[]
    b.string("minecraft:overworld");
    b.varint(c.maxPlayers);
    b.varint(c.viewDistance);
    b.varint(std::min(c.simulationDistance, 10));
    b.boolean(false);                              // reduced debug
    b.boolean(true);                               // respawn screen
    b.boolean(false);                              // do limited crafting
    // SpawnInfo
    {
        WriteBuffer ws = makeWorldState(c);
        b.raw(ws.data.data(), ws.data.size());
    }
    b.boolean(false);                              // enforces secure chat
    conn_->sendPacket(pl::sc::Login, b);
}

void Session::sendAbilities() {
    WriteBuffer b;
    b.i8(0x01 | 0x04 | 0x08);                      // invulnerable, allow flying, instant build
    b.f32(0.05f);
    b.f32(0.10f);
    conn_->sendPacket(pl::sc::Abilities, b);
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

static WriteBuffer makeSpawnEntity(const Player& p) {
    WriteBuffer b;
    b.varint(p.entityId);
    b.uuid(p.uuid.data());
    b.varint(static_cast<std::int32_t>(gen::kPlayerEntityTypeId));
    b.f64(p.x); b.f64(p.y); b.f64(p.z);
    const auto toAngle = [](float deg) { return static_cast<std::uint8_t>(deg * 256.f / 360.f); };
    b.i8(static_cast<std::int8_t>(toAngle(p.pitch)));
    b.i8(static_cast<std::int8_t>(toAngle(p.yaw)));
    b.i8(static_cast<std::int8_t>(toAngle(p.yaw)));   // head pitch
    b.varint(0);                                      // object data
    b.i16(0); b.i16(0); b.i16(0);                     // velocity
    return b;
}

void Session::broadcastSpawnEntity(Player* about) {
    WriteBuffer b = makeSpawnEntity(*about);
    srv_.broadcastPacketExcept(about, pl::sc::SpawnEntity, b);
    // also tell the newcomer about everyone else
    for (auto& other : srv_.playersSnapshot()) {
        if (other.get() == about || !other->spawned) continue;
        WriteBuffer ob = makeSpawnEntity(*other);
        try { about->conn->sendPacket(pl::sc::SpawnEntity, ob); } catch (...) {}
    }
}

void Session::sendPlayerInfoAddSelf() {
    WriteBuffer add;
    add.u8(0x01 | 0x04 | 0x08);                    // add_player | update_game_mode | update_listed
    add.varint(1);
    add.uuid(self_->uuid.data());
    add.string(self_->name);
    add.varint(0);                                 // properties
    add.varint(1);                                 // gamemode creative
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
    add.varint(1);
    add.varint(1);
    srv_.broadcastPacketExcept(about, pl::sc::PlayerInfoUpdate, add);
}

void Session::sendStarterInventory() {
    WriteBuffer b;
    b.u8(0);                                       // window id: player inventory
    b.varint(1);                                   // state id
    b.varint(46);                                  // slots
    auto emptySlot = [&]{ b.varint(0); };
    auto itemSlot = [&](std::uint32_t itemId) {
        b.varint(1);                               // count present => item
        b.varint(static_cast<std::int32_t>(itemId));
        b.varint(0);                               // added components
        b.varint(0);                               // removed components
    };
    for (int i = 0; i < 46; ++i) emptySlot();
    for (int i = 0; i < 9 && i < static_cast<int>(g_hotbar.size()); ++i)
        itemSlot(g_hotbar[static_cast<std::size_t>(i)].itemId);
    emptySlot();                                   // carried item
    conn_->sendPacket(pl::sc::ContainerSetContent, b);
}

void Session::sendSystemText(const std::string& text) {
    WriteBuffer body;
    nbt::writeTextComponent(body, text);
    body.boolean(false);
    conn_->sendPacket(pl::sc::SystemChat, body);
}

// ------------------------------------------------------------------ chunking

void Session::sendChunk(std::int32_t cx, std::int32_t cz) {
    static const std::uint32_t biomeIdx = srv_.data().biomeIndex(srv_.config().worldBiome);
    GameServer::ChunkBodyRef body;
    if (!srv_.getCachedChunk(cx, cz, biomeIdx, body)) {
        auto fresh = std::make_shared<const std::vector<std::uint8_t>>([&]{
            WriteBuffer wb;
            srv_.world().generateChunkIfMissing(cx, cz);
            srv_.world().withChunk(cx, cz, [&](const Chunk& c) {
                serializeLevelChunkBody(wb, cx, cz, c, biomeIdx);
            });
            return wb.data;
        }());
        srv_.storeChunk(cx, cz, 0, fresh);
        body = fresh;
    }
    conn_->sendPacketBuf(pl::sc::LevelChunkWithLight, *body);
    sentChunks_.insert(chunkKey(cx, cz));
}

void Session::streamInitialChunks() {
    std::fprintf(stderr, "[cppfm] %s: streaming initial chunks\n", self_->name.c_str());
    chunksStreamed_ = true;
    tickChunksAround(self_->x, self_->z);
}

void Session::tickChunksAround(double px, double pz) {
    const int vd = std::min(srv_.config().viewDistance, 12);
    const std::int32_t pcx = static_cast<std::int32_t>(std::floor(px)) >> 4;
    const std::int32_t pcz = static_cast<std::int32_t>(std::floor(pz)) >> 4;

    if (pcx != lastCx_ || pcz != lastCz_) {
        WriteBuffer center;
        center.varint(pcx);
        center.varint(pcz);
        try { conn_->sendPacket(pl::sc::SetCenterChunk, center); } catch (...) {}
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

    // forget distant chunks
    std::vector<std::int64_t> forget;
    for (auto k : sentChunks_) {
        const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
        const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
        if (std::abs(cx - pcx) > vd + 1 || std::abs(cz - pcz) > vd + 1)
            forget.push_back(k);
    }
    if (!forget.empty()) {
        for (auto k : forget) {
            WriteBuffer f;
            f.i32(static_cast<std::int32_t>(k & 0xFFFFFFFFLL));   // z first per schema!
            f.i32(static_cast<std::int32_t>(k >> 32));
            try { conn_->sendPacket(pl::sc::ForgetLevelChunk, f); } catch (...) {}
            sentChunks_.erase(k);
        }
    }
}

// ------------------------------------------------------------------ play loop

void Session::ack(std::int32_t sequence) {
    WriteBuffer b;
    b.varint(sequence);
    conn_->sendPacket(pl::sc::AckBlockChange, b);
}

void Session::handlePlay() {
    for (;;) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        self_->lastSeenMs = nowMs();
        switch (in.u8()) {
        case pl::cs::AcceptTeleportation: {
            in.varint();
            if (!chunksStreamed_) streamInitialChunks();
            break;
        }
        case pl::cs::MovePlayerPos:       onMovement(in, true, false); break;
        case pl::cs::MovePlayerPosRot:    onMovement(in, true, true);  break;
        case pl::cs::MovePlayerRot:       onMovement(in, false, true); break;
        case pl::cs::MovePlayerStatusOnly:onMovement(in, false, false);break;
        case pl::cs::KeepAlive: {
            const std::int64_t id = in.i64();
            if (self_->pendingKeepAlive == 0 || id == self_->pendingKeepAlive) {
                self_->pendingKeepAlive = 0;
                WriteBuffer b; b.i64(id);
                conn_->sendPacket(pl::sc::KeepAlive, b);
            }
            break;
        }
        case pl::cs::ChatMessage:         onChatMessage(in); break;
        case pl::cs::ChatCommand:         onChatCommand(in); break;
        case pl::cs::PlayerAction:        onPlayerAction(in); break;
        case pl::cs::UseItemOn:           onUseItemOn(in); break;
        case pl::cs::UseItem:             onUseItem(in); break;
        case pl::cs::HeldItemSlot:        onHeldSlot(in); break;
        case pl::cs::ChunkBatchReceived:  in.f32(); break;
        case pl::cs::PingRequest: {
            const std::int64_t id = in.i64();
            WriteBuffer b; b.i64(id);
            conn_->sendPacket(0x38 /*ping response*/, b);
            break;
        }
        case pl::cs::ClientTickEnd: break;
        case pl::cs::PlayerLoaded:                    // 0x2a
            if (!chunksStreamed_) streamInitialChunks();
            break;
        case pl::cs::Swing: break;
        case pl::cs::SetCreativeModeSlot: {
            // parse defensively: plain items only; bail out on components
            (void)in.i16();
            if (in.varint() > 0) {
                (void)in.varint();                  // item id
                const std::int32_t add = in.varint();
                const std::int32_t rem = in.varint();
                if (add != 0 || rem != 0)
                    throw std::runtime_error("creative slot with unsupported components");
            }
            break;
        }
        case pl::cs::ChangeDifficulty: (void)in.u8(); break;
        case pl::cs::ClientCommand: {
            const std::int32_t action = in.varint();
            if (action == 0) handleRespawnRequest();
            break;
        }
        case pl::cs::PlayerInput: in.skipRest(); break;
        case pl::cs::MoveVehicle: in.skipRest(); break;
        case pl::cs::SignUpdate: in.skipRest(); break;
        default:
            // Unknown packets: skip payload to stay aligned, but log loudly.
            std::fprintf(stderr, "[cppfm] unknown play packet from %s\n",
                         conn_->peer().c_str());
            in.skipRest();
            break;
        }
    }
}

void Session::onMovement(ReadBuffer& in, bool hasPos, bool hasRot) {
    if (hasPos) {
        self_->x = in.f64();
        self_->y = in.f64();
        self_->z = in.f64();
    }
    if (hasRot) {
        self_->yaw = in.f32();
        self_->pitch = in.f32();
    }
    self_->onGround = in.boolean();
    if (hasPos) {
        if (self_->y < -2048.0 || self_->y > 2048.0)
            throw std::runtime_error("player moved out of world bounds");
        self_->spawned = true;
        if (!chunksStreamed_) streamInitialChunks();
        else tickChunksAround(self_->x, self_->z);
    }
    broadcastMovement();
}

void Session::broadcastMovement() {
    if (!self_->spawned) return;
    const bool first = !hasSent_;
    const double dx = first ? 0 : self_->x - sentX_;
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
                srv_.broadcastPacketExcept(nullptr, pl::sc::MoveEntityPosRot, b);
            } else {
                WriteBuffer b;
                b.varint(self_->entityId);
                b.i16(static_cast<std::int16_t>(dx * 4096));
                b.i16(static_cast<std::int16_t>(dy * 4096));
                b.i16(static_cast<std::int16_t>(dz * 4096));
                b.boolean(self_->onGround);
                srv_.broadcastPacketExcept(nullptr, pl::sc::MoveEntityPos, b);
            }
            WriteBuffer h;
            h.varint(self_->entityId);
            h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
            srv_.broadcastPacketExcept(nullptr, pl::sc::RotateHead, h);
        } else {                                    // teleport-class delta
            WriteBuffer b;
            b.varint(self_->entityId);
            b.f64(self_->x); b.f64(self_->y); b.f64(self_->z);
            b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
            b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
            b.boolean(self_->onGround);
            srv_.broadcastPacketExcept(nullptr, pl::sc::EntityTeleport, b);
        }
    } else if (rotated) {                           // pure rotation
        WriteBuffer b;
        b.varint(self_->entityId);
        b.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        b.i8(static_cast<std::int8_t>(self_->pitch * 256.f / 360.f));
        b.boolean(self_->onGround);
        srv_.broadcastPacketExcept(nullptr, 0x33 /*entity_look*/, b);
        WriteBuffer h;
        h.varint(self_->entityId);
        h.i8(static_cast<std::int8_t>(self_->yaw * 256.f / 360.f));
        srv_.broadcastPacketExcept(nullptr, pl::sc::RotateHead, h);
    }

    sentX_ = self_->x; sentY_ = self_->y; sentZ_ = self_->z;
    sentYaw_ = self_->yaw; sentPitch_ = self_->pitch;
    hasSent_ = true;
}

void Session::onChatMessage(ReadBuffer& in) {
    const std::string msg = in.string(256);
    (void)in.i64();                                  // timestamp
    (void)in.i64();                                  // salt
    if (in.boolean()) in.bytes(256);                 // signature
    (void)in.varint();                               // offset
    in.bytes(3);                                     // acknowledged

    if (!msg.empty() && msg[0] == '/') return dispatchCommand(msg.substr(1));
    const std::string line = "<" + self_->name + "> " + msg;
    srv_.broadcastSystemText(line, nullptr);
    sendSystemText(line);
}

void Session::onChatCommand(ReadBuffer& in) {
    const std::string cmd = in.string(256);
    dispatchCommand(cmd);
}

void Session::dispatchCommand(const std::string& line) {
    const std::string head = line.substr(0, line.find(' '));
    if (head == "ping") sendSystemText("\u00a7aPong!");
    else if (head == "help") sendSystemText("\u00a77Commands: /ping /help");
    else sendSystemText("\u00a7cUnknown command: /" + line);
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

    const bool digLike = (status == 0 || status == 2);   // started/finished
    if (digLike) {
        srv_.world().setBlock(x, y, z, 0);
        srv_.broadcastBlockChange(x, y, z, 0);
    }
    ack(sequence);                                      // ALWAYS ack sequences
}

void Session::onUseItemOn(ReadBuffer& in) {
    (void)in.varint();                                  // hand
    std::int32_t x, y, z;
    in.position(x, y, z);
    const std::int32_t dir = in.varint();
    (void)in.f32(); (void)in.f32(); (void)in.f32();     // cursor
    (void)in.boolean();                                 // inside block
    (void)in.boolean();                                 // world border hit
    const std::int32_t sequence = in.varint();

    static constexpr int DX[] = {0, 0, 0, 0, -1, 1};
    static constexpr int DY[] = {1, -1, 0, 0, 0, 0};    // face: -Y? order below
    static constexpr int DZ[] = {0, 0, 1, -1, 0, 0};
    // vanilla face ids: 0 bottom(-Y), 1 top(+Y), 2 north(-Z), 3 south(+Z), 4 west(-X), 5 east(+X)
    static constexpr int FX[] = {0, 0, 0, 0, -1, 1};
    static constexpr int FY[] = {-1, 1, 0, 0, 0, 0};
    static constexpr int FZ[] = {0, 0, -1, 1, 0, 0};
    (void)DX; (void)DY; (void)DZ;
    const int d = (dir >= 0 && dir < 6) ? dir : 0;
    const std::int32_t tx = x + FX[d], ty = y + FY[d], tz = z + FZ[d];

    if (srv_.world().getBlock(tx, ty, tz) == 0 &&
        self_->heldSlot < static_cast<std::int32_t>(g_hotbar.size())) {
        const auto& entry = g_hotbar[static_cast<std::size_t>(self_->heldSlot)];
        srv_.world().setBlock(tx, ty, tz, entry.stateId);
        srv_.broadcastBlockChange(tx, ty, tz, entry.stateId);
    }
    ack(sequence);
}

void Session::onUseItem(ReadBuffer& in) {
    (void)in.varint();                                  // hand
    const std::int32_t sequence = in.varint();
    (void)in.f32(); (void)in.f32();                     // rotation
    ack(sequence);
}

} // namespace cppfm
