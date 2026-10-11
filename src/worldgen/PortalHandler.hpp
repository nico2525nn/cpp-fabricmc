#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include "../game/World.hpp"
#include "../game/GameServer.hpp"
#include "../game/BlockStateTraits.hpp"
#include "../generated/BlockStates.hpp"
#include "../proto/Ids.hpp"
#include "../core/ByteBuffer.hpp"

namespace cppfm {

class PortalHandler {
public:
    static constexpr std::array<std::array<int, 2>, 16> kSafeSpawnOffsets{{
        {{0,0}}, {{1,0}}, {{-1,0}}, {{0,1}}, {{0,-1}}, {{1,1}},
        {{-1,-1}}, {{1,-1}}, {{-1,1}}, {{2,0}}, {{-2,0}}, {{0,2}},
        {{0,-2}}, {{2,1}}, {{1,2}}, {{-2,-1}}
    }};

    static bool tryTransformCoordinates(BlockPos src, std::int8_t fromDim,
                                        std::int8_t toDim, BlockPos& out) {
        if (toDim == 1) {
            // End island
            out = BlockPos{0, 65, 0};
            return true;
        }
        if (fromDim == 1 && toDim == 0) {
            // End -> overworld spawn handled in tryTeleport, but keep src for now
            out = src;
            return true;
        }
        if (fromDim == 0 && toDim == -1) {
            out = BlockPos{
                static_cast<std::int32_t>(std::floor(src.x / 8.0)), src.y,
                static_cast<std::int32_t>(std::floor(src.z / 8.0))};
            return true;
        }
        if (fromDim == -1 && toDim == 0) {
            const auto x = static_cast<std::int64_t>(src.x) * 8;
            const auto z = static_cast<std::int64_t>(src.z) * 8;
            if (x < std::numeric_limits<std::int32_t>::min() ||
                x > std::numeric_limits<std::int32_t>::max() ||
                z < std::numeric_limits<std::int32_t>::min() ||
                z > std::numeric_limits<std::int32_t>::max())
                return false;
            out = BlockPos{static_cast<std::int32_t>(x), src.y,
                           static_cast<std::int32_t>(z)};
            return true;
        }
        out = src;
        return true;
    }

    static BlockPos transformCoordinates(BlockPos src, std::int8_t fromDim,
                                          std::int8_t toDim) {
        BlockPos out{};
        (void)tryTransformCoordinates(src, fromDim, toDim, out);
        return out;
    }

    static int clampSafeFeetY(const DimensionGeometry& geometry, int y) {
        return std::clamp(y, geometry.minY + 1, geometry.maxY() - 2);
    }

    // Portal chunk preparation can be expensive. Call this while the
    // simulation dispatch gate is released. The guarded search below is
    // deliberately loaded-chunk-only; if a prepared chunk was evicted before
    // the gate is reacquired, that portal attempt fails and can retry next
    // tick rather than generating terrain while holding the global gate.
    static bool prepareDestinationChunks(World& world, std::int32_t targetX,
                                         std::int32_t targetZ) {
        for (const auto& offset : kSafeSpawnOffsets) {
            const auto x = static_cast<std::int64_t>(targetX) + offset[0];
            const auto z = static_cast<std::int64_t>(targetZ) + offset[1];
            if (x < std::numeric_limits<std::int32_t>::min() ||
                x > std::numeric_limits<std::int32_t>::max() ||
                z < std::numeric_limits<std::int32_t>::min() ||
                z > std::numeric_limits<std::int32_t>::max())
                continue;
            if (!world.generateChunkIfMissing(
                    static_cast<std::int32_t>(x) >> 4,
                    static_cast<std::int32_t>(z) >> 4))
                return false;
        }
        if (targetX == std::numeric_limits<std::int32_t>::min() ||
            targetX == std::numeric_limits<std::int32_t>::max() ||
            targetZ == std::numeric_limits<std::int32_t>::min() ||
            targetZ == std::numeric_limits<std::int32_t>::max())
            return false;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                if (!world.generateChunkIfMissing(
                        static_cast<std::int32_t>(targetX + dx) >> 4,
                        static_cast<std::int32_t>(targetZ + dz) >> 4))
                    return false;
        return true;
    }

