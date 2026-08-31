// PacketBatcher: coalesces block updates into BundleDelimiter / MultiBlockChange.
// - BundleDelimiter 0x00 wraps heterogeneous packets (strict 1.21.4, PLAN10 §3).
// - MultiBlockChange 0x4E coalesces same-section BlockUpdate 0x09 with vanilla
//   axis pack (state<<12)|(x<<8)|(z<<4)|y (plan28 finish fixed the y/x swap).
// - plan28 finish: threadsafe — chat commands execute on the Session (connection)
//   thread and queue block changes (queueBlockChange) while the game tick thread
//   flushes the same queue; a plain vector raced (lost fill/MultiBlockChange
//   packets). mtx_ guards the queue; lastFlushMs is atomic.
// - 64-count flush threshold (50ms window), per-section grouping, last-write-wins.
// ChatMessageProcessor: RSA-SHA256 verifies PlayerChat 0x07 signatures when a
// ChatSession is present; falls back to SystemChat 0x73 otherwise (N6 HIGH).
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

class PacketBatcher {
public:
    struct Queued {
        uint8_t id;
        WriteBuffer body;
    };
    std::vector<Queued> queue;                // guarded by mtx_
    std::mutex mtx_;
    std::atomic<int64_t> lastFlushMs{0};

    void queuePacket(uint8_t id, WriteBuffer body) {
        std::lock_guard lk(mtx_);
        queue.push_back({id, std::move(body)});
    }
    [[nodiscard]] bool empty() noexcept {
        std::lock_guard lk(mtx_);
        return queue.empty();
    }
    [[nodiscard]] size_t size() {
        std::lock_guard lk(mtx_);
        return queue.size();
    }
    void clear() noexcept {
        std::lock_guard lk(mtx_);
        queue.clear();
    }

    // Flushes queued packets. If multiple, wraps in BundleDelimiter (0x00) start/end
    // or coalesces to MultiBlockChange when all BlockUpdates share same chunk section.
    void flush(GameServer& srv, const Player* except);

private:
    bool tryFlushAsMultiBlockChange(GameServer& srv, const Player* except,
                                    std::vector<Queued>& q);
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
