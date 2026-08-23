// EmbeddedData: loads captured reference configuration payloads (registry blobs, tags)
// and derives ordered registry id tables from them at startup.
//
// The .bin files are raw packet BODIES (packet id excluded) as observed on the wire.
// We replay them verbatim; ids used elsewhere (biome index in chunks) are derived
// from the same blobs, keeping everything self-consistent.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <fstream>

namespace cppfm {

struct RegistryBlob { std::string key; std::vector<std::uint8_t> body; };

class EmbeddedData {
public:
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
            blob.key = restoreKey(r);
            registries_.push_back(std::move(blob));
        }
        tags_ = readFile(dir + "/tags.bin");
        parseBiomeOrder();
    }

    const std::vector<RegistryBlob>& registries() const { return registries_; }
    const std::vector<std::uint8_t>& tags() const { return tags_; }

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
            (void)key;
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
