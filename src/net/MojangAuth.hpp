// Mojang session-server auth helpers (libcurl).
#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cppfm {

// Uses the system curl binary (present on virtually all servers).
inline std::string httpGet(const std::string& url, long timeoutSec = 10) {
    std::string cmd = "curl -sS --max-time " + std::to_string(timeoutSec) +
                      " --fail '" + url + "' 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) throw std::runtime_error("popen curl failed");
    std::string out;
    char buf[4096];
    std::size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const int rc = pclose(f);
    if (rc != 0 && out.empty()) throw std::runtime_error("curl exited " + std::to_string(rc));
    return out;
}

namespace mojang_detail {
inline std::vector<std::uint8_t> base64Decode(const std::string& text) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> out;
    std::uint32_t accumulator = 0;
    unsigned int bits = 0;
    bool padding = false;
    std::size_t useful = 0;
    for (unsigned char ch : text) {
        if (ch == '=') {
            padding = true;
            continue;
        }
        const char* pos = std::find(alphabet, alphabet + 64, static_cast<char>(ch));
        if (pos == alphabet + 64 || padding) return {};
        ++useful;
        accumulator = (accumulator << 6) |
                      static_cast<std::uint32_t>(pos - alphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(accumulator >> bits));
            accumulator &= bits == 0 ? 0u : ((1u << bits) - 1u);
        }
    }
    const auto remainder = useful % 4;
    if (text.empty() || remainder == 1 ||
        (remainder == 2 && accumulator != 0) ||
        (remainder == 3 && accumulator != 0) ||
        (text.find('=') != std::string::npos &&
         (text.size() % 4 != 0 || text.find('=') < text.size() - 2))) return {};
    return out;
}

using ProfilePublicKeys = std::vector<std::vector<std::uint8_t>>;

inline ProfilePublicKeys parseProfilePublicKeys(const std::string& json) {
    const auto keyNamespace = json.find("\"playerCertificateKeys\"");
    if (keyNamespace == std::string::npos) return {};
    const auto arrayBegin = json.find('[', keyNamespace);
    if (arrayBegin == std::string::npos) return {};
    const auto arrayEnd = json.find(']', arrayBegin + 1);
    if (arrayEnd == std::string::npos) return {};

    ProfilePublicKeys keys;
    std::size_t cursor = arrayBegin + 1;
    while (cursor < arrayEnd) {
        const auto keyTag = json.find("\"publicKey\"", cursor);
        if (keyTag == std::string::npos || keyTag >= arrayEnd) break;
        const auto begin = json.find('\"', keyTag + 11);
        if (begin == std::string::npos || begin >= arrayEnd) break;
        const auto end = json.find('\"', begin + 1);
        if (end == std::string::npos || end > arrayEnd) break;
        auto decoded = mojang_detail::base64Decode(json.substr(begin + 1, end - begin - 1));
        if (!decoded.empty()) keys.push_back(std::move(decoded));
        cursor = end + 1;
    }
    return keys;
}

class ProfilePublicKeyCache {
public:
    using Clock = std::chrono::steady_clock;
    explicit ProfilePublicKeyCache(
        Clock::duration refresh = std::chrono::minutes(5),
        Clock::duration stale = std::chrono::minutes(30),
        Clock::duration retry = std::chrono::seconds(10),
        std::function<Clock::time_point()> now = [] { return Clock::now(); })
        : refresh_(refresh), stale_(stale), retry_(retry), now_(std::move(now)) {}

    template <typename Loader>
    ProfilePublicKeys get(Loader&& loader) {
        std::unique_lock lock(mutex_);
        for (;;) {
            const auto now = now_();
            if (now < refreshAt_) {
                if (!keys_.empty() && now >= staleUntil_) keys_.clear();
                return keys_;
            }
            if (!loading_) {
                loading_ = true;
                break;
            }
            changed_.wait(lock, [this] { return !loading_; });
        }
        lock.unlock();

        ProfilePublicKeys fresh;
        std::exception_ptr failure;
        try {
            fresh = std::forward<Loader>(loader)();
        } catch (...) {
            failure = std::current_exception();
        }

        lock.lock();
        const auto now = now_();
        if (failure) {
            if (keys_.empty() || now >= staleUntil_) keys_.clear();
            refreshAt_ = now + retry_;
        } else if (!fresh.empty()) {
            keys_ = std::move(fresh);
            refreshAt_ = now + refresh_;
            staleUntil_ = now + stale_;
        } else {
            if (keys_.empty() || now >= staleUntil_) keys_.clear();
            refreshAt_ = now + retry_;
        }
        auto result = keys_;
        loading_ = false;
        lock.unlock();
        changed_.notify_all();
        if (failure && result.empty()) std::rethrow_exception(failure);
        return result;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    ProfilePublicKeys keys_;
    Clock::time_point refreshAt_{};
    Clock::time_point staleUntil_{};
    Clock::duration refresh_;
    Clock::duration stale_;
    Clock::duration retry_;
    std::function<Clock::time_point()> now_;
    bool loading_ = false;
};
} // namespace mojang_detail

inline std::vector<std::vector<std::uint8_t>> fetchMojangProfilePublicKeys() {
    static mojang_detail::ProfilePublicKeyCache cache;
    return cache.get([] {
        return mojang_detail::parseProfilePublicKeys(
            httpGet("https://api.minecraftservices.com/publickeys", 10));
    });
}

struct HasJoinedResult {
    std::string uuidNoDashes;
    struct Prop { std::string name, value, signature; };
    std::vector<Prop> props;
};

// Minimal JSON scan for hasJoined response: {"id":"..","name":"..","properties":[{"name":"textures","value":"..","signature":".."}]}
inline bool parseHasJoined(const std::string& json, HasJoinedResult& out) {
    auto findStr = [&](const std::string& key) -> std::string {
        const std::string k = "\"" + key + "\":\"";
        const auto p = json.find(k);
        if (p == std::string::npos) return {};
        const auto s = p + k.size();
        const auto e = json.find('"', s);
        if (e == std::string::npos) return {};
        return json.substr(s, e - s);
    };
    out.uuidNoDashes = findStr("id");
    if (out.uuidNoDashes.size() != 32) return false;
    // properties array entries
    const auto parr = json.find("\"properties\"");
    if (parr != std::string::npos) {
        std::size_t pos = parr;
        while (true) {
            pos = json.find("\"name\":\"", pos + 1);
            if (pos == std::string::npos) break;
            const auto ns = pos + 8;
            const auto ne = json.find('"', ns);
            if (ne == std::string::npos) break;
            HasJoinedResult::Prop prop;
            prop.name = json.substr(ns, ne - ns);
            const std::string vkey = "\"value\":\"";
            const auto vs = json.find(vkey, ne);
            if (vs == std::string::npos) break;
            const auto vsStart = vs + vkey.size();
            const auto ve = json.find('"', vsStart);
            prop.value = json.substr(vsStart, ve - vsStart);
            const std::string gkey = "\"signature\":\"";
            const auto gs = json.find(gkey, ve);
            if (gs != std::string::npos && gs < ve + 64) {
                const auto gsStart = gs + gkey.size();
                const auto ge = json.find('"', gsStart);
                prop.signature = json.substr(gsStart, ge - gsStart);
            }
            out.props.push_back(prop);
        }
    }
    return true;
}

} // namespace cppfm
