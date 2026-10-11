// Items: ItemStack with 1.20.5+ data components, item tables and helpers.
#pragma once
#include "Constants.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <limits>
#include <iterator>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <type_traits>
#include "../core/ByteBuffer.hpp"
#include "../core/NBT.hpp"
#include "../core/NBTValue.hpp"
#include "../generated/ItemIds.hpp"
#include "ItemComponentNbt.hpp"

namespace cppfm {

struct PersistedItemNbt {
    std::vector<std::uint8_t> record;
    // Count and Slot are excluded so equivalent stacks can still merge.
    std::vector<std::uint8_t> identity;
    // Vanilla NBT can contain components this build cannot encode on the
    // protocol. Keep them for persistence, but never silently omit them from
    // an outgoing ItemStack.
    bool hasOpaqueComponents = false;
};

struct ItemStack {
    static constexpr std::size_t kMaxComponentCount = 4096;
    static constexpr std::size_t kMaxComponentPayloadBytes = 1024u * 1024u;
    static constexpr std::size_t kMaxTotalComponentPayloadBytes =
        2u * 1024u * 1024u;
    // The 1.21.4 protocol's SlotComponentType registry ends at id 66.
    // Payload support is narrower and checked separately when encoding.
    static constexpr std::uint32_t kMaxProtocolComponentId = 66;

    std::uint32_t itemId = 0;
    std::int16_t count = 0;
    // added components: typeId -> raw payload bytes
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> components;
    std::vector<std::uint32_t> removedComponents;
    // Opaque block-entity item data survives moves/splits through ordinary
    // ItemStack copies without being mistaken for a newly created same-ID item.
    std::shared_ptr<const PersistedItemNbt> persistedNbt;
    // Component setters retain the original item NBT template and record
    // removed fields so a save can patch known values without dropping opaque
    // components from the same stack.
    std::vector<std::uint32_t> persistedNbtEditedComponents;

    // Stage persistence bookkeeping and vector growth before replacing live
    // component state. After reserve succeeds, erase and moves are
    // non-throwing for this component representation.
    void replaceComponentsForNbtEdit(
        std::initializer_list<std::uint32_t> affectedIds,
        std::uint32_t replacementId,
        std::optional<std::vector<std::uint8_t>> replacement) {
        using Component = decltype(components)::value_type;
        static_assert(std::is_nothrow_move_assignable_v<Component>);
        static_assert(std::is_nothrow_move_constructible_v<Component>);

        auto editedComponents = persistedNbtEditedComponents;
        auto nextRemovedComponents = removedComponents;
        if (persistedNbt) {
            for (const auto id : affectedIds) {
                if (std::find(editedComponents.begin(), editedComponents.end(),
                              id) == editedComponents.end())
                    editedComponents.push_back(id);
            }
        }
        // A component cannot be both added/replaced and removed in the same
        // protocol patch. Editing either enchantment component also replaces
        // the other one (ordinary vs. stored), so clear both removal markers.
        nextRemovedComponents.erase(std::remove_if(
            nextRemovedComponents.begin(), nextRemovedComponents.end(),
            [&](const auto id) {
                return std::find(affectedIds.begin(), affectedIds.end(), id) !=
                       affectedIds.end();
            }), nextRemovedComponents.end());
        if (replacement) components.reserve(components.size() + 1);

        components.erase(std::remove_if(
            components.begin(), components.end(), [&](const auto& component) {
                return std::find(affectedIds.begin(), affectedIds.end(),
                                 component.first) != affectedIds.end();
            }), components.end());
        if (replacement)
            components.emplace_back(replacementId, std::move(*replacement));
        persistedNbtEditedComponents.swap(editedComponents);
        removedComponents.swap(nextRemovedComponents);
    }

    bool empty() const { return count <= 0 || itemId == 0; }
    // Item identity for merging deliberately excludes count and emptiness;
    // callers retain their existing empty-slot policy.
    static bool sameComponents(
        const std::vector<std::pair<std::uint32_t,
                                    std::vector<std::uint8_t>>>& lhs,
        const std::vector<std::pair<std::uint32_t,
                                    std::vector<std::uint8_t>>>& rhs) {
        // A valid patch has at most one entry for each protocol component id
        // (0..66). Bound the order-insensitive comparison before running the
        // allocation-free multiset check so malformed in-memory stacks cannot
        // trigger quadratic work over an attacker-sized vector.
        constexpr std::size_t kMaximumUniqueComponents =
            static_cast<std::size_t>(kMaxProtocolComponentId) + 1;
        if (lhs.size() != rhs.size() ||
            lhs.size() > kMaximumUniqueComponents) return false;
        return std::is_permutation(lhs.begin(), lhs.end(),
                                   rhs.begin(), rhs.end());
    }

    static bool sameRemovedComponents(
        const std::vector<std::uint32_t>& lhs,
        const std::vector<std::uint32_t>& rhs) {
        constexpr std::size_t kMaximumUniqueComponents =
            static_cast<std::size_t>(kMaxProtocolComponentId) + 1;
        if (lhs.size() != rhs.size() ||
            lhs.size() > kMaximumUniqueComponents) return false;
        return std::is_permutation(lhs.begin(), lhs.end(),
                                   rhs.begin(), rhs.end());
    }

    static const std::unordered_map<std::uint32_t, std::string>&
    itemNamesById() {
        static const auto names = [] {
            std::unordered_map<std::uint32_t, std::string> result;
            result.reserve(gen::kItems.size());
            for (const auto& [name, id] : gen::kItems)
                result.emplace(id, std::string(name));
            return result;
        }();
        return names;
    }

    bool sameKnownItemData(const ItemStack& other) const {
        return itemId == other.itemId &&
               sameComponents(components, other.components) &&
               sameRemovedComponents(removedComponents,
                                     other.removedComponents);
    }

    bool sameItemData(const ItemStack& other) const {
        static const std::vector<std::uint8_t> kNoOpaqueIdentity;
        const auto& lhsOpaque = persistedNbt ? persistedNbt->identity
                                             : kNoOpaqueIdentity;
        const auto& rhsOpaque = other.persistedNbt ? other.persistedNbt->identity
                                                   : kNoOpaqueIdentity;
        return lhsOpaque == rhsOpaque && sameKnownItemData(other);
    }

