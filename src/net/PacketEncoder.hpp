// PacketEncoder: converts packet id + ByteBuffer payload to framed wire bytes.
// Handles VarInt length prefix, optional zlib compression and AES-CFB8 encryption
// mirroring the logic in Connection::sendFramed. Also provides ByteBuffer helpers.
#pragma once
#include <cstdint>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "../core/Zlib.hpp"
#include "Crypto.hpp"

namespace cppfm {

class PacketEncoder {
public:
    // Encode id + payload (WriteBuffer) into a length-prefixed frame.
    // If compressionThreshold >=0, compresses when total >= threshold.
    // If enc != nullptr, encrypts the outer buffer (length+frame) with AES-CFB8.
    static std::vector<std::uint8_t> encode(uint8_t id, const WriteBuffer& payload,
                                            int compressionThreshold = -1,
                                            crypto::AesCfb8* enc = nullptr) {
        return encodeRaw(&id, 1, payload.data.data(), payload.data.size(),
                         compressionThreshold, enc);
    }

    static std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& idAndPayload,
                                            int compressionThreshold = -1,
                                            crypto::AesCfb8* enc = nullptr) {
        if (idAndPayload.empty()) return {};
        return encodeRaw(idAndPayload.data(), idAndPayload.size(),
                         nullptr, 0, compressionThreshold, enc);
    }

    // Two-segment variant (id byte + payload) without extra copy when possible.
    static std::vector<std::uint8_t> encodeRaw(const std::uint8_t* a, std::size_t na,
                                               const std::uint8_t* b, std::size_t nb,
                                               int compressionThreshold,
                                               crypto::AesCfb8* enc) {
        const std::size_t total = na + nb;
        std::vector<std::uint8_t> frame;
        frame.reserve(total + 5);

        if (compressionThreshold >= 0) {
            if (total >= static_cast<std::size_t>(compressionThreshold)) {
                // dataLength = uncompressed size
                WriteBuffer::writeVarintTo(frame, static_cast<std::int32_t>(total));
                std::vector<std::uint8_t> comp;
                if (b && nb) {
                    std::vector<std::uint8_t> joined;
                    joined.reserve(total);
                    joined.insert(joined.end(), a, a + na);
                    joined.insert(joined.end(), b, b + nb);
                    compressRaw(joined.data(), joined.size(), comp);
                } else {
                    compressRaw(a, na, comp);
                }
                frame.insert(frame.end(), comp.begin(), comp.end());
            } else {
                frame.push_back(0); // dataLength 0 = not compressed
                frame.insert(frame.end(), a, a + na);
                if (b && nb) frame.insert(frame.end(), b, b + nb);
            }
        } else {
            frame.insert(frame.end(), a, a + na);
            if (b && nb) frame.insert(frame.end(), b, b + nb);
        }

        std::vector<std::uint8_t> outer;
        outer.reserve(frame.size() + 5);
        WriteBuffer::writeVarintTo(outer, static_cast<std::int32_t>(frame.size()));
        outer.insert(outer.end(), frame.begin(), frame.end());
        if (enc) enc->crypt(outer.data(), outer.size(), outer.data());
        return outer;
    }

    // ByteBuffer conversion helpers ------------------------------------------------
    // Convert id + WriteBuffer to raw idAndPayload vector (without framing).
    static std::vector<std::uint8_t> toBytes(std::uint8_t id, const WriteBuffer& payload) {
        std::vector<std::uint8_t> out;
        out.reserve(1 + payload.data.size());
        out.push_back(id);
        out.insert(out.end(), payload.data.begin(), payload.data.end());
        return out;
    }

    // Convert WriteBuffer payload to vector (raw bytes).
    static std::vector<std::uint8_t> payloadToBytes(const WriteBuffer& payload) {
        return payload.data;
    }

    // Wrap a WriteBuffer payload with an id byte into a WriteBuffer.
    static WriteBuffer wrap(std::uint8_t id, const WriteBuffer& payload) {
        WriteBuffer wb;
        wb.u8(id);
        wb.raw(payload.data.data(), payload.data.size());
        return wb;
    }

    // Build a WriteBuffer from id + raw bytes.
    static WriteBuffer fromRaw(std::uint8_t id, const std::uint8_t* data, std::size_t n) {
        WriteBuffer wb;
        wb.u8(id);
        if (n) wb.raw(data, n);
        return wb;
    }
};

} // namespace cppfm
