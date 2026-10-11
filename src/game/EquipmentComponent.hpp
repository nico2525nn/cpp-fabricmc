// calculation and EquipmentComponent-driven state sync.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <string>
#include "../core/ByteBuffer.hpp"
#include "Items.hpp"
#include "Entities.hpp"

namespace cppfm {

// Equipment slots as per vanilla: 0 mainhand 1 offhand 2 boots 3 leggings 4 chest 5 head
class EquipmentComponent {
public:
    static constexpr int SLOT_MAINHAND = 0;
    static constexpr int SLOT_OFFHAND  = 1;
    static constexpr int SLOT_BOOTS    = 2;
    static constexpr int SLOT_LEGGINGS = 3;
    static constexpr int SLOT_CHEST    = 4;
    static constexpr int SLOT_HEAD     = 5;
    static constexpr int COUNT = 6;

    EquipmentComponent() = default;
    explicit EquipmentComponent(const std::array<ItemStack,6>& arr) : slots_(arr) {}

    const ItemStack& get(int slot) const { return slots_[slot]; }
    ItemStack& get(int slot) { return slots_[slot]; }
    void set(int slot, const ItemStack& stack) { if (slot>=0 && slot<COUNT) slots_[slot]=stack; }
    bool hasAny() const { for (auto& s: slots_) if (!s.empty()) return true; return false; }
    const std::array<ItemStack,6>& array() const { return slots_; }
    std::array<ItemStack,6>& array() { return slots_; }

    // Compute total armor points from equipped armor slots (2..5)
    int totalArmor() const {
        int tot=0;
        for (int i=2;i<6;++i) if (!slots_[i].empty()) tot+=armorPointsForItem(slots_[i].itemId);
        return tot;
    }

    std::array<float,2> handDropChances{0.085f,0.085f};
    std::array<float,4> armorDropChances{0.085f,0.085f,0.085f,0.085f};

    // Write SetEquipment payload (without entity id). Caller prefixes varint entityId.
    // 1.21.4 encodes each slot marker as one signed byte. Bit 0x80 marks
    // another entry; it is not a VarInt continuation bit.
    void writePayload(WriteBuffer& out,
                      const bool includeEmpty = false) const {
        std::array<int, COUNT> present{};
        std::size_t presentCount = 0;
        for (int i = 0; i < COUNT; ++i) {
            if (includeEmpty || !slots_[i].empty())
                present[presentCount++] = i;
        }
        for (std::size_t idx = 0; idx < presentCount; ++idx) {
            int i = present[idx];
            const bool more = idx + 1 < presentCount;
            const auto slotByte = static_cast<std::uint8_t>(
                i | (more ? 0x80 : 0));
            out.u8(slotByte);
            if (slots_[i].empty()) ItemStack::air().write(out);
            else slots_[i].write(out);
        }
    }
    // Single-slot payload helper (dynamic sync on equip change)
    void writePayloadSingle(WriteBuffer& out, int slot) const {
        if (slot<0 || slot>=COUNT) return;
        out.u8(static_cast<std::uint8_t>(slot));
        if (!slots_[slot].empty()) slots_[slot].write(out);
        else ItemStack::air().write(out);
    }

    // Apply equipment from a data-driven map (slot -> item name)
    void applyFromData(const std::unordered_map<int,std::string>& eq, const std::unordered_map<std::string,std::uint32_t>& itemMap) {
        for (auto& kv: eq) {
            int slot = kv.first;
            auto it = itemMap.find(kv.second);
            if (it != itemMap.end() && slot>=0 && slot<COUNT) slots_[slot]=ItemStack::of(it->second,1);
        }
    }

private:
    std::array<ItemStack,6> slots_{};
};

// Helper to sync equipment to all tracking players (broadcast SetEquipment) is implemented in GameServer_items.cpp.
} // namespace cppfm
