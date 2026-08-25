// GameData: runtime views over the synced registries (plan2.md "登録情報同期").
//
// The captured registry blobs are replayed verbatim during configuration; this
// module additionally parses their entry keys in wire order so gameplay code
// can translate between registry identifiers and numeric ids exactly like a
// vanilla client would after registry sync.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "../core/NBT.hpp"
#include "EmbeddedData.hpp"

namespace cppfm {

class GameData {
public:
    // Parse all synced registries from the embedded blobs.
    void load(const EmbeddedData& data) {
        for (const auto& r : data.registries()) {
            ReadBuffer in(r.body);
            const std::string key = in.string();     // registry name
            auto& list = orders_[key];
            const std::int32_t n = in.varint();
            list.reserve(static_cast<std::size_t>(n));
            for (std::int32_t i = 0; i < n; ++i) {
                std::string ek = in.string();
                if (in.boolean()) { nbt::Reader reader(in); reader.skipRoot(); }
                list.push_back(std::move(ek));
            }
        }
    }

    // Numeric id of `key` inside `registry` ("minecraft:damage_type", ...).
    std::int32_t idOf(const std::string& registry, const std::string& key) const {
        auto it = orders_.find(registry);
        if (it == orders_.end()) return -1;
        for (std::size_t i = 0; i < it->second.size(); ++i)
            if (it->second[i] == key) return static_cast<std::int32_t>(i);
        return -1;
    }
    const std::string& keyOf(const std::string& registry, std::int32_t id) const {
        static const std::string empty;
        auto it = orders_.find(registry);
        if (it == orders_.end() || id < 0 ||
            static_cast<std::size_t>(id) >= it->second.size()) return empty;
        return it->second[static_cast<std::size_t>(id)];
    }
    const std::vector<std::string>& order(const std::string& registry) const {
        static const std::vector<std::string> empty;
        auto it = orders_.find(registry);
        return it != orders_.end() ? it->second : empty;
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> orders_;
};

} // namespace cppfm
