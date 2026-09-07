// RCON (Source RCON protocol) + whitelist support (plan.md Phase 5).
#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <set>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "../core/Json.hpp"

namespace cppfm {

struct RconConfig {
    bool enabled = false;
    std::uint16_t port = 25575;
    std::string password;
};

inline bool readBoundedTextFile(const std::string& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (!sizeError && size > json::Value::kMaxInputBytes) return false;
    content.assign((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
    // istreambuf_iterator reads through the stream buffer directly and is not
    // required to set eofbit.  badbit is the reliable signal for an I/O error.
    return !file.bad() && content.size() <= json::Value::kMaxInputBytes;
}

// Minimal Source-RCON server. Commands are dispatched to a callback that returns the response text.
class RconServer {
public:
    using Handler = std::function<std::string(const std::string&)>;

    RconServer(RconConfig cfg, Handler handler)
        : cfg_(std::move(cfg)), handler_(std::move(handler)) {}

    ~RconServer() { stop(); }

    bool start() {
        std::lock_guard lifecycleLock(lifecycleMtx_);
        if (!cfg_.enabled || cfg_.password.empty()) return false;
        if (running_.load(std::memory_order_acquire) || worker_.joinable()) return false;
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        fd_.store(fd, std::memory_order_release);
        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local-only by default
        addr.sin_port = htons(cfg_.port);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(fd, 8) != 0) {
            fd_.exchange(-1, std::memory_order_acq_rel);
            ::close(fd);
            return false;
        }
        running_ = true;
        try {
            worker_ = std::thread([this]{ loop(); });
        } catch (...) {
            running_.store(false, std::memory_order_release);
            if (const int opened = fd_.exchange(-1, std::memory_order_acq_rel); opened >= 0)
                ::close(opened);
            throw;
        }
        return true;
    }
    void stop() {
        std::lock_guard lifecycleLock(lifecycleMtx_);
        running_.store(false, std::memory_order_release);
        // close() alone does not reliably interrupt it on Linux.
        if (const int fd = fd_.exchange(-1, std::memory_order_acq_rel); fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        std::vector<int> clients;
        {
            std::lock_guard lock(clientsMtx_);
            clients.assign(clientFds_.begin(), clientFds_.end());
        }
        for (const int client : clients) ::shutdown(client, SHUT_RDWR);
        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                std::fprintf(stderr, "[rcon] accept worker requested self-stop; deferred join\n");
            } else {
                worker_.join();
            }
        }
        std::vector<std::thread> clientsToJoin;
        {
            std::lock_guard lock(clientsMtx_);
            clientsToJoin.swap(clientThreads_);
        }
        std::vector<std::thread> deferred;
        for (auto& client : clientsToJoin) {
            if (!client.joinable()) continue;
            if (client.get_id() == std::this_thread::get_id()) {
                // A command handler may request shutdown from its own worker.
                // Keep the handle owned by RconServer so a later external
                // stop can join it; detaching would permit use-after-free.
                deferred.push_back(std::move(client));
            } else {
                client.join();
            }
        }
        {
            std::lock_guard lock(clientsMtx_);
            clientThreads_.reserve(clientThreads_.size() + deferred.size());
            for (auto& client : deferred) clientThreads_.push_back(std::move(client));
            clientFds_.clear();
        }
    }

private:
    static constexpr std::size_t kMaxPacketLength = 4110;
    static constexpr std::size_t kMaxPayloadLength = kMaxPacketLength - 10;

    static void le32(std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
        v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
    }

    static bool sendAll(int fd, const std::uint8_t* data, std::size_t size) {
        if (fd < 0 || (size != 0 && data == nullptr)) return false;
        std::size_t offset = 0;
        while (offset < size) {
            const ssize_t sent = ::send(fd, data + offset, size - offset,
                                        MSG_NOSIGNAL);
            if (sent < 0 && errno == EINTR) continue;
            if (sent <= 0) return false;
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }

    static void sendPacket(int fd, std::int32_t id, std::int32_t type,
                           const std::string& payload) {
        // Source RCON has a bounded packet length.  Long command output is
        // legal as a sequence of response packets, not as one oversized
        // allocation on the peer.
        std::size_t offset = 0;
        do {
            const std::size_t chunk = std::min(kMaxPayloadLength, payload.size() - offset);
            std::vector<std::uint8_t> body;
            body.reserve(10 + chunk);
            le32(body, static_cast<std::uint32_t>(id));
            le32(body, static_cast<std::uint32_t>(type));
            body.insert(body.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                        payload.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
            body.push_back(0); body.push_back(0);
            std::vector<std::uint8_t> frame;
            frame.reserve(4 + body.size());
            le32(frame, static_cast<std::uint32_t>(body.size()));
            frame.insert(frame.end(), body.begin(), body.end());
            if (!sendAll(fd, frame.data(), frame.size())) return;
            offset += chunk;
        } while (offset < payload.size());
    }

    static bool addClientThread(std::vector<std::thread>& threads,
                                std::function<void()> task) {
        try {
            threads.emplace_back(std::move(task));
            return true;
        } catch (...) {
            return false;
        }
    }
    static bool recvExact(int fd, void* dst, std::size_t n) {
        if (n != 0 && dst == nullptr) return false;
        auto* p = static_cast<std::uint8_t*>(dst);
        while (n > 0) {
            ssize_t r = ::recv(fd, p, n, 0);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return false;
            p += r; n -= static_cast<std::size_t>(r);
        }
        return true;
    }
    static bool recvPacket(int fd, std::int32_t& id, std::int32_t& type, std::string& body) {
        std::uint8_t lb[4];
        if (!recvExact(fd, lb, 4)) return false;
        const std::uint32_t len = static_cast<std::uint32_t>(lb[0]) |
                                  (static_cast<std::uint32_t>(lb[1]) << 8) |
                                  (static_cast<std::uint32_t>(lb[2]) << 16) |
                                  (static_cast<std::uint32_t>(lb[3]) << 24);
        if (len < 10 || len > kMaxPacketLength) return false;
        std::vector<std::uint8_t> buf(len);
        if (!recvExact(fd, buf.data(), len)) return false;
        const auto readLe32 = [](const std::uint8_t* bytes) -> std::uint32_t {
            return static_cast<std::uint32_t>(bytes[0]) |
                   (static_cast<std::uint32_t>(bytes[1]) << 8) |
                   (static_cast<std::uint32_t>(bytes[2]) << 16) |
                   (static_cast<std::uint32_t>(bytes[3]) << 24);
        };
        if (buf[len - 1] != 0 || buf[len - 2] != 0) return false;
        id = static_cast<std::int32_t>(readLe32(buf.data()));
        type = static_cast<std::int32_t>(readLe32(buf.data() + 4));
        body.assign(reinterpret_cast<const char*>(buf.data()+8),
                    len >= 10 ? len - 10 : 0);            // strip two trailing NULs
        return true;
    }

    void loop() {
        while (running_.load(std::memory_order_acquire)) {
            sockaddr_in cli{}; socklen_t cl = sizeof(cli);
            const int listenFd = fd_.load(std::memory_order_acquire);
            if (listenFd < 0) break;
            const int cfd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&cli), &cl);
            if (cfd < 0) {
                if (!running_.load(std::memory_order_acquire) || errno == EINTR) continue;
                std::fprintf(stderr, "[rcon] accept failed: %s\n", std::strerror(errno));
                continue;
            }
            std::lock_guard lock(clientsMtx_);
            if (!running_.load(std::memory_order_acquire)) {
                ::shutdown(cfd, SHUT_RDWR);
                ::close(cfd);
                break;
            }
            clientFds_.insert(cfd);
            if (!addClientThread(clientThreads_, [this, cfd] { clientLoop(cfd); })) {
                clientFds_.erase(cfd);
                ::shutdown(cfd, SHUT_RDWR);
                ::close(cfd);
                std::fprintf(stderr, "[rcon] could not create client worker\n");
            }
        }
    }

    void clientLoop(int cfd) noexcept {
        try {
            bool authed = false;
            while (running_.load(std::memory_order_acquire)) {
                std::int32_t id = 0, type = 0;
                std::string body;
                const bool ok = recvPacket(cfd, id, type, body);
                if (!ok) break;
                if (type == 3) {
                    if (body == cfg_.password) { authed = true; sendPacket(cfd, id, 2, ""); }
                    else sendPacket(cfd, -1, 2, "");
                    continue;
                }
                if (type == 2) {
                    if (!authed) break;
                    const std::string out = handler_(body);
                    sendPacket(cfd, id, 2, out.empty() ? "OK" : out);
                    continue;
                }
                break;
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[rcon] client fd=%d failed: %s\n", cfd, e.what());
        } catch (...) {
            std::fprintf(stderr, "[rcon] client fd=%d failed\n", cfd);
        }
        {
            std::lock_guard lock(clientsMtx_);
            clientFds_.erase(cfd);
        }
        ::shutdown(cfd, SHUT_RDWR);
        ::close(cfd);
    }

    RconConfig cfg_;
    Handler handler_;
    std::atomic<bool> running_{false};
    std::atomic<int> fd_{-1};
    std::thread worker_;
    std::mutex lifecycleMtx_;
    std::mutex clientsMtx_;
    std::set<int> clientFds_;
    std::vector<std::thread> clientThreads_;
};

// Whitelist: names loaded from the vanilla JSON array of {"name": "X"}
// objects.  Parsing the document matters: a name containing a quote or a
// second field must not change which users are admitted.
class Whitelist {
public:
    void load(const std::string& path) {
        names_.clear();
        std::ifstream f(path);
        enabled_ = f.good();
        if (!f) return;
        std::string content;
        if (!readBoundedTextFile(path, content)) {
            std::fprintf(stderr, "[Whitelist] refusing unreadable or oversized %s\n",
                         path.c_str());
            return;
        }
        try {
            const auto root = json::Value::parse(content);
            if (!root.isArr()) {
                throw std::runtime_error("whitelist root must be an array");
            }
            for (const auto& entry : root.arr) {
                if (!entry.isObj()) continue;
                const auto* name = entry.find("name");
                if (name && name->isStr() && !name->str.empty())
                    names_.insert(name->str);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[Whitelist] ignoring malformed %s: %s\n",
                         path.c_str(), e.what());
        } catch (...) {
            std::fprintf(stderr, "[Whitelist] ignoring malformed %s\n", path.c_str());
        }
    }
    bool enabled() const { return enabled_; }
    bool contains(const std::string& name) const { return names_.count(name) != 0; }
    void setEnabled(bool v) { enabled_ = v; }
    void insert(const std::string& n) { names_.insert(n); }
    bool remove(const std::string& n) { return names_.erase(n) > 0; }
    void clear() { names_.clear(); }
    const std::set<std::string>& names() const { return names_; }
    std::size_t size() const { return names_.size(); }
    bool save(const std::string& path) const {
        if (names_.size() > json::Value::kMaxContainerEntries) {
            std::fprintf(stderr, "[Whitelist] refusing to write oversized %s\n",
                         path.c_str());
            return false;
        }
        json::Value root = json::Value::array();
        for (const auto& n : names_) {
            json::Value entry = json::Value::object();
            entry.set("name", json::Value::ofString(n));
            root.push(std::move(entry));
        }
        const std::string serialized = root.dump() + "\n";
        const std::string temporary = path + ".new";
        std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
        if (!f) {
            std::fprintf(stderr, "[Whitelist] could not write %s\n", path.c_str());
            return false;
        }
        f.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        f.flush();
        if (!f) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            std::fprintf(stderr, "[Whitelist] write failed for %s\n", path.c_str());
            return false;
        }
        f.close();
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            std::fprintf(stderr, "[Whitelist] replace failed for %s: %s\n",
                         path.c_str(), ec.message().c_str());
            return false;
        }
        return true;
    }

private:
    bool enabled_ = false;
    std::set<std::string> names_;
};

} // namespace cppfm
