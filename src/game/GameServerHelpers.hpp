#pragma once
// Wire-unchanged, inline helpers.

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <openssl/evp.h>
#include "../generated/BlockStates.hpp"
#include "../generated/ItemIds.hpp"

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

} // namespace cppfm
