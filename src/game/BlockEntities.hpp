// BlockEntities: chest/furnace state stored per block position (plan3.md
// "チェスト/かまどUI" data side). Serialized into Anvil `block_entities`
// compounds so a vanilla server reads our chests and vice versa.
#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../core/NBTValue.hpp"
#include "../generated/BlockStates.hpp"
#include "Items.hpp"

namespace cppfm {

// Vanilla-style packed position: x:26 | z:26 | y:12 (signed fields).
inline std::int64_t posKey(std::int32_t x, std::int32_t y, std::int32_t z) {
    return (static_cast<std::int64_t>(static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(x)) & 0x3FFFFFFULL) << 38) |
           (static_cast<std::int64_t>(static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(z)) & 0x3FFFFFFULL) << 12) |
           static_cast<std::int64_t>(static_cast<std::uint64_t>(y) & 0xFFFULL);
}
inline std::int32_t posKeyUnpackX(std::int64_t k) {
    const std::uint64_t v = (static_cast<std::uint64_t>(k) >> 38) & 0x3FFFFFFULL;
    return static_cast<std::int32_t>(v >= (1ULL << 25) ? v - (1ULL << 26) : v);
}
inline std::int32_t posKeyUnpackZ(std::int64_t k) {
    const std::uint64_t v = (static_cast<std::uint64_t>(k) >> 12) & 0x3FFFFFFULL;
    return static_cast<std::int32_t>(v >= (1ULL << 25) ? v - (1ULL << 26) : v);
}
inline std::int32_t posKeyUnpackY(std::int64_t k) {
    const std::uint64_t v = static_cast<std::uint64_t>(k) & 0xFFFULL;
    return static_cast<std::int32_t>(v >= (1ULL << 11) ? v - (1ULL << 12) : v);
}

struct ChestData {
    static constexpr int kSlots = 27;
    ItemStack slots[kSlots];
};

struct FurnaceData {
    static constexpr int kInput = 0, kFuel = 1, kOutput = 2;
    ItemStack slots[3];
    std::int16_t burnTicks = 0;          // remaining fuel burn
    std::int16_t burnDuration = 0;       // total of currently burning fuel
    std::int16_t cookProgress = 0;       // ticks toward cookTotal
    std::int16_t cookTotal = 200;
};

struct BrewingData {
    static constexpr int kSlots = 5; // 0-2 potions, 3 ingredient, 4 fuel (blaze powder)
    ItemStack slots[kSlots];
    std::int16_t brewTime = 0; // 0..400
    std::int16_t fuel = 0;     // remaining fuel (0..20)
};

// Generic container used by hoppers (5) and dispensers (9).
struct GenericContainerData {
    static constexpr int kMaxSlots = 9;
    ItemStack slots[kMaxSlots];
    std::uint8_t slotCount = kMaxSlots;
};

struct BlockEntity {
    enum class Kind { Chest, Furnace, Hopper, Dispenser, Dropper, Barrel, ShulkerBox, Brewing };
    Kind kind = Kind::Chest;
    ChestData chest{};
    FurnaceData furnace{};
    GenericContainerData generic{};      // hopper/dispenser/dropper
    BrewingData brewing{};
    bool isDropper() const { return kind == Kind::Dropper; }
    bool isDispenser() const { return kind == Kind::Dispenser; }
};

class BlockEntityStore {
public:
    BlockEntity* get(std::int64_t key) {
        auto it = map_.find(key);
        return it == map_.end() ? nullptr : &it->second;
    }
    BlockEntity* getAt(std::int32_t x, std::int32_t y, std::int32_t z) {
        return get(posKey(x, y, z));
    }

    BlockEntity& create(std::int64_t key, BlockEntity::Kind kind) {
        BlockEntity& be = map_[key];
        be = BlockEntity{};
        be.kind = kind;
        dirty_.insert(key);
        return be;
    }
    void remove(std::int64_t key) {
        auto it = map_.find(key);
        if (it != map_.end()) { map_.erase(it); dirty_.insert(key); }
    }
    // Remove entities whose block no longer exists (called after chunk edits).
    bool empty() const { return map_.empty(); }
    std::size_t size() const { return map_.size(); }

