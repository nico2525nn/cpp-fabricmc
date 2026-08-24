// Anvil persistence: chunk <-> NBT (1.21.4, DataVersion 4189) + region files.
#pragma once
#include "World.hpp"
#include "RegionFile.hpp"
#include "../core/NBTValue.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <set>
#include <thread>

namespace cppfm {

constexpr std::int32_t kDataVersion = 4189;      // 1.21.4
constexpr std::int32_t kMinYSections = -4;       // minY -64 / 16

// ------------------------------------------------------------- chunk -> NBT
inline nbt::Value blockStateEntry(std::uint16_t stateId,
                                  const std::string& nameForDefault) {
    nbt::Value e = nbt::Value::makeCompound();
    e.set("Name", nbt::Value::makeString(nameForDefault));
    return e;
}

inline nbt::Value chunkToNBT(std::int32_t cx, std::int32_t cz,
                             const Chunk& chunk,
                             const std::string& biomeKey) {
    namespace nv = nbt;
    nv::Value root = nv::Value::makeCompound();
    root.set("DataVersion", nv::Value::makeInt(kDataVersion));
    root.set("xPos", nv::Value::makeInt(cx));
    root.set("zPos", nv::Value::makeInt(cz));
    root.set("yPos", nv::Value::makeInt(kMinYSections));
    root.set("Status", nv::Value::makeString("minecraft:full"));
    root.set("LastUpdate", nv::Value::makeLong(0));

    // heightmaps (same packing as wire)
    std::vector<std::int64_t> hm;
    packHeightmap(hm, *const_cast<Chunk*>(&chunk) /* read-only use */);
    {
        nv::Value hms = nv::Value::makeCompound();
        nv::Value la; la.tag = nv::LongArray; la.longArray = hm;
        hms.set("MOTION_BLOCKING", la);
        hms.set("WORLD_SURFACE", la);
        root.set("Heightmaps", hms);
    }

    // invert: state id -> name (first match)
    static thread_local std::unordered_map<std::uint32_t, std::string> inv;
    if (inv.empty())
        for (auto& [n, s] : gen::kBlocks) inv.emplace(s, std::string(n));

    nv::Value sections = nv::Value::makeList(nv::Compound, 24);
    for (int s = 0; s < kSectionsPerChunk; ++s) {
        const std::size_t base = static_cast<std::size_t>(s) * 4096;
        // palette
        std::vector<std::uint32_t> pal;
        std::unordered_map<std::uint32_t, std::uint16_t> idx;
        for (int i = 0; i < 4096; ++i) {
            const std::uint32_t st = chunk.blocks[base + i];
            if (!idx.count(st)) { idx.emplace(st, (std::uint16_t)pal.size()); pal.push_back(st); }
        }
        nv::Value sec = nv::Value::makeCompound();
        sec.set("Y", nv::Value::makeByte(static_cast<std::int8_t>(s + kMinYSections)));

        {   // biomes: uniform plains
            nv::Value bio = nv::Value::makeCompound();
            nv::Value bp = nv::Value::makeList(nv::String);
            bp.list.push_back(nv::Value::makeString(biomeKey));
            bio.set("palette", bp);
            sec.set("biomes", bio);
        }
        {   // block_states
            nv::Value bs = nv::Value::makeCompound();
            nv::Value pl = nv::Value::makeList(nv::Compound, pal.size());
            for (auto id : pal) {
                nv::Value entry = nv::Value::makeCompound();
                auto it = inv.find(id);
                entry.set("Name", nv::Value::makeString(it != inv.end() ? it->second : "minecraft:air"));
                pl.list.push_back(entry);
            }
            bs.set("palette", pl);
            if (pal.size() > 1) {
                const int bits = std::max(4, ceilLog2((std::uint32_t)pal.size()));
                const int per = 64 / bits;
                nv::Value data; data.tag = nv::LongArray;
                data.longArray.assign((4096 + per - 1) / per, 0);
                for (int i = 0; i < 4096; ++i) {
                    const std::uint16_t pi = idx.at(chunk.blocks[base + i]);
                    data.longArray[i / per] |= static_cast<std::int64_t>(
                        static_cast<std::uint64_t>(pi) << ((i % per) * bits));
                }
                bs.set("data", data);
            }
            sec.set("block_states", bs);
        }
        sections.list.push_back(sec);
    }
    root.set("sections", sections);
    root.set("block_entities", nv::Value::makeList(nv::Compound));
    return root;
}

// ------------------------------------------------------------- NBT -> chunk
inline bool chunkFromNBT(const nbt::Value& root, Chunk& chunk,
                         const std::unordered_map<std::string, std::uint32_t>& biomeIds,
                         std::string& biomeOut) {
    const auto* status = root.get("Status");
    if (!status || status->str.find("full") == std::string::npos)
        return false;                                       // unfinished: fall back to gen
    const auto* xs = root.get("xPos"), * zs = root.get("zPos");
    if (!xs || !zs) return false;

    for (auto& [k, v] : root.comp) {
        if (k == "sections" && v.tag == nbt::List) {
            for (auto& sec : v.list) {
                const auto* yv = sec.get("Y");
                if (!yv) continue;
                const int secY = yv->b + 4;                 // 0..23
                if (secY < 0 || secY >= kSectionsPerChunk) continue;
                const std::size_t base = static_cast<std::size_t>(secY) * 4096;

                const auto* bs = sec.get("block_states");
                if (!bs) continue;
                std::vector<std::uint32_t> pal;
                if (const auto* p = bs->get("palette")) {
                    for (auto& e : p->list) {
                        std::string name = "minecraft:air";
                        if (const auto* nm = e.get("Name")) name = nm->str;
                        const auto& table = gen::blockNameToState();
                        auto it = table.find(name);
                        pal.push_back(it != table.end()
                                      ? static_cast<std::uint32_t>(it->second) : 0);
                    }
                }
                std::vector<std::uint32_t> vals(4096, pal.empty() ? 0 : pal.front());
                if (pal.size() > 1) {
                    if (const auto* d = bs->get("data"); d && d->tag == nbt::LongArray) {
                        const int bits = std::max(4, ceilLog2((std::uint32_t)pal.size()));
                        const int per = 64 / bits;
                        for (int i = 0; i < 4096; ++i) {
                            const auto w = d->longArray[i / per];
                            vals[i] = pal[(static_cast<std::uint64_t>(w) >> ((i % per) * bits))
                                          & ((1u << bits) - 1)];
                        }
                    }
                }
                for (int i = 0; i < 4096; ++i) chunk.blocks[base + i] = static_cast<std::uint16_t>(vals[i]);
            }
        } else if (k == "Heightmaps") {
            // accepted silently; recomputed on demand anyway
        }
    }
    // biome from first section's palette (uniform worlds only)
    for (auto& [k, v] : root.comp) {
        if (k == "sections") {
            if (!v.list.empty()) {
                if (const auto* bio = v.list.front().get("biomes")) {
                    if (const auto* p = bio->get("palette"); p && !p->list.empty()) {
                        if (p->list.front().tag == nbt::String) biomeOut = p->list.front().str;
                    }
                }
            }
            break;
        }
    }
    return true;
}

} // namespace cppfm
