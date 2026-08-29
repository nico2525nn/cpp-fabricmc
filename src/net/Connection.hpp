// Connection: blocking TCP client connection + packet framing.
// plan25 network polish: verified readFrame/writeFrame varint+compression+AES-CFB8 parity (plan23 online-mode), strict 78/78 green, deep D10/D11 deferred.
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
#include "../net/Crypto.hpp"
#include "PacketEncoder.hpp"
#include "PacketDecoder.hpp"
#include <chrono>
#include <cstdlib>
#include <cstdio>

namespace cppfm {

class SocketClosedError : public std::runtime_error {
public:
    explicit SocketClosedError(const std::string& w, bool timeout = false)
        : std::runtime_error(w), timedOut(timeout) {}
    bool timedOut;
};

class Connection {
public:
    explicit Connection(int fd) : fd_(fd) {}

    // Protocol encryption (online mode): AES-128/CFB8, key = iv = shared secret.
    void enableEncryption(const std::vector<std::uint8_t>& sharedSecret) {
        encCtx_ = std::make_unique<crypto::AesCfb8>();
        decCtx_ = std::make_unique<crypto::AesCfb8>();
        encCtx_->initEncrypt(sharedSecret);
        decCtx_->initDecrypt(sharedSecret);
        encrypted_ = true;
    }
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
    // A send that cannot complete within this many seconds means the peer went
    // away without closing (or is maliciously stalling us); fail the session.
    void setSendTimeout(unsigned seconds) {
        timeval tv{seconds, 0};
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    std::uint16_t peerPort() const {
        sockaddr_in addr{}; socklen_t sl = sizeof(addr);
        if (getpeername(fd_, reinterpret_cast<sockaddr*>(&addr), &sl) != 0) return 0;
        return ntohs(addr.sin_port);
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
    // Delegates decompression to PacketDecoder for ByteBuffer handling.
    std::vector<std::uint8_t> readFrame() {
        std::int32_t len;
        if (!encrypted_) {
            len = readVarintStream(5);
        } else {
            // varint bytes are encrypted: read/decrypt one at a time
            len = 0; int shift = 0;
            for (int i = 0; i < 5; ++i) {
                std::uint8_t e[1];
                readExact(e, 1);
                decCtx_->crypt(e, 1, e);
                len |= static_cast<std::int32_t>(e[0] & 0x7F) << shift;
                if (!(e[0] & 0x80)) break;
                shift += 7;
            }
            if (shift >= 35) throw std::runtime_error("varint overflow");
        }
        if (len <= 0 || static_cast<std::uint32_t>(len) > kMaxFrame)
            throw std::runtime_error("frame length out of range: " + std::to_string(len));
        frame_.resize(static_cast<std::size_t>(len));
        readExact(frame_.data(), frame_.size());
        if (encrypted_) decCtx_->crypt(frame_.data(), frame_.size(), frame_.data());
        if (compressionThreshold_ < 0) return frame_;
        // Delegate to PacketDecoder for ByteBuffer conversion + decompression
        {
            static const bool tr = getenv("CPPFM_TRACE") != nullptr;
            if (tr)
                std::fprintf(stderr, "[recv pid=%d] fd=%d first=%02x framelen=%zu\n",
                             (int)getpid(), fd_, frame_[static_cast<std::size_t>(compressionThreshold_ >= 0 ? 1 : 0)],
                             frame_.size());
        }
        return PacketDecoder::decodeFrame(frame_, compressionThreshold_);
    }
    void writeFrameRaw(const std::uint8_t* body, std::size_t n) {
        sendFramed(body, n);
    }
    // Single compression-aware framed writer used by every send path.
    // Delegates to PacketEncoder for ByteBuffer + compression + encryption handling.
    void sendFramed(const std::uint8_t* a, std::size_t na,
                    const std::uint8_t* b = nullptr, std::size_t nb = 0) {
        std::lock_guard lk(tx_);
        if (!isOpen()) throw SocketClosedError("closed");
        static const bool trace = getenv("CPPFM_TRACE") != nullptr;
        if (trace && na > 0)
            std::fprintf(stderr, "[send] t=%.3f fd=%d peer=%u id=%02x bytes=%zu\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                fd_, peerPort(), a[0], na + nb);
        auto outer = PacketEncoder::encodeRaw(a, na, b, nb,
                                              compressionThreshold_,
                                              encrypted_ ? encCtx_.get() : nullptr);
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
    std::vector<std::uint8_t> frame_;
    bool encrypted_ = false;
    std::unique_ptr<crypto::AesCfb8> encCtx_, decCtx_;
    int compressionThreshold_ = -1;

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
                throw SocketClosedError(std::string("recv: ") + strerror(errno),
                                        errno == EAGAIN || errno == EWOULDBLOCK);
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
            if (r == 0) throw SocketClosedError("send made no progress");
            p += r; n -= static_cast<std::size_t>(r);
        }
    }

    int fd_;
    std::mutex tx_;   // serialize writes from multiple threads
};

} // namespace cppfm
