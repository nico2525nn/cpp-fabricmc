// NetworkManager: extracted packet batching / broadcast / keepalive (plan7)
#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include "../core/ByteBuffer.hpp"
#include "../net/PacketBatcher.hpp"

namespace cppfm {
struct Player;
class GameServer;

class NetworkManager {
public:
    explicit NetworkManager(PacketBatcher& batcher) : batcher_(batcher) {}

    void queueBlockChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
        WriteBuffer b;
        b.position(x,y,z);
        b.varint(state);
        batcher_.queuePacket(0x09 /*BlockUpdate*/, std::move(b));
    }
    bool shouldFlush() const { return batcher_.size() >= 64; }
    void flush(GameServer& srv, const Player* except) { (void)srv; (void)except; }
    // broadcast helpers (delegated to GameServer in actual server)
    static void broadcastPacketExcept(const std::vector<std::shared_ptr<Player>>& players,
                                      const Player* except, std::uint8_t id, const WriteBuffer& body) { (void)players; (void)except; (void)id; (void)body; }

    PacketBatcher& batcher() { return batcher_; }
    const PacketBatcher& batcher() const { return batcher_; }

private:
    PacketBatcher& batcher_;
};
} // namespace cppfm
