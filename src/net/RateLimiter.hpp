
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace cppfm {

// ---- bandwidth token bucket (O-13 A1/A5) --------------------------------- capacity = max burst (bytes), refillPerSec = sustained rate
// (bytes/s). Session-thread only (no locking); Connection owns one instance.
struct RateLimiter {
    double tokens;
    double capacity;
    double refillPerSec;
    std::int64_t lastMs;

    explicit RateLimiter(double cap = 2.0 * 1024 * 1024,
                         double refill = 1.0 * 1024 * 1024,
                         std::int64_t nowMs = 0)
        : tokens(cap), capacity(cap), refillPerSec(refill), lastMs(nowMs) {}

    // Returns true when n bytes fit the budget (and deducts them).
    bool consume(double n, std::int64_t nowMs) {
        const double dt = static_cast<double>(nowMs - lastMs) / 1000.0;
        if (dt > 0) {
            tokens = std::min(capacity, tokens + dt * refillPerSec);
            lastMs = nowMs;
        }
        if (tokens < n) return false;
        tokens -= n;
        return true;
    }

    void reset(std::int64_t nowMs) { tokens = capacity; lastMs = nowMs; }
};

// ---- vanilla chat-spam throttle (O-13 A3) --------------------------------- Mirrors ServerPlayNetworkHandler.chatSpamThresholdCount =
// 200: every chat/command adds 20, every server tick removes 1 (lazy decay). onChat returns true when the peer must be kicked
// (disconnect.spam). Driven by server tickNo (not wall-clock) so tests stay deterministic.
struct SpamTracker {
    int count = 0;
    std::int64_t lastTick = 0;

    bool onChat(std::int64_t tickNo) {
        const std::int64_t dt = tickNo - lastTick;
        if (dt > 0) {
            count -= static_cast<int>(std::min<std::int64_t>(dt, count));
            lastTick = tickNo;
        }
        count += 20;
        return count > 200;
    }

    void reset(std::int64_t tickNo) { count = 0; lastTick = tickNo; }
};

// ---- global accept gate (O-13 A5) ------------------------------------------
// At most maxPerSec accepted connections per wall-clock second window;
// excess connections are refused immediately (close fd, no thread spawn).
class AcceptGate {
public:
    explicit AcceptGate(int maxPerSec = 20) : max_(maxPerSec) {}

    bool allow(std::int64_t nowMs) {
        std::lock_guard<std::mutex> lk(m_);
        if (nowMs - windowStart_ >= 1000) {
            windowStart_ = nowMs;
            count_ = 0;
        }
        if (count_ >= max_) return false;
        ++count_;
        return true;
    }

    void setMax(int n) {
        std::lock_guard<std::mutex> lk(m_);
        max_ = n;
    }

private:
    int max_;
    std::int64_t windowStart_ = 0;
    int count_ = 0;
    std::mutex m_;
};

// ---- rate-limited logging (flood-time disk-fill guard) ---------------------
struct RateLimitedLog {
    std::int64_t lastMs = 0;

    bool shouldLog(std::int64_t nowMs, std::int64_t intervalMs = 1000) {
        if (nowMs - lastMs < intervalMs) return false;
        lastMs = nowMs;
        return true;
    }
};

inline std::int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace cppfm
