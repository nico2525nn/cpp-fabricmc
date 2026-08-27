// EquipmentComponent — plan8 entity section
// Encapsulates mob equipment (6 slots) and synchronizes via SetEquipment 0x60.
// Provides armor calculation and EquipmentComponent-driven state sync.
#pragma once
#include <array>
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

    // Write SetEquipment payload (without entity id). Caller prefixes varint entityId.
    void writePayload(WriteBuffer& out) const {
        for (int i=0;i<COUNT;++i) {
            if (slots_[i].empty()) continue;
            out.varint(i);
            slots_[i].write(out);
        }
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

// Helper to sync equipment to all tracking players (broadcast SetEquipment)
// Implemented in GameServer.cpp via GameServer::sendEquipment
} // namespace cppfm