    static bool hasSafeSupport(std::uint16_t state) {
        return hasSafeLandingSupport(state);
    }

    // Search the local horizontal candidates at the transformed destination
    // height first, then expand vertically within the target world's physical
    // bounds. Fixed Overworld-era Y bands miss valid Nether/End positions.
    static bool findSafeSpawn(World& toWorld, std::int32_t& outX, std::int32_t& outY, std::int32_t& outZ,
                              std::int32_t targetX, std::int32_t targetY,
                              std::int32_t targetZ) {
        const auto geometry = toWorld.verticalGeometry();
        const int minFeetY = geometry.minY + 1;
        const int maxFeetY = geometry.maxY() - 2;
        const int preferredY = clampSafeFeetY(geometry, targetY);
        const int maxDelta = std::max(preferredY - minFeetY,
                                      maxFeetY - preferredY);
        auto isSafe = [&](std::int32_t x, std::int32_t y, std::int32_t z) -> bool {
            bool safe = false;
            const bool loaded = toWorld.withChunk(
                x >> 4, z >> 4, [&](const Chunk& chunk) {
                    const auto stateAt = [&](int worldY) {
                        const int localY = worldY - kMinY;
                        return chunk.blockAt(Chunk::index(
                            localY >> 4, localY & 15, z & 15, x & 15));
                    };
                    safe = hasSafeSupport(stateAt(y - 1)) &&
                           isAirBlockState(stateAt(y)) &&
                           isAirBlockState(stateAt(y + 1));
                });
            return loaded && safe;
        };
        for (int attempt = 0; attempt < 16; ++attempt) {
            const auto tx64 = static_cast<std::int64_t>(targetX) +
                              kSafeSpawnOffsets[attempt][0];
            const auto tz64 = static_cast<std::int64_t>(targetZ) +
                              kSafeSpawnOffsets[attempt][1];
            if (tx64 < std::numeric_limits<std::int32_t>::min() ||
                tx64 > std::numeric_limits<std::int32_t>::max() ||
                tz64 < std::numeric_limits<std::int32_t>::min() ||
                tz64 > std::numeric_limits<std::int32_t>::max())
                continue;
            const auto tx = static_cast<std::int32_t>(tx64);
            const auto tz = static_cast<std::int32_t>(tz64);
            // A missing candidate means destination preparation lost a race
            // with chunk eviction. Do not synchronously generate under the
            // simulation guard; the next portal tick will prepare it again.
            if (!toWorld.hasChunk(tx >> 4, tz >> 4)) continue;
            for (int delta = 0; delta <= maxDelta; ++delta) {
                const int up = preferredY + delta;
                if (up <= maxFeetY && isSafe(tx, up, tz)) {
                    outX = tx; outY = up; outZ = tz;
                    return true;
                }
                const int down = preferredY - delta;
                if (delta != 0 && down >= minFeetY && isSafe(tx, down, tz)) {
                    outX = tx; outY = down; outZ = tz;
                    return true;
                }
            }
        }
        return false;
    }

    // Last-resort destination construction still requires clear feet/head
    // space. If none exists in the requested column, decline the transfer
    // instead of teleporting the player into blocks.
    using PlatformBlockObserver = std::function<void(
        std::int8_t, const World::BlockUpdate&)>;

