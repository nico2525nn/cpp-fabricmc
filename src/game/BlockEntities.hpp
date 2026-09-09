// ResetScore 0x49 (D26) — BE tick/save/load paths verified intact after scoreboard reset hardening; no BE state touches Scoreboard scores.
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
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
    std::int16_t burnTicks = 0;
    std::int16_t burnDuration = 0;
    std::int16_t cookProgress = 0;
    std::int16_t cookTotal = 200;
};

struct BrewingData {
    static constexpr int kSlots = 5;
    static constexpr int kBrewTicks = 400;
    static constexpr int kFuelPerBlaze = 20;
    ItemStack slots[kSlots];
    std::int16_t brewTime = 0;
    std::int16_t fuel = 0;
};

struct GenericContainerData {
    static constexpr int kMaxSlots = 9;
    ItemStack slots[kMaxSlots];
    std::uint8_t slotCount = kMaxSlots;
};

// CrafterBlockEntity is a nine-slot recipe inventory, but its disabled-slot
// mask and redstone bookkeeping are distinct from a dispenser/dropper.  Keep
// that state separate so automation cannot accidentally treat a disabled
// recipe slot as an ordinary container slot.
struct CrafterData {
    static constexpr int kSlots = 9;
    ItemStack slots[kSlots];
    std::uint16_t disabledSlots = 0; // bit i corresponds to recipe slot i
    std::int32_t craftingTicksRemaining = 0;
    std::int64_t redstoneCraftDueTick = -1;
    bool triggered = false;

    bool isSlotDisabled(int slot) const {
        return slot >= 0 && slot < kSlots &&
               (disabledSlots & (std::uint16_t{1} << slot)) != 0;
    }
    void setSlotEnabled(int slot, bool enabled) {
        if (slot < 0 || slot >= kSlots) return;
        const auto bit = static_cast<std::uint16_t>(std::uint16_t{1} << slot);
        if (enabled) disabledSlots = static_cast<std::uint16_t>(disabledSlots & ~bit);
        else disabledSlots = static_cast<std::uint16_t>(disabledSlots | bit);
    }
};

struct MovingPistonData {
    std::uint16_t state = 0;
    std::int32_t facing = 0;
    bool extending = true;
    float progress = 0.f;
    std::int64_t finishTick = 0;
};

struct SignData {
    std::string front[4];
    std::string back[4];
    bool hasFront = false;
    bool hasBack = false;
};

struct BlockEntity {
    enum class Kind { Chest, Furnace, Hopper, Dispenser, Dropper, Barrel, ShulkerBox, Brewing, Crafter, MovingPiston, Sign };
    Kind kind = Kind::Chest;
    ChestData chest{};
    FurnaceData furnace{};
    GenericContainerData generic{};
    CrafterData crafter{};
    BrewingData brewing{};
    MovingPistonData movingPiston{};
    SignData sign{};
    // Menus from different sessions can operate on one container while the
    // server tick is ticking the same block entity.  Keep the lock shared
    // across copies/owners; BlockEntityStore snapshots retain the object.
    mutable std::shared_ptr<std::recursive_mutex> stateMtx =
        std::make_shared<std::recursive_mutex>();

    BlockEntity() = default;
    BlockEntity(const BlockEntity&) = default;
    BlockEntity& operator=(const BlockEntity& other) {
        if (this == &other) return *this;
        kind = other.kind;
        chest = other.chest;
        furnace = other.furnace;
        generic = other.generic;
        crafter = other.crafter;
        brewing = other.brewing;
        movingPiston = other.movingPiston;
        sign = other.sign;
        // Preserve this object's mutex identity when resetting its contents.
        return *this;
    }
    bool isDropper() const { return kind == Kind::Dropper; }
    bool isDispenser() const { return kind == Kind::Dispenser; }
};

class BlockEntityStore {
public:
    using Owner = std::shared_ptr<BlockEntity>;

    Owner getShared(std::int64_t key) const {
        std::lock_guard lock(mutex_);
        const auto it = map_.find(key);
        return it == map_.end() ? Owner{} : it->second;
    }

    BlockEntity* get(std::int64_t key) {
        return getShared(key).get();
    }
    BlockEntity* getAt(std::int32_t x, std::int32_t y, std::int32_t z) {
        return get(posKey(x, y, z));
    }

