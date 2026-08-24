// RCON (Source RCON protocol) + whitelist support (plan.md Phase 5).
#pragma once
#include <atomic>
#include <fstream>
#include <mutex>
#include <netinet/in.h>
#include <set>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "../core/ByteBuffer.hpp"

namespace cppfm {

struct RconConfig {
    bool enabled = false;
    std::uint16_t port = 25575;
    std::string password;
};

// Minimal Source-RCON server. Commands are dispatched to a callback that
// returns the response text.
class RconServer {
public:
    using Handler = std::function<std::string(const std::string&)>;

    RconServer(RconConfig cfg, Handler handler)
        : cfg_(std::move(cfg)), handler_(std::move(handler)) {}

    bool start() {
        if (!cfg_.enabled || cfg_.password.empty()) return false;
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local-only by default
        addr.sin_port = htons(cfg_.port);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(fd_, 8) != 0) {
            ::close(fd_); fd_ = -1;
            return false;
        }
        running_ = true;
        worker_ = std::thread([this]{ loop(); });
        return true;
    }
    void stop() {
        running_ = false;
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        if (worker_.joinable()) worker_.join();
    }

private:
    static void le32(std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
        v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
    }
    static void sendPacket(int fd, std::int32_t id, std::int32_t type,
                           const std::string& payload) {
        std::vector<std::uint8_t> body;
        le32(body, static_cast<std::uint32_t>(id));
        le32(body, static_cast<std::uint32_t>(type));
        body.insert(body.end(), payload.begin(), payload.end());
        body.push_back(0); body.push_back(0);
        std::vector<std::uint8_t> frame;
        le32(frame, static_cast<std::uint32_t>(body.size()));
        frame.insert(frame.end(), body.begin(), body.end());
        ::send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    }
    static bool recvExact(int fd, void* dst, std::size_t n) {
        auto* p = static_cast<std::uint8_t*>(dst);
        while (n > 0) {
            ssize_t r = ::recv(fd, p, n, 0);
            if (r <= 0) return false;
            p += r; n -= static_cast<std::size_t>(r);
        }
        return true;
    }
    static bool recvPacket(int fd, std::int32_t& id, std::int32_t& type, std::string& body) {
        std::uint8_t lb[4];
        if (!recvExact(fd, lb, 4)) {
            if (getenv("RCON_TRACE")) std::fprintf(stderr, "[rcon] len-read failed\n");
            return false;
        }
        const std::uint32_t len = lb[0] | (lb[1]<<8) | (lb[2]<<16) | ((std::uint32_t)lb[3]<<24);
        if (getenv("RCON_TRACE"))
            std::fprintf(stderr, "[rcon] len=%u raw=%02x%02x%02x%02x\n",
                         len, lb[0], lb[1], lb[2], lb[3]);
        if (len > 4110) return false;
        std::vector<std::uint8_t> buf(len);
        if (!recvExact(fd, buf.data(), len)) return false;
        if (getenv("RCON_TRACE")) {
            std::fprintf(stderr, "[rcon-recv] len=%u head=", len);
            for (std::size_t k = 0; k < len && k < 10; ++k)
                std::fprintf(stderr, "%02x", buf[k]);
            std::fprintf(stderr, "\n");
        }
        std::memcpy(&id, buf.data(), 4);      // little-endian host layout
        std::memcpy(&type, buf.data()+4, 4);
        body.assign(reinterpret_cast<const char*>(buf.data()+8),
                    len >= 10 ? len - 10 : 0);            // strip two trailing NULs
        return true;
    }

    void loop() {
        while (running_) {
            sockaddr_in cli{}; socklen_t cl = sizeof(cli);
            const int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&cli), &cl);
            if (cfd < 0) { if (!running_) break; continue; }
            std::thread([this, cfd]{
                bool authed = false;
                while (running_) {
                    std::int32_t id, type; std::string body;
                    const bool ok = recvPacket(cfd, id, type, body);
                    if (getenv("RCON_TRACE"))
                        std::fprintf(stderr, "[rcon] fd=%d ok=%d id=%d type=%d body='%s'\n",
                                     cfd, (int)ok, id, type, body.c_str());
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
                }
                ::close(cfd);
            }).detach();
        }
    }

    RconConfig cfg_;
    Handler handler_;
    std::atomic<bool> running_{false};
    int fd_ = -1;
    std::thread worker_;
};

// Whitelist: names loaded from a JSON file of {"name": "X"} objects.
class Whitelist {
public:
    void load(const std::string& path) {
        std::ifstream f(path);
        enabled_ = f.good();
        if (!f) return;
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        // naive scan for "name" values; robust enough for our own files
        std::size_t pos = 0;
        while ((pos = content.find("\"name\"", pos)) != std::string::npos) {
            const auto q1 = content.find('"', pos + 6);
            if (q1 == std::string::npos) break;
            const auto q2 = content.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            names_.insert(content.substr(q1 + 1, q2 - q1 - 1));
            pos = q2 + 1;
        }
    }
    bool enabled() const { return enabled_; }
    bool contains(const std::string& name) const { return names_.count(name) != 0; }
    void setEnabled(bool v) { enabled_ = v; }
    void insert(const std::string& n) { names_.insert(n); }

private:
    bool enabled_ = false;
    std::set<std::string> names_;
};

} // namespace cppfm
