// Attributes: vanilla attribute system (clean-room, 1.21.4 subset).
// Each Attribute has base + modifiers (UUID/amount/operation). Operations follow vanilla:
// 0 add_value, 1 add_multiplied_base, 2 add_multiplied_total.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace cppfm {

enum class Attribute : std::uint8_t {
    MOVEMENT_SPEED = 0,
    MAX_HEALTH,
    KNOCKBACK_RESISTANCE,
    ARMOR,
    ARMOR_TOUGHNESS,
    ATTACK_DAMAGE,
    ATTACK_SPEED,
    FLYING_SPEED,
    FOLLOW_RANGE
};

struct AttributeModifier {
    std::string uuid;      // textual uuid / name key
    double amount = 0;
    int operation = 0;     // 0 add, 1 multiply_base, 2 multiply_total
};

struct AttributeInstance {
    double base = 0;
    std::vector<AttributeModifier> modifiers;

    double computed() const {
        double v = base;
        double add = 0;
        for (auto &m : modifiers) if (m.operation == 0) add += m.amount;
        v += add;
        double mulBase = 0;
        for (auto &m : modifiers) if (m.operation == 1) mulBase += m.amount;
        v += base * mulBase;
        double mulTotal = 1.0;
        for (auto &m : modifiers) if (m.operation == 2) mulTotal *= (1.0 + m.amount);
        v *= mulTotal;
        return v;
    }
    void addModifier(AttributeModifier m) {
        for (auto &ex : modifiers) if (ex.uuid == m.uuid) { ex = m; return; }
        modifiers.push_back(std::move(m));
    }
    void removeModifier(const std::string &uuid) {
        modifiers.erase(std::remove_if(modifiers.begin(), modifiers.end(),
            [&](auto &x){ return x.uuid == uuid; }), modifiers.end());
    }
};

class AttributeManager {
public:
    AttributeManager() {
        setBase(Attribute::MOVEMENT_SPEED, 0.10);
        setBase(Attribute::MAX_HEALTH, 20.0);
        setBase(Attribute::KNOCKBACK_RESISTANCE, 0.0);
        setBase(Attribute::ARMOR, 0.0);
        setBase(Attribute::ARMOR_TOUGHNESS, 0.0);
        setBase(Attribute::ATTACK_DAMAGE, 1.0);
        setBase(Attribute::ATTACK_SPEED, 4.0);
        setBase(Attribute::FLYING_SPEED, 0.02);
        setBase(Attribute::FOLLOW_RANGE, 32.0);
    }
    void setBase(Attribute a, double v) { map_[a].base = v; }
    double getBase(Attribute a) const {
        auto it = map_.find(a);
        return it == map_.end() ? 0 : it->second.base;
    }
    double getValue(Attribute a) const {
        auto it = map_.find(a);
        return it == map_.end() ? 0 : it->second.computed();
    }
    void addModifier(Attribute a, AttributeModifier m) { map_[a].addModifier(std::move(m)); }
    void removeModifier(Attribute a, const std::string &uuid) {
        auto it = map_.find(a);
        if (it != map_.end()) it->second.removeModifier(uuid);
    }
    void clearModifiers(Attribute a) {
        auto it = map_.find(a);
        if (it != map_.end()) it->second.modifiers.clear();
    }
    void applyEffectModifiers(const std::vector<struct EffectInstance> &effects);
    void clearAllModifiers() { for (auto &kv : map_) kv.second.modifiers.clear(); }

    // Serialize to UpdateAttributes (0x7C) packet: varint entityId + varint count + entries
    template<typename WriteBuf>
    void writeUpdate(WriteBuf &out, std::int32_t entityId) const {
        out.varint(entityId);
        // count entries we send
        out.varint(3);
        auto writeOne = [&](Attribute attr, const char *key){
            out.string(key);
            out.f64(getValue(attr));
            auto it = map_.find(attr);
            std::size_t n = it == map_.end() ? 0 : it->second.modifiers.size();
            out.varint((std::int32_t)n);
            if (it != map_.end()) {
                for (auto &m : it->second.modifiers) {
                    std::uint8_t dummy[16]={};
                    for (size_t i=0;i<m.uuid.size()&&i<16;i++) dummy[i]=static_cast<std::uint8_t>(m.uuid[i]);
                    out.uuid(dummy);
                    out.f64(m.amount);
                    out.i8((std::int8_t)m.operation);
                }
            }
        };
        writeOne(Attribute::MAX_HEALTH, "minecraft:generic.max_health");
        writeOne(Attribute::MOVEMENT_SPEED, "minecraft:generic.movement_speed");
        writeOne(Attribute::ATTACK_DAMAGE, "minecraft:generic.attack_damage");
    }

private:
    std::unordered_map<Attribute, AttributeInstance> map_;
};

} // namespace cppfm
