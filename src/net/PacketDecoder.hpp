// PacketDecoder: converts framed wire bytes to packet id + ByteBuffer payload. Handles VarInt length prefix, AES-CFB8 decryption and zlib
// decompression. Provides ByteBuffer conversion helpers.
#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
#include "../core/ByteBuffer.hpp"
#include "../core/Zlib.hpp"
#include "Crypto.hpp"

namespace cppfm {

struct DecodedPacket {
    std::uint8_t id = 0;
    std::vector<std::uint8_t> payload; // bytes after the id

    ReadBuffer reader() const {
        return ReadBuffer(payload.data(), payload.size());
    }
    ReadBuffer readerWithId(std::vector<std::uint8_t>& tmp) const {
        tmp.clear();
        tmp.push_back(id);
        tmp.insert(tmp.end(), payload.begin(), payload.end());
        return ReadBuffer(tmp.data(), tmp.size());
    }
};

class PacketDecoder {
public:
    static constexpr std::uint32_t kMaxFrame = 8u * 1024 * 1024;
    static constexpr std::uint32_t kMaxDeclared = 2u * 1024 * 1024;

    struct OversizeError : std::runtime_error {
        explicit OversizeError(const std::string& w) : std::runtime_error(w) {}
    };

    // Decode a raw outer frame (length varint already stripped) ? Actually frame_ is the content after outer length varint (and after
    // decryption). If compressionThreshold <0, frame is id+payload directly. Otherwise frame = varint dataLength + (compressed|raw) body.
    static std::vector<std::uint8_t> decodeFrame(const std::vector<std::uint8_t>& frame,
                                                 int compressionThreshold) {
        if (frame.size() > kMaxFrame)
            throw OversizeError("frame exceeds 8MB budget");
        if (compressionThreshold < 0) {
            return frame;
        }
        if (frame.empty()) throw std::runtime_error("empty frame");
        ReadBuffer in(frame);
        std::int32_t dataLen = in.varint();
        std::size_t left = in.remaining();
        if (dataLen == 0) {
            if (compressionThreshold == 0)
                throw std::runtime_error("uncompressed frame with threshold=0");
            if (compressionThreshold > 0 && left >= static_cast<std::size_t>(compressionThreshold))
                throw std::runtime_error("uncompressed frame is at or above compression threshold");
            return in.bytes(left);
        }
        if (dataLen < 0)
            throw std::runtime_error("negative declared size");
        if (compressionThreshold > 0 &&
            static_cast<std::uint32_t>(dataLen) <
                static_cast<std::uint32_t>(compressionThreshold))
            throw std::runtime_error("forged dataLength below threshold");
        if (static_cast<std::uint32_t>(dataLen) > kMaxDeclared)
            throw OversizeError("declared size exceeds 2MB budget");
        if (static_cast<std::uint32_t>(dataLen) > kMaxFrame)
            throw OversizeError("declared size out of range");
        std::vector<std::uint8_t> out;
        decompressChecked(in.p + in.off, left, static_cast<std::size_t>(dataLen), out);
        in.skipRest();
        return out;
    }

    // Full outer decode: outer = varint(length) + frame (possibly encrypted). If dec != nullptr, decrypts in-place before parsing. Returns
    // id+payload body.
    static std::vector<std::uint8_t> decodeOuter(std::vector<std::uint8_t> outer,
                                                  int compressionThreshold,
                                                  crypto::AesCfb8* dec = nullptr) {
        if (outer.empty()) throw std::runtime_error("empty outer");
        std::vector<std::uint8_t> work = std::move(outer);
        if (dec) dec->crypt(work.data(), work.size(), work.data());

        ReadBuffer in(work);
        const std::int32_t len = in.varint();
        if (len <= 0 || static_cast<std::uint32_t>(len) > kMaxFrame)
            throw OversizeError("outer frame length out of range");
        if (static_cast<std::size_t>(len) != in.remaining())
            throw std::runtime_error("outer length mismatch");
        std::vector<std::uint8_t> frame = in.bytes(static_cast<std::size_t>(len));
        return decodeFrame(frame, compressionThreshold);
    }

    // Convenience: decode outer bytes given pointer/len (includes length varint)
    static std::vector<std::uint8_t> decodeOuter(const std::uint8_t* data, std::size_t n,
                                                  int compressionThreshold,
                                                  crypto::AesCfb8* dec = nullptr) {
        if (n == 0) throw std::runtime_error("empty outer");
        if (n != 0 && data == nullptr) throw std::invalid_argument("null outer buffer");
        std::vector<std::uint8_t> outer(data, data + n);
        return decodeOuter(std::move(outer), compressionThreshold, dec);
    }

    // ByteBuffer conversion: split id+payload body into DecodedPacket
    static DecodedPacket toPacket(const std::vector<std::uint8_t>& body) {
        if (body.empty()) throw std::runtime_error("empty packet body");
        DecodedPacket p;
        p.id = body[0];
        if (body.size() > 1)
            p.payload.assign(body.begin() + 1, body.end());
        return p;
    }

    static DecodedPacket toPacket(std::vector<std::uint8_t>&& body) {
        if (body.empty()) throw std::runtime_error("empty packet body");
        DecodedPacket p;
        p.id = body[0];
        if (body.size() > 1) {
            p.payload.assign(std::make_move_iterator(body.begin() + 1),
                             std::make_move_iterator(body.end()));
        }
        return p;
    }

    // Decode directly from a ReadBuffer that holds id+payload body.
    static DecodedPacket fromReadBuffer(ReadBuffer& in, std::size_t bodyLen) {
        if (bodyLen == 0) throw std::runtime_error("empty body");
        in.need(bodyLen);
        DecodedPacket p;
        p.id = in.u8();
        std::size_t left = bodyLen - 1;
        if (left) p.payload = in.bytes(left);
        return p;
    }

    // Helper to get a ReadBuffer view of id+payload body for handler dispatch
    static ReadBuffer asReadBuffer(const std::vector<std::uint8_t>& body) {
        return ReadBuffer(body.data(), body.size());
    }

    // Decrypt helper for streaming varint (mirrors Connection::readFrame encrypted varint)
    static std::int32_t readVarintEncrypted(const std::uint8_t* encBytes, std::size_t n,
                                            crypto::AesCfb8& dec, std::size_t& consumed) {
        (void)dec; // The streaming caller decrypts each byte before this helper.
        ReadBuffer in(encBytes, n);
        const std::int32_t result = in.varint();
        consumed = in.off;
        return result;
    }
};

} // namespace cppfm