    bool hasValidComponentData() const {
        if (components.size() > kMaxComponentCount ||
            removedComponents.size() > kMaxComponentCount ||
            itemId == 0 || !itemNamesById().contains(itemId) ||
            itemId > static_cast<std::uint32_t>(
                         std::numeric_limits<std::int32_t>::max()))
            return false;
        std::size_t totalPayloadBytes = 0;
        std::unordered_set<std::uint32_t> addedIds;
        addedIds.reserve(components.size());
        for (const auto& [typeId, payload] : components) {
            if (typeId > kMaxProtocolComponentId ||
                !addedIds.insert(typeId).second ||
                payload.size() > kMaxComponentPayloadBytes ||
                payload.size() > kMaxTotalComponentPayloadBytes -
                                     totalPayloadBytes)
                return false;
            totalPayloadBytes += payload.size();
        }
        std::unordered_set<std::uint32_t> removedIds;
        removedIds.reserve(removedComponents.size());
        for (const auto typeId : removedComponents) {
            if (typeId > kMaxProtocolComponentId ||
                addedIds.contains(typeId) || !removedIds.insert(typeId).second)
                return false;
        }
        return true;
    }

    bool canWriteToProtocol() const {
        if (empty()) return true;
        if (!hasValidComponentData() ||
            (persistedNbt && persistedNbt->hasOpaqueComponents))
            return false;
        for (const auto& [typeId, payload] : components) {
            if (!item_component_nbt::componentName(typeId) ||
                !item_component_nbt::isValidProtocolComponentPayload(
                    typeId, payload))
                return false;
        }
        return true;
    }
    static ItemStack air() { return {}; }
    static ItemStack of(std::uint32_t id, std::int16_t n = 1) {
        ItemStack s; s.itemId = id; s.count = n; return s;
    }
    static ItemStack ofName(const std::string& name, std::int16_t n = 1) {
        auto it = gen::itemIdByName().find(name);
        return it != gen::itemIdByName().end() ? of(it->second, n) : air();
    }

    static void canonicalizeCompoundOrder(nbt::Value& value) {
        if (value.tag == nbt::Compound) {
            for (auto& [name, child] : value.comp) {
                (void)name;
                canonicalizeCompoundOrder(child);
            }
            std::stable_sort(value.comp.begin(), value.comp.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.first < rhs.first;
                });
        } else if (value.tag == nbt::List) {
            for (auto& child : value.list) canonicalizeCompoundOrder(child);
        }
    }

    static std::shared_ptr<const PersistedItemNbt> preserveNbtTemplate(
        const nbt::Value& item, std::size_t depth = 0) {
        if (depth > item_component_nbt::kMaxComponentDepth) return {};
        if (item.tag != nbt::Compound) return {};

        bool hasTemplate = false;
        bool hasOpaqueComponents = false;
        nbt::Value opaqueIdentity = nbt::Value::makeCompound();
        for (const auto& [key, value] : item.comp) {
            if (key == "Count" || key == "count" || key == "Slot" ||
                key == "id") continue;
            hasTemplate = true;
            if (key == "removed") continue;
            if (key == "components" && value.tag == nbt::List) {
                // Older saves can encode components as a list of numeric IDs
                // and opaque payloads. Preserve the complete list instead of
                // reinterpreting only entries that this build recognizes.
                if (!value.list.empty()) {
                    hasOpaqueComponents = true;
                    opaqueIdentity.set("components", value);
                }
                continue;
            }
            if (key != "components" || value.tag != nbt::Compound) {
                // Unknown root-level fields are persisted in the template but
                // have no corresponding protocol representation here. Mark
                // them opaque so legacy `tag` or mod/plugin data cannot be
                // silently stripped on a client-facing stack update.
                hasOpaqueComponents = true;
                opaqueIdentity.set(key, value);
                continue;
            }

            nbt::Value opaqueComponents = nbt::Value::makeCompound();
            for (const auto& [name, component] : value.comp) {
                if (!item_component_nbt::protocolComponentFromNbt(name,
                        component, depth + 1))
                    opaqueComponents.set(name, component);
            }
            hasOpaqueComponents = hasOpaqueComponents ||
                                  !opaqueComponents.comp.empty();
            if (!opaqueComponents.comp.empty())
                opaqueIdentity.set("components", std::move(opaqueComponents));
        }
        if (!hasTemplate) return {};

        nbt::Value record = item;
        canonicalizeCompoundOrder(opaqueIdentity);
        WriteBuffer recordBytes;
        nbt::writeFileRoot(recordBytes, record);
        auto preserved = std::make_shared<PersistedItemNbt>();
        preserved->record = std::move(recordBytes.data);
        preserved->hasOpaqueComponents = hasOpaqueComponents;
        if (!opaqueIdentity.comp.empty()) {
            WriteBuffer identityBytes;
            nbt::writeFileRoot(identityBytes, opaqueIdentity);
            preserved->identity = std::move(identityBytes.data);
        }
        return preserved;
    }

