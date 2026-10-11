// Mojang session-server authentication helpers.
#pragma once
#include "../core/Json.hpp"
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
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace cppfm {

namespace mojang_detail {
inline constexpr std::size_t kMaxHttpResponseBytes = 1024U * 1024U;

// Authentication only contacts these two Mojang services. Keeping the URL
// grammar deliberately narrow makes the shell-backed curl invocation safe on
// both POSIX shells and cmd.exe, and prevents turning this helper into an
// unrestricted server-side HTTP client.
inline bool isAllowedServiceUrl(std::string_view url) {
    constexpr std::string_view prefix = "https://";
    if (url.size() <= prefix.size() || url.size() > 2048 ||
        url.substr(0, prefix.size()) != prefix)
        return false;

    const auto authorityEnd = url.find_first_of("/?#", prefix.size());
    const auto authority = url.substr(
        prefix.size(), authorityEnd == std::string_view::npos
            ? url.size() - prefix.size() : authorityEnd - prefix.size());
    if (authority != "sessionserver.mojang.com" &&
        authority != "api.minecraftservices.com")
        return false;

    const auto path = authorityEnd == std::string_view::npos
        ? std::string_view{} : url.substr(authorityEnd);
    for (const unsigned char ch : path) {
        const bool allowed =
            (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '/' || ch == '?' ||
            ch == '&' || ch == '=' || ch == '_' || ch == '-' ||
            ch == '.' || ch == '~' || ch == ':';
        if (!allowed) return false;
    }
    return true;
}

inline bool curlSucceeded(int status) {
#ifdef _WIN32
    return status == 0;
#else
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}
} // namespace mojang_detail

// Uses the system curl executable so the server has no extra linked HTTP
// library dependency. Requests are HTTPS-only, host-allowlisted and bounded in
// time and response size. The process exit status is authoritative even when
// curl emitted an HTTP error body.
inline std::string httpGet(const std::string& url, long timeoutSec = 10,
                           std::size_t maxResponseBytes =
                               mojang_detail::kMaxHttpResponseBytes) {
    if (!mojang_detail::isAllowedServiceUrl(url))
        throw std::invalid_argument("Mojang HTTP URL is not an allowed service URL");
    if (timeoutSec < 1 || timeoutSec > 120)
        throw std::invalid_argument("Mojang HTTP timeout must be in 1..120 seconds");
    if (maxResponseBytes == 0 ||
        maxResponseBytes > mojang_detail::kMaxHttpResponseBytes)
        throw std::invalid_argument("Mojang HTTP response limit is out of range");

#ifdef _WIN32
    const char* executable = "curl.exe";
    const char* redirect = "2>NUL";
#else
    const char* executable = "curl";
    const char* redirect = "2>/dev/null";
#endif
    // The allowlist rejects quotes, percent signs, whitespace, shell
    // metacharacters, and backslashes, so this remains one quoted argument on
    // both supported command interpreters.
    const std::string cmd = std::string(executable) +
        " --disable -sS --fail --max-time " + std::to_string(timeoutSec) +
        " --connect-timeout " + std::to_string(std::min(timeoutSec, 5L)) +
        " --max-filesize " + std::to_string(maxResponseBytes) +
        " --proto =https -- \"" + url + "\" " + redirect;
#ifdef _WIN32
    FILE* f = _popen(cmd.c_str(), "rb");
#else
    FILE* f = popen(cmd.c_str(), "r");
#endif
    if (!f) throw std::runtime_error("popen curl failed");
    std::string out;
    char buf[4096];
    std::size_t n;
    bool tooLarge = false;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (n > maxResponseBytes - out.size()) {
            tooLarge = true;
        } else if (!tooLarge) {
            out.append(buf, n);
        }
    }
    const bool readFailed = std::ferror(f) != 0;
#ifdef _WIN32
    const int rc = _pclose(f);
#else
    const int rc = pclose(f);
#endif
    if (tooLarge) throw std::runtime_error("curl response exceeds configured limit");
    if (readFailed) throw std::runtime_error("failed reading curl response");
    if (!mojang_detail::curlSucceeded(rc))
        throw std::runtime_error("curl failed with status " + std::to_string(rc));
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
    ProfilePublicKeys keys;
    try {
        const auto root = cppfm::json::Value::parse(json);
        const auto* entries = root.find("playerCertificateKeys");
        if (!entries || !entries->isArr()) return {};
        for (const auto& entry : entries->arr) {
            if (!entry.isObj()) continue;
            const auto* publicKey = entry.find("publicKey");
            if (!publicKey || !publicKey->isStr()) continue;
            auto decoded = mojang_detail::base64Decode(publicKey->str);
            if (!decoded.empty()) keys.push_back(std::move(decoded));
        }
    } catch (const std::exception&) {
        return {};
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

// Parse the typed response shape rather than scanning key-like substrings:
// property fields may be escaped and unrelated nested objects may reuse keys.
inline bool parseHasJoined(const std::string& json, HasJoinedResult& out) {
    try {
        const auto root = cppfm::json::Value::parse(json);
        if (!root.isObj()) return false;
        const auto* id = root.find("id");
        if (!id || !id->isStr() || id->str.size() != 32) return false;
        for (const unsigned char ch : id->str) {
            if (!((ch >= '0' && ch <= '9') ||
                  (ch >= 'a' && ch <= 'f') ||
                  (ch >= 'A' && ch <= 'F')))
                return false;
        }

        HasJoinedResult parsed;
        parsed.uuidNoDashes = id->str;
        if (const auto* properties = root.find("properties")) {
            if (!properties->isArr()) return false;
            for (const auto& property : properties->arr) {
                if (!property.isObj()) return false;
                const auto* name = property.find("name");
                const auto* value = property.find("value");
                const auto* signature = property.find("signature");
                if (!name || !name->isStr() || !value || !value->isStr() ||
                    (signature && !signature->isStr()))
                    return false;
                parsed.props.push_back({name->str, value->str,
                    signature ? signature->str : std::string{}});
            }
        }
        out = std::move(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace cppfm
