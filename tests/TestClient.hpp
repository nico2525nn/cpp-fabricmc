// Native C++ test client used by the self-test suite.
// Talks the real protocol over the SAME Connection/framing code the server uses,
// so every byte passes through production paths (incl. zlib compression).
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../src/net/Connection.hpp"
#include "../src/proto/Ids.hpp"

namespace cpptest {

using namespace cppfm;

struct Packet {
    std::uint8_t id;
    std::vector<std::uint8_t> body;
    double t = 0;                       // seconds since client start
};

class TestClient {
public:
    bool connect(const std::string& host, std::uint16_t port, int timeoutSec = 15);
    void close() noexcept;
    ~TestClient() noexcept { try { close(); } catch (...) {} }

    // ---- flow -------------------------------------------------------------
    // handshake -> status request -> parse json-ish (returns raw body) -> ping
    std::string queryStatusJson(std::uint16_t portField = 25565);

    // handshake -> login hello(name) -> handle Set Compression -> success ->
    // login acknowledged -> configuration exchange (brand/knownpacks/registries/
    // tags/finish) -> play entry. Returns false on any deviation.
    bool join(const std::string& name);
    bool joinOnline(const std::string& name);   // full encryption handshake

    // ---- play actions -----------------------------------------------------
    void confirmTeleport(std::int32_t teleportId);
    void sendPlayerLoaded();
    void sendPosition(double x, double y, double z, bool onGround = true);
    void sendChatMessage(const std::string& message);
    void sendChatCommand(const std::string& command);
    void sendDig(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t seq);
    void sendUseItemOn(std::int32_t x, std::int32_t y, std::int32_t z, int face=1, std::int32_t seq=1);
    void sendUseEntity(std::int32_t entityId, int action, bool sneaking=false); // plan41 C-10 horse window
    // plan43 B1+B2: spec-exact send helpers (protocol.json 1.21.4 hand-built)
    void sendSignedCommand(const std::string& command, int nSignatures, std::uint8_t sigByte=0xAB); // cs 0x06
    void sendTabComplete(std::int32_t transactionId, const std::string& text);                       // cs 0x0D (2 fields, no bool)
    void sendMovePlayerFlags(double x, double y, double z, std::uint8_t flags);                      // cs 0x1C raw flags
    void sendMovePlayerPosRotFlags(double x, double y, double z, float yaw, float pitch, std::uint8_t flags); // cs 0x1D
    void sendMovePlayerRotFlags(float yaw, float pitch, std::uint8_t flags);                                 // cs 0x1E
    void sendFlyingFlags(std::uint8_t flags);                                                       // cs 0x1F raw flags
    void sendUseEntityFull(std::int32_t target, int mouse, int hand, bool sneaking);                 // cs 0x18 spec layout
    void sendAbilitiesFlags(std::int8_t flags);                                                     // cs 0x26
    void sendSignUpdate(std::int32_t x, std::int32_t y, std::int32_t z, bool front,
                        const std::string lines[4]);                                                // cs 0x39
    void sendRawPlay(std::uint8_t pid, const WriteBuffer& body);                                    // escape hatch
    bool joinWithFinishContamination(const std::string& name); // plan43 W-12: settings/pong/pack/known-packs before finish-ack
    bool alive() const { return running_.load(); }
    struct Suggestion { std::string match; };
    struct SuggestionsResp { std::int32_t transactionId=-1, start=0, length=0; std::vector<Suggestion> matches; };
    bool waitSuggestions(std::int32_t transactionId, SuggestionsResp& out, int timeoutMs=5000);
    struct Spawned { std::int32_t eid=0; std::int32_t type=0; std::string kind; double x=0,y=0,z=0; };
    std::vector<Spawned> spawns() const;
    void sendMoveVehicle(double x, double y, double z, float yaw, float pitch); // plan41 C-10 VehicleMove
    void sendRespawnRequest();
    void respondKeepAlive(std::int64_t id);
    void sendPlayerLoadedOnce();
    void handleIncoming(std::uint8_t pid, std::vector<std::uint8_t> body);

    // ---- receiving ---------------------------------------------------------
    // Pumps socket for up to `ms`, filing packets into typed buckets.
    void pump(int ms);
    // Blocks until a packet matching `pred` arrives (pumping), or timeoutMs.
    bool waitFor(std::function<bool(const Packet&)> pred, int timeoutMs, Packet* out = nullptr);

    size_t count(std::uint8_t id) const;
    // debug accessors
    auto& mtx_public(){ return mtx_; }
    auto& recentPublic(){ return recent_; }

    // observed state
    std::vector<std::uint8_t> joinGameBody;
    std::vector<std::pair<std::int32_t,std::int32_t>> chunkCoords;
    std::vector<std::vector<std::uint8_t>> rawChunks;      // bodies after header strip
    std::vector<std::string> chatLines;
    struct BlockUpd { std::int32_t x,y,z; std::uint32_t state; };
    std::vector<BlockUpd> blockUpdates;
    int acks = 0, spawnsReceived = 0, entityMoves = 0, timeUpdates = 0, declares = 0;
    bool gotRespawn = false;
    bool hasChunk00 = false;

    std::uint16_t localPort() const { return localPort_; }
    double x = 8.5, y = -60.0, z = 8.5;
    std::string lastError;

private:
    void readerLoop();
    void filePacket(Packet p);
    static bool extractChatText(const std::vector<std::uint8_t>& nbt, std::string& out);

    std::unique_ptr<Connection> conn_;
    std::thread reader_;
    std::atomic<bool> running_{false};

    mutable std::mutex mtx_;
    bool playerLoadedSent_ = false;
    std::uint16_t localPort_ = 0;
    int myFirstPackets = 0;
    std::deque<Packet> recent_;                 // ring of unmatched packets
    std::condition_variable cv_;
};

} // namespace cpptest
