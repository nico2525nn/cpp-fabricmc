// PacketDecoder: converts framed wire bytes to packet id + ByteBuffer payload. Handles VarInt length prefix, AES-CFB8 decryption and zlib
// decompression. Provides ByteBuffer conversion helpers.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include "../core/ByteBuffer.hpp"
#include "../core/Zlib.hpp"
#include "Crypto.hpp"

namespace cppfm {

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
        if (compressionThreshold < -1)
            throw std::invalid_argument("compression threshold must be -1 or non-negative");
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
            if (left == 0)
                throw std::runtime_error("empty packet frame");
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

    // Decrypt helper for streaming varint (mirrors Connection::readFrame encrypted varint)
    static std::int32_t readVarintEncrypted(const std::uint8_t* encBytes, std::size_t n,
                                            crypto::AesCfb8& dec, std::size_t& consumed) {
        if (n == 0 || encBytes == nullptr)
            throw std::invalid_argument("encrypted VarInt buffer is empty");
        std::vector<std::uint8_t> plain;
        plain.reserve(std::min<std::size_t>(n, 5));
        for (std::size_t i = 0; i < n && i < 5; ++i) {
            std::uint8_t byte = encBytes[i];
            dec.crypt(&byte, 1, &byte);
            plain.push_back(byte);
            if ((byte & 0x80u) == 0) {
                ReadBuffer in(plain);
                const auto result = in.varint();
                consumed = i + 1;
                return result;
            }
        }
        consumed = plain.size();
        throw std::runtime_error("encrypted VarInt too large");
    }
};

} // namespace cppfm