    Owner createShared(std::int64_t key, BlockEntity::Kind kind) {
        std::lock_guard lock(mutex_);
        auto& owner = map_[key];
        if (!owner) owner = std::make_shared<BlockEntity>();
        // Preserve the shared object's identity so menus that already hold an
        // owner never retain a dangling pointer when a block changes kind.
        std::lock_guard entityLock(*owner->stateMtx);
        *owner = BlockEntity{};
        owner->kind = kind;
        dirty_.insert(key);
        return owner;
    }

    BlockEntity& create(std::int64_t key, BlockEntity::Kind kind) {
        return *createShared(key, kind);
    }
    void remove(std::int64_t key) {
        std::lock_guard lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) { map_.erase(it); dirty_.insert(key); }
    }
    void markDirty(std::int64_t key) {
        std::lock_guard lock(mutex_);
        dirty_.insert(key);
    }
    bool empty() const {
        std::lock_guard lock(mutex_);
        return map_.empty();
    }
    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return map_.size();
    }

    template <typename Fn> void forEach(Fn fn) {
        std::vector<std::pair<std::int64_t, Owner>> snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot.reserve(map_.size());
            for (const auto& [key, owner] : map_)
                snapshot.emplace_back(key, owner);
        }
        // Invoke user code outside the map lock.  Besides avoiding a long
        // critical section, this permits callbacks to remove/create entries
        // without invalidating the iteration.  The shared owners keep the
        // pointed-to values alive for the callback duration.
        for (auto& [key, owner] : snapshot)
            if (owner) {
                std::lock_guard entityLock(*owner->stateMtx);
                fn(key, *owner);
            }
    }

    void writeChunkNbt(std::int32_t cx, std::int32_t cz, nbt::Value& outList) const {
        const auto snapshot = ownersSnapshot();
        for (const auto& [k, owner] : snapshot) {
            if (!owner) continue;
            std::lock_guard entityLock(*owner->stateMtx);
            const BlockEntity& be = *owner;
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
            } else if (be.kind == BlockEntity::Kind::Crafter) {
                e.set("id", nbt::Value::makeString("minecraft:crafter"));
                writeItems(e, be.crafter.slots, CrafterData::kSlots, "Items");
                nbt::Value disabled;
                disabled.tag = nbt::IntArray;
                for (int slot = 0; slot < CrafterData::kSlots; ++slot)
                    if (be.crafter.isSlotDisabled(slot))
                        disabled.intArray.push_back(slot);
                e.set("disabled_slots", std::move(disabled));
                e.set("crafting_ticks_remaining",
                      nbt::Value::makeInt(be.crafter.craftingTicksRemaining));
                e.set("triggered", nbt::Value::makeByte(be.crafter.triggered ? 1 : 0));
            } else if (be.kind == BlockEntity::Kind::Sign) {
                e.set("id", nbt::Value::makeString("minecraft:sign"));
                e.set("is_waxed", nbt::Value::makeByte(0));
                auto writeSide = [&](const char* key, const std::string lines[4]) {
                    nbt::Value t = nbt::Value::makeCompound();
                    nbt::Value msgs = nbt::Value::makeList(nbt::String, 4);
                    for (int i = 0; i < 4; ++i)
                        msgs.list.push_back(nbt::Value::makeString(lines[i]));
                    t.set("messages", std::move(msgs));
                    t.set("color", nbt::Value::makeString("black"));
                    t.set("has_glowing_text", nbt::Value::makeByte(0));
                    e.set(key, std::move(t));
                };
                writeSide("front_text", be.sign.front);
                writeSide("back_text", be.sign.back);
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
        std::lock_guard lock(mutex_);
        for (auto& [k, v] : root.comp) {
            if ((k == "block_entities" || k == "TileEntities") && v.tag == nbt::List) {
                for (const auto& e : v.list) readOne(e);
            }
        }
    }

private:
    std::vector<std::pair<std::int64_t, Owner>> ownersSnapshot() const {
        std::lock_guard lock(mutex_);
        std::vector<std::pair<std::int64_t, Owner>> snapshot;
        snapshot.reserve(map_.size());
        for (const auto& [key, owner] : map_)
            snapshot.emplace_back(key, owner);
        return snapshot;
    }

    BlockEntity& upsertLocked(std::int64_t key) {
        auto& owner = map_[key];
        if (!owner) owner = std::make_shared<BlockEntity>();
        return *owner;
    }

    void readOne(const nbt::Value& e) {
        const auto* xs = e.get("x"), * ys = e.get("y"), * zs = e.get("z");
        const auto* idv = e.get("id");
        if (!xs || !ys || !zs || !idv || idv->tag != nbt::String) return;
        const std::int64_t key = posKey(xs->i, ys->i, zs->i);
        const std::string& id = idv->str;
        BlockEntity& be = upsertLocked(key);
        std::lock_guard entityLock(*be.stateMtx);
        if (id.find("chest") != std::string::npos &&
            id.find("ender") == std::string::npos) {
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Chest;
            readItems(e, be.chest.slots, ChestData::kSlots, "Items");
            dirty_.insert(key);
        } else if (id == "minecraft:hopper" || id == "minecraft:dispenser" ||
                   id == "minecraft:dropper") {
            be = BlockEntity{};
            if (id=="minecraft:hopper") be.kind = BlockEntity::Kind::Hopper;
            else if (id=="minecraft:dropper") be.kind = BlockEntity::Kind::Dropper;
            else be.kind = BlockEntity::Kind::Dispenser;
            const int n = be.kind == BlockEntity::Kind::Hopper ? 5 : 9;
            readItems(e, be.generic.slots, n, "Items");
            dirty_.insert(key);
        } else if (id == "minecraft:barrel") {
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Barrel;
            readItems(e, be.chest.slots, ChestData::kSlots, "Items");
            dirty_.insert(key);
        } else if (id.find("shulker_box") != std::string::npos) {
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::ShulkerBox;
            readItems(e, be.chest.slots, ChestData::kSlots, "Items");
            dirty_.insert(key);
        } else if (id.find("brewing") != std::string::npos) {
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Brewing;
            readItems(e, be.brewing.slots, BrewingData::kSlots, "Items");
            if (const auto* b = e.get("BrewTime")) be.brewing.brewTime = b->s;
            if (const auto* f = e.get("Fuel")) be.brewing.fuel = static_cast<std::int16_t>(f->b);
            dirty_.insert(key);
        } else if (id == "minecraft:crafter") {
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Crafter;
            readItems(e, be.crafter.slots, CrafterData::kSlots, "Items");
            if (const auto* disabled = e.get("disabled_slots")) {
                if (disabled->tag == nbt::IntArray) {
                    for (const auto slot : disabled->intArray)
                        be.crafter.setSlotEnabled(slot, false);
                } else if (disabled->tag == nbt::List) {
                    // Be lenient with hand-authored/older fixtures that use
                    // an integer list instead of the vanilla int array.
                    for (const auto& value : disabled->list)
                        if (value.tag == nbt::Int)
                            be.crafter.setSlotEnabled(value.i, false);
                }
            }
            if (const auto* ticks = e.get("crafting_ticks_remaining")) {
                if (ticks->tag == nbt::Int) be.crafter.craftingTicksRemaining = std::max(0, ticks->i);
                else if (ticks->tag == nbt::Short) be.crafter.craftingTicksRemaining = std::max(0, static_cast<int>(ticks->s));
            }
            if (const auto* triggered = e.get("triggered")) {
                if (triggered->tag == nbt::Byte) be.crafter.triggered = triggered->b != 0;
                else if (triggered->tag == nbt::Int) be.crafter.triggered = triggered->i != 0;
            }
            dirty_.insert(key);
        } else if (id.find("furnace") != std::string::npos ||
                   id.find("smoker") != std::string::npos ||
                   id.find("blast_furnace") != std::string::npos) {
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Furnace;
            readItems(e, be.furnace.slots, 3, "Items");
            if (const auto* b = e.get("BurnTime")) be.furnace.burnTicks = b->s;
            if (const auto* c = e.get("CookTime")) be.furnace.cookProgress = c->s;
            if (const auto* t = e.get("CookTimeTotal")) be.furnace.cookTotal = t->s;
            dirty_.insert(key);
        } else if (id == "minecraft:sign" || id == "minecraft:hanging_sign") {
            be = BlockEntity{};
            be.kind = BlockEntity::Kind::Sign;
            auto readSide = [&](const char* side, std::string out[4], bool& has) {
                const auto* t = e.get(side);
                if (!t || t->tag != nbt::Compound) return;
                const auto* m = t->get("messages");
                if (!m || m->tag != nbt::List) return;
                for (int i = 0; i < 4 && i < static_cast<int>(m->list.size()); ++i)
                    if (m->list[i].tag == nbt::String) out[i] = m->list[i].str;
                has = true;
            };
            readSide("front_text", be.sign.front, be.sign.hasFront);
            readSide("back_text", be.sign.back, be.sign.hasBack);
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

    mutable std::recursive_mutex mutex_;
    std::unordered_map<std::int64_t, Owner> map_;
    std::unordered_set<std::int64_t> dirty_;
};

} // namespace cppfm