    static std::optional<ItemStack> fromNbt(const nbt::Value& item,
                                            std::size_t depth = 0) {
        if (depth > item_component_nbt::kMaxComponentDepth)
            return std::nullopt;
        if (item.tag != nbt::Compound) return std::nullopt;
        const auto* id = item.get("id");
        const auto* countTag = item.get("count");
        if (!countTag) countTag = item.get("Count");
        if (!id || id->tag != nbt::String) return std::nullopt;
        const auto itemId = gen::itemIdByName().find(id->str);
        if (itemId == gen::itemIdByName().end()) return std::nullopt;

        std::int32_t countValue = 1;
        if (countTag) {
            if (countTag == item.get("count") && countTag->tag == nbt::Int)
                countValue = countTag->i;
            else if (countTag->tag == nbt::Byte)
                countValue = countTag->b <=
                                     static_cast<std::uint8_t>(
                                         std::numeric_limits<std::int8_t>::max())
                                 ? countTag->b : -1;
            else if (countTag->tag == nbt::Short) countValue = countTag->s;
            else if (countTag->tag == nbt::Int) countValue = countTag->i;
            else return std::nullopt;
        }
        if (countValue <= 0 || countValue > std::numeric_limits<std::int16_t>::max())
            return std::nullopt;

        ItemStack stack = of(itemId->second,
                             static_cast<std::int16_t>(countValue));
        stack.persistedNbt = preserveNbtTemplate(item, depth);
        if (const auto* components = item.get("components")) {
            if (components->tag == nbt::Compound) {
                for (const auto& [name, saved] : components->comp) {
                    if (auto component =
                            item_component_nbt::protocolComponentFromNbt(
                                name, saved, depth + 1))
                        stack.components.emplace_back(
                            component->id, std::move(component->payload));
                }
            } else if (components->tag == nbt::List) {
                // Non-empty legacy lists are marked opaque by
                // preserveNbtTemplate(). Do not expose a partial typed view:
                // a skipped/unknown entry would otherwise disappear on save.
            } else {
                return std::nullopt;
            }
        }
        if (const auto* removed = item.get("removed")) {
            if (removed->tag != nbt::List) return std::nullopt;
            for (const auto& entry : removed->list) {
                if (entry.tag == nbt::Int && entry.i >= 0)
                    stack.removedComponents.push_back(
                        static_cast<std::uint32_t>(entry.i));
                else if (entry.tag == nbt::Byte)
                    stack.removedComponents.push_back(
                        static_cast<std::uint8_t>(entry.b));
            }
        }
        if (!stack.hasValidComponentData()) return std::nullopt;
        return stack;
    }

    nbt::Value toNbt(std::int32_t slot, std::size_t depth = 0) const {
        if (depth > item_component_nbt::kMaxComponentDepth)
            throw std::length_error("nested item component depth exceeds limit");
        if (!hasValidComponentData())
            throw std::length_error("item component data exceeds limits");
        nbt::Value item = nbt::Value::makeCompound();
        if (persistedNbt && !persistedNbt->record.empty()) {
            ReadBuffer input(persistedNbt->record);
            nbt::Parser parser(input);
            item = parser.readFileRoot();
            if (item.tag != nbt::Compound)
                throw std::runtime_error("persisted item NBT is not a compound");
        }
        item.set("id", nbt::Value::makeString(name()));
        item.comp.erase(std::remove_if(item.comp.begin(), item.comp.end(),
            [](const auto& field) { return field.first == "Count"; }),
            item.comp.end());
        item.set("count", nbt::Value::makeInt(count));
        if (slot >= 0) {
            item.set("Slot", nbt::Value::makeByte(
                                   static_cast<std::int8_t>(slot)));
        } else {
            item.comp.erase(std::remove_if(item.comp.begin(), item.comp.end(),
                [](const auto& field) { return field.first == "Slot"; }),
                item.comp.end());
        }
        const auto* savedComponents = item.get("components");
        if (savedComponents && savedComponents->tag == nbt::List) {
            if (!(persistedNbt && persistedNbt->hasOpaqueComponents)) {
                nbt::Value encoded = nbt::Value::makeList(nbt::Compound);
                for (const auto& [componentId, payload] : components) {
                    nbt::Value entry = nbt::Value::makeCompound();
                    entry.set("type", nbt::Value::makeInt(
                        static_cast<std::int32_t>(componentId)));
                    nbt::Value data;
                    data.tag = nbt::ByteArray;
                    data.byteArray = payload;
                    entry.set("data", std::move(data));
                    encoded.list.push_back(std::move(entry));
                }
                item.set("components", std::move(encoded));
            }
        } else {
            for (const auto& [componentId, payload] : components) {
                const auto componentName =
                    item_component_nbt::componentName(componentId);
                if (componentName &&
                    !item_component_nbt::applyProtocolComponentToNbt(
                        item, componentId, &payload, depth + 1))
                    throw std::runtime_error(
                        "cannot serialize item component " +
                        std::to_string(componentId) + " (" +
                        std::string(*componentName) + ")");
            }
            for (const auto componentId : persistedNbtEditedComponents) {
                const bool remainsPresent = std::any_of(
                    components.begin(), components.end(),
                    [componentId](const auto& component) {
                        return component.first == componentId;
                    });
                if (!remainsPresent &&
                    item_component_nbt::componentName(componentId) &&
                    !item_component_nbt::applyProtocolComponentToNbt(
                        item, componentId, nullptr, depth + 1))
                    throw std::runtime_error("cannot remove item component");
            }
        }
        if (removedComponents.empty()) {
            item.comp.erase(std::remove_if(item.comp.begin(), item.comp.end(),
                [](const auto& field) { return field.first == "removed"; }),
                item.comp.end());
        } else {
            nbt::Value removed = nbt::Value::makeList(nbt::Int);
            for (const auto componentId : removedComponents)
                removed.list.push_back(nbt::Value::makeInt(
                    static_cast<std::int32_t>(componentId)));
            item.set("removed", std::move(removed));
        }
        return item;
    }

    // ------------------------------------------------------------------ io
    void write(WriteBuffer& out) const {
        if (empty()) { out.varint(0); return; }
        if (!hasValidComponentData())
            throw std::length_error("item component data exceeds limits");
        if (persistedNbt && persistedNbt->hasOpaqueComponents)
            throw std::runtime_error(
                "cannot encode item with opaque saved components");
        for (const auto& [typeId, payload] : components) {
            if (!item_component_nbt::componentName(typeId) ||
                !item_component_nbt::isValidProtocolComponentPayload(
                    typeId, payload))
                throw std::runtime_error(
                    "unsupported or malformed item component " +
                    std::to_string(typeId));
        }
        out.varint(count);
        out.varint(static_cast<std::int32_t>(itemId));
        out.varint(static_cast<std::int32_t>(components.size()));
        out.varint(static_cast<std::int32_t>(removedComponents.size()));
        for (auto& [typeId, payload] : components) {
            out.varint(static_cast<std::int32_t>(typeId));
            out.raw(payload.data(), payload.size());
        }
        for (auto t : removedComponents) out.varint(static_cast<std::int32_t>(t));
    }

