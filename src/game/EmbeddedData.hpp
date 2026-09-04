// EmbeddedData: loads captured reference configuration payloads (registry blobs, tags)
// and derives ordered registry id tables from them at startup.
//
// The .bin files are raw packet BODIES (packet id excluded) as observed on the wire.
// We replay them verbatim; ids used elsewhere (biome index in chunks) are derived
// from the same blobs, keeping everything self-consistent.
//
// D10 (plan26 §3): registry set exactness & order lock — 12 `minecraft:*` registries
// must match PROTOCOL_NOTES.md:24-32 exactly in count and wire order, otherwise client
// kicks with `unknown registry`. See `kRegistrySpec`.
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <unordered_map>
#include <stdexcept>
#include <fstream>
#include <cstdio>
#include "../core/ByteBuffer.hpp"
#include "../core/NBT.hpp"

namespace cppfm {

struct RegistryBlob { std::string key; std::vector<std::uint8_t> body; };

class EmbeddedData {
public:
    // D10 lock: exact 12 registries and counts as captured from vanilla 1.21.4 reference server
    // PROTOCOL_NOTES.md:24-32 — order is wire order, must not be re-sorted
    static constexpr std::array<std::pair<std::string_view,int>,12> kRegistrySpec{{
        {"minecraft:worldgen/biome",65},
        {"minecraft:chat_type",7},
        {"minecraft:trim_pattern",18},
        {"minecraft:trim_material",11},
        {"minecraft:wolf_variant",9},
        {"minecraft:painting_variant",50},
        {"minecraft:dimension_type",4},
        {"minecraft:damage_type",49},
        {"minecraft:banner_pattern",43},
        {"minecraft:enchantment",42},
        {"minecraft:jukebox_song",19},
        {"minecraft:instrument",8},
    }};
    // dir: directory containing registry_*.bin and tags.bin
    void load(const std::string& dir) {
        static const char* kRegistries[] = {
            "minecraft__worldgen_biome", "minecraft__chat_type",
            "minecraft__trim_pattern", "minecraft__trim_material",
            "minecraft__wolf_variant", "minecraft__painting_variant",
            "minecraft__dimension_type", "minecraft__damage_type",
            "minecraft__banner_pattern", "minecraft__enchantment",
            "minecraft__jukebox_song", "minecraft__instrument",
        };
        for (auto* r : kRegistries) {
            RegistryBlob blob;
            blob.body = readFile(dir + "/registry_" + r + ".bin");
            {   // canonical key = first string inside the blob body
                ReadBuffer in(blob.body);
                blob.key = in.string();
            }
            registries_.push_back(std::move(blob));
        }
        tags_ = readFile(dir + "/tags.bin");
        parseBiomeOrder();
        verifyRegistrySpec();
    }

    const std::vector<RegistryBlob>& registries() const { return registries_; }
    const std::vector<std::uint8_t>& tags() const { return tags_; }

    // D10 helpers: order lock and count verification
    std::vector<std::string> registryIdsInSendOrder() const {
        std::vector<std::string> out;
        out.reserve(registries_.size());
        for (auto &r : registries_) out.push_back(r.key);
        return out;
    }
    int registryEntryCountInt(const std::string& registryKey) const {
        for (auto &r : registries_) if (r.key == registryKey) {
            ReadBuffer in(r.body);
            (void)in.string();
            return in.varint();
        }
        return -1;
    }
    // verify that the 12 registries match kRegistrySpec in order and count; log drift but don't throw in production
    void verifyRegistrySpec() const {
        if (registries_.size() != kRegistrySpec.size()) {
            std::fprintf(stderr, "[Registry] size %zu != %zu expected (drift)\n", registries_.size(), kRegistrySpec.size());
        }
        for (size_t i = 0; i < kRegistrySpec.size() && i < registries_.size(); ++i) {
            auto [expKey, expCount] = kRegistrySpec[i];
            const std::string &actKey = registries_[i].key;
            if (actKey != expKey) {
                std::fprintf(stderr, "[Registry] order mismatch at %zu: got %s expected %.*s\n",
                    i, actKey.c_str(), (int)expKey.size(), expKey.data());
            }
            int actCount = registryEntryCountInt(std::string(expKey));
            if (actCount != expCount) {
                std::fprintf(stderr, "[Registry] %.*s size %d != %d (drift)\n",
                    (int)expKey.size(), expKey.data(), actCount, expCount);
            }
        }
        // biome sanity: plains 40 desert 14 pale_garden present
        auto itPlains = biomeIndex_.find("minecraft:plains");
        if (itPlains != biomeIndex_.end() && itPlains->second != 40)
            std::fprintf(stderr, "[Registry] biome plains index %u != 40\n", itPlains->second);
        auto itDesert = biomeIndex_.find("minecraft:desert");
        if (itDesert != biomeIndex_.end() && itDesert->second != 14)
            std::fprintf(stderr, "[Registry] biome desert index %u != 14\n", itDesert->second);
        if (biomeIndex_.find("minecraft:pale_garden") == biomeIndex_.end())
            std::fprintf(stderr, "[Registry] missing pale_garden (expected 65)\n");
        if (biomeIndex_.size() != 65)
            std::fprintf(stderr, "[Registry] biome count %zu != 65\n", biomeIndex_.size());
    }

    std::uint32_t biomeIndex(const std::string& key) const {
        auto it = biomeIndex_.find(key);
        if (it == biomeIndex_.end())
            throw std::runtime_error("unknown biome: " + key);
        return it->second;
    }

private:
    static std::vector<std::uint8_t> readFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("missing asset: " + path);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                          std::istreambuf_iterator<char>());
    }
    static std::string restoreKey(const std::string& fileStem) {
        // minecraft__worldgen_biome -> minecraft:worldgen/biome ; others -> minecraft:x
        std::string s = fileStem.substr(strlen("minecraft__"));
        if (s.starts_with("worldgen_")) return "minecraft:" + s;
        return "minecraft:" + s;
    }
    // walk the worldgen/biome blob capturing entry keys in wire order
    void parseBiomeOrder() {
        for (auto& r : registries_) {
            if (r.key != "minecraft:worldgen/biome") continue;
            ReadBuffer in(r.body);
            const std::string key = in.string();
            const std::int32_t n = in.varint();
            for (std::int32_t i = 0; i < n; ++i) {
                std::string ek = in.string();
                bool has = in.boolean();
                if (has) { nbt::Reader reader(in); reader.skipRoot(); }
                biomeIndex_.emplace(ek, static_cast<std::uint32_t>(i));
            }
        }
    }

    std::vector<RegistryBlob> registries_;
    std::vector<std::uint8_t> tags_;
    std::unordered_map<std::string, std::uint32_t> biomeIndex_;
};

} // namespace cppfm
