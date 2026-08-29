// Attributes: attribute system (plan8 Combat/Survival — full AttributeManager)
// Each attribute has base value + vector<Modifier{uuid,amount,operation}>
// Operations: 0 add, 1 multiply_base, 2 multiply_total — order is add -> multiply_base -> multiply_total
// Vanilla 1.21.4 attributes: see net.minecraft.world.entity.ai.attributes.Attributes
// plan15 combat strict: expanded 11→32 per Yarn 1.21.4 EntityAttributes (attack_knockback/block_break_speed/gravity etc)
// + ARMOR/TOUGHNESS/KB_RESIST sync via UpdateAttributes 0x7C — caps 30/20 verified 2026-08-28
// plan21 combat polish: 32 attributes retry verify, ARMOR 30/TOUGHNESS 20 caps, GRAVITY 0.08 etc, sync via 0x7C (retry).
// plan22 combat polish: verify 32 attributes (E5) with correct bases, caps 30/20, ovoid 15x20 sonic boom連携
// plan23 combat polish: 32 attributes verify (11→32), gravity 0.08 scale 1.0 safe_fall 3, armor single formula f=2+t/4 caps 30/20 wire 0x7C (verify).
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "MobEffects.hpp"
namespace cppfm {
enum class Attribute : uint8_t {
    MOVEMENT_SPEED=0, MAX_HEALTH, KNOCKBACK_RESISTANCE, ARMOR, ARMOR_TOUGHNESS,
    ATTACK_DAMAGE, ATTACK_SPEED, FLYING_SPEED, FOLLOW_RANGE, MAX_ABSORPTION, STEP_HEIGHT,
    // plan15 strict 21 additions for Yarn 1.21.4 32 total
    ATTACK_KNOCKBACK, BLOCK_BREAK_SPEED, BLOCK_INTERACTION_RANGE, BURNING_TIME,
    ENTITY_INTERACTION_RANGE, EXPLOSION_KNOCKBACK_RESISTANCE, FALL_DAMAGE_MULTIPLIER,
    GRAVITY, JUMP_STRENGTH, LUCK, MINING_EFFICIENCY, MOVEMENT_EFFICIENCY, OXYGEN_BONUS,
    SAFE_FALL_DISTANCE, SCALE, SNEAKING_SPEED, SPAWN_REINFORCEMENTS, SUBMERGED_MINING_SPEED,
    SWEEPING_DAMAGE_RATIO, TEMPT_RANGE, WATER_MOVEMENT_EFFICIENCY
};
inline const char* attributeKey(Attribute a){
    switch(a){
        case Attribute::MOVEMENT_SPEED: return "minecraft:generic.movement_speed";
        case Attribute::MAX_HEALTH: return "minecraft:generic.max_health";
        case Attribute::KNOCKBACK_RESISTANCE: return "minecraft:generic.knockback_resistance";
        case Attribute::ARMOR: return "minecraft:generic.armor";
        case Attribute::ARMOR_TOUGHNESS: return "minecraft:generic.armor_toughness";
        case Attribute::ATTACK_DAMAGE: return "minecraft:generic.attack_damage";
        case Attribute::ATTACK_SPEED: return "minecraft:generic.attack_speed";
        case Attribute::FLYING_SPEED: return "minecraft:generic.flying_speed";
        case Attribute::FOLLOW_RANGE: return "minecraft:generic.follow_range";
        case Attribute::MAX_ABSORPTION: return "minecraft:generic.max_absorption";
        case Attribute::STEP_HEIGHT: return "minecraft:generic.step_height";
        case Attribute::ATTACK_KNOCKBACK: return "minecraft:generic.attack_knockback";
        case Attribute::BLOCK_BREAK_SPEED: return "minecraft:player.block_break_speed";
        case Attribute::BLOCK_INTERACTION_RANGE: return "minecraft:player.block_interaction_range";
        case Attribute::BURNING_TIME: return "minecraft:generic.burning_time";
        case Attribute::ENTITY_INTERACTION_RANGE: return "minecraft:player.entity_interaction_range";
        case Attribute::EXPLOSION_KNOCKBACK_RESISTANCE: return "minecraft:generic.explosion_knockback_resistance";
        case Attribute::FALL_DAMAGE_MULTIPLIER: return "minecraft:generic.fall_damage_multiplier";
        case Attribute::GRAVITY: return "minecraft:generic.gravity";
        case Attribute::JUMP_STRENGTH: return "minecraft:generic.jump_strength";
        case Attribute::LUCK: return "minecraft:generic.luck";
        case Attribute::MINING_EFFICIENCY: return "minecraft:generic.mining_efficiency";
        case Attribute::MOVEMENT_EFFICIENCY: return "minecraft:generic.movement_efficiency";
        case Attribute::OXYGEN_BONUS: return "minecraft:generic.oxygen_bonus";
        case Attribute::SAFE_FALL_DISTANCE: return "minecraft:generic.safe_fall_distance";
        case Attribute::SCALE: return "minecraft:generic.scale";
        case Attribute::SNEAKING_SPEED: return "minecraft:generic.sneaking_speed";
        case Attribute::SPAWN_REINFORCEMENTS: return "minecraft:zombie.spawn_reinforcements";
        case Attribute::SUBMERGED_MINING_SPEED: return "minecraft:player.submerged_mining_speed";
        case Attribute::SWEEPING_DAMAGE_RATIO: return "minecraft:generic.sweeping_damage_ratio";
        case Attribute::TEMPT_RANGE: return "minecraft:generic.tempt_range";
        case Attribute::WATER_MOVEMENT_EFFICIENCY: return "minecraft:generic.water_movement_efficiency";
        default: return "minecraft:generic.movement_speed";
    }
}
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
        // plan15 strict defaults per Yarn 1.21.4 + wiki
        setBase(Attribute::ATTACK_KNOCKBACK,0);
        setBase(Attribute::BLOCK_BREAK_SPEED,1);
        setBase(Attribute::BLOCK_INTERACTION_RANGE,4.5);
        setBase(Attribute::BURNING_TIME,1);
        setBase(Attribute::ENTITY_INTERACTION_RANGE,3);
        setBase(Attribute::EXPLOSION_KNOCKBACK_RESISTANCE,0);
        setBase(Attribute::FALL_DAMAGE_MULTIPLIER,1);
        setBase(Attribute::GRAVITY,0.08);
        setBase(Attribute::JUMP_STRENGTH,0.42);
        setBase(Attribute::LUCK,0);
        setBase(Attribute::MINING_EFFICIENCY,0);
        setBase(Attribute::MOVEMENT_EFFICIENCY,0);
        setBase(Attribute::OXYGEN_BONUS,0);
        setBase(Attribute::SAFE_FALL_DISTANCE,3);
        setBase(Attribute::SCALE,1);
        setBase(Attribute::SNEAKING_SPEED,0.3);
        setBase(Attribute::SPAWN_REINFORCEMENTS,0);
        setBase(Attribute::SUBMERGED_MINING_SPEED,0.2);
        setBase(Attribute::SWEEPING_DAMAGE_RATIO,0);
        setBase(Attribute::TEMPT_RANGE,10);
        setBase(Attribute::WATER_MOVEMENT_EFFICIENCY,0);
    }
    void setBase(Attribute a,double v){
        // plan23 edge: clamp NaN/Inf and vanilla bounds (gravity >=0, scale 0.0625-16, armor 30 toughness 20)
        if (!std::isfinite(v)) v = 0;
        if (a == Attribute::GRAVITY) v = std::max(0.0, v);
        else if (a == Attribute::SCALE) v = std::clamp(v, 0.0625, 16.0);
        else if (a == Attribute::ARMOR) v = std::clamp(v, 0.0, 30.0);
        else if (a == Attribute::ARMOR_TOUGHNESS) v = std::clamp(v, 0.0, 20.0);
        else if (a == Attribute::SAFE_FALL_DISTANCE) v = std::max(0.0, v);
        else if (a == Attribute::FALL_DAMAGE_MULTIPLIER) v = std::max(0.0, v);
        map_[a].base=v;
    }
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
        // plan15 strict: send 32 attributes per Yarn 1.21.4 (was 6). Keep 6 for minimal but strict wants 32.
        // To remain compatible we send all 32 — client will accept any count.
        static const Attribute order[] = {
            Attribute::MAX_HEALTH, Attribute::MOVEMENT_SPEED, Attribute::ATTACK_DAMAGE,
            Attribute::ARMOR, Attribute::ARMOR_TOUGHNESS, Attribute::KNOCKBACK_RESISTANCE,
            Attribute::ATTACK_KNOCKBACK, Attribute::BLOCK_BREAK_SPEED, Attribute::BLOCK_INTERACTION_RANGE,
            Attribute::BURNING_TIME, Attribute::ENTITY_INTERACTION_RANGE, Attribute::EXPLOSION_KNOCKBACK_RESISTANCE,
            Attribute::FALL_DAMAGE_MULTIPLIER, Attribute::FLYING_SPEED, Attribute::FOLLOW_RANGE,
            Attribute::GRAVITY, Attribute::JUMP_STRENGTH, Attribute::LUCK, Attribute::MAX_ABSORPTION,
            Attribute::MINING_EFFICIENCY, Attribute::MOVEMENT_EFFICIENCY, Attribute::OXYGEN_BONUS,
            Attribute::SAFE_FALL_DISTANCE, Attribute::SCALE, Attribute::SNEAKING_SPEED,
            Attribute::SPAWN_REINFORCEMENTS, Attribute::STEP_HEIGHT, Attribute::SUBMERGED_MINING_SPEED,
            Attribute::SWEEPING_DAMAGE_RATIO, Attribute::TEMPT_RANGE, Attribute::WATER_MOVEMENT_EFFICIENCY,
            Attribute::ATTACK_SPEED
        };
        constexpr int N = sizeof(order)/sizeof(order[0]);
        out.varint(eid); out.varint(N);
        for(int i=0;i<N;++i){
            Attribute at = order[i];
            out.string(attributeKey(at)); out.f64(getValue(at));
            auto it=map_.find(at); size_t n=it==map_.end()?0:it->second.modifiers.size(); out.varint((int32_t)n);
            if(it!=map_.end()) for(auto &m:it->second.modifiers){ uint8_t d[16]={}; for(size_t j=0;j<m.uuid.size()&&j<16;j++) d[j]=uint8_t(m.uuid[j]); out.uuid(d); out.f64(m.amount); out.i8(int8_t(m.operation)); }
        }
    }
private: std::unordered_map<Attribute,AttributeInstance> map_;
};
}