    template <typename Fn> void forEach(Fn fn) {
        for (auto& [k, be] : map_) fn(k, be);
    }
    std::unordered_map<std::int64_t, BlockEntity>& raw() { return map_; }

    // ------------------------------------------------------------ persistence
    // Writes all entities inside chunk (cx,cz) into `outList` (a List value).
    void writeChunkNbt(std::int32_t cx, std::int32_t cz, nbt::Value& outList) const {
        for (const auto& [k, be] : map_) {
            const std::int32_t x = posKeyUnpackX(k);
            const std::int32_t y = posKeyUnpackY(k);
            const std::int32_t z = posKeyUnpackZ(k);
            if ((x >> 4) != cx || (z >> 4) != cz) continue;
            nbt::Value e = nbt::Value::makeCompound();
            e.set("keepPacked", nbt::Value::makeByte(0));
            if (be.kind == BlockEntity::Kind::Chest) {
                e.set("id", nbt::Value::makeString("minecraft:chest"));
                writeItems(e, be.chest.slots, ChestData::kSlots, "Items");
            } else if (be.kind == BlockEntity::Kind::Hopper) {
                e.set("id", nbt::Value::makeString("minecraft:hopper"));
                writeItems(e, be.generic.slots, 5, "Items");
            } else if (be.kind == BlockEntity::Kind::Dispenser) {
                e.set("id", nbt::Value::makeString("minecraft:dispenser"));
                writeItems(e, be.generic.slots, 9, "Items");
            } else if (be.kind == BlockEntity::Kind::Dropper) {
                e.set("id", nbt::Value::makeString("minecraft:dropper"));
                writeItems(e, be.generic.slots, 9, "Items");
            } else if (be.kind == BlockEntity::Kind::Barrel) {
                e.set("id", nbt::Value::makeString("minecraft:barrel"));
                writeItems(e, be.chest.slots, ChestData::kSlots, "Items");
            } else if (be.kind == BlockEntity::Kind::ShulkerBox) {
                e.set("id", nbt::Value::makeString("minecraft:shulker_box"));
                writeItems(e, be.chest.slots, ChestData::kSlots, "Items");
            } else if (be.kind == BlockEntity::Kind::Brewing) {
                e.set("id", nbt::Value::makeString("minecraft:brewing_stand"));
                writeItems(e, be.brewing.slots, BrewingData::kSlots, "Items");
                e.set("BrewTime", nbt::Value::makeShort(be.brewing.brewTime));
                e.set("Fuel", nbt::Value::makeByte(static_cast<std::int8_t>(be.brewing.fuel)));
            } else {
                e.set("id", nbt::Value::makeString("minecraft:furnace"));
                writeFurnaceItems(e, be.furnace);
                e.set("BurnTime", nbt::Value::makeShort(be.furnace.burnTicks));
                e.set("CookTime", nbt::Value::makeShort(be.furnace.cookProgress));
                e.set("CookTimeTotal", nbt::Value::makeShort(be.furnace.cookTotal));
            }
            e.set("x", nbt::Value::makeInt(x));
            e.set("y", nbt::Value::makeInt(y));
            e.set("z", nbt::Value::makeInt(z));
            outList.list.push_back(std::move(e));
        }
    }