    static ItemStack read(ReadBuffer& in) {
        const std::int32_t cnt = in.varint();
        if (cnt == 0) return air();
        if (cnt < 0) throw std::runtime_error("negative item stack count");
        if (cnt > std::numeric_limits<std::int16_t>::max())
            throw std::runtime_error("item stack count out of range");
        ItemStack s;
        s.count = static_cast<std::int16_t>(cnt);
        const std::int32_t itemId = in.varint();
        if (itemId <= 0 ||
            !itemNamesById().contains(static_cast<std::uint32_t>(itemId)))
            throw std::runtime_error("unknown item id");
        s.itemId = static_cast<std::uint32_t>(itemId);
        const std::int32_t addC = in.varint();
        const std::int32_t remC = in.varint();
        if (addC < 0 || remC < 0 ||
            static_cast<std::size_t>(addC) > kMaxComponentCount ||
            static_cast<std::size_t>(remC) > kMaxComponentCount)
            throw std::runtime_error("item component count out of range");
        s.components.reserve(static_cast<std::size_t>(addC));
        s.removedComponents.reserve(static_cast<std::size_t>(remC));
        std::size_t totalPayloadBytes = 0;
        for (std::int32_t i = 0; i < addC; ++i) {
            const auto encodedTypeId = in.varint();
            if (encodedTypeId < 0)
                throw std::runtime_error("negative item component id");
            const auto typeId = static_cast<std::uint32_t>(encodedTypeId);
            auto decodedPayload =
                item_component_nbt::readProtocolComponentPayload(in, typeId);
            if (!decodedPayload)
                throw std::runtime_error(
                    "unsupported or malformed item component " +
                    std::to_string(typeId));
            auto payload = std::move(*decodedPayload);
            if (payload.size() > kMaxComponentPayloadBytes ||
                payload.size() >
                    kMaxTotalComponentPayloadBytes - totalPayloadBytes)
                throw std::runtime_error("item component payload out of range");
            totalPayloadBytes += payload.size();
            s.components.emplace_back(typeId, std::move(payload));
        }
        for (std::int32_t i = 0; i < remC; ++i) {
            const auto typeId = in.varint();
            if (typeId < 0)
                throw std::runtime_error("negative removed item component id");
            s.removedComponents.push_back(static_cast<std::uint32_t>(typeId));
        }
        if (!s.hasValidComponentData())
            throw std::runtime_error("duplicate or conflicting item components");
        return s;
    }

    std::string name() const {
        const auto& names = itemNamesById();
        const auto it = names.find(itemId);
        if (it == names.end()) return "minecraft:air";
        if (it->second.rfind("minecraft:", 0) == 0 && it->second.find('{') == std::string::npos)
            return it->second;
        return it->second;
    }

    // SlotComponentType ids verified vs protocol.json 1.21.4 (pc/1.21.4/protocol.json SlotComponentType mapper): 0 custom_data, 1
    // max_stack_size, 2 max_damage, 3 damage, 4 unbreakable, 5 custom_name, 6 item_name, 7 item_model, 8 lore, 9 rarity, 10 enchantments,
    // 11 can_place_on, 12 can_break, 13 attribute_modifiers, 14 custom_model_data, 15 hide_additional_tooltip, 16 hide_tooltip, 17
    // repair_cost, 18 creative_slot_lock, 19 enchantment_glint_override, 20 intangible_projectile, 21 food, 22 consumable, 23
    // use_remainder, 24 use_cooldown, 25 damage_resistant, 26 tool, 27 enchantable, 28 equippable, 29 repairable, 30 glider, 31
    // tooltip_style, 32 death_protection, 33 stored_enchantments, 34 dyed_color, 35 map_color, 36 map_id, 37 map_decorations, 38
    // map_post_processing, 39 charged_projectiles, 40 bundle_contents, 41 potion_contents, 42 suspicious_stew_effects, 43
    // writable_book_content, 44 written_book_content, 45 trim, 46 debug_stick_state, ... 62 container, 66 container_loot — strict audit HIGH I6/I11. Yarn
    static constexpr std::uint32_t kDamageComponentId = 3;
    static constexpr std::uint32_t kRepairCostComponentId = 17;
    static constexpr std::uint32_t kEnchantmentsComponentId = 10;
    static constexpr std::uint32_t kStoredEnchantmentsComponentId = 33;
    static constexpr std::uint32_t kCanPlaceOnComponentId = 11;
    static constexpr std::uint32_t kCanBreakComponentId = 12;
    static constexpr std::uint32_t kCustomNameComponentId = 5;
    static constexpr std::uint32_t kPotionContentsComponentId = 41;
    int getDamage() const {
        for (auto &pr : components) {
            if (pr.first==kDamageComponentId) {
                if (pr.second.empty()) return 0;
                try {
                    ReadBuffer in(pr.second);
                    const auto value = in.varint();
                    return value < 0 ? 0 : value;
                } catch (...) {
                    return 0;
                }
            }
        }
        return 0;
    }
    void setDamage(int dmg) {
        if (dmg <= 0) {
            replaceComponentsForNbtEdit({kDamageComponentId},
                                        kDamageComponentId, std::nullopt);
            return;
        }
        WriteBuffer encoded;
        encoded.varint(dmg);
        std::vector<std::uint8_t> payload(encoded.data.begin(),
                                          encoded.data.end());
        replaceComponentsForNbtEdit({kDamageComponentId},
                                    kDamageComponentId,
                                    std::move(payload));
    }
    bool applyDamage(int amount) {
        if (empty()) return false;
        int maxd = maxDamageFor(itemId);
        if (maxd<=0) return false;
        int cur = getDamage();
        cur += amount;
        if (cur >= maxd) { count=0; itemId=0; components.clear(); removedComponents.clear(); return true; }
        setDamage(cur);
        return false;
    }
    static int maxDamageFor(std::uint32_t id) {
        static std::unordered_map<std::uint32_t,int> cache;
        static std::mutex cacheMutex;
        std::lock_guard lock(cacheMutex);
        auto itc=cache.find(id);
        if(itc!=cache.end()) return itc->second;
        std::string n;
        for(auto &e: gen::kItems) if(e.second==id){ n=std::string(e.first); break; }
        int v=0;
        if(n.find("wooden_")!=std::string::npos){
            if(n.find("sword")!=std::string::npos||n.find("shovel")!=std::string::npos||n.find("pickaxe")!=std::string::npos||n.find("axe")!=std::string::npos||n.find("hoe")!=std::string::npos) v=59;
        } else if(n.find("stone_")!=std::string::npos) v=131;
        else if(n.find("iron_")!=std::string::npos){
            if(n.find("sword")!=std::string::npos||n.find("shovel")!=std::string::npos||n.find("pickaxe")!=std::string::npos||n.find("axe")!=std::string::npos||n.find("hoe")!=std::string::npos) v=250;
            else if(n.find("helmet")!=std::string::npos) v=165;
            else if(n.find("chestplate")!=std::string::npos) v=240;
            else if(n.find("leggings")!=std::string::npos) v=225;
            else if(n.find("boots")!=std::string::npos) v=195;
        } else if(n.find("golden_")!=std::string::npos) v=32;
        else if(n.find("diamond_")!=std::string::npos){
            if(n.find("sword")!=std::string::npos||n.find("shovel")!=std::string::npos||n.find("pickaxe")!=std::string::npos||n.find("axe")!=std::string::npos||n.find("hoe")!=std::string::npos) v=1561;
            else if(n.find("helmet")!=std::string::npos) v=363;
            else if(n.find("chestplate")!=std::string::npos) v=528;
            else if(n.find("leggings")!=std::string::npos) v=495;
            else if(n.find("boots")!=std::string::npos) v=429;
        } else if(n.find("netherite_")!=std::string::npos){
            if(n.find("sword")!=std::string::npos||n.find("shovel")!=std::string::npos||n.find("pickaxe")!=std::string::npos||n.find("axe")!=std::string::npos||n.find("hoe")!=std::string::npos) v=2031;
            else if(n.find("helmet")!=std::string::npos) v=407;
            else if(n.find("chestplate")!=std::string::npos) v=592;
            else if(n.find("leggings")!=std::string::npos) v=555;
            else if(n.find("boots")!=std::string::npos) v=481;
        } else if(n=="minecraft:shears") v=238;
        else if(n=="minecraft:bow") v=384;
        else if(n=="minecraft:crossbow") v=465;
        else if(n=="minecraft:trident") v=250;
        else if(n=="minecraft:shield") v=336;
        else if(n=="minecraft:flint_and_steel") v=64;
        else if(n=="minecraft:carrot_on_a_stick"||n=="minecraft:warped_fungus_on_a_stick") v=25;
        else if(n=="minecraft:fishing_rod") v=64;
        else if(n=="minecraft:elytra") v=432;
        cache[id]=v;
        return v;
    }

