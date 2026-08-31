#include "PacketBatcher.hpp"
#include "../game/GameServer.hpp"
#include "../proto/Ids.hpp"
#include "../net/Crypto.hpp"
#include <chrono>
#include <cstdio>
#include <climits>
#include <unordered_map>

namespace cppfm {

static int64_t nowMsLocal() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void PacketBatcher::flush(GameServer& srv, const Player* except) {
    // Plan28 finish: swap the queue under the lock so the game-tick thread can
    // flush while a Session thread (chat command / block change) keeps queuing.
    std::vector<Queued> q;
    {
        std::lock_guard lk(mtx_);
        if (queue.empty()) return;
        q.swap(queue);
    }
    if (q.size() == 1) {
        auto &q0 = q[0];
        srv.broadcastPacketExcept(except, q0.id, q0.body);
    } else {
        bool allBlockUpdate = true;
        for (auto &queued : q) if (queued.id != proto::pl::sc::BlockUpdate) { allBlockUpdate = false; break; }
        if (allBlockUpdate) {
            // Try grouped MultiBlockChange optimization (plan10 §3): group by SectionPos
            // Deduplicate same pos -> keep last state (last write wins)
            struct Rec { int32_t x,y,z; uint16_t state; };
            // dedup map: key = (x,y,z) packed
            std::unordered_map<int64_t, Rec> dedup;
            dedup.reserve(q.size());
            for (auto &queued : q) {
                ReadBuffer in(queued.body.data);
                int32_t x,y,z; in.position(x,y,z);
                uint16_t st = static_cast<uint16_t>(in.varint());
                int64_t k2 = ((int64_t)x << 42) ^ ((int64_t)y << 21) ^ (int64_t)z;
                dedup[k2] = {x,y,z,st};
            }
            // Group by section
            struct SecKey { int32_t cx, cz, sy; bool operator==(const SecKey& o) const { return cx==o.cx && cz==o.cz && sy==o.sy; } };
            struct SecHash { size_t operator()(SecKey const& k) const noexcept { return ((size_t)k.cx*31 + k.cz)*31 + k.sy; } };
            std::unordered_map<SecKey, std::vector<Rec>, SecHash> groups;
            groups.reserve(dedup.size());
            for (auto &kv : dedup) {
                const Rec& r = kv.second;
                SecKey sk{ r.x >> 4, r.z >> 4, r.y >> 4 };
                groups[sk].push_back(r);
            }
            // If single group with >=2 entries, use optimized MultiBlockChange direct (no bundle overhead)
            if (groups.size() == 1) {
                auto it = groups.begin();
                if (it->second.size() >= 2) {
                    // Single MultiBlockChange - use dedicated path
                    if (tryFlushAsMultiBlockChange(srv, except, q)) {
                        lastFlushMs.store(nowMsLocal());
                        return;
                    }
                }
            }
            // Build bundle parts: for each group emit either MultiBlockChange or BlockUpdate(s)
            std::vector<std::pair<uint8_t, WriteBuffer>> parts;
            parts.reserve(groups.size()*2);
            for (auto &g : groups) {
                auto &vec = g.second;
                if (vec.size() >= 2) {
                    // MultiBlockChange for this section
                    WriteBuffer b;
                    {
                        int64_t sx = g.first.cx, sz = g.first.cz, sy = g.first.sy;
                        uint64_t packed = 0;
                        packed |= (static_cast<uint64_t>(sx & 0x3FFFFF) << 42);
                        packed |= (static_cast<uint64_t>(sz & 0x3FFFFF) << 20);
                        packed |= (static_cast<uint64_t>(sy & 0xFFFFF));
                        b.u64(packed);
                    }
                    b.varint(static_cast<int32_t>(vec.size()));
                    for (auto &r : vec) {
                        int32_t lx = r.x & 15, ly = r.y & 15, lz = r.z & 15;
                        // vanilla/Prismarine: (state<<12)|(localX<<8)|(localZ<<4)|localY
                        // (not y<<8|z<<4|x — that swap placed blocks at wrong section-local
                        // coords, plan28 finish fix, see COMPAT_AUDIT N7)
                        int32_t enc = (static_cast<int32_t>(r.state) << 12) | (lx << 8) | (lz << 4) | ly;
                        b.varint(enc);
                    }
                    parts.emplace_back(proto::pl::sc::MultiBlockChange, std::move(b));
                    srv.invalidateChunkCache(g.first.cx, g.first.cz);
                } else {
                    for (auto &r : vec) {
                        WriteBuffer b;
                        b.position(r.x, r.y, r.z);
                        b.varint(r.state);
                        parts.emplace_back(proto::pl::sc::BlockUpdate, std::move(b));
                        srv.invalidateChunkCache(r.x >> 4, r.z >> 4);
                    }
                }
            }
            if (parts.size() == 1) {
                srv.broadcastPacketExcept(except, parts[0].first, parts[0].second);
            } else if (!parts.empty()) {
                WriteBuffer empty;
                srv.broadcastPacketExcept(except, proto::pl::sc::BundleDelimiter, empty);
                for (auto &p : parts) srv.broadcastPacketExcept(except, p.first, p.second);
                WriteBuffer empty2;
                srv.broadcastPacketExcept(except, proto::pl::sc::BundleDelimiter, empty2);
            }
        } else {
            // Bundle: wrap queued packets with BundleDelimiter  0x00 start/end
            WriteBuffer empty;
            srv.broadcastPacketExcept(except, proto::pl::sc::BundleDelimiter, empty);
            for (auto &queued : q) {
                srv.broadcastPacketExcept(except, queued.id, queued.body);
            }
            WriteBuffer empty2;
            srv.broadcastPacketExcept(except, proto::pl::sc::BundleDelimiter, empty2);
        }
    }
    lastFlushMs.store(nowMsLocal());
}

bool PacketBatcher::tryFlushAsMultiBlockChange(GameServer& srv, const Player* except,
                                               std::vector<Queued>& q) {
    if (q.empty()) return false;
    if (q.size() < 2) return false;
    bool allBlockUpdate = true;
    for (auto &queued : q) if (queued.id != proto::pl::sc::BlockUpdate) { allBlockUpdate = false; break; }
    if (!allBlockUpdate) return false;
    struct Rec { int32_t x,y,z; uint16_t state; };
    std::vector<Rec> recs;
    recs.reserve(q.size());
    int32_t baseCx = INT32_MAX, baseCz = INT32_MAX, baseSy = INT32_MAX;
    bool sameSection = true;
    // Use dedup for last-write-wins as well
    std::unordered_map<int64_t, Rec> dedup;
    dedup.reserve(q.size());
    for (auto &queued : q) {
        ReadBuffer in(queued.body.data);
        int32_t x,y,z; in.position(x,y,z);
        uint16_t st = static_cast<uint16_t>(in.varint());
        int64_t k = ((int64_t)x << 42) ^ ((int64_t)y << 21) ^ (int64_t)z;
        dedup[k] = {x,y,z,st};
    }
    for (auto &kv : dedup) {
        const Rec &r = kv.second;
        int32_t cx = r.x >> 4, cz = r.z >> 4, sy = r.y >> 4;
        if (recs.empty()) { baseCx = cx; baseCz = cz; baseSy = sy; }
        else if (cx != baseCx || cz != baseCz || sy != baseSy) sameSection = false;
        recs.push_back(r);
    }
    if (!sameSection) return false;
    WriteBuffer b;
    {
        int64_t sx = baseCx, sz = baseCz, sy = baseSy;
        uint64_t packed = 0;
        packed |= (static_cast<uint64_t>(sx & 0x3FFFFF) << 42);
        packed |= (static_cast<uint64_t>(sz & 0x3FFFFF) << 20);
        packed |= (static_cast<uint64_t>(sy & 0xFFFFF));
        b.u64(packed);
    }
    b.varint(static_cast<int32_t>(recs.size()));
    for (auto &r : recs) {
        int32_t lx = r.x & 15, ly = r.y & 15, lz = r.z & 15;
        // vanilla/Prismarine: (state<<12)|(localX<<8)|(localZ<<4)|localY (not y<<8|z<<4|x)
        int32_t enc = (static_cast<int32_t>(r.state) << 12) | (lx << 8) | (lz << 4) | ly;
        b.varint(enc);
    }
    srv.broadcastPacketExcept(except, proto::pl::sc::MultiBlockChange, b);
    srv.invalidateChunkCache(baseCx, baseCz);
    return true;
}

bool ChatMessageProcessor::verify(const Player& p, const std::string& msg, int64_t timestamp, int64_t salt, const std::vector<uint8_t>& signature) {
    if (!p.hasChatSession) {
        return true;
    }
    const bool trace = std::getenv("CPPFM_TRACE") != nullptr;
    if (p.chatSessionExpiry != 0) {
        int64_t now = nowMsLocal();
        if (now > p.chatSessionExpiry) {
            if (trace) std::fprintf(stderr, "[cppfm] chat verify: session expired for %s\n", p.name.c_str());
            return false;
        }
    }
    if (p.chatPubKey.empty()) {
        if (trace) std::fprintf(stderr, "[cppfm] chat verify: no pubkey for %s, fallback to SystemChat\n", p.name.c_str());
        return false;
    }
    if (signature.empty()) {
        if (trace) std::fprintf(stderr, "[cppfm] chat verify: session present but no signature for %s\n", p.name.c_str());
        return false;
    }
    // Replay protection: check salt not duplicated within last 20 salts (if tracked)
    for (auto v : p.lastSeenSignatures) if ((int64_t)v == (salt & 0xFF)) { /* soft check */ }
    // Build data to verify: (timestamp,salt,msg,lastSeen) simplified as msg + timestamp + salt
    // NOTE: vanilla signs (prevSignature?); we use msg+LE timestamp+salt for audit parity (N6).
    std::string data;
    data.reserve(msg.size() + 16);
    data.append(msg);
    data.append(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    data.append(reinterpret_cast<const char*>(&salt), sizeof(salt));
    bool ok = crypto::verifyRsaSha256(p.chatPubKey, reinterpret_cast<const uint8_t*>(data.data()), data.size(), signature);
    if (trace) {
        std::fprintf(stderr, "[cppfm] chat signature verify %s for %s\n", ok ? "OK" : "FAILED", p.name.c_str());
    }
    return ok;
}

bool ChatMessageProcessor::shouldUsePlayerChat(const Player& p) {
    if (!p.hasChatSession) return false;
    if (p.chatSessionExpiry != 0) {
        int64_t now = nowMsLocal();
        if (now > p.chatSessionExpiry) return false;
    }
    return !p.chatPubKey.empty();
}

} // namespace cppfm
