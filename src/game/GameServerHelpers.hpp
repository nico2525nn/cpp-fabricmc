#pragma once
// Shared inline server helpers.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <chrono>
#include <openssl/evp.h>
#include "../core/ByteBuffer.hpp"
#include "../generated/BlockStates.hpp"

namespace cppfm {
extern std::atomic<bool> g_stopRequested;

inline std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Deterministic pack UUID for AddResourcePack 0x09
inline std::array<std::uint8_t,16> packUuidFromUrl(const std::string& url) {
    std::array<std::uint8_t,16> out{};
    unsigned char md[20];
    unsigned int ml = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
        EVP_DigestUpdate(ctx, url.data(), url.size());
        EVP_DigestFinal_ex(ctx, md, &ml);
        EVP_MD_CTX_free(ctx);
        for (int i = 0; i < 16; ++i) out[static_cast<size_t>(i)] = md[i];
        out[6] = (out[6] & 0x0F) | 0x50;
        out[8] = (out[8] & 0x3F) | 0x80;
    } else {
        uint64_t h = std::hash<std::string>{}(url);
        for (int i = 0; i < 8; ++i) out[static_cast<size_t>(i)] = static_cast<std::uint8_t>((h >> (i * 8)) & 0xFF);
        for (int i = 8; i < 16; ++i) out[static_cast<size_t>(i)] = static_cast<std::uint8_t>((i * 37 + h) & 0xFF);
        out[6] = (out[6] & 0x0F) | 0x40;
        out[8] = (out[8] & 0x3F) | 0x80;
    }
    return out;
}

struct HotbarEntry { std::uint32_t itemId; std::uint16_t stateId; };
inline const struct { const char* name; int cnt; } kKit[] = {
    {"minecraft:iron_sword",1}, {"minecraft:iron_pickaxe",1}, {"minecraft:iron_axe",1},
    {"minecraft:bread",8}, {"minecraft:apple",4},
    {"minecraft:cobblestone",64}, {"minecraft:oak_planks",64}, {"minecraft:torch",32},
    {"minecraft:dirt",64},
};

inline std::string blockNameByState(std::uint16_t sid) {
    if (auto* d = gen::blockByState(sid)) return std::string(d->name);
    return "minecraft:air";
}

// Recipe-book SlotDisplay item encoding shared by session and command paths.
inline void writeSlotDisplayItem(WriteBuffer& out, std::uint32_t itemId) {
    out.varint(itemId ? 2 : 0);
    if (itemId) out.varint(static_cast<std::int32_t>(itemId));
}

// Minecraft 1.21.4's EntityPositionS2CPacket (play clientbound
// `teleport_entity`) carries a PlayerPosition record, not the older
// three-coordinate/byte-angle shape found in some protocol tables.  The
// record is position (3 x f64), delta movement (3 x f64), rotation (2 x
// f32), followed by PositionFlagSet (fixed i32) and on-ground (bool).
// Relative flags are zero for the absolute teleports emitted by the server.
inline WriteBuffer makeEntityTeleportBody(
    std::int32_t entityId,
    double x, double y, double z,
    double deltaX = 0.0, double deltaY = 0.0, double deltaZ = 0.0,
    float yaw = 0.0f, float pitch = 0.0f,
    std::int32_t relativeFlags = 0,
    bool onGround = true) {
    WriteBuffer out;
    out.varint(entityId);
    out.f64(x);
    out.f64(y);
    out.f64(z);
    out.f64(deltaX);
    out.f64(deltaY);
    out.f64(deltaZ);
    out.f32(yaw);
    out.f32(pitch);
    out.i32(relativeFlags);
    out.boolean(onGround);
    return out;
}

} // namespace cppfm