    // enchant helpers: binary NBT format per protocol.json 1.21.4 (SlotComponentType enchantments=10)
    // Payload: varint count, then each {varint id, varint level}, then bool showTooltip
    static int enchantIdByName(const std::string& n) {
        return item_component_nbt::enchantmentId(n).value_or(-1);
    }
    static std::string enchantNameById(int id) {
        const auto name = item_component_nbt::enchantmentName(id);
        return name ? std::string(*name) : std::string{};
    }
    struct DecodedEnchants {
        std::vector<std::pair<int, int>> entries;
        bool showTooltip = true;
    };
    static DecodedEnchants decodeEnchantData(
        const std::vector<std::uint8_t>& payload) {
        DecodedEnchants decoded;
        auto& out = decoded.entries;
        if (payload.empty()) return decoded;
        bool maybeText=false;
        for (auto b: payload) if (b==':' || b==',') { maybeText=true; break; }
        if (maybeText) {
            std::string txt(payload.begin(), payload.end());
            size_t pos=0;
            while (pos < txt.size()) {
                size_t colon = txt.find(':', pos);
                if (colon==std::string::npos) break;
                size_t comma = txt.find(',', colon);
                std::string name = txt.substr(pos, colon-pos);
                // trim?
                std::string lvlStr = txt.substr(colon+1, (comma==std::string::npos? txt.size():comma)-colon-1);
                // name may include "minecraft:"; extract id
                int id = enchantIdByName(name);
                if (id>=0) {
                    try { int lvl=std::stoi(lvlStr); out.emplace_back(id,lvl); } catch(...) { out.emplace_back(id,1); }
                }
                if (comma==std::string::npos) break;
                pos = comma+1;
            }
            return decoded;
        }
        // binary: varint count, then id/level pairs, then bool
        ReadBuffer rb(payload.data(), payload.size());
        try {
            int cnt = rb.varint();
            if (cnt<0 || cnt>64) return decoded;
            for (int i=0;i<cnt;++i) {
                if (rb.remaining() <2) return DecodedEnchants{};
                int id = rb.varint();
                int lvl = rb.varint();
                out.emplace_back(id,lvl);
            }
            if (rb.remaining() != 1) return DecodedEnchants{};
            decoded.showTooltip = rb.boolean();
            if (rb.remaining() != 0) return DecodedEnchants{};
        } catch(...) { return DecodedEnchants{}; }
        return decoded;
    }
    static std::vector<std::pair<int,int>> decodeEnchants(
        const std::vector<std::uint8_t>& payload) {
        return decodeEnchantData(payload).entries;
    }
    static std::vector<std::uint8_t> encodeEnchants(
        const std::vector<std::pair<int,int>>& ench, bool showTooltip = true) {
        WriteBuffer wb;
        wb.varint((int)ench.size());
        for (auto &pr: ench) { wb.varint(pr.first); wb.varint(pr.second); }
        wb.boolean(showTooltip);
        return std::vector<std::uint8_t>(wb.data.begin(), wb.data.end());
    }
    static void addEnchant(ItemStack &s, const std::string &enchName, int lvl) {
        int id = enchantIdByName(enchName);
        if (id<0) return;
        const auto enchantedBook = gen::itemIdByName().find(
            "minecraft:enchanted_book");
        const auto targetComponentId =
            enchantedBook != gen::itemIdByName().end() &&
                    s.itemId == enchantedBook->second
                ? kStoredEnchantmentsComponentId
                : kEnchantmentsComponentId;

        // If the saved target component is opaque (for example it contains a
        // modded enchantment unknown to this build), replacing its payload
        // would silently erase that entry from the persisted item. Keep the
        // original stack untouched until we can represent it losslessly.
        if (s.persistedNbt && !s.persistedNbt->record.empty()) {
            try {
                ReadBuffer input(s.persistedNbt->record);
                nbt::Parser parser(input);
                const auto savedItem = parser.readFileRoot();
                const auto* savedComponents = savedItem.get("components");
                for (const auto componentId :
                     {kEnchantmentsComponentId,
                      kStoredEnchantmentsComponentId}) {
                    const auto name = item_component_nbt::componentName(
                        componentId);
                    const auto* savedComponent = savedComponents && name
                        ? savedComponents->get(*name) : nullptr;
                    if (savedComponent &&
                        !item_component_nbt::protocolComponentFromNbt(
                            *name, *savedComponent))
                        return;
                }
            } catch (const std::exception&) {
                return;
            }
        }
        std::vector<std::pair<int,int>> cur;
        bool showTooltip = true;
        for (auto &pr: s.components) if(pr.first==kEnchantmentsComponentId || pr.first==33){
            auto decoded = decodeEnchantData(pr.second);
            cur = std::move(decoded.entries);
            showTooltip = decoded.showTooltip;
            break;
        }
        bool found=false;
        for (auto &pr: cur) if (pr.first==id) { pr.second = std::max(pr.second, lvl); found=true; break; }
        if (!found) cur.emplace_back(id,lvl);
        s.replaceComponentsForNbtEdit(
            {kEnchantmentsComponentId, kStoredEnchantmentsComponentId},
            targetComponentId, encodeEnchants(cur, showTooltip));
    }
    bool hasEnchant(const std::string &enchName) const {
        int id = enchantIdByName(enchName);
        if (id<0) {
            // fallback string search for unknown
            for(auto &pr: components) if(pr.first==kEnchantmentsComponentId||pr.first==33){
                std::string txt(pr.second.begin(), pr.second.end());
                if(txt.find(enchName)!=std::string::npos) return true;
            }
            return false;
        }
        for(auto &pr: components) if(pr.first==kEnchantmentsComponentId||pr.first==33){
            auto v = decodeEnchants(pr.second);
            for(auto &e: v) if(e.first==id) return true;
            // also textual fallback inside decode already handled
            if (!v.empty()) continue;
            std::string txt(pr.second.begin(), pr.second.end());
            if(txt.find(enchName)!=std::string::npos) return true;
        }
        return false;
    }
    int enchantLevel(const std::string &enchName) const {
        int id = enchantIdByName(enchName);
        if (id<0) return 0;
        for(auto &pr: components) if(pr.first==kEnchantmentsComponentId||pr.first==33){
            auto v = decodeEnchants(pr.second);
            for(auto &e: v) if(e.first==id) return e.second;
        }
        return 0;
    }
    bool hasSilkTouch() const { return hasEnchant("silk_touch") || hasEnchant("minecraft:silk_touch"); }
    int fortuneLevel() const { int a=enchantLevel("fortune"); int b=enchantLevel("minecraft:fortune"); return std::max(a,b); }
    int lootingLevel() const { int a=enchantLevel("looting"); int b=enchantLevel("minecraft:looting"); return std::max(a,b); }

