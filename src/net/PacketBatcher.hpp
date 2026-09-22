// PacketBatcher: coalesces block updates into BundleDelimiter / MultiBlockChange.
// - MultiBlockChange 0x4E coalesces same-section BlockUpdate 0x09, axis pack
//   queue (queueBlockChange), tick flushes; mtx_ guards queue, lastFlushMs atomic.
// - 64-count flush threshold (50ms), per-section grouping, last-write-wins.
// ChatMessageProcessor: RSA-SHA256 verifies PlayerChat 0x07 with a ChatSession,
// else SystemChat 0x73 (N6 HIGH).
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include <string>
#include "../core/ByteBuffer.hpp"
#include "../proto/Ids.hpp"

namespace cppfm {

struct Player;
class GameServer;

namespace packet_batch_detail {
struct PositionKey {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    bool operator<(const PositionKey& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
};

struct SectionKey {
    std::int32_t cx = 0;
    std::int32_t cz = 0;
    std::int32_t sy = 0;
    bool operator<(const SectionKey& other) const {
        if (cx != other.cx) return cx < other.cx;
        if (cz != other.cz) return cz < other.cz;
        return sy < other.sy;
    }
};
}

class PacketBatcher {
public:
    struct Queued {
        uint8_t id;
        WriteBuffer body;
        std::int8_t dimension = 0;
    };
    std::vector<Queued> queue;                // guarded by mtx_
    std::mutex mtx_;
    std::atomic<int64_t> lastFlushMs{0};

    void queuePacketFor(std::int8_t dimension, uint8_t id, WriteBuffer body) {
        std::lock_guard lk(mtx_);
        if (dimension != -1 && dimension != 1) dimension = 0;
        queue.push_back({id, std::move(body), dimension});
    }
    [[nodiscard]] bool empty() noexcept {
        std::lock_guard lk(mtx_);
        return queue.empty();
    }
    [[nodiscard]] size_t size() {
        std::lock_guard lk(mtx_);
        return queue.size();
    }
    // Flushes queued packets. If multiple, wraps in BundleDelimiter (0x00) start/end
    // or coalesces to MultiBlockChange when all BlockUpdates share same chunk section.
    void flush(GameServer& srv, const Player* except);

private:
    void flushDimension(GameServer& srv, const Player* except,
                        std::vector<Queued>& q);
    bool tryFlushAsMultiBlockChange(GameServer& srv, const Player* except,
                                    std::vector<Queued>& q);
};

class ChatMessageProcessor {
public:
    // Verifies RSA-SHA256 signature when hasChatSession==true. The server currently
    // emits no signed outbound chat entries, so a non-zero acknowledgement mask
    // cannot be incorporated into the transcript and fails closed. The protocol
    // offset itself is not a message count and may advance beyond the 20-entry
    // acknowledgement window.
    static bool verify(const Player& p, const std::string& msg, int64_t timestamp,
                       int64_t salt, int32_t lastSeenOffset,
                       std::uint32_t acknowledgedMask,
                       const std::vector<uint8_t>& signature);
    [[nodiscard]] static bool shouldUsePlayerChat(const Player& p);
};

} // namespace cppfm
