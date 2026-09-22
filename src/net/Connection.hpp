// Connection: blocking TCP client connection + packet framing.
#pragma once
#include <cstdint>
#include <limits>
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
#include <condition_variable>
#include <deque>
#include <thread>
#include <utility>

namespace cppfm {

// Sends from a simulation transition are encoded and handed to a dedicated
// writer thread.  The transition never releases its state gate around socket
// I/O, so queued output cannot expose a partially committed mutation.
inline thread_local unsigned activeSimulationDispatchDepth = 0;

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
        std::lock_guard lock(tx_);
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
        // Preserve a short disconnect/configuration tail, but bound teardown
        // so a stalled client cannot hold the simulation caller indefinitely.
        stopWriter(true, false);
        try {
            std::lock_guard lk(tx_);
            closeFd(false);
        } catch (...) {}
    }
    void abort() noexcept {
        stopWriter(false, true);
        try {
            std::lock_guard lk(tx_);
            closeFd(true);
        } catch (...) {}
    }
    void setNoDelay() {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return;
        int one = 1;
        (void)platform::setSocketOption(fd, IPPROTO_TCP, TCP_NODELAY, &one,
                                        static_cast<platform::socket_length_t>(sizeof(one)));
    }
    void setSendTimeoutMs(unsigned milliseconds) {
        const auto fd = fd_.load(std::memory_order_acquire);
        if (!platform::isValid(fd)) return;
        (void)platform::setSocketTimeoutMs(fd, SO_SNDTIMEO, milliseconds);
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
    void setCompression(int threshold) {
        if (threshold < -1)
            throw std::invalid_argument("compression threshold must be -1 or non-negative");
        compressionThreshold_ = threshold;
    }

    // Reads one frame payload (length-prefixed, optionally compressed). Returns the packet body (packet id + payload). Throws
    // SocketClosedError on EOF. Delegates decompression to PacketDecoder for ByteBuffer handling.
    std::vector<std::uint8_t> readFrame() {
        return readFrameUntil(nullptr);
    }
    std::vector<std::uint8_t> readFrameWithTimeout(std::chrono::milliseconds timeout) {
        if (timeout.count() < 0)
            throw std::invalid_argument("negative frame timeout");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        return readFrameUntil(&deadline);
    }

private:
    std::vector<std::uint8_t> readFrameUntil(
        const std::chrono::steady_clock::time_point* deadline) {
        std::int32_t len = 0;
        if (!encrypted_) {
            len = readVarintStream(5, deadline);
        } else {
            // varint bytes are encrypted: read/decrypt one at a time
            std::uint32_t ulen = 0;
            for (int i = 0; i < 5; ++i) {
                std::uint8_t e[1];
                readExact(e, 1, deadline);
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
        readExact(frame_.data(), frame_.size(), deadline);
        if (encrypted_) decCtx_->crypt(frame_.data(), frame_.size(), frame_.data());
        if (compressionThreshold_ < 0) return frame_;
        // Delegate to PacketDecoder for ByteBuffer conversion + decompression
        return PacketDecoder::decodeFrame(frame_, compressionThreshold_);
    }
public:
    // Single compression-aware framed writer used by every send path.
    // Delegates to PacketEncoder for ByteBuffer + compression + encryption handling.
    void sendFramed(const std::uint8_t* a, std::size_t na,
                    const std::uint8_t* b = nullptr, std::size_t nb = 0,
                    bool lowPriority = false) {
        std::unique_lock txLock(tx_);
        if (!isOpen()) throw SocketClosedError("closed");
        auto outer = PacketEncoder::encodeRaw(a, na, b, nb,
                                              compressionThreshold_,
                                              encrypted_ ? encCtx_.get() : nullptr);
        // AES-CFB8 is stateful across frames.  Encrypted frames must remain
        // in one FIFO; reordering already-encrypted bytes corrupts the cipher
        // stream even though each frame was encoded under tx_.
        if (encrypted_) lowPriority = false;
        bool queue = false;
        {
            // Serialize the first writer assignment with stopWriter's join and
            // reset; queueMtx_ alone does not protect std::thread itself.
            std::lock_guard lifecycleLock(lifecycleMtx_);
            std::lock_guard queueLock(queueMtx_);
            queue = writerStarted_ || activeSimulationDispatchDepth != 0;
            if (queue) enqueueLocked(std::move(outer), lowPriority);
        }
        if (!queue) sendAll(outer.data(), outer.size());
    }
    void sendPacketBuf(std::uint8_t id, const std::vector<std::uint8_t>& payload) {
        sendFramed(&id, 1, payload.data(), payload.size());
    }
    void sendPacketBufLowPriority(std::uint8_t id, const std::vector<std::uint8_t>& payload) {
        sendFramed(&id, 1, payload.data(), payload.size(), true);
    }
    void sendPacket(std::uint8_t id, const WriteBuffer& payload) {
        sendPacketBuf(id, payload.data);
    }
    void sendPacketLowPriority(std::uint8_t id, const WriteBuffer& payload) {
        sendFramed(&id, 1, payload.data.data(), payload.data.size(), true);
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
    bool trySendPacketLowPriority(std::uint8_t id, const WriteBuffer& payload) noexcept {
        try {
            sendPacketLowPriority(id, payload);
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
    // A stalled client gets a bounded per-connection queue; exceeding it
    // closes that client rather than multiplying memory by max-players.
    static constexpr std::size_t kMaxQueuedBytes = 4u * 1024u * 1024u;
    std::vector<std::uint8_t> frame_;
    bool encrypted_ = false;
    std::unique_ptr<crypto::AesCfb8> encCtx_, decCtx_;
    int compressionThreshold_ = -1;
    bool floodBudget_ = false;
    RateLimiter bw_;

    std::int32_t readVarintStream(int maxBytes,
                                  const std::chrono::steady_clock::time_point* deadline = nullptr) {
        if (maxBytes <= 0 || maxBytes > 5) throw std::invalid_argument("invalid VarInt limit");
        std::uint32_t result = 0;
        for (int i = 0; i < maxBytes; ++i) {
            std::uint8_t b;
            readExact(&b, 1, deadline);
            if (i == 4 && (b & 0xF0u) != 0)
                throw std::runtime_error("varint overflow in frame length");
            result |= static_cast<std::uint32_t>(b & 0x7F) << (i * 7);
            if ((b & 0x80u) == 0) return static_cast<std::int32_t>(result);
        }
        throw std::runtime_error("varint overflow in frame length");
    }
    void readExact(void* dst, std::size_t n,
                   const std::chrono::steady_clock::time_point* deadline = nullptr) {
        if (n != 0 && dst == nullptr) throw std::invalid_argument("null receive buffer");
        auto* p = static_cast<std::uint8_t*>(dst);
        while (n > 0) {
            const auto fd = fd_.load(std::memory_order_acquire);
            if (!platform::isValid(fd)) throw SocketClosedError("closed");
            if (deadline) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= *deadline)
                    throw SocketClosedError("read timeout", true);
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    *deadline - now);
                const int waitMs = static_cast<int>(std::min<std::int64_t>(
                    std::max<std::int64_t>(1, remaining.count()),
                    std::numeric_limits<int>::max()));
                for (;;) {
                    const int ready = platform::waitReadable(fd, waitMs);
                    if (ready > 0) break;
                    if (ready == 0)
                        throw SocketClosedError("read timeout", true);
                    const int error = platform::lastSocketError();
                    if (platform::isInterrupted(error)) continue;
                    throw SocketClosedError("poll: " + platform::socketErrorText(error));
                }
            }
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
    void enqueueLocked(std::vector<std::uint8_t>&& frame, bool lowPriority) {
        if (!isOpen() || writerStop_) throw SocketClosedError("closed");
        if (frame.size() > kMaxQueuedBytes || queuedBytes_ > kMaxQueuedBytes - frame.size()) {
            writerStop_ = true;
            queuedFrames_.clear();
            queuedLowFrames_.clear();
            queuedBytes_ = 0;
            closeFd(true);
            queueCv_.notify_all();
            throw SocketClosedError("connection output queue full", true);
        }
        if (!writerStarted_) {
            writerStop_ = false;
            writerStarted_ = true;
            try {
                writer_ = std::thread([this] { writerLoop(); });
            } catch (...) {
                writerStarted_ = false;
                throw;
            }
        }
        queuedBytes_ += frame.size();
        if (lowPriority) queuedLowFrames_.push_back(std::move(frame));
        else queuedFrames_.push_back(std::move(frame));
        queueCv_.notify_one();
    }

    void writerLoop() noexcept {
        for (;;) {
            std::vector<std::uint8_t> frame;
            {
                std::unique_lock lock(queueMtx_);
                queueCv_.wait(lock, [this] {
                    return writerStop_ || !queuedFrames_.empty() || !queuedLowFrames_.empty();
                });
                if (queuedFrames_.empty() && queuedLowFrames_.empty()) {
                    if (writerStop_) break;
                    continue;
                }
                if (!queuedFrames_.empty()) {
                    frame = std::move(queuedFrames_.front());
                    queuedFrames_.pop_front();
                } else {
                    frame = std::move(queuedLowFrames_.front());
                    queuedLowFrames_.pop_front();
                }
                queuedBytes_ -= frame.size();
                writerBusy_ = true;
                queueCv_.notify_all();
            }
            try {
                sendAll(frame.data(), frame.size());
                std::lock_guard lock(queueMtx_);
                writerBusy_ = false;
                queueCv_.notify_all();
            } catch (...) {
                const auto fd = fd_.exchange(platform::invalid_socket,
                                             std::memory_order_acq_rel);
                if (platform::isValid(fd)) {
                    platform::shutdownSocket(fd);
                    platform::closeSocket(fd);
                }
                std::lock_guard lock(queueMtx_);
                writerStop_ = true;
                writerBusy_ = false;
                queuedFrames_.clear();
                queuedLowFrames_.clear();
                queuedBytes_ = 0;
                queueCv_.notify_all();
                break;
            }
        }
    }

    void stopWriter(bool drain, bool abortive) noexcept {
        // close()/abort() may race with session teardown and server shutdown;
        // serialize ownership of the std::thread join/reset sequence.
        std::lock_guard lifecycleLock(lifecycleMtx_);
        {
            std::lock_guard lock(queueMtx_);
            if (!writerStarted_ && !writer_.joinable()) return;
            writerStop_ = true;
            if (!drain) {
                queuedFrames_.clear();
                queuedLowFrames_.clear();
                queuedBytes_ = 0;
            }
        }
        queueCv_.notify_all();
        if (drain) {
            std::unique_lock lock(queueMtx_);
            if (!queueCv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                    return queuedFrames_.empty() && queuedLowFrames_.empty() && !writerBusy_;
                })) {
                lock.unlock();
                closeFd(abortive);
                lock.lock();
                queuedFrames_.clear();
                queuedLowFrames_.clear();
                queuedBytes_ = 0;
            }
        } else {
            // Wake a blocked send before joining the writer.
            closeFd(abortive);
        }
        queueCv_.notify_all();
        if (writer_.joinable() && writer_.get_id() != std::this_thread::get_id())
            writer_.join();
        std::lock_guard lock(queueMtx_);
        writerStarted_ = false;
        writerStop_ = false;
        queuedFrames_.clear();
        queuedLowFrames_.clear();
        queuedBytes_ = 0;
    }

    void closeFd(bool abortive) noexcept {
        const auto fd = fd_.exchange(platform::invalid_socket,
                                     std::memory_order_acq_rel);
        if (!platform::isValid(fd)) return;
        platform::shutdownSocket(fd);
        if (abortive) {
            struct linger l{};
            l.l_onoff = 1; l.l_linger = 0;
            platform::setSocketOption(fd, SOL_SOCKET, SO_LINGER, &l,
                                      static_cast<platform::socket_length_t>(sizeof(l)));
        }
        platform::closeSocket(fd);
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
    std::mutex lifecycleMtx_;
    std::mutex tx_;   // serialize framing/encryption state
    std::mutex queueMtx_;
    std::condition_variable queueCv_;
    std::deque<std::vector<std::uint8_t>> queuedFrames_;
    std::deque<std::vector<std::uint8_t>> queuedLowFrames_;
    std::size_t queuedBytes_ = 0;
    bool writerStarted_ = false;
    bool writerStop_ = false;
    bool writerBusy_ = false;
    std::thread writer_;
};

} // namespace cppfm