    // ----- ArmorTrim component (SlotComponentType trim=45 per protocol.json 1.21.4) ----- Payload per protocol.json `trim`: { material:
    // registryEntryHolder<ArmorTrimMaterial>, pattern: registryEntryHolder<ArmorTrimPattern>, showInTooltip bool }. Holder is varint id
    // (direct) or inline NBT. Registry entries are sent as direct VarInt ids;
    // vanilla 1.21.4 writes material, then pattern, then showInTooltip.
    struct ArmorTrim {
        std::string pattern;  // e.g. "minecraft:coast"
        std::string material; // e.g. "minecraft:iron"
        bool has=false;
        bool showInTooltip=true;
    };
    static constexpr std::uint32_t kTrimComponentId = 45;
    // alphabetical registry order as captured (wire parity)
    static const std::unordered_map<std::string,int>& trimPatternIds() {
        static const std::unordered_map<std::string, int> m = [] {
            std::unordered_map<std::string, int> result;
            for (std::size_t id = 0;
                 id < item_component_nbt::kTrimPatternNames.size(); ++id)
                result.emplace(std::string(item_component_nbt::kTrimPatternNames[id]),
                               static_cast<int>(id));
            return result;
        }();
        return m;
    }
    static const std::unordered_map<std::string,int>& trimMaterialIds() {
        static const std::unordered_map<std::string, int> m = [] {
            std::unordered_map<std::string, int> result;
            for (std::size_t id = 0;
                 id < item_component_nbt::kTrimMaterialNames.size(); ++id)
                result.emplace(std::string(item_component_nbt::kTrimMaterialNames[id]),
                               static_cast<int>(id));
            return result;
        }();
        return m;
    }
    static std::optional<int> trimPatternIdFor(const std::string& id) {
        return item_component_nbt::trimPatternId(id);
    }
    static std::optional<int> trimMaterialIdFor(const std::string& id) {
        return item_component_nbt::trimMaterialId(id);
    }
    bool setTrim(const ArmorTrim& t) {
        if (t.has && !t.pattern.empty()) {
            const auto patternId = trimPatternIdFor(t.pattern);
            const auto materialId = trimMaterialIdFor(t.material);
            if (!patternId || !materialId) return false;
        }
        if (!t.has || t.pattern.empty()) {
            replaceComponentsForNbtEdit({kTrimComponentId},
                                        kTrimComponentId, std::nullopt);
            return true;
        }
        const auto patternId = trimPatternIdFor(t.pattern);
        const auto materialId = trimMaterialIdFor(t.material);
        WriteBuffer wb;
        wb.varint(*materialId);
        wb.varint(*patternId);
        wb.boolean(t.showInTooltip);
        std::vector<std::uint8_t> payload(wb.data.begin(), wb.data.end());
        replaceComponentsForNbtEdit({kTrimComponentId}, kTrimComponentId,
                                    std::move(payload));
        return true;
    }
    // ----- Brewing potion_contents (41) helpers — nether_wart -> awkward ----- Payload: option potionId (bool+varint), option customColor
    // (bool), varint customEffectsCount, option customName (bool) Vanilla minecraft:potion registry 45 entries (1.21.4): water 0, mundane
    // 1, thick 2, awkward 3, night_vision 4, ... wind_charged 42 etc. Wire id is registry index. Previous code used 0 water -> 1 awkward
    // custom mapping, breaking brewing (client interprets 1 as mundane). Fix to registry-aware 0->3.
    static const std::unordered_map<std::string,int>& potionIds() {
        static const std::unordered_map<std::string, int> m = [] {
            std::unordered_map<std::string, int> result;
            for (std::size_t id = 0;
                 id < item_component_nbt::kPotionNames.size(); ++id)
                result.emplace(std::string(item_component_nbt::kPotionNames[id]),
                               static_cast<int>(id));
            return result;
        }();
        return m;
    }
    static int potionIdByName(const std::string& n) {
        return item_component_nbt::potionId(n).value_or(0);
    }
    static std::string potionNameById(int id) {
        const auto name = item_component_nbt::potionName(id);
        return name ? std::string(*name) : "minecraft:water";
    }
    bool hasPotionContents() const {
        for (auto &pr : components) if (pr.first==kPotionContentsComponentId) return true;
        return false;
    }
    int getPotionId() const {
        for (auto &pr : components) if (pr.first==kPotionContentsComponentId) {
            if (pr.second.empty()) return 0;
            ReadBuffer rb(pr.second.data(), pr.second.size());
            try {
                bool hasId = rb.boolean();
                if (!hasId) return 0;
                return rb.varint();
            } catch (...) { return 0; }
        }
        return 0; // water by default (no component)
    }
    void setPotionId(int id) {
        WriteBuffer wb;
        wb.boolean(true);
        wb.varint(id);

        const auto existing = std::find_if(components.begin(), components.end(),
            [](const auto& component) {
                return component.first == kPotionContentsComponentId;
            });
        bool copiedRemainder = false;
        if (existing != components.end()) {
            try {
                ReadBuffer input(existing->second);
                if (input.boolean()) (void)input.varint();
                wb.raw(input.p + input.off, input.remaining());
                copiedRemainder = true;
            } catch (const std::exception&) {
                // Replace a malformed old payload with a valid empty tail.
            }
        }
        if (!copiedRemainder) {
            wb.boolean(false); // customColor absent
            wb.varint(0); // customEffects empty
            wb.boolean(false); // customName absent
        }
        std::vector<std::uint8_t> payload(wb.data.begin(), wb.data.end());
        replaceComponentsForNbtEdit({kPotionContentsComponentId},
                                    kPotionContentsComponentId,
                                    std::move(payload));
    }
    void setPotionByName(const std::string& name) { setPotionId(potionIdByName(name)); }
    void clearPotionContents() {
        replaceComponentsForNbtEdit({kPotionContentsComponentId},
                                    kPotionContentsComponentId, std::nullopt);
    }
    bool isWaterPotion() const {
        if (itemId == 0) return false;
        std::string n=name();
        if (n!="minecraft:potion" && n!="minecraft:splash_potion" && n!="minecraft:lingering_potion" && n!="minecraft:tipped_arrow") return false;
        return getPotionId()==potionIdByName("minecraft:water");
    }
    bool isAwkwardPotion() const { return getPotionId()==potionIdByName("minecraft:awkward"); }

