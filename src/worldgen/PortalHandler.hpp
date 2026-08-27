#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <atomic>
#include "../game/World.hpp"
#include "../game/GameServer.hpp"
#include "../generated/BlockStates.hpp"
#include "../proto/Ids.hpp"
#include "../core/ByteBuffer.hpp"

namespace cppfm {

struct BlockPos {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

class PortalHandler {
public:
    static BlockPos transformCoordinates(BlockPos src, std::int8_t fromDim, std::int8_t toDim) {
        if (toDim == 1) {
            // End island
            return BlockPos{0, 65, 0};
        }
        if (fromDim == 1 && toDim == 0) {
            // End -> overworld spawn handled in tryTeleport, but keep src for now
            return src;
        }
        if (fromDim == 0 && toDim == -1) {
            std::int32_t nx = static_cast<std::int32_t>(std::floor(src.x / 8.0));
            std::int32_t nz = static_cast<std::int32_t>(std::floor(src.z / 8.0));
            return BlockPos{nx, src.y, nz};
        }
        if (fromDim == -1 && toDim == 0) {
            return BlockPos{src.x * 8, src.y, src.z * 8};
        }
        return src;
    }

    // plan6 §3: vertical search 6 blocks up/down around the best ground level
    static bool findSafeSpawn(World& toWorld, std::int32_t& outX, std::int32_t& outY, std::int32_t& outZ,
                              std::int32_t targetX, std::int32_t targetZ) {
        const int offsets[16][2] = {
            {0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1},{2,0},{-2,0},{0,2},{0,-2},{2,1},{1,2},{-2,-1}
        };
        auto isSafe = [&](std::int32_t x, std::int32_t y, std::int32_t z) -> bool {
            std::uint16_t below = toWorld.getBlock(x, y - 1, z);
            std::uint16_t at = toWorld.getBlock(x, y, z);
            std::uint16_t above = toWorld.getBlock(x, y + 1, z);
            return below != 0 && at == 0 && above == 0;
        };
        for (int attempt = 0; attempt < 16; ++attempt) {
            std::int32_t tx = targetX + offsets[attempt][0];
            std::int32_t tz = targetZ + offsets[attempt][1];
            toWorld.generateChunkIfMissing(tx >> 4, tz >> 4);
            toWorld.generateChunkIfMissing((tx+1) >> 4, tz >> 4);
            toWorld.generateChunkIfMissing(tx >> 4, (tz+1) >> 4);
            // First find highest solid ground near default band 80..-64
            int groundY = INT32_MIN;
            for (int y = 80; y >= kMinY + 1; --y) {
                if (toWorld.getBlock(tx, y - 1, tz) != 0 && isSafe(tx, y, tz)) { groundY = y; break; }
            }
            if (groundY != INT32_MIN) {
                // primary spot is groundY, now vertical search 6 up/down
                if (isSafe(tx, groundY, tz)) { outX=tx; outY=groundY; outZ=tz; return true; }
            }
            // systematic scan 80..60 plus 6 up/down expansion
            for (int y = 80; y >= 60; --y) {
                if (isSafe(tx, y, tz)) { outX=tx; outY=y; outZ=tz; return true; }
                // 6 up/down search around y
                for (int dy = 1; dy <= 6; ++dy) {
                    if (y + dy <= kMaxY - 2 && isSafe(tx, y + dy, tz)) { outX=tx; outY=y+dy; outZ=tz; return true; }
                    if (y - dy >= kMinY + 1 && isSafe(tx, y - dy, tz)) { outX=tx; outY=y-dy; outZ=tz; return true; }
                }
            }
            // fallback exhaustive scan with 6-range expansion
            for (int y = 80; y >= -20; --y) {
                if (isSafe(tx, y, tz)) { outX=tx; outY=y; outZ=tz; return true; }
            }
        }
        return false;
    }

