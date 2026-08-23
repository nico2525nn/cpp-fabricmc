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
#include "../core/Zlib.hpp"

namespace cppfm {

class SocketClosedError : public std::runtime_error {
public:
    explicit SocketClosedError(const std::string& w) : std::runtime_error(w) {}
};

class Connection {
public:
    explicit Connection(int fd) : fd_(fd), compressionThreshold_(-1) {}
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
    void setCompression(int threshold) { compressionThreshold_ = threshold; }

    // Reads one frame payload (length-prefixed, optionally compressed).
    // Returns the packet body (packet id + payload). Throws SocketClosedError on EOF.
    std::vector<std::uint8_t> readFrame() {
        const std::int32_t len = readVarintStream(5);
        if (len <= 0 || static_cast<std::uint32_t>(len) > kMaxFrame)
            throw std::runtime_error("frame length out of range: " + std::to_string(len));
        frame_.resize(static_cast<std::size_t>(len));
        readExact(frame_.data(), frame_.size());
        if (compressionThreshold_ < 0) return frame_;
        // compressed layout: varint dataLength + (compressed | raw) body
        ReadBuffer in(frame_);
        const std::int32_t dataLen = in.varint();
        const std::size_t left = in.remaining();
        if (dataLen == 0) {
            return std::vector<std::uint8_t>(in.p + in.off, in.p + in.off + left);
        }
        if (static_cast<std::uint32_t>(dataLen) > kMaxFrame)
            throw std::runtime_error("declared size out of range");
        std::vector<std::uint8_t> out;
        decompressRaw(in.p + in.off, left, static_cast<std::size_t>(dataLen), out);
        return out;
    }
    void writeFrameRaw(const std::uint8_t* body, std::size_t n) {
        sendFramed(body, n);
    }
    // Single compression-aware framed writer used by every send path.
    void sendFramed(const std::uint8_t* a, std::size_t na,
                    const std::uint8_t* b = nullptr, std::size_t nb = 0) {
        std::lock_guard lk(tx_);
        if (!isOpen()) throw SocketClosedError("closed");
        const std::size_t total = na + nb;
        std::vector<std::uint8_t> frame;
        if (compressionThreshold_ >= 0) {
            if (total >= static_cast<std::size_t>(compressionThreshold_)) {
                WriteBuffer::writeVarintTo(frame, static_cast<std::int32_t>(total));
                std::vector<std::uint8_t> joined;
                const std::uint8_t* src; std::size_t slen;
                if (nb) {                            // rare: join segments
                    joined.reserve(total);
                    joined.insert(joined.end(), a, a + na);
                    joined.insert(joined.end(), b, b + nb);
                    src = joined.data(); slen = total;
                } else { src = a; slen = na; }
                std::vector<std::uint8_t> comp;
                compressRaw(src, slen, comp);        // zlib format
                frame.insert(frame.end(), comp.begin(), comp.end());
            } else {
                frame.push_back(0);                  // dataLength 0: stored raw
                frame.insert(frame.end(), a, a + na);
                if (nb) frame.insert(frame.end(), b, b + nb);
            }
        } else {
            frame.insert(frame.end(), a, a + na);
            if (nb) frame.insert(frame.end(), b, b + nb);
        }
        std::vector<std::uint8_t> outer;
        WriteBuffer::writeVarintTo(outer, static_cast<std::int32_t>(frame.size()));
        outer.insert(outer.end(), frame.begin(), frame.end());
        sendAll(outer.data(), outer.size());
    }
    void sendPacketBuf(std::uint8_t id, const std::vector<std::uint8_t>& payload) {
        sendFramed(&id, 1, payload.data(), payload.size());
    }
    void sendPacket(std::uint8_t id, const WriteBuffer& payload) {
        sendPacketBuf(id, payload.data);
    }
    void sendRawBody(const std::vector<std::uint8_t>& idAndBody) { // replay helper
        sendFramed(idAndBody.data(), idAndBody.size());
    }

private:
    static constexpr std::uint32_t kMaxFrame = 8u * 1024 * 1024;
    int compressionThreshold_;
    std::vector<std::uint8_t> frame_;

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