    bool isArmor() const {
        std::string n=name();
        return n.find("helmet")!=std::string::npos || n.find("chestplate")!=std::string::npos ||
               n.find("leggings")!=std::string::npos || n.find("boots")!=std::string::npos ||
               n.find("elytra")!=std::string::npos;
    }
    bool isTool() const {
        std::string n=name();
        return n.find("pickaxe")!=std::string::npos || n.find("axe")!=std::string::npos ||
               n.find("shovel")!=std::string::npos || n.find("hoe")!=std::string::npos ||
               n.find("sword")!=std::string::npos || n.find("shears")!=std::string::npos ||
               n.find("fishing_rod")!=std::string::npos;
    }
    int getRepairCost() const {
        for (auto &pr : components) if (pr.first==kRepairCostComponentId) {
            if (pr.second.empty()) return 0;
            int v=0; int shift=0;
            for (std::uint8_t b: pr.second) { v |= (b & 0x7F) << shift; if (!(b & 0x80)) break; shift+=7; }
            return v;
        }
        return 0;
    }
    void setRepairCost(int c) {
        if (c <= 0) {
            replaceComponentsForNbtEdit({kRepairCostComponentId},
                                        kRepairCostComponentId, std::nullopt);
            return;
        }
        WriteBuffer encoded;
        encoded.varint(c);
        std::vector<std::uint8_t> payload(encoded.data.begin(),
                                          encoded.data.end());
        replaceComponentsForNbtEdit({kRepairCostComponentId},
                                    kRepairCostComponentId,
                                    std::move(payload));
    }
    std::string getCustomName() const {
        for (auto &pr: components) if(pr.first==kCustomNameComponentId){
            if (pr.second.empty()) return "";
            if (!pr.second.empty() && pr.second[0]==0x0A) {
                // NBT compound {text:"..."} written by setCustomName via writeTextComponent
                // Payload: 0x0A 0x08 0x00 0x04 't' 'e' 'x' 't' 0x00 len 'value' 0x00
                try {
                    ReadBuffer rb(pr.second.data(), pr.second.size());
                    nbt::Tag rt = static_cast<nbt::Tag>(rb.u8());
                    if (rt == nbt::Compound) {
                        while (true) {
                            nbt::Tag et = static_cast<nbt::Tag>(rb.u8());
                            if (et == nbt::End) break;
                            uint16_t nl = rb.u16();
                            std::string name(reinterpret_cast<const char*>(rb.p + rb.off), nl); rb.off+=nl;
                            if (name=="text" && et==nbt::String) {
                                uint16_t sl = rb.u16();
                                if (rb.remaining() >= sl) {
                                    std::string out(reinterpret_cast<const char*>(rb.p + rb.off), sl);
                                    return out;
                                }
                                return "";
                            } else {
                                break;
                            }
                        }
                    }
                } catch (...) {}
                // fallback: scan for "text" as string
                std::string txt(pr.second.begin(), pr.second.end());
                auto p = txt.find("text");
                if (p!=std::string::npos) {
                    // try NBT style: after "text", the next bytes are u16 len find the string length bytes after "text" simplest fallback:
                    // extract between quotes if present
                    auto q1 = txt.find('"', p+4);
                    if (q1!=std::string::npos) {
                        auto q2 = txt.find('"', q1+1);
                        if (q2!=std::string::npos) return txt.substr(q1+1, q2-q1-1);
                    }
                }
                return txt;
            }
            return std::string(pr.second.begin(), pr.second.end());
        }
        return "";
    }
    void setCustomName(const std::string& n) {
        if (n.empty()) {
            replaceComponentsForNbtEdit({kCustomNameComponentId},
                                        kCustomNameComponentId, std::nullopt);
            return;
        }
        WriteBuffer wb;
        nbt::writeTextComponent(wb, n);
        std::vector<std::uint8_t> payload(wb.data.begin(), wb.data.end());
        replaceComponentsForNbtEdit({kCustomNameComponentId},
                                    kCustomNameComponentId,
                                    std::move(payload));
    }
    int efficiencyLevel() const { return std::max(enchantLevel("efficiency"), enchantLevel("minecraft:efficiency")); }
    int frostWalkerLevel() const { return std::max(enchantLevel("frost_walker"), enchantLevel("minecraft:frost_walker")); }
    int soulSpeedLevel() const { return std::max(enchantLevel("soul_speed"), enchantLevel("minecraft:soul_speed")); }
    int swiftSneakLevel() const { return std::max(enchantLevel("swift_sneak"), enchantLevel("minecraft:swift_sneak")); }
    int unbreakingLevel() const { return std::max(enchantLevel("unbreaking"), enchantLevel("minecraft:unbreaking")); }
    int mendingLevel() const { return std::max(enchantLevel("mending"), enchantLevel("minecraft:mending")); }
    std::vector<std::string> lore;
    std::string displayNameLoot;
};