    static bool tryTeleport(GameServer& srv, Player& p, std::int8_t toDim) {
        if (!p.conn) return false;
        if (p.portalCooldownUntilTick > srv.tickNow()) return false;
        if (p.dimension == toDim) return false;
        std::int8_t fromDim = p.dimension;
        BlockPos src{ static_cast<std::int32_t>(std::floor(p.x)),
                      static_cast<std::int32_t>(std::floor(p.y)),
                      static_cast<std::int32_t>(std::floor(p.z)) };
        BlockPos tgt = transformCoordinates(src, fromDim, toDim);
        World& toWorld = srv.worldFor(toDim);
        std::int32_t outX = 0, outY = 0, outZ = 0;
        bool found = false;
        if (toDim == 1) {
            outX = 0; outY = 65; outZ = 0;
            toWorld.generateChunkIfMissing(outX >> 4, outZ >> 4);
            found = true;
        } else if (fromDim == 1 && toDim == 0) {
            auto sp = srv.world().spawnPoint();
            if (findSafeSpawn(toWorld, outX, outY, outZ, sp.x, sp.z)) {
                found = true;
            } else {
                outX = sp.x; outY = 65; outZ = sp.z;
                // try to ensure safe: generate chunk and check; if solid below missing, place platform?
                toWorld.generateChunkIfMissing(outX >> 4, outZ >> 4);
                // fallback scan for ground
                for (int y = 80; y >= -64; --y) {
                    if (toWorld.getBlock(outX, y, outZ) != 0 && toWorld.getBlock(outX, y+1, outZ)==0 && toWorld.getBlock(outX, y+2, outZ)==0) { outY = y+1; break; }
                }
                found = true;
            }
        } else {
            std::int32_t targetX = tgt.x;
            std::int32_t targetZ = tgt.z;
            if (findSafeSpawn(toWorld, outX, outY, outZ, targetX, targetZ)) {
                found = true;
            } else {
                toWorld.generateChunkIfMissing(targetX >> 4, targetZ >> 4);
                outX = targetX;
                outY = 70;
                outZ = targetZ;
                // try to find nearest safe by scanning down from 80
                for (int y = 80; y >= 60; --y) {
                    if (toWorld.getBlock(outX, y, outZ)==0 && toWorld.getBlock(outX, y+1, outZ)==0 && toWorld.getBlock(outX, y-1, outZ)!=0) { outY = y; break; }
                }
                // if still not safe, create simple platform
                if (toWorld.getBlock(outX, outY-1, outZ)==0) {
                    const auto& mp = gen::blockNameToState();
                    auto it = mp.find("minecraft:obsidian");
                    std::uint16_t obs = it != mp.end() ? static_cast<std::uint16_t>(it->second) : 2397;
                    // create 3x3 platform at y-1
                    for (int dx=-1; dx<=1; ++dx) for (int dz=-1; dz<=1; ++dz) {
                        if (toWorld.getBlock(outX+dx, outY-1, outZ+dz)==0) {
                            toWorld.setBlock(outX+dx, outY-1, outZ+dz, obs);
                            srv.invalidateChunkCache((outX+dx)>>4, (outZ+dz)>>4);
                        }
                    }
                }
                found = true;
            }
        }
        if (!found) return false;
        p.dimension = toDim;
        p.x = outX + 0.5;
        p.y = static_cast<double>(outY);
        p.z = outZ + 0.5;
        p.portalCooldownUntilTick = srv.tickNow() + 90;
        p.fallDist = 0;
        p.onGround = false;

        // Build Respawn packet with per-dimension data
        std::string dimName;
        std::string dimTypeKey;
        if (toDim == 0) { dimName = "minecraft:overworld"; dimTypeKey = "minecraft:overworld"; }
        else if (toDim == -1) { dimName = "minecraft:the_nether"; dimTypeKey = "minecraft:the_nether"; }
        else if (toDim == 1) { dimName = "minecraft:the_end"; dimTypeKey = "minecraft:the_end"; }
        else { dimName = "minecraft:overworld"; dimTypeKey = "minecraft:overworld"; }
        std::int32_t dimTypeIdx = srv.gameData_.idOf("minecraft:dimension_type", dimTypeKey);
        if (dimTypeIdx < 0) {
            if (toDim == -1) dimTypeIdx = 3;
            else if (toDim == 1) dimTypeIdx = 2;
            else dimTypeIdx = 0;
        }
        WriteBuffer ws;
        ws.varint(dimTypeIdx);
        ws.string(dimName);
        ws.i64(srv.config().hashedSeed);
        ws.i8(static_cast<std::int8_t>(p.gamemode));
        ws.u8(255);
        ws.boolean(false);
        ws.boolean(srv.config().levelType == "flat");
        ws.boolean(false);
        ws.varint(0);
        // sea level from target world
        std::int32_t sea = toWorld.seaLevel();
        // flat's sea is -63; keep consistent
        ws.varint(sea);
        WriteBuffer b;
        b.raw(ws.data.data(), ws.data.size());
        b.u8(0x03);
        try { p.conn->sendPacket(proto::pl::sc::Respawn, b); } catch (...) {}

        // PlayerAbilities reset on dimension change (plan6 §3) — send 0x3A (0x44 alias per task)
        {
            WriteBuffer ab;
            std::uint8_t flags = 0;
            if (p.gamemode == 1) flags = 0x01 | 0x04 | 0x08; // invuln + allowFlying + creative
            else if (p.gamemode == 3) flags = 0x01 | 0x08;
            else flags = 0x00;
            ab.i8(flags);
            ab.f32(0.05f);
            ab.f32(p.gamemode == 1 ? 0.10f : 0.05f);
            try { p.conn->sendPacket(proto::pl::sc::Abilities, ab); } catch (...) {}
        }

        // PlayerPosition sync
        WriteBuffer tp;
        static std::atomic<std::int32_t> nextId{1000};
        std::int32_t tid = nextId.fetch_add(1);
        tp.varint(tid);
        tp.f64(p.x); tp.f64(p.y); tp.f64(p.z);
        tp.f64(0); tp.f64(0); tp.f64(0);
        tp.f32(p.yaw); tp.f32(p.pitch);
        tp.u32(0);
        try { p.conn->sendPacket(proto::pl::sc::PlayerPosition, tp); } catch (...) {}

        // Invalidate chunk caches for old and new positions
        srv.invalidateChunkCache(tgt.x >> 4, tgt.z >> 4);
        srv.invalidateChunkCache(outX >> 4, outZ >> 4);
        srv.clearChunkCache();
        for (int dz=-2; dz<=2; ++dz) for (int dx=-2; dx<=2; ++dx) {
            srv.invalidateChunkCache((outX>>4)+dx, (outZ>>4)+dz);
        }
        return true;
    }
};

} // namespace cppfm
