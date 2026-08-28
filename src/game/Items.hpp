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
#include <algorithm>
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

    // component 6 = minecraft:damage (1.21.4 data component), type 3 per task shim
    int getDamage() const {
        for (auto &pr : components) {
            if (pr.first==3 || pr.first==6) {
                if (pr.second.empty()) return 0;
                // payload is varint[len][bytes]-> we stored raw varint bytes directly
                // Decode as varint if possible
                int v=0; int shift=0;
                for (std::uint8_t b: pr.second) { v |= (b & 0x7F) << shift; if (!(b & 0x80)) break; shift+=7; }
                return v;
            }
        }
        return 0;
    }
    void setDamage(int dmg) {
        // remove existing
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==3 || p.first==6; }), components.end());
        if (dmg<=0) return;
        WriteBuffer tmp; tmp.varint(dmg);
        components.emplace_back(6, std::vector<std::uint8_t>(tmp.data.begin(), tmp.data.end()));
        // also keep alias id 3 for task spec
        // duplicate payload for both ids if needed - keep one
    }
    // returns true if item should be destroyed (damage >= max)
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

    // enchant helpers: component type 10 (or 21) holds enchantments
    static void addEnchant(ItemStack &s, const std::string &enchName, int lvl) {
        // append or create enchantments payload as textual "name:lvl," for simple parsing
        std::string payloadStr;
        for (auto &pr: s.components) if(pr.first==10||pr.first==21){
            payloadStr.assign(pr.second.begin(), pr.second.end());
            break;
        }
        if(!payloadStr.empty() && payloadStr.back()!=',') payloadStr+=',';
        payloadStr += enchName + ":" + std::to_string(lvl) + ",";
        // remove old
        s.components.erase(std::remove_if(s.components.begin(), s.components.end(),
            [](auto &p){ return p.first==10 || p.first==21; }), s.components.end());
        s.components.emplace_back(10, std::vector<std::uint8_t>(payloadStr.begin(), payloadStr.end()));
    }
    bool hasEnchant(const std::string &enchName) const {
        for(auto &pr: components) if(pr.first==10||pr.first==21){
            std::string txt(pr.second.begin(), pr.second.end());
            if(txt.find(enchName)!=std::string::npos) return true;
        }
        return false;
    }
    int enchantLevel(const std::string &enchName) const {
        for(auto &pr: components) if(pr.first==10||pr.first==21){
            std::string txt(pr.second.begin(), pr.second.end());
            auto pos=txt.find(enchName);
            if(pos==std::string::npos) continue;
            auto colon=txt.find(':',pos);
            if(colon==std::string::npos) return 1;
            auto comma=txt.find(',',colon);
            std::string num=txt.substr(colon+1, (comma==std::string::npos? txt.size():comma)-colon-1);
            try{ return std::stoi(num); }catch(...){ return 1; }
        }
        return 0;
    }
    bool hasSilkTouch() const { return hasEnchant("silk_touch") || hasEnchant("minecraft:silk_touch"); }
    int fortuneLevel() const { int a=enchantLevel("fortune"); int b=enchantLevel("minecraft:fortune"); return std::max(a,b); }

    // ----- ArmorTrim component (plan13 §2) -----
    // Stored as component id 42 textual payload "pattern|material" (e.g. "minecraft:coast|minecraft:iron").
    // Registry values validated against assets/registry/trim_pattern.bin / trim_material.bin (18 patterns, 11 materials).
    struct ArmorTrim {
        std::string pattern;  // e.g. "minecraft:coast"
        std::string material; // e.g. "minecraft:iron"
        bool has=false;
    };
    static constexpr std::uint32_t kTrimComponentId = 42;
    bool hasTrim() const {
        for (auto &pr: components) if (pr.first==kTrimComponentId) return true;
        return false;
    }
    ArmorTrim getTrim() const {
        for (auto &pr: components) if (pr.first==kTrimComponentId) {
            std::string txt(pr.second.begin(), pr.second.end());
            auto sep = txt.find('|');
            if (sep==std::string::npos) sep = txt.find(',');
            ArmorTrim t;
            t.has=true;
            if (sep!=std::string::npos) {
                t.pattern = txt.substr(0, sep);
                t.material = txt.substr(sep+1);
            } else {
                t.pattern = txt;
            }
            // ensure minecraft: prefix
            if (!t.pattern.empty() && t.pattern.find(':')==std::string::npos) t.pattern = "minecraft:"+t.pattern;
            if (!t.material.empty() && t.material.find(':')==std::string::npos) t.material = "minecraft:"+t.material;
            return t;
        }
        return {};
    }
    void setTrim(const ArmorTrim& t) {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kTrimComponentId; }), components.end());
        if (!t.has || t.pattern.empty()) return;
        std::string txt = t.pattern + "|" + t.material;
        components.emplace_back(kTrimComponentId, std::vector<std::uint8_t>(txt.begin(), txt.end()));
    }
    void clearTrim() {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kTrimComponentId; }), components.end());
    }

    // ---- plan13 §4/§5 helpers ----
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
        for (auto &pr : components) if (pr.first==7) {
            if (pr.second.empty()) return 0;
            int v=0; int shift=0;
            for (std::uint8_t b: pr.second) { v |= (b & 0x7F) << shift; if (!(b & 0x80)) break; shift+=7; }
            return v;
        }
        return 0;
    }
    void setRepairCost(int c) {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==7; }), components.end());
        if (c<=0) return;
        WriteBuffer tmp; tmp.varint(c);
        components.emplace_back(7, std::vector<std::uint8_t>(tmp.data.begin(), tmp.data.end()));
    }
    std::string getCustomName() const {
        for (auto &pr: components) if(pr.first==5){
            return std::string(pr.second.begin(), pr.second.end());
        }
        return "";
    }
    void setCustomName(const std::string& n) {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==5; }), components.end());
        if(n.empty()) return;
        components.emplace_back(5, std::vector<std::uint8_t>(n.begin(), n.end()));
    }
    int efficiencyLevel() const { return std::max(enchantLevel("efficiency"), enchantLevel("minecraft:efficiency")); }
    int frostWalkerLevel() const { return std::max(enchantLevel("frost_walker"), enchantLevel("minecraft:frost_walker")); }
    int soulSpeedLevel() const { return std::max(enchantLevel("soul_speed"), enchantLevel("minecraft:soul_speed")); }
    int swiftSneakLevel() const { return std::max(enchantLevel("swift_sneak"), enchantLevel("minecraft:swift_sneak")); }
    int unbreakingLevel() const { return std::max(enchantLevel("unbreaking"), enchantLevel("minecraft:unbreaking")); }
    int mendingLevel() const { return std::max(enchantLevel("mending"), enchantLevel("minecraft:mending")); }
};

} // namespace cppfm
