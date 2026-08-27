#include "PacketBatcher.hpp"
#include "../game/GameServer.hpp"
#include "../proto/Ids.hpp"

namespace cppfm {

void PacketBatcher::flush(GameServer& srv, const Player* except) {
    if (queue.empty()) return;
    // Simple: if single, send directly; if multiple, try bundle else individual
    if (queue.size() == 1) {
        auto &q = queue[0];
        srv.broadcastPacketExcept(except, q.id, q.body);
    } else {
        // Try multi_block_change coalescing: check if all are BlockUpdate in same chunk section
        // For now, just send as bundle delimiter wrapping if bundle supported
        // Fallback: send each individually (still correct, just not batched)
        // Check if all BlockUpdate and same chunk
        bool allBlockUpdate = true;
        int32_t cx0 = INT32_MAX, cz0 = INT32_MAX;
        int section0 = INT32_MAX;
        for (auto &q : queue) {
            if (q.id != proto::pl::sc::BlockUpdate) { allBlockUpdate = false; break; }
            // need to parse position from body to get chunk: body contains varint? Actually BlockUpdate body is position + varint state
            // For simplicity, skip multi_block_change and just bundle
        }
        if (allBlockUpdate && tryFlushAsMultiBlockChange(srv, except)) {
            // already flushed as multi_block_change
        } else {
            // Send as bundle if client supports (0x00 BundleDelimiter)
            // For compatibility, just send each individually
            for (auto &q : queue) {
                srv.broadcastPacketExcept(except, q.id, q.body);
            }
        }
    }
    queue.clear();
    lastFlushMs = 0; // caller will set, but ensure cleared
}

bool PacketBatcher::tryFlushAsMultiBlockChange(GameServer& srv, const Player* except) {
    // Check if all queued are BlockUpdate in same chunk (section)
    // For now, not implemented - return false to fallback to individual
    (void)srv; (void)except;
    return false;
}

bool ChatMessageProcessor::verify(const Player& p, const std::string& msg, int64_t timestamp, int64_t salt, const std::vector<uint8_t>& signature) {
    (void)p; (void)msg; (void)timestamp; (void)salt; (void)signature;
    // TODO: real RSA verification with p.chatPubKey
    return true;
}

bool ChatMessageProcessor::shouldUsePlayerChat(const Player& p) {
    return p.hasChatSession;
}

} // namespace cppfm
