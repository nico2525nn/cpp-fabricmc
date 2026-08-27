// PacketDecoder: converts framed wire bytes to packet id + ByteBuffer payload.
// Handles VarInt length prefix, AES-CFB8 decryption and zlib decompression.
// Provides ByteBuffer conversion helpers.
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

    // Decode a raw outer frame (length varint already stripped) ? Actually frame_
    // is the content after outer length varint (and after decryption). If
    // compressionThreshold <0, frame is id+payload directly.
    // Otherwise frame = varint dataLength + (compressed|raw) body.
    static std::vector<std::uint8_t> decodeFrame(const std::vector<std::uint8_t>& frame,
                                                 int compressionThreshold) {
        if (compressionThreshold < 0) {
            return frame;
        }
        if (frame.empty()) throw std::runtime_error("empty frame");
        ReadBuffer in(frame);
        std::int32_t dataLen = in.varint();
        std::size_t left = in.remaining();
        if (dataLen == 0) {
            return std::vector<std::uint8_t>(in.p + in.off, in.p + in.off + left);
        }
        if (static_cast<std::uint32_t>(dataLen) > kMaxFrame)
            throw std::runtime_error("declared size out of range");
        std::vector<std::uint8_t> out;
        decompressRaw(in.p + in.off, left, static_cast<std::size_t>(dataLen), out);
        return out;
    }

    // Full outer decode: outer = varint(length) + frame (possibly encrypted).
    // If dec != nullptr, decrypts in-place before parsing.
    // Returns id+payload body.
    static std::vector<std::uint8_t> decodeOuter(std::vector<std::uint8_t> outer,
                                                  int compressionThreshold,
                                                  crypto::AesCfb8* dec = nullptr) {
        if (outer.empty()) throw std::runtime_error("empty outer");
        std::vector<std::uint8_t> work = std::move(outer);
        if (dec) dec->crypt(work.data(), work.size(), work.data());

        // parse outer varint length
        std::size_t off = 0;
        std::int32_t len = 0;
        int shift = 0;
        for (int i = 0; i < 5; ++i) {
            if (off >= work.size()) throw std::runtime_error("outer varint truncated");
            std::uint8_t b = work[off++];
            len |= static_cast<std::int32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
        }
        if (len < 0 || static_cast<std::size_t>(len) != work.size() - off)
            throw std::runtime_error("outer length mismatch");
        std::vector<std::uint8_t> frame(work.begin() + off, work.end());
        return decodeFrame(frame, compressionThreshold);
    }

    // Convenience: decode outer bytes given pointer/len (includes length varint)
    static std::vector<std::uint8_t> decodeOuter(const std::uint8_t* data, std::size_t n,
                                                  int compressionThreshold,
                                                  crypto::AesCfb8* dec = nullptr) {
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
        std::int32_t result = 0;
        int shift = 0;
        consumed = 0;
        for (std::size_t i = 0; i < n && i < 5; ++i) {
            std::uint8_t b = encBytes[i];
            // caller should have decrypted single byte before calling; we decrypt here if needed
            std::uint8_t decB = b;
            // Not decrypting here; assume already decrypted
            result |= static_cast<std::int32_t>(decB & 0x7F) << shift;
            consumed++;
            if (!(decB & 0x80)) return result;
            shift += 7;
        }
        throw std::runtime_error("varint overflow");
    }
};

} // namespace cppfm