    static bool prepareSafeFallback(
                                    World& targetWorld,
                                    std::int32_t& outX,
                                    std::int32_t& outY,
                                    std::int32_t& outZ,
                                    std::vector<World::BlockUpdate>& outPlatform,
                                    std::int32_t targetX,
                                    std::int32_t targetY,
                                    std::int32_t targetZ) {
        outPlatform.clear();
        const auto geometry = targetWorld.verticalGeometry();
        const int preferredY = clampSafeFeetY(geometry, targetY);
        const int minFeetY = geometry.minY + 1;
        const int maxFeetY = geometry.maxY() - 2;
        // This routine may run while SimulationDispatchGuard is held. It must
        // only inspect already-prepared chunks; a missing chunk is a retryable
        // miss, not permission to synchronously generate in the guarded phase.
        if (!targetWorld.hasChunk(targetX >> 4, targetZ >> 4))
            return false;

        int safeY = preferredY;
        bool foundClearSpace = false;
        const int maxDelta = std::max(preferredY - minFeetY,
                                      maxFeetY - preferredY);
        for (int delta = 0; delta <= maxDelta && !foundClearSpace; ++delta) {
            for (const int direction : {1, -1}) {
                if (delta == 0 && direction < 0) continue;
                const int candidateY = preferredY + direction * delta;
                if (candidateY < minFeetY || candidateY > maxFeetY ||
                    !isAirBlockState(targetWorld.getBlock(
                        targetX, candidateY, targetZ)) ||
                    !isAirBlockState(targetWorld.getBlock(
                        targetX, candidateY + 1, targetZ)))
                    continue;
                safeY = candidateY;
                foundClearSpace = true;
                break;
            }
        }
        if (!foundClearSpace) return false;

        // Every cell of a possible 3x3 platform must be backed by a loaded
        // chunk before emptiness is inspected. World::getBlock deliberately
        // returns air for an unloaded chunk; checking first and loading later
        // could overwrite generated terrain at a chunk edge.
        constexpr auto minCoordinate =
            std::numeric_limits<std::int32_t>::min();
        constexpr auto maxCoordinate =
            std::numeric_limits<std::int32_t>::max();
        if (targetX <= minCoordinate || targetX >= maxCoordinate ||
            targetZ <= minCoordinate || targetZ >= maxCoordinate)
            return false;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                const auto x = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(targetX) + dx);
                const auto z = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(targetZ) + dz);
                if (!targetWorld.hasChunk(x >> 4, z >> 4))
                    return false;
            }
        }
        if (!isAirBlockState(targetWorld.getBlock(targetX, safeY, targetZ)) ||
            !isAirBlockState(
                targetWorld.getBlock(targetX, safeY + 1, targetZ)))
            return false;

        const auto support = targetWorld.getBlock(targetX, safeY - 1, targetZ);
        const bool safeSupport = hasSafeSupport(support);
        if (!safeSupport && !isAirBlockState(support)) return false;
        if (!safeSupport) {
            const auto minCoordinate =
                static_cast<std::int64_t>(
                    std::numeric_limits<std::int32_t>::min());
            const auto maxCoordinate =
                static_cast<std::int64_t>(
                    std::numeric_limits<std::int32_t>::max());
            if (static_cast<std::int64_t>(targetX) - 1 < minCoordinate ||
                static_cast<std::int64_t>(targetX) + 1 > maxCoordinate ||
                static_cast<std::int64_t>(targetZ) - 1 < minCoordinate ||
                static_cast<std::int64_t>(targetZ) + 1 > maxCoordinate)
                return false;

            const auto obsidian = gen::blockNameToState().find(
                "minecraft:obsidian");
            if (obsidian == gen::blockNameToState().end()) return false;
            std::vector<World::BlockUpdate> platform;
            platform.reserve(9);
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto x = static_cast<std::int32_t>(targetX + dx);
                    const auto z = static_cast<std::int32_t>(targetZ + dz);
                    if (!isAirBlockState(
                            targetWorld.getBlock(x, safeY - 1, z)))
                        continue;
                    platform.push_back({x, safeY - 1, z,
                        static_cast<std::uint16_t>(obsidian->second)});
                }
            }
            if (platform.empty()) return false;
            outPlatform = std::move(platform);
        }

        outX = targetX;
        outY = safeY;
        outZ = targetZ;
        return true;
    }

    static bool findOrCreateSafeFallback(
        const PlatformBlockObserver& onPlatformBlock, World& targetWorld,
        std::int8_t targetDimension, std::int32_t& outX,
        std::int32_t& outY, std::int32_t& outZ, std::int32_t targetX,
        std::int32_t targetY, std::int32_t targetZ) {
        std::vector<World::BlockUpdate> platform;
        std::int32_t candidateX = 0, candidateY = 0, candidateZ = 0;
        if (!prepareSafeFallback(targetWorld, candidateX, candidateY,
                                 candidateZ, platform, targetX, targetY,
                                 targetZ))
            return false;
        if (!platform.empty() &&
            !targetWorld.setBlockBatch(std::span<const World::BlockUpdate>(
                platform.data(), platform.size())))
            return false;
        for (const auto& block : platform)
            if (onPlatformBlock) onPlatformBlock(targetDimension, block);
        outX = candidateX;
        outY = candidateY;
        outZ = candidateZ;
        return true;
    }

    static bool tryTeleport(GameServer& srv, Player& p, std::int8_t toDim,
                            SimulationDispatchGuard& simulationLock) {
        std::shared_ptr<Connection> connection;
        std::int8_t fromDim = 0;
        double sourceX = 0, sourceY = 0, sourceZ = 0;
        std::int64_t sourceCooldown = 0;
        {
            std::lock_guard playerLock(p.stateMtx);
            connection = p.conn;
            fromDim = p.dimension;
            sourceX = p.x;
            sourceY = p.y;
            sourceZ = p.z;
            sourceCooldown = p.portalCooldownUntilTick;
        }
        if (!connection || sourceCooldown > srv.tickNow() || fromDim == toDim)
            return false;
        const auto floorCoordinate = [](double coordinate,
                                        std::int32_t& result) {
            if (!std::isfinite(coordinate)) return false;
            const double floored = std::floor(coordinate);
            if (floored < std::numeric_limits<std::int32_t>::min() ||
                floored > std::numeric_limits<std::int32_t>::max())
                return false;
            result = static_cast<std::int32_t>(floored);
            return true;
        };
        BlockPos src;
        if (!floorCoordinate(sourceX, src.x) ||
            !floorCoordinate(sourceY, src.y) ||
            !floorCoordinate(sourceZ, src.z))
            return false;
        BlockPos tgt;
        if (!tryTransformCoordinates(src, fromDim, toDim, tgt)) return false;
        World& toWorld = srv.worldFor(toDim);
        std::int32_t searchX = tgt.x;
        std::int32_t searchY = tgt.y;
        std::int32_t searchZ = tgt.z;
        if (toDim == 1) {
            searchX = 0; searchY = 65; searchZ = 0;
        } else if (fromDim == 1 && toDim == 0) {
            const auto spawn = srv.world().spawnPoint();
            searchX = spawn.x; searchY = spawn.y; searchZ = spawn.z;
        }

        // Generate the small candidate neighborhood without holding the
        // global simulation gate. Other sessions and ticks may proceed while
        // cold destination terrain is prepared. The player and source portal
        // are revalidated after reacquiring the gate before any transition.
        simulationLock.unlock();
        bool chunksReady = false;
        try {
            chunksReady = prepareDestinationChunks(toWorld, searchX, searchZ);
        } catch (...) {
            chunksReady = false;
        }
        simulationLock.lock();
        if (!chunksReady) return false;
        {
            std::lock_guard playerLock(p.stateMtx);
            if (p.conn != connection || p.dimension != fromDim ||
                p.x != sourceX || p.y != sourceY || p.z != sourceZ ||
                p.portalCooldownUntilTick != sourceCooldown ||
                sourceCooldown > srv.tickNow())
                return false;
        }
        const World& fromWorld = srv.worldFor(fromDim);
        bool sourcePortalStillPresent = false;
        for (int dy = 0; dy <= 1; ++dy) {
            const auto state = fromWorld.getBlock(src.x, src.y + dy, src.z);
            const auto* block = gen::blockByState(state);
            if (block && (std::string_view(block->name) ==
                              "minecraft:nether_portal" ||
                          std::string_view(block->name) ==
                              "minecraft:end_portal")) {
                sourcePortalStillPresent = true;
                break;
            }
        }
        if (!sourcePortalStillPresent) return false;

        const auto notifyPlatformBlock = [&srv](
            std::int8_t dimension, const World::BlockUpdate& block) {
            srv.invalidateChunkCacheFor(dimension, block.x >> 4,
                                        block.z >> 4);
            srv.broadcastBlockChangeFor(dimension, block.x, block.y,
                                        block.z, block.state);
        };
        std::vector<World::BlockUpdate> fallbackPlatform;
        std::int32_t outX = 0, outY = 0, outZ = 0;
        bool found = false;
        if (toDim == 1) {
            found = findSafeSpawn(toWorld, outX, outY, outZ, 0, 65, 0) ||
                    prepareSafeFallback(toWorld, outX, outY, outZ,
                                        fallbackPlatform, 0, 65, 0);
        } else if (fromDim == 1 && toDim == 0) {
            auto sp = srv.world().spawnPoint();
            if (findSafeSpawn(toWorld, outX, outY, outZ,
                              sp.x, sp.y, sp.z)) {
                found = true;
            } else {
                found = prepareSafeFallback(toWorld, outX, outY, outZ,
                    fallbackPlatform, sp.x, sp.y, sp.z);
            }
        } else {
            if (findSafeSpawn(toWorld, outX, outY, outZ,
                              searchX, searchY, searchZ)) {
                found = true;
            } else {
                found = prepareSafeFallback(toWorld, outX, outY, outZ,
                    fallbackPlatform, searchX, searchY, searchZ);
            }
        }
        if (!found) return false;

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
        WriteBuffer ge;
        ge.u8(11); // GameEvent type for portal cooldown / respawn-screen state
        ge.f32(90.f);
        if (!connection->trySendPacket(proto::pl::sc::GameEvent, ge) ||
            !connection->trySendPacketBarrier(proto::pl::sc::Respawn, b)) {
            connection->close();
            return false;
        }

        if (!fallbackPlatform.empty() &&
            !toWorld.setBlockBatch(std::span<const World::BlockUpdate>(
                fallbackPlatform.data(), fallbackPlatform.size()))) {
            // Respawn was admitted, so a failed destination commit cannot be
            // recovered in-place. Close rather than leave the client and
            // authoritative player in different dimensions.
            connection->close();
            return false;
        }
        for (const auto& block : fallbackPlatform)
            notifyPlatformBlock(toDim, block);

        {
            std::lock_guard playerLock(p.stateMtx);
            p.dimension = toDim;
            p.x = outX + 0.5;
            p.y = static_cast<double>(outY);
            p.z = outZ + 0.5;
            p.portalCooldownUntilTick = srv.tickNow() + 90;
            p.fallDist = 0;
            p.onGround = false;
        }

        {
            WriteBuffer ab;
            std::uint8_t flags = 0;
            if (p.gamemode == 1) flags = 0x01 | 0x04 | 0x08; // invuln + allowFlying + creative
            else if (p.gamemode == 3) flags = 0x01 | 0x08;
            else flags = 0x00;
            ab.i8(flags);
            ab.f32(0.05f);
            ab.f32(p.gamemode == 1 ? 0.10f : 0.05f);
            if (!connection->trySendPacket(proto::pl::sc::Abilities, ab)) {
                connection->close();
                return false;
            }
        }

        // Use the same per-player confirmation sequence as commands and
        // session-originated corrections; independent IDs desynchronize a
        // later teleport acknowledgement.
        if (!srv.teleportPlayer(p, outX + 0.5, outY, outZ + 0.5)) {
            connection->close();
            return false;
        }

        // Chunk coordinates are local to a dimension.  Invalidate only the
        // source and destination views touched by the transfer; clearing the
        // global cache here used to evict unrelated dimensions and could also
        // re-serve an Overworld body for a same-coordinate Nether request.
        srv.invalidateChunkCacheFor(fromDim, src.x >> 4, src.z >> 4);
        srv.invalidateChunkCacheFor(toDim, outX >> 4, outZ >> 4);
        for (int dz=-2; dz<=2; ++dz) for (int dx=-2; dx<=2; ++dx) {
            srv.invalidateChunkCacheFor(toDim, (outX>>4)+dx,
                                        (outZ>>4)+dz);
        }
        return true;
    }
};

} // namespace cppfm