    void readChunkNbt(const nbt::Value& root) {
        for (auto& [k, v] : root.comp) {
            if ((k == "block_entities" || k == "TileEntities") && v.tag == nbt::List) {
                for (const auto& e : v.list) readOne(e);
            }
        }
    }

private:
    void readOne(const nbt::Value& e) {
        const auto* xs = e.get("x"), * ys = e.get("y"), * zs = e.get("z");
        const auto* idv = e.get("id");
        if (!xs || !ys || !zs || !idv || idv->tag != nbt::String) return;
        const std::int64_t key = posKey(xs->i, ys->i, zs->i);
        const std::string& id = idv->str;
        if (id.find("chest") != std::string::npos &&
            id.find("ender") == std::string::npos) {
            BlockEntity& be = map_[key];
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Chest;
            readItems(e, be.chest.slots, ChestData::kSlots, "Items");
            dirty_.insert(key);
        } else if (id == "minecraft:hopper" || id == "minecraft:dispenser" ||
                   id == "minecraft:dropper") {
            BlockEntity& be = map_[key];
            be = BlockEntity{};
            if (id == "minecraft:hopper") be.kind = BlockEntity::Kind::Hopper;
            else if (id == "minecraft:dropper") be.kind = BlockEntity::Kind::Dropper;
            else be.kind = BlockEntity::Kind::Dispenser;
            const int n = be.kind == BlockEntity::Kind::Hopper ? 5 : 9;
            readItems(e, be.generic.slots, n, "Items");
            dirty_.insert(key);
        } else if (id == "minecraft:barrel") {
            BlockEntity& be = map_[key];
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Barrel;
            readItems(e, be.chest.slots, ChestData::kSlots, "Items");
            dirty_.insert(key);
        } else if (id.find("shulker_box") != std::string::npos) {
            BlockEntity& be = map_[key];
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::ShulkerBox;
            readItems(e, be.chest.slots, ChestData::kSlots, "Items");
            dirty_.insert(key);
        } else if (id.find("brewing") != std::string::npos) {
            BlockEntity& be = map_[key];
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Brewing;
            readItems(e, be.brewing.slots, BrewingData::kSlots, "Items");
            if (const auto* b = e.get("BrewTime")) be.brewing.brewTime = b->s;
            if (const auto* f = e.get("Fuel")) be.brewing.fuel = static_cast<std::int16_t>(f->b);
            dirty_.insert(key);
        } else if (id.find("furnace") != std::string::npos ||
                   id.find("smoker") != std::string::npos ||
                   id.find("blast_furnace") != std::string::npos) {
            BlockEntity& be = map_[key];
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Furnace;
            readItems(e, be.furnace.slots, 3, "Items");
            if (const auto* b = e.get("BurnTime")) be.furnace.burnTicks = b->s;
            if (const auto* c = e.get("CookTime")) be.furnace.cookProgress = c->s;
            if (const auto* t = e.get("CookTimeTotal")) be.furnace.cookTotal = t->s;
            dirty_.insert(key);
        }
    }
    static void writeItems(nbt::Value& e, const ItemStack* slots, int count,
                           const char* listName) {
        nbt::Value arr = nbt::Value::makeList(nbt::Compound);
        for (int i = 0; i < count; ++i) {
            const auto& s = slots[i];
            if (s.empty()) continue;
            nbt::Value item = nbt::Value::makeCompound();
            item.set("id", nbt::Value::makeString(s.name()));
            item.set("Count", nbt::Value::makeByte(static_cast<std::int8_t>(s.count)));
            item.set("Slot", nbt::Value::makeByte(static_cast<std::int8_t>(i)));
            arr.list.push_back(std::move(item));
        }
        e.set(listName, std::move(arr));
    }
    static void writeFurnaceItems(nbt::Value& e, const FurnaceData& f) {
        writeItems(e, f.slots, 3, "Items");
    }
    static void readItems(const nbt::Value& e, ItemStack* slots, int count,
                          const char* listName) {
        const nbt::Value* arr = e.get(listName);
        if (!arr) return;
        for (const auto& item : arr->list) {
            const auto* idv = item.get("id");
            const auto* cv = item.get("Count");
            const auto* sv = item.get("Slot");
            if (!idv || idv->tag != nbt::String) continue;
            auto it = gen::itemIdByName().find(idv->str);
            if (it == gen::itemIdByName().end()) continue;
            const int slot = sv ? sv->b : -1;
            if (slot < 0 || slot >= count) continue;
            slots[slot] = ItemStack::of(it->second, cv ? cv->b : 1);
        }
    }

    std::unordered_map<std::int64_t, BlockEntity> map_;

public:
    // Keys whose NBT must be flushed / clients re-synced.
    std::unordered_set<std::int64_t> dirty_;
};

} // namespace cppfm
