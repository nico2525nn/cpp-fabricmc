// Items: ItemStack with 1.20.5+ data components, item tables and helpers.
//
// Wire format (Slot): varint count; when >0: varint itemId,
// varint addedComponents, varint removedComponents, then each added component
// as (varint typeId, varint payloadLen, payload bytes) and each removed entry
// as a bare varint typeId. We keep unknown component payloads verbatim so
// client-provided stacks round-trip losslessly.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "../generated/ItemIds.hpp"

namespace cppfm {

struct ItemStack {
    std::uint32_t itemId = 0;
    std::int16_t count = 0;
    // added components: typeId -> raw payload bytes
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> components;
    std::vector<std::uint32_t> removedComponents;

    bool empty() const { return count <= 0 || itemId == 0; }
    static ItemStack air() { return {}; }
    static ItemStack of(std::uint32_t id, std::int16_t n = 1) {
        ItemStack s; s.itemId = id; s.count = n; return s;
    }
    static ItemStack ofName(const std::string& name, std::int16_t n = 1) {
        auto it = gen::itemIdByName().find(name);
        return it != gen::itemIdByName().end() ? of(it->second, n) : air();
    }

    // ------------------------------------------------------------------ io
    void write(WriteBuffer& out) const {
        if (empty()) { out.varint(0); return; }
        out.varint(count);
        out.varint(static_cast<std::int32_t>(itemId));
        out.varint(static_cast<std::int32_t>(components.size()));
        out.varint(static_cast<std::int32_t>(removedComponents.size()));
        for (auto& [typeId, payload] : components) {
            out.varint(static_cast<std::int32_t>(typeId));
            WriteBuffer tmp;
            tmp.varint(static_cast<std::int32_t>(payload.size()));
            tmp.raw(payload.data(), payload.size());
            out.raw(tmp.data.data(), tmp.data.size());
        }
        for (auto t : removedComponents) out.varint(static_cast<std::int32_t>(t));
    }

    static ItemStack read(ReadBuffer& in) {
        const std::int32_t cnt = in.varint();
        if (cnt <= 0) return air();
        ItemStack s;
        s.count = static_cast<std::int16_t>(cnt);
        s.itemId = static_cast<std::uint32_t>(in.varint());
        const std::int32_t addC = in.varint();
        const std::int32_t remC = in.varint();
        for (std::int32_t i = 0; i < addC; ++i) {
            const auto typeId = static_cast<std::uint32_t>(in.varint());
            const auto len = in.varint();
            auto payload = in.bytes(static_cast<std::size_t>(len < 0 ? 0 : len));
            s.components.emplace_back(typeId, std::move(payload));
        }
        for (std::int32_t i = 0; i < remC; ++i)
            s.removedComponents.push_back(static_cast<std::uint32_t>(in.varint()));
        return s;
    }

    std::string name() const {
        static thread_local std::unordered_map<std::uint32_t, std::string> inv;
        if (inv.empty())
            for (auto& e : gen::kItems) inv.emplace(e.second, std::string(e.first));
        auto it = inv.find(itemId);
        if (it == inv.end()) return "minecraft:air";
        if (it->second.rfind("minecraft:", 0) == 0 && it->second.find('{') == std::string::npos)
            return it->second;
        return it->second;
    }
};

} // namespace cppfm
