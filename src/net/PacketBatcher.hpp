// PacketBatcher: coalesces block updates into BundleDelimiter / MultiBlockChange.
// - BundleDelimiter 0x00 wraps heterogeneous packets (strict 1.21.4, PLAN10 §3).
// - MultiBlockChange 0x4E coalesces same-section BlockUpdate 0x09 with axis pack
//   ly<<8|lz<<4|lx (fixed from lx<<8 axis swap) + last-write-wins dedup.
// ChatMessageProcessor: RSA-SHA256 verifies PlayerChat 0x07 signatures when a
// ChatSession is present; falls back to SystemChat 0x73 otherwise.
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
    [[nodiscard]] bool empty() const noexcept { return queue.empty(); }
    [[nodiscard]] size_t size() const noexcept { return queue.size(); }
    void clear() noexcept { queue.clear(); }

    // Flushes queued packets. If multiple, wraps in BundleDelimiter (0x00) start/end
    // or coalesces to MultiBlockChange when all BlockUpdates share same chunk section.
    void flush(GameServer& srv, const Player* except);

private:
    bool tryFlushAsMultiBlockChange(GameServer& srv, const Player* except);
};

class ChatMessageProcessor {
public:
    // Verifies RSA-SHA256 signature when hasChatSession==true.
    // Returns true if message should be accepted as PlayerChat, false to
    // downgrade to SystemChat / reject. Expired sessions and missing keys
    // return false (caller should send SystemChat).
    static bool verify(const Player& p, const std::string& msg, int64_t timestamp, int64_t salt, const std::vector<uint8_t>& signature);
    [[nodiscard]] static bool shouldUsePlayerChat(const Player& p);
};

} // namespace cppfm
