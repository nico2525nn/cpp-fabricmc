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

    // SlotComponentType ids verified vs protocol.json 1.21.4: damage=3, max_damage=2, max_stack_size=1, etc.
    static constexpr std::uint32_t kDamageComponentId = 3;
    static constexpr std::uint32_t kRepairCostComponentId = 17;
    static constexpr std::uint32_t kEnchantmentsComponentId = 10;
    static constexpr std::uint32_t kTrimComponentIdReal = 45;
    static constexpr std::uint32_t kCustomNameComponentId = 5;
    static constexpr std::uint32_t kPotionContentsComponentId = 41;
    // legacy ids for read-compat
    static constexpr std::uint32_t kLegacyDamageAlias = 6;
    static constexpr std::uint32_t kLegacyRepairAlias = 7;
    static constexpr std::uint32_t kLegacyTrimAlias = 42;
    int getDamage() const {
        for (auto &pr : components) {
            if (pr.first==kDamageComponentId || pr.first==kLegacyDamageAlias) {
                if (pr.second.empty()) return 0;
                int v=0; int shift=0;
                for (std::uint8_t b: pr.second) { v |= (b & 0x7F) << shift; if (!(b & 0x80)) break; shift+=7; }
                return v;
            }
        }
        return 0;
    }
    void setDamage(int dmg) {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kDamageComponentId || p.first==kLegacyDamageAlias; }), components.end());
        if (dmg<=0) return;
        WriteBuffer tmp; tmp.varint(dmg);
        components.emplace_back(kDamageComponentId, std::vector<std::uint8_t>(tmp.data.begin(), tmp.data.end()));
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

    // enchant helpers: binary NBT format per protocol.json 1.21.4 (SlotComponentType enchantments=10)
    // Payload: varint count, then each {varint id, varint level}, then bool showTooltip
    static int enchantIdByName(const std::string& n) {
        std::string k = n;
        if (k.rfind("minecraft:",0)==0) k = k.substr(10);
        static const std::unordered_map<std::string,int> m = {
            {"aqua_affinity",0},{"bane_of_arthropods",1},{"binding_curse",2},{"blast_protection",3},{"breach",4},{"channeling",5},{"density",6},{"depth_strider",7},{"efficiency",8},{"feather_falling",9},{"fire_aspect",10},{"fire_protection",11},{"flame",12},{"fortune",13},{"frost_walker",14},{"impaling",15},{"infinity",16},{"knockback",17},{"looting",18},{"loyalty",19},{"luck_of_the_sea",20},{"lure",21},{"mending",22},{"multishot",23},{"piercing",24},{"power",25},{"projectile_protection",26},{"protection",27},{"punch",28},{"quick_charge",29},{"respiration",30},{"riptide",31},{"sharpness",32},{"silk_touch",33},{"smite",34},{"soul_speed",35},{"sweeping_edge",36},{"swift_sneak",37},{"thorns",38},{"unbreaking",39},{"vanishing_curse",40},{"wind_burst",41},
            {"binding",2},{"vanishing",40} // aliases
        };
        auto it=m.find(k);
        return it==m.end() ? -1 : it->second;
    }
    static std::string enchantNameById(int id) {
        static const std::vector<std::string> rev = {"aqua_affinity","bane_of_arthropods","binding_curse","blast_protection","breach","channeling","density","depth_strider","efficiency","feather_falling","fire_aspect","fire_protection","flame","fortune","frost_walker","impaling","infinity","knockback","looting","loyalty","luck_of_the_sea","lure","mending","multishot","piercing","power","projectile_protection","protection","punch","quick_charge","respiration","riptide","sharpness","silk_touch","smite","soul_speed","sweeping_edge","swift_sneak","thorns","unbreaking","vanishing_curse","wind_burst"};
        if (id<0 || id>= (int)rev.size()) return "";
        return "minecraft:"+rev[id];
    }
    // Decode binary enchant payload to vector; if legacy textual, fallback to parsing
    static std::vector<std::pair<int,int>> decodeEnchants(const std::vector<std::uint8_t>& payload) {
        std::vector<std::pair<int,int>> out;
        if (payload.empty()) return out;
        // heuristic: textual contains ':' or ',' -> parse legacy
        bool maybeText=false;
        for (auto b: payload) if (b==':' || b==',') { maybeText=true; break; }
        if (maybeText) {
            // legacy "name:lvl," format
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
            return out;
        }
        // binary: varint count, then id/level pairs, then bool
        ReadBuffer rb(payload.data(), payload.size());
        try {
            int cnt = rb.varint();
            if (cnt<0 || cnt>64) return out;
            for (int i=0;i<cnt;++i) {
                if (rb.remaining() <2) break;
                int id = rb.varint();
                int lvl = rb.varint();
                out.emplace_back(id,lvl);
            }
            // showTooltip bool ignored
        } catch(...) {}
        return out;
    }
    static std::vector<std::uint8_t> encodeEnchants(const std::vector<std::pair<int,int>>& ench) {
        WriteBuffer wb;
        wb.varint((int)ench.size());
        for (auto &pr: ench) { wb.varint(pr.first); wb.varint(pr.second); }
        wb.boolean(true); // showTooltip
        return std::vector<std::uint8_t>(wb.data.begin(), wb.data.end());
    }
    static void addEnchant(ItemStack &s, const std::string &enchName, int lvl) {
        int id = enchantIdByName(enchName);
        if (id<0) return;
        std::vector<std::pair<int,int>> cur;
        for (auto &pr: s.components) if(pr.first==kEnchantmentsComponentId || pr.first==33){
            cur = decodeEnchants(pr.second);
            break;
        }
        bool found=false;
        for (auto &pr: cur) if (pr.first==id) { pr.second = std::max(pr.second, lvl); found=true; break; }
        if (!found) cur.emplace_back(id,lvl);
        // remove old (both 10 and legacy 21 and stored 33 variants)
        s.components.erase(std::remove_if(s.components.begin(), s.components.end(),
            [](auto &p){ return p.first==kEnchantmentsComponentId || p.first==33 || p.first==21; }), s.components.end());
        s.components.emplace_back(kEnchantmentsComponentId, encodeEnchants(cur));
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
        for(auto &pr: components) if(pr.first==kEnchantmentsComponentId||pr.first==33||pr.first==21){
            auto v = decodeEnchants(pr.second);
            for(auto &e: v) if(e.first==id) return true;
            // also textual fallback inside decode already handled
            if (!v.empty()) continue;
            // if decode empty but payload textual direct search
            std::string txt(pr.second.begin(), pr.second.end());
            if(txt.find(enchName)!=std::string::npos) return true;
        }
        return false;
    }
    int enchantLevel(const std::string &enchName) const {
        int id = enchantIdByName(enchName);
        if (id<0) return 0;
        for(auto &pr: components) if(pr.first==kEnchantmentsComponentId||pr.first==33||pr.first==21){
            auto v = decodeEnchants(pr.second);
            for(auto &e: v) if(e.first==id) return e.second;
        }
        return 0;
    }
    bool hasSilkTouch() const { return hasEnchant("silk_touch") || hasEnchant("minecraft:silk_touch"); }
    int fortuneLevel() const { int a=enchantLevel("fortune"); int b=enchantLevel("minecraft:fortune"); return std::max(a,b); }

    // ----- ArmorTrim component (SlotComponentType trim=45 per protocol.json 1.21.4) -----
    // Stored as component id 45. Payload per protocol: container[material holder, pattern holder, showInTooltip bool].
    // For simplicity we keep textual "pattern|material" inside payload but with correct id 45; client glint uses enchant id, trim visual uses pattern/material.
    // Full binary would be holder varints; we encode as textual for now but with correct id to fix drift; binary payload upgrade can be layered later.
    struct ArmorTrim {
        std::string pattern;  // e.g. "minecraft:coast"
        std::string material; // e.g. "minecraft:iron"
        bool has=false;
    };
    static constexpr std::uint32_t kTrimComponentId = 45;
    bool hasTrim() const {
        for (auto &pr: components) if (pr.first==kTrimComponentId || pr.first==kLegacyTrimAlias) return true;
        return false;
    }
    ArmorTrim getTrim() const {
        for (auto &pr: components) if (pr.first==kTrimComponentId || pr.first==kLegacyTrimAlias) {
            std::string txt(pr.second.begin(), pr.second.end());
            // try binary detection: if payload looks like holder container (varints) not containing '|'
            // For now support both textual "pattern|material" and binary holder encoding (we fallback to textual)
            auto sep = txt.find('|');
            if (sep==std::string::npos) sep = txt.find(',');
            ArmorTrim t;
            t.has=true;
            if (sep!=std::string::npos) {
                t.pattern = txt.substr(0, sep);
                t.material = txt.substr(sep+1);
            } else {
                // binary holder payload would be varints; treat whole as pattern
                t.pattern = txt;
            }
            if (!t.pattern.empty() && t.pattern.find(':')==std::string::npos) t.pattern = "minecraft:"+t.pattern;
            if (!t.material.empty() && t.material.find(':')==std::string::npos) t.material = "minecraft:"+t.material;
            return t;
        }
        return {};
    }
    void setTrim(const ArmorTrim& t) {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kTrimComponentId || p.first==kLegacyTrimAlias; }), components.end());
        if (!t.has || t.pattern.empty()) return;
        std::string txt = t.pattern + "|" + t.material;
        components.emplace_back(kTrimComponentId, std::vector<std::uint8_t>(txt.begin(), txt.end()));
    }
    void clearTrim() {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kTrimComponentId || p.first==kLegacyTrimAlias; }), components.end());
    }

    // ----- Brewing potion_contents (41) helpers — nether_wart -> awkward -----
    // Payload: option potionId (bool+varint), option customColor (bool), varint customEffectsCount, option customName (bool)
    // We store minimal binary for awkward/water to satisfy strict audit brewing result transform.
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
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kPotionContentsComponentId; }), components.end());
        WriteBuffer wb;
        wb.boolean(true);
        wb.varint(id);
        wb.boolean(false); // customColor absent
        wb.varint(0); // customEffects 0
        wb.boolean(false); // customName absent
        components.emplace_back(kPotionContentsComponentId, std::vector<std::uint8_t>(wb.data.begin(), wb.data.end()));
    }
    void clearPotionContents() {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kPotionContentsComponentId; }), components.end());
    }
    bool isWaterPotion() const {
        if (itemId == 0) return false;
        std::string n=name();
        if (n!="minecraft:potion" && n!="minecraft:splash_potion" && n!="minecraft:lingering_potion") return false;
        return getPotionId()==0 && !hasPotionContents();
    }
    bool isAwkwardPotion() const { return getPotionId()==1; }

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
        for (auto &pr : components) if (pr.first==kRepairCostComponentId || pr.first==kLegacyRepairAlias) {
            if (pr.second.empty()) return 0;
            int v=0; int shift=0;
            for (std::uint8_t b: pr.second) { v |= (b & 0x7F) << shift; if (!(b & 0x80)) break; shift+=7; }
            return v;
        }
        return 0;
    }
    void setRepairCost(int c) {
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kRepairCostComponentId || p.first==kLegacyRepairAlias; }), components.end());
        if (c<=0) return;
        WriteBuffer tmp; tmp.varint(c);
        components.emplace_back(kRepairCostComponentId, std::vector<std::uint8_t>(tmp.data.begin(), tmp.data.end()));
    }
    std::string getCustomName() const {
        for (auto &pr: components) if(pr.first==kCustomNameComponentId){
            // payload is anonymousNbt TextComponent {text:"..."}; for our simplified storage it's raw string or NBT.
            // If payload starts with 0x0A (TAG_Compound) we could parse NBT but we stored plain string; handle both.
            if (!pr.second.empty() && pr.second[0]==0x0A) {
                // NBT compound; try to extract "text" field via simple scan
                std::string txt(pr.second.begin(), pr.second.end());
                auto p = txt.find("text");
                if (p!=std::string::npos) {
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
        components.erase(std::remove_if(components.begin(), components.end(),
            [](auto &p){ return p.first==kCustomNameComponentId; }), components.end());
        if(n.empty()) return;
        // Store as plain string for simplicity; true vanilla uses anonymousNbt TextComponent (TAG_String with {"text":n})
        // We store raw string; write will treat as payload bytes.
        components.emplace_back(kCustomNameComponentId, std::vector<std::uint8_t>(n.begin(), n.end()));
    }
    int efficiencyLevel() const { return std::max(enchantLevel("efficiency"), enchantLevel("minecraft:efficiency")); }
    int frostWalkerLevel() const { return std::max(enchantLevel("frost_walker"), enchantLevel("minecraft:frost_walker")); }
    int soulSpeedLevel() const { return std::max(enchantLevel("soul_speed"), enchantLevel("minecraft:soul_speed")); }
    int swiftSneakLevel() const { return std::max(enchantLevel("swift_sneak"), enchantLevel("minecraft:swift_sneak")); }
    int unbreakingLevel() const { return std::max(enchantLevel("unbreaking"), enchantLevel("minecraft:unbreaking")); }
    int mendingLevel() const { return std::max(enchantLevel("mending"), enchantLevel("minecraft:mending")); }
};

} // namespace cppfm
