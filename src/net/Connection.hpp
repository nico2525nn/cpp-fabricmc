// Connection: blocking TCP client connection + packet framing.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../core/ByteBuffer.hpp"

namespace cppfm {

class SocketClosedError : public std::runtime_error {
public:
    explicit SocketClosedError(const std::string& w) : std::runtime_error(w) {}
};

class Connection {
public:
    explicit Connection(int fd) : fd_(fd) {}
    ~Connection() { close(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return fd_; }
    bool isOpen() const { return fd_ >= 0; }

    void close() {
        std::lock_guard lk(tx_);
        if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; }
    }
    void setNoDelay() {
        int one = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    // returns peer ip:port string (best effort)
    std::string peer() const {
        sockaddr_in addr{}; socklen_t sl = sizeof(addr);
        if (getpeername(fd_, reinterpret_cast<sockaddr*>(&addr), &sl) != 0) return "?";
        char buf[64];
        inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    // ---- framed io -------------------------------------------------------
    // Reads one frame payload (length-prefixed). Throws SocketClosedError on EOF.
    std::vector<std::uint8_t> readFrame() {
        const std::int32_t len = readVarintStream(5);
        if (len <= 0 || static_cast<std::uint32_t>(len) > kMaxFrame)
            throw std::runtime_error("frame length out of range: " + std::to_string(len));
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
        readExact(buf.data(), buf.size());
        return buf;
    }
    void writeFrame(const WriteBuffer& wb) { writeFrameRaw(wb.data.data(), wb.data.size()); }
    void writeFrameRaw(const std::uint8_t* body, std::size_t n) {
        std::vector<std::uint8_t> frame;
        WriteBuffer::writeVarintTo(frame, static_cast<std::int32_t>(n));
        frame.insert(frame.end(), body, body + n);
        sendAll(frame.data(), frame.size());
    }
    void sendPacket(std::uint8_t id, const WriteBuffer& payload) {
        std::lock_guard lk(tx_);
        if (!isOpen()) throw SocketClosedError("closed");
        WriteBuffer head;
        head.u8(id);
        writeFrameRaw2(head.data.data(), head.data.size(), payload.data.data(), payload.data.size());
    }
    void sendRawBody(const std::vector<std::uint8_t>& idAndBody) { // replay helper
        std::lock_guard lk(tx_);
        writeFrameRaw(idAndBody.data(), idAndBody.size());
    }

private:
    static constexpr std::uint32_t kMaxFrame = 4u * 1024 * 1024;

    void writeFrameRaw2(const std::uint8_t* a, std::size_t na,
                        const std::uint8_t* b, std::size_t nb) {
        std::vector<std::uint8_t> frame;
        WriteBuffer::writeVarintTo(frame, static_cast<std::int32_t>(na + nb));
        frame.insert(frame.end(), a, a + na);
        frame.insert(frame.end(), b, b + nb);
        sendAll(frame.data(), frame.size());
    }
    std::int32_t readVarintStream(int maxBytes) {
        std::int32_t result = 0; int shift = 0;
        for (int i = 0; i < maxBytes; ++i) {
            std::uint8_t b;
            readExact(&b, 1);
            result |= static_cast<std::int32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return result;
            shift += 7;
        }
        throw std::runtime_error("varint overflow in frame length");
    }
    void readExact(void* dst, std::size_t n) {
        auto* p = static_cast<std::uint8_t*>(dst);
        while (n > 0) {
            ssize_t r = ::recv(fd_, p, n, 0);
            if (r == 0) throw SocketClosedError("peer closed");
            if (r < 0) {
                if (errno == EINTR) continue;
                throw SocketClosedError(std::string("recv: ") + strerror(errno));
            }
            p += r; n -= static_cast<std::size_t>(r);
        }
    }
    void sendAll(const std::uint8_t* p, std::size_t n) {
        while (n > 0) {
            ssize_t r = ::send(fd_, p, n, MSG_NOSIGNAL);
            if (r < 0) {
                if (errno == EINTR) continue;
                throw SocketClosedError(std::string("send: ") + strerror(errno));
            }
            p += r; n -= static_cast<std::size_t>(r);
        }
    }

    int fd_;
    std::mutex tx_;   // serialize writes from multiple threads
};

} // namespace cppfm
