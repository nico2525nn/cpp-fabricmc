#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <vector>
#include <algorithm>

namespace cppfm {

inline std::int64_t ticketChunkKey(std::int32_t cx, std::int32_t cz) {
    return (static_cast<std::int64_t>(static_cast<std::uint32_t>(cx)) << 32)
         | static_cast<std::uint32_t>(cz);
}

// ChunkTicket levels mirror vanilla: lower level = higher priority / more ticking
// Vanilla: 31 = spawn / forced, 33 = player,  etc. We use 31 for spawn forced.
enum class TicketType : uint8_t {
    SPAWN = 0,   // spawn chunk loader 5x5
    FORCED = 1,  // /forceload or forcedChunks
    PLAYER = 2,  // player simulation distance
    LIGHT = 3,   // light
    UNKNOWN = 255
};

struct ChunkTicket {
    TicketType type = TicketType::UNKNOWN;
    int level = 33;
    int64_t createdTick = 0;
    int64_t timeoutTick = -1; // -1 = never expires (spawn)
};

class ChunkTicketManager {
public:
    void addTicket(int32_t cx, int32_t cz, TicketType type, int level, int64_t now) {
        int64_t k = ticketChunkKey(cx, cz);
        auto &vec = tickets_[k];
        // replace or add ticket of same type
        for (auto &t : vec) if (t.type == type) { t.level = level; t.createdTick = now; return; }
        vec.push_back({type, level, now, -1});
    }
    void removeTicket(int32_t cx, int32_t cz, TicketType type) {
        int64_t k = ticketChunkKey(cx, cz);
        auto it = tickets_.find(k);
        if (it == tickets_.end()) return;
        auto &vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const ChunkTicket& t){ return t.type==type; }), vec.end());
        if (vec.empty()) tickets_.erase(it);
    }
    bool hasTicket(int32_t cx, int32_t cz, TicketType type) const {
        int64_t k = ticketChunkKey(cx, cz);
        auto it = tickets_.find(k);
        if (it == tickets_.end()) return false;
        for (auto &t : it->second) if (t.type==type) return true;
        return false;
    }
    bool hasAnyTicket(int32_t cx, int32_t cz) const {
        int64_t k = ticketChunkKey(cx, cz);
        return tickets_.find(k) != tickets_.end();
    }
    int getMinLevel(int32_t cx, int32_t cz) const {
        int64_t k = ticketChunkKey(cx, cz);
        auto it = tickets_.find(k);
        if (it == tickets_.end()) return 33;
        int minL = 33;
        for (auto &t : it->second) minL = std::min(minL, t.level);
        return minL;
    }
    bool shouldTick(int32_t cx, int32_t cz) const {
        return getMinLevel(cx, cz) <= 31;
    }
    bool shouldTickEntities(int32_t cx, int32_t cz) const {
        return getMinLevel(cx, cz) <= 32;
    }
    void clear() { tickets_.clear(); }
    size_t count() const { return tickets_.size(); }
    // Enumerate all ticket keys for persistence (ForcedChunks NBT)
    std::vector<int64_t> allTicketKeys() const {
        std::vector<int64_t> out;
        out.reserve(tickets_.size());
        for (auto &kv : tickets_) out.push_back(kv.first);
        return out;
    }
    void forEach(std::function<void(int32_t,int32_t,const ChunkTicket&)> fn) const {
        for (auto &kv : tickets_) {
            int32_t cx = static_cast<int32_t>(kv.first >> 32);
            int32_t cz = static_cast<int32_t>(kv.first & 0xFFFFFFFFLL);
            for (auto &t : kv.second) fn(cx, cz, t);
        }
    }
private:
    std::unordered_map<int64_t, std::vector<ChunkTicket>> tickets_;
};

} // namespace cppfm
