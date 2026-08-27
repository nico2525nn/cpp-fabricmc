// PacketBatcher: coalesces block updates into bundles / multi_block_change.
// ChatMessageProcessor: verifies chat signatures (stub).
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "../core/ByteBuffer.hpp"
#include "../proto/Ids.hpp"

namespace cppfm {

struct Player;
class GameServer;

class PacketBatcher {
public:
    struct Queued {
        uint8_t id;
        WriteBuffer body;
    };
    std::vector<Queued> queue;
    int64_t lastFlushMs = 0;

    void queuePacket(uint8_t id, WriteBuffer body) {
        queue.push_back({id, std::move(body)});
    }
    bool empty() const { return queue.empty(); }
    size_t size() const { return queue.size(); }
    void clear() { queue.clear(); }

    // Flushes queued packets. If multiple, wraps in BundleDelimiter (0x00) start/end
    // or coalesces to MultiBlockChange when all BlockUpdates share same chunk section.
    void flush(GameServer& srv, const Player* except);

private:
    bool tryFlushAsMultiBlockChange(GameServer& srv, const Player* except);
};

class ChatMessageProcessor {
public:
    // Verifies signature if present. Returns true if message should be accepted.
    // For now logs and accepts, but performs RSA verification when hasChatSession true.
    static bool verify(const Player& p, const std::string& msg, int64_t timestamp, int64_t salt, const std::vector<uint8_t>& signature);
    static bool shouldUsePlayerChat(const Player& p);
};

} // namespace cppfm