namespace item_component_nbt {

inline std::optional<std::vector<std::uint8_t>>
containerComponentNbtToProtocol(const nbt::Value& saved, std::size_t depth) {
    if (depth > kMaxComponentDepth || saved.tag != nbt::List ||
        saved.list.size() > kMaxContainerSlots)
        return std::nullopt;

    std::array<std::optional<ItemStack>, kMaxContainerSlots> slots;
    int lastNonEmpty = -1;
    for (const auto& entry : saved.list) {
        if (entry.tag != nbt::Compound) return std::nullopt;
        const auto slot = integer(entry.get("slot"));
        const auto* item = entry.get("item");
        if (!slot || *slot < 0 ||
            static_cast<std::size_t>(*slot) >= slots.size() ||
            !item || item->tag != nbt::Compound ||
            slots[static_cast<std::size_t>(*slot)])
            return std::nullopt;
        auto stack = ItemStack::fromNbt(*item, depth + 1);
        if (!stack || stack->empty()) return std::nullopt;
        slots[static_cast<std::size_t>(*slot)] = std::move(*stack);
        lastNonEmpty = std::max(lastNonEmpty, *slot);
    }

    try {
        WriteBuffer output;
        output.varint(lastNonEmpty + 1);
        for (int slot = 0; slot <= lastNonEmpty; ++slot) {
            const auto& stack = slots[static_cast<std::size_t>(slot)];
            if (stack) stack->write(output);
            else output.varint(0);
        }
        return std::move(output.data);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<nbt::Value> containerComponentProtocolToNbt(
    const std::vector<std::uint8_t>& payload, std::size_t depth) {
    if (depth > kMaxComponentDepth) return std::nullopt;
    try {
        ReadBuffer input(payload);
        const auto count = input.varint();
        if (count < 0 ||
            static_cast<std::size_t>(count) > kMaxContainerSlots)
            return std::nullopt;
        nbt::Value contents = nbt::Value::makeList(nbt::Compound);
        contents.list.reserve(static_cast<std::size_t>(count));
        for (std::int32_t slot = 0; slot < count; ++slot) {
            auto stack = ItemStack::read(input);
            if (stack.empty()) continue;
            nbt::Value entry = nbt::Value::makeCompound();
            entry.set("slot", nbt::Value::makeInt(slot));
            entry.set("item", stack.toNbt(-1, depth + 1));
            contents.list.push_back(std::move(entry));
        }
        if (input.remaining() != 0) return std::nullopt;
        return contents;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace item_component_nbt

inline int insertPlayerStackPartial(std::array<ItemStack, 46>& inventory,
                                    const ItemStack& source, int amount,
                                    int stackLimit) {
    if (amount <= 0 || source.empty() || stackLimit <= 0) return 0;
    int remaining = amount;
    static constexpr int kHotbar[] = {36, 37, 38, 39, 40, 41, 42, 43, 44};
    static constexpr int kMain[] = {
        9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};

    const auto mergeInto = [&](const int* slots, std::size_t slotCount) {
        for (std::size_t i = 0; i < slotCount && remaining > 0; ++i) {
            ItemStack& destination = inventory[slots[i]];
            if (!destination.sameItemData(source) ||
                destination.count >= stackLimit)
                continue;
            const int moved = std::min(
                remaining, stackLimit - static_cast<int>(destination.count));
            destination.count = static_cast<std::int16_t>(destination.count + moved);
            remaining -= moved;
        }
    };
    const auto fillEmpty = [&](const int* slots, std::size_t slotCount) {
        for (std::size_t i = 0; i < slotCount && remaining > 0; ++i) {
            ItemStack& destination = inventory[slots[i]];
            if (!destination.empty()) continue;
            destination = source;
            destination.count = static_cast<std::int16_t>(
                std::min(remaining, stackLimit));
            remaining -= destination.count;
        }
    };

    mergeInto(kHotbar, std::size(kHotbar));
    mergeInto(kMain, std::size(kMain));
    fillEmpty(kHotbar, std::size(kHotbar));
    fillEmpty(kMain, std::size(kMain));
    return amount - remaining;
}

inline bool insertCompletePlayerStack(std::array<ItemStack, 46>& inventory,
                                      const ItemStack& source,
                                      std::uint16_t amount, int stackLimit) {
    if (amount == 0 || source.empty()) return amount == 0;
    auto trial = inventory;
    if (insertPlayerStackPartial(trial, source, amount, stackLimit) != amount)
        return false;
    inventory = std::move(trial);
    return true;
}

} // namespace cppfm
