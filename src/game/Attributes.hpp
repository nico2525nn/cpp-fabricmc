// Attributes: attribute system (plan8 Combat/Survival — full AttributeManager)
// Each attribute has base value + vector<Modifier{uuid,amount,operation}>
// Operations: 0 add, 1 multiply_base, 2 multiply_total — order is add -> multiply_base -> multiply_total
// Vanilla 1.21.4 attributes: see net.minecraft.world.entity.ai.attributes.Attributes
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "MobEffects.hpp"
namespace cppfm {
enum class Attribute : uint8_t { MOVEMENT_SPEED=0, MAX_HEALTH, KNOCKBACK_RESISTANCE, ARMOR, ARMOR_TOUGHNESS, ATTACK_DAMAGE, ATTACK_SPEED, FLYING_SPEED, FOLLOW_RANGE, MAX_ABSORPTION, STEP_HEIGHT };
struct AttributeModifier { std::string uuid; double amount=0; int operation=0; };
struct AttributeInstance {
    double base=0;
    std::vector<AttributeModifier> modifiers;
    double computed() const {
        double v=base;
        double add=0; for(auto &m:modifiers) if(m.operation==0) add+=m.amount; v+=add;
        double mb=0; for(auto &m:modifiers) if(m.operation==1) mb+=m.amount; v+=base*mb;
        double mt=1.0; for(auto &m:modifiers) if(m.operation==2) mt*=(1.0+m.amount); v*=mt; return v;
    }
    void addModifier(AttributeModifier m){ for(auto &e:modifiers) if(e.uuid==m.uuid){e=m;return;} modifiers.push_back(std::move(m));}
    void removeModifier(const std::string& u){ modifiers.erase(std::remove_if(modifiers.begin(),modifiers.end(),[&](auto &x){return x.uuid==u;}),modifiers.end());}
};
class AttributeManager{
public:
    AttributeManager(){
        setBase(Attribute::MOVEMENT_SPEED,0.10); setBase(Attribute::MAX_HEALTH,20);
        setBase(Attribute::KNOCKBACK_RESISTANCE,0); setBase(Attribute::ARMOR,0);
        setBase(Attribute::ARMOR_TOUGHNESS,0); setBase(Attribute::ATTACK_DAMAGE,1);
        setBase(Attribute::ATTACK_SPEED,4); setBase(Attribute::FLYING_SPEED,0.02); setBase(Attribute::FOLLOW_RANGE,32);
        setBase(Attribute::MAX_ABSORPTION,0); setBase(Attribute::STEP_HEIGHT,0.6);
    }
    void setBase(Attribute a,double v){ map_[a].base=v; }
    double getBase(Attribute a) const{ auto it=map_.find(a); return it==map_.end()?0:it->second.base; }
    double getValue(Attribute a) const{ auto it=map_.find(a); return it==map_.end()?0:it->second.computed(); }
    void addModifier(Attribute a,AttributeModifier m){ map_[a].addModifier(std::move(m)); }
    void removeModifier(Attribute a,const std::string& u){ auto it=map_.find(a); if(it!=map_.end()) it->second.removeModifier(u); }
    void clearModifiers(Attribute a){ auto it=map_.find(a); if(it!=map_.end()) it->second.modifiers.clear(); }
    void clearAllModifiers(){ for(auto &kv:map_) kv.second.modifiers.clear(); }
    void applyEffectModifiers(const std::vector<struct EffectInstance>& eff){
        removeModifier(Attribute::MOVEMENT_SPEED, "effect_speed");
        removeModifier(Attribute::MOVEMENT_SPEED, "effect_slowness");
        removeModifier(Attribute::MAX_HEALTH, "effect_health_boost");
        removeModifier(Attribute::ATTACK_DAMAGE, "effect_strength");
        removeModifier(Attribute::ATTACK_DAMAGE, "effect_weakness");
        removeModifier(Attribute::MAX_ABSORPTION, "effect_absorption");
        double speedMod = speedModifierFor(eff);
        if (speedMod != 0.0) {
            addModifier(Attribute::MOVEMENT_SPEED, {"effect_speed", speedMod, 2});
        }
        int hb = 0;
        for (auto &e : eff) if (e.type == effects::HealthBoost) hb = std::max(hb, int(e.amplifier)+1);
        if (hb > 0) addModifier(Attribute::MAX_HEALTH, {"effect_health_boost", double(hb * 4), 0});
        else { /* keep removed */ }
        float dmgBonus = meleeDamageBonusFor(eff);
        if (dmgBonus != 0.f) addModifier(Attribute::ATTACK_DAMAGE, {"effect_strength", double(dmgBonus), 0});
        // Absorption handled via MAX_ABSORPTION (not serialized in UpdateAttributes, but tracked)
        float abs = absorptionFor(eff);
        if (abs > 0) addModifier(Attribute::MAX_ABSORPTION, {"effect_absorption", double(abs), 0});
        // Attack speed from Haste/MiningFatigue not vanilla but we keep digSpeed separate
    }
    // ARMOR sync helpers (plan7): derive armor/toughness/kb from equipped items
    void syncArmor(int armor, int toughness, float kbResist){
        setBase(Attribute::ARMOR, double(armor));
        setBase(Attribute::ARMOR_TOUGHNESS, double(toughness));
        setBase(Attribute::KNOCKBACK_RESISTANCE, double(kbResist));
    }
    bool armorDirty(int armor, int toughness, float kbResist) const {
        return getBase(Attribute::ARMOR) != double(armor)
            || getBase(Attribute::ARMOR_TOUGHNESS) != double(toughness)
            || getBase(Attribute::KNOCKBACK_RESISTANCE) != double(kbResist);
    }
    // plan13 §5: SoulSpeed / SwiftSneak movement modifiers
    void applySoulSpeed(int lvl){
        removeModifier(Attribute::MOVEMENT_SPEED, "soul_speed");
        if(lvl>0){
            // vanilla uses AttributeModifier multiply_base 0.105*lvl
            addModifier(Attribute::MOVEMENT_SPEED, {"soul_speed", 0.105 * double(lvl), 1});
        }
    }
    void applySwiftSneak(int lvl){
        removeModifier(Attribute::MOVEMENT_SPEED, "swift_sneak");
        if(lvl>0){
            // vanilla reduces sneak penalty: sneak speed 0.3 -> ~0.9 at lvl3
            // we model as multiply_total boost so sneaking feels faster
            double boost = 0.15 * double(lvl); // lvl3 => 0.45
            addModifier(Attribute::MOVEMENT_SPEED, {"swift_sneak", boost, 2});
        }
    }
    void clearEnchantSpeedModifiers(){
        removeModifier(Attribute::MOVEMENT_SPEED, "soul_speed");
        removeModifier(Attribute::MOVEMENT_SPEED, "swift_sneak");
    }
    // helper to sync both at once
    void syncEnchantSpeed(int soulLvl, int swiftLvl, bool isSneaking, bool onSoul){
        // soul speed only when on soul block and not sneaking
        if(onSoul && !isSneaking) applySoulSpeed(soulLvl); else removeModifier(Attribute::MOVEMENT_SPEED, "soul_speed");
        if(isSneaking && swiftLvl>0) applySwiftSneak(swiftLvl); else removeModifier(Attribute::MOVEMENT_SPEED, "swift_sneak");
    }
    template<typename W> void writeUpdate(W& out,int32_t eid) const{
        out.varint(eid); out.varint(6);
        auto wo=[&](Attribute at,const char* key){
            out.string(key); out.f64(getValue(at));
            auto it=map_.find(at); size_t n=it==map_.end()?0:it->second.modifiers.size(); out.varint((int32_t)n);
            if(it!=map_.end()) for(auto &m:it->second.modifiers){ uint8_t d[16]={}; for(size_t i=0;i<m.uuid.size()&&i<16;i++) d[i]=uint8_t(m.uuid[i]); out.uuid(d); out.f64(m.amount); out.i8(int8_t(m.operation)); }
        };
        wo(Attribute::MAX_HEALTH,"minecraft:generic.max_health");
        wo(Attribute::MOVEMENT_SPEED,"minecraft:generic.movement_speed");
        wo(Attribute::ATTACK_DAMAGE,"minecraft:generic.attack_damage");
        wo(Attribute::ARMOR,"minecraft:generic.armor");
        wo(Attribute::ARMOR_TOUGHNESS,"minecraft:generic.armor_toughness");
        wo(Attribute::KNOCKBACK_RESISTANCE,"minecraft:generic.knockback_resistance");
    }
private: std::unordered_map<Attribute,AttributeInstance> map_;
};
}
