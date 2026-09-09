// Connection: blocking TCP client connection + packet framing.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include "../core/ByteBuffer.hpp"
#include "../core/Zlib.hpp"
#include "../platform/Socket.hpp"
#include "../net/Crypto.hpp"
#include "PacketEncoder.hpp"
#include "PacketDecoder.hpp"
#include "RateLimiter.hpp"
#include <chrono>

namespace cppfm {

class SocketClosedError : public std::runtime_error {
public:
    explicit SocketClosedError(const std::string& w, bool timeout = false)
        : std::runtime_error(w), timedOut(timeout) {}
    bool timedOut;
};

class Connection {
public:
    explicit Connection(platform::socket_t fd) : fd_(fd) {}

    // Protocol encryption (online mode): AES-128/CFB8, key = iv = shared secret.
    void enableEncryption(const std::vector<std::uint8_t>& sharedSecret) {
        encCtx_ = std::make_unique<crypto::AesCfb8>();
        decCtx_ = std::make_unique<crypto::AesCfb8>();
        encCtx_->initEncrypt(sharedSecret);
        decCtx_->initDecrypt(sharedSecret);
        encrypted_ = true;
    }
    ~Connection() noexcept { close(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    platform::socket_t fd() const { return fd_.load(std::memory_order_acquire); }
    bool isOpen() const { return platform::isValid(fd()); }

    void close() noexcept {
        try {
            std::lock_guard lk(tx_);
            const auto fd = fd_.exchange(platform::invalid_socket, std::memory_order_acq_rel);
            if (platform::isValid(fd)) {
                platform::shutdownSocket(fd);
                platform::closeSocket(fd);
            }
        } catch (...) {}
    }
    void abort() noexcept {
        try {
            std::lock_guard lk(tx_);
            const auto fd = fd_.exchange(platform::invalid_socket, std::memory_order_acq_rel);
            if (platform::isValid(fd)) {
                // FIN first (reliably delivered/retransmitted), then RST.
                platform::shutdownSocket(fd);
                struct linger l{};
                l.l_onoff = 1; l.l_linger = 0;
                platform::setSocketOption(fd, SOL_SOCKET, SO_LINGER, &l,
                                          static_cast<platform::socket_length_t>(sizeof(l)));
                platform::closeSocket(fd);
            }
        } catch (...) {}
    }
    void setNoDelay() {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return;
        int one = 1;
        (void)platform::setSocketOption(fd, IPPROTO_TCP, TCP_NODELAY, &one,
                                        static_cast<platform::socket_length_t>(sizeof(one)));
    }
    // A send that cannot complete within this many seconds means the peer went
    // away without closing (or is maliciously stalling us); fail the session.
    void setSendTimeout(unsigned seconds) {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return;
        (void)platform::setSocketTimeout(fd, SO_SNDTIMEO, seconds);
    }
    void setRecvTimeout(unsigned seconds) {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return;
        (void)platform::setSocketTimeout(fd, SO_RCVTIMEO, seconds);
    }
    void enableFloodBudget(bool on) { floodBudget_ = on; }
    int peekFirstByte(int timeoutMs) const {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return -1;
        const int r = platform::waitReadable(fd, timeoutMs);
        if (r <= 0) return -1;
        std::uint8_t b = 0;
        const auto n = platform::receive(fd, &b, 1, MSG_PEEK);
        if (n != 1) return -1;
        return static_cast<int>(b);
    }
    // length-prefix/compression/encryption — pre-1.7 clients speak no framing).
    void sendRaw(const std::uint8_t* d, std::size_t n) {
        if (n != 0 && d == nullptr) throw std::invalid_argument("null send buffer");
        std::lock_guard lk(tx_);
        sendAll(d, n);
    }
    bool trySendRaw(const std::uint8_t* data, std::size_t size) noexcept {
        try {
            sendRaw(data, size);
            return true;
        } catch (...) {
            return false;
        }
    }
    std::uint16_t peerPort() const {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return 0;
        sockaddr_in addr{};
        platform::socket_length_t sl = sizeof(addr);
        if (platform::peerName(fd, reinterpret_cast<sockaddr*>(&addr), &sl) != 0) return 0;
        return ntohs(addr.sin_port);
    }
    std::string peer() const {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return "?";
        sockaddr_in addr{};
        platform::socket_length_t sl = sizeof(addr);
        if (platform::peerName(fd, reinterpret_cast<sockaddr*>(&addr), &sl) != 0) return "?";
        char buf[64];
        inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    // ---- framed io -------------------------------------------------------
    void setCompression(int threshold) { compressionThreshold_ = threshold; }

    // Reads one frame payload (length-prefixed, optionally compressed). Returns the packet body (packet id + payload). Throws
    // SocketClosedError on EOF. Delegates decompression to PacketDecoder for ByteBuffer handling.
    std::vector<std::uint8_t> readFrame() {
        std::int32_t len = 0;
        if (!encrypted_) {
            len = readVarintStream(5);
        } else {
            // varint bytes are encrypted: read/decrypt one at a time
            std::uint32_t ulen = 0;
            for (int i = 0; i < 5; ++i) {
                std::uint8_t e[1];
                readExact(e, 1);
                decCtx_->crypt(e, 1, e);
                if (i == 4 && (e[0] & 0xF0u) != 0)
                    throw std::runtime_error("varint overflow");
                ulen |= static_cast<std::uint32_t>(e[0] & 0x7F) << (i * 7);
                if ((e[0] & 0x80u) == 0) {
                    len = static_cast<std::int32_t>(ulen);
                    break;
                }
                if (i == 4) throw std::runtime_error("varint overflow");
            }
        }
        if (len <= 0 || static_cast<std::uint32_t>(len) > kMaxFrame)
            throw PacketDecoder::OversizeError(
                "frame length out of range: " + std::to_string(len));
        if (floodBudget_ &&
            !bw_.consume(static_cast<double>(len), steadyNowMs()))
            throw PacketDecoder::OversizeError("connection bandwidth budget exceeded");
        frame_.resize(static_cast<std::size_t>(len));
        readExact(frame_.data(), frame_.size());
        if (encrypted_) decCtx_->crypt(frame_.data(), frame_.size(), frame_.data());
        if (compressionThreshold_ < 0) return frame_;
        // Delegate to PacketDecoder for ByteBuffer conversion + decompression
        return PacketDecoder::decodeFrame(frame_, compressionThreshold_);
    }
    std::vector<std::uint8_t> readFrameWithTimeout(std::chrono::milliseconds timeout) {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) throw SocketClosedError("closed");
        for (;;) {
            const int r = platform::waitReadable(fd, static_cast<int>(timeout.count()));
            if (r > 0) return readFrame();
            if (r == 0) throw SocketClosedError("read timeout", true);
            const int error = platform::lastSocketError();
            if (platform::isInterrupted(error)) continue;
            throw SocketClosedError("poll: " + platform::socketErrorText(error));
        }
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
    // Best-effort notifications must not obscure the required send path with
    // repeated catch-all blocks.  Handshake/state transitions use sendPacket
    // directly and still surface transport errors to their owner.
    bool trySendPacket(std::uint8_t id, const WriteBuffer& payload) noexcept {
        try {
            sendPacket(id, payload);
            return true;
        } catch (...) {
            return false;
        }
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
    bool floodBudget_ = false;
    RateLimiter bw_;

    std::int32_t readVarintStream(int maxBytes) {
        if (maxBytes <= 0 || maxBytes > 5) throw std::invalid_argument("invalid VarInt limit");
        std::uint32_t result = 0;
        for (int i = 0; i < maxBytes; ++i) {
            std::uint8_t b;
            readExact(&b, 1);
            if (i == 4 && (b & 0xF0u) != 0)
                throw std::runtime_error("varint overflow in frame length");
            result |= static_cast<std::uint32_t>(b & 0x7F) << (i * 7);
            if ((b & 0x80u) == 0) return static_cast<std::int32_t>(result);
        }
        throw std::runtime_error("varint overflow in frame length");
    }
    void readExact(void* dst, std::size_t n) {
        if (n != 0 && dst == nullptr) throw std::invalid_argument("null receive buffer");
        auto* p = static_cast<std::uint8_t*>(dst);
        while (n > 0) {
            const auto fd = fd_.load(std::memory_order_acquire);
            if (!platform::isValid(fd)) throw SocketClosedError("closed");
            const auto r = platform::receive(fd, p, n, 0);
            if (r == 0) throw SocketClosedError("peer closed");
            if (r < 0) {
                const int error = platform::lastSocketError();
                if (platform::isInterrupted(error)) continue;
                throw SocketClosedError("recv: " + platform::socketErrorText(error),
                                        platform::isWouldBlock(error));
            }
            p += r; n -= static_cast<std::size_t>(r);
        }
    }
    void sendAll(const std::uint8_t* p, std::size_t n) {
        if (n != 0 && p == nullptr) throw std::invalid_argument("null send buffer");
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) throw SocketClosedError("closed");
        while (n > 0) {
            const auto r = platform::send(fd, p, n, platform::sendFlags());
            if (r < 0) {
                const int error = platform::lastSocketError();
                if (platform::isInterrupted(error)) continue;
                throw SocketClosedError("send: " + platform::socketErrorText(error));
            }
            if (r == 0) throw SocketClosedError("send made no progress");
            p += r; n -= static_cast<std::size_t>(r);
        }
    }

    std::atomic<platform::socket_t> fd_;
    std::mutex tx_;   // serialize writes from multiple threads
};

} // namespace cppfm
