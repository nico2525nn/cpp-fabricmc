// Attributes: attribute system (plan5 エンティティ・Mobシステム)
// Each attribute has base value + vector<Modifier{uuid,amount,operation}>
// Operations: 0 add, 1 multiply_base, 2 multiply_total
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>
namespace cppfm {
enum class Attribute : uint8_t { MOVEMENT_SPEED=0, MAX_HEALTH, KNOCKBACK_RESISTANCE, ARMOR, ARMOR_TOUGHNESS, ATTACK_DAMAGE, ATTACK_SPEED, FLYING_SPEED, FOLLOW_RANGE };
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
    }
    void setBase(Attribute a,double v){ map_[a].base=v; }
    double getBase(Attribute a) const{ auto it=map_.find(a); return it==map_.end()?0:it->second.base; }
    double getValue(Attribute a) const{ auto it=map_.find(a); return it==map_.end()?0:it->second.computed(); }
    void addModifier(Attribute a,AttributeModifier m){ map_[a].addModifier(std::move(m)); }
    void removeModifier(Attribute a,const std::string& u){ auto it=map_.find(a); if(it!=map_.end()) it->second.removeModifier(u); }
    void clearModifiers(Attribute a){ auto it=map_.find(a); if(it!=map_.end()) it->second.modifiers.clear(); }
    void clearAllModifiers(){ for(auto &kv:map_) kv.second.modifiers.clear(); }
    void applyEffectModifiers(const std::vector<struct EffectInstance>& eff);
    template<typename W> void writeUpdate(W& out,int32_t eid) const{
        out.varint(eid); out.varint(3);
        auto wo=[&](Attribute at,const char* key){
            out.string(key); out.f64(getValue(at));
            auto it=map_.find(at); size_t n=it==map_.end()?0:it->second.modifiers.size(); out.varint((int32_t)n);
            if(it!=map_.end()) for(auto &m:it->second.modifiers){ uint8_t d[16]={}; for(size_t i=0;i<m.uuid.size()&&i<16;i++) d[i]=uint8_t(m.uuid[i]); out.uuid(d); out.f64(m.amount); out.i8(int8_t(m.operation)); }
        };
        wo(Attribute::MAX_HEALTH,"minecraft:generic.max_health");
        wo(Attribute::MOVEMENT_SPEED,"minecraft:generic.movement_speed");
        wo(Attribute::ATTACK_DAMAGE,"minecraft:generic.attack_damage");
    }
private: std::unordered_map<Attribute,AttributeInstance> map_;
};
}
