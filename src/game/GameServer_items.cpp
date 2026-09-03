#include "GameServer.hpp"
#include "BlockEvent.hpp"
#include "MetadataTypes.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "StairsHelper.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "BehaviorTree.hpp"
#include "BehaviorTreeParser.hpp"
#include "EquipmentComponent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "CombatManager.hpp"
#include "MobSpawner.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
#include "PotionBrewing.hpp"
#include "Particles.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppfm {
using namespace proto;
void GameServer::sendEquipment(const MobEntity& mob) {
    // Plan13 §2: EquipmentComponent with ArmorTrim + HandDropChances, 0x80 grouping
    EquipmentComponent comp(mob.equipment);
    if (!comp.hasAny()) return;
    WriteBuffer b;
    b.varint(mob.entityId);
    comp.writePayload(b);
    broadcastPacketExcept(nullptr, proto::pl::sc::SetEquipment, b);
}
void GameServer::sendEquipmentSlot(const MobEntity& mob, int slot) {
    if (slot<0||slot>=6) return;
    EquipmentComponent comp(mob.equipment);
    WriteBuffer b;
    b.varint(mob.entityId);
    comp.writePayloadSingle(b, slot);
    broadcastPacketExcept(nullptr, proto::pl::sc::SetEquipment, b);
}
void GameServer::broadcastPlayerEquipment(const Player& p) {
    std::array<ItemStack,6> arr{};
    if (p.heldSlot>=0 && p.heldSlot<9) arr[0] = p.inv[36 + p.heldSlot];
    arr[1] = p.inv[45];
    arr[2] = p.inv[5];
    arr[3] = p.inv[6];
    arr[4] = p.inv[7];
    arr[5] = p.inv[8];
    EquipmentComponent comp(arr);
    if (!comp.hasAny()) return;
    WriteBuffer b;
    b.varint(p.entityId);
    comp.writePayload(b);
    broadcastPacketExcept(&p, proto::pl::sc::SetEquipment, b);
    try { p.conn->sendPacket(proto::pl::sc::SetEquipment, b); } catch(...){}
}
void GameServer::syncEquipmentOnChange(Player& p){
    broadcastPlayerEquipment(p);
}
void GameServer::handleMoveVehicle(Player& p, double x, double y, double z, float yaw, float pitch) {
    // plan14 §5: MoveVehicle 0x20 – update boat/minecart pos, clamp to WorldBorder, broadcast teleport
    // plan41 C-10: also broadcast VehicleMove 0x33 (x f64 y f64 z f64 yaw f32 pitch f32) for smooth vehicle movement
    if (p.vehicleId==-1) return;
    std::shared_ptr<MobEntity> veh;
    {
        std::lock_guard lk(entsMtx_);
        for(auto &m: mobs_) if(m->entityId==p.vehicleId){veh=m;break;}
    }
    if (!veh) return;
    // WorldBorder clamp (edge case)
    if (!isInsideBorder(x, z)) {
        // stop if outside border
        veh->velX = 0; veh->velZ = 0;
        return;
    }
    // compute velocity delta for smoothing (optional)
    double dx = x - veh->x, dz = z - veh->z;
    veh->velX = dx * 0.5; veh->velZ = dz * 0.5;
    veh->x = x; veh->y = y; veh->z = z; veh->yaw = yaw;
    // also update player to vehicle pos
    p.x = x; p.y = y; p.z = z; p.yaw = yaw; p.pitch = pitch;
    WriteBuffer tp;
    tp.varint(veh->entityId);
    tp.f64(x); tp.f64(y); tp.f64(z);
    tp.f32(yaw); tp.f32(pitch); tp.boolean(true);
    broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
    // plan41 C-10 VehicleMove 0x33 broadcast (server→client) for boat/minecart smooth movement
    WriteBuffer vm;
    vm.f64(x); vm.f64(y); vm.f64(z);
    vm.f32(yaw); vm.f32(pitch);
    broadcastPacketExcept(&p, proto::pl::sc::VehicleMove, vm);
    // plan42 R1: MoveMinecart 0x31 for minecart kinds (lerp steps)
    if (veh->kind==MobKind::Minecart || veh->kind==MobKind::ChestMinecart || veh->kind==MobKind::FurnaceMinecart || veh->kind==MobKind::TntMinecart || veh->kind==MobKind::HopperMinecart || veh->kind==MobKind::CommandBlockMinecart || veh->kind==MobKind::SpawnerMinecart) {
        broadcastMoveMinecart(veh->entityId, x, y, z, yaw, pitch, &p);
    }
}
void GameServer::handleHorseJump(Player& p, int power) {
    if (p.vehicleId==-1) return;
    std::shared_ptr<MobEntity> veh;
    {
        std::lock_guard lk(entsMtx_);
        for(auto &m: mobs_) if(m->entityId==p.vehicleId){veh=m;break;}
    }
    if(!veh || (veh->kind!=MobKind::Horse && veh->kind!=MobKind::Llama && veh->kind!=MobKind::Pig)) return;
    float f = std::clamp(power/100.0f, 0.0f, 1.0f);
    veh->velY = 0.42 + f*0.6;
    veh->velX *= 1.05; veh->velZ *= 1.05;
    if(veh->velY>1.2) veh->velY=1.2;
    WriteBuffer vb;
    vb.varint(veh->entityId);
    vb.i16((int16_t)(veh->velX*8000)); vb.i16((int16_t)(veh->velY*8000)); vb.i16((int16_t)(veh->velZ*8000));
    broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vb);
    veh->lastTeleportTick = tickNo_;
}
void GameServer::broadcastSetPassengers(std::int32_t vehicleId) {
    std::shared_ptr<MobEntity> veh;
    {
        std::lock_guard lk(entsMtx_);
        for (auto &m : mobs_) if (m->entityId==vehicleId) { veh=m; break; }
    }
    if (!veh) return;
    WriteBuffer b;
    b.varint(vehicleId);
    if (veh->riderEntityId != -1) {
        b.varint(1);
        b.varint(veh->riderEntityId);
    } else {
        b.varint(0);
    }
    broadcastPacketExcept(nullptr, proto::pl::sc::SetPassengers, b);
}
void GameServer::hoppersTick() {
    if (tickNo_ % HOPPER_TRANSFER_INTERVAL_TICKS != 0) return;
    std::vector<std::pair<std::int64_t, BlockEntity>> snapshot;
    blockEntities_.forEach([&](std::int64_t k, BlockEntity& be) {
        if (be.kind == BlockEntity::Kind::Hopper ||
            be.kind == BlockEntity::Kind::Dispenser)
            snapshot.emplace_back(k, be);
    });
    for (auto& [key, be] : snapshot) {
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        // hopper lock: when powered by redstone, skip transfer (plan8 hopper fix)
        if (be.kind == BlockEntity::Kind::Hopper && redstone_ && redstone_->isPoweredHere(x, y, z)) continue;
        ItemStack* slots = be.generic.slots;
        const int count = be.kind == BlockEntity::Kind::Hopper ? 5 : 9;

        auto mergeIntoFirstFit = [&](const ItemStack& src) -> bool {
            for (int i = 0; i < count; ++i) {
                auto& s = slots[i];
                if (s.empty()) { s = src; return true; }
                if (s.itemId == src.itemId && s.count < 64) {
                    const int take = std::min<int>(64 - s.count, src.count);
                    s.count += take;
                    if (take >= src.count) return true;
                }
            }
            return false;
        };
        auto extractOneFrom = [&](BlockEntity* other) -> bool {
            if (!other) return false;
            ItemStack* oslots = nullptr; int on = 0;
            switch (other->kind) {
            case BlockEntity::Kind::Chest: oslots = other->chest.slots; on = 27; break;
            case BlockEntity::Kind::Hopper: oslots = other->generic.slots; on = 5; break;
            case BlockEntity::Kind::Dispenser: oslots = other->generic.slots; on = 9; break;
            default: return false;
            }
            for (int i = 0; i < on; ++i) {
                auto& s = oslots[i];
                if (s.empty()) continue;
                ItemStack one = ItemStack::of(s.itemId, 1);
                if (mergeIntoFirstFit(one)) {
                    if (--s.count <= 0) s = ItemStack::air();
                    blockEntities_.dirty_.insert(key);
                    return true;
                }
            }
            return false;
        };

        // ---- pull from above
        int n = 0; BlockEntity::Kind k{};
        if (ItemStack* p =
                containerAt(x, y + 1, z, n, k)) {
            (void)p; (void)n; (void)k;
            if (auto* other = blockEntities_.getAt(x, y + 1, z))
                extractOneFrom(other);
        }
        // ---- item entity pickup from the hopper cell itself
        {
            std::lock_guard lk(entsMtx_);
            for (auto& e : itemDrops_) {
                if (!e->collected &&
                    std::abs(e->x - (x + .5)) < 0.8 &&
                    std::abs(e->z - (z + .5)) < 0.8 &&
                    e->y > y - 0.2 && e->y < y + 1.3) {
                    ItemStack one = ItemStack::of(e->itemId, 1);
                    if (mergeIntoFirstFit(one)) {
                        if (--e->count <= 0) e->collected = true;
                        WriteBuffer c;
                        c.varint(e->entityId);
                        c.varint(0);                     // collector: hopper
                        c.varint(1);
                        broadcastPacketExcept(nullptr, pl::sc::Collect, c);
                        break;
                    }
                }
            }
        }
        // ---- push downward
        if (auto* below = blockEntities_.getAt(x, y - 1, z)) {
            if (below != &be && below->kind != BlockEntity::Kind::Furnace) {
                for (int i = 0; i < count; ++i) {
                    auto& s = slots[i];
                    if (s.empty()) continue;
                    ItemStack one = ItemStack::of(s.itemId, 1);
                    ItemStack* oslots = nullptr; int on = 0;
                    switch (below->kind) {
                    case BlockEntity::Kind::Chest: oslots = below->chest.slots; on = 27; break;
                    case BlockEntity::Kind::Hopper: oslots = below->generic.slots; on = 5; break;
                    case BlockEntity::Kind::Dispenser: oslots = below->generic.slots; on = 9; break;
                    default: break;
                    }
                    bool moved = false;
                    if (oslots) {
                        for (int j = 0; j < on && !moved; ++j) {
                            auto& d = oslots[j];
                            if (d.empty()) { d = one; moved = true; }
                            else if (d.itemId == one.itemId && d.count < 64) {
                                ++d.count; moved = true;
                            }
                        }
                    }
                    if (moved) {
                        if (--s.count <= 0) s = ItemStack::air();
                        blockEntities_.dirty_.insert(key);
                    }
                    break;
                }
            }
        }

        // ---- dispenser/dropper: eject when powered (edge-triggered) per-item plan12 §9/§10 + QC (plan38 B-08 isQuasiPowered)
        if (be.kind == BlockEntity::Kind::Dispenser) {
            bool powered = redstone_->isQuasiPowered(x, y, z);
            bool& was = dispenserPower_[key];
            if (powered && !was) {
                // detect dropper vs dispenser by world block name
                bool isDropper = false;
                {
                    uint16_t bs = world_.getBlock(x, y, z);
                    const gen::BlockDef* bd = gen::blockByState(bs);
                    if (bd && std::string(bd->name)=="minecraft:dropper") isDropper=true;
                }
                // pick random non-empty slot (vanilla random)
                std::vector<int> nonEmpty;
                for(int i=0;i<9;++i) if(!slots[i].empty()) nonEmpty.push_back(i);
                if(!nonEmpty.empty()){
                    int pick = nonEmpty[rand()%nonEmpty.size()];
                    auto& s = slots[pick];
                    double dx = 0, dy = 0, dz = 0;
                    std::string facing = "north";
                    std::uint16_t bstate = world_.getBlock(x, y, z);
                    if (bstate) {
                        for (auto& [pk, pv] : gen::propsOf(bstate))
                            if (pk == "facing") facing = std::string(pv);
                    }
                    if (facing == "north") dz = -1;
                    else if (facing == "south") dz = 1;
                    else if (facing == "west") dx = -1;
                    else if (facing == "east") dx = 1;
                    else if (facing == "up") dy = 1;
                    else if (facing == "down") dy = -1;
                    int tx = x + (int)dx, ty = y + (int)dy, tz = z + (int)dz;
                    double sx = x + .5 + dx * .7;
                    double sy = y + .5 + dy * .7;
                    double sz = z + .5 + dz * .7;
                    std::string iname = s.name();

                    auto doDropperInsert = [&]() -> bool {
                        auto* beT = blockEntities_.getAt(tx,ty,tz);
                        if(!beT) return false;
                        // plan18 §7: furnace top ingredient / side fuel + barrel/shulker/brewing etc
                        std::string insertDir;
                        if(facing=="north") insertDir="south";
                        else if(facing=="south") insertDir="north";
                        else if(facing=="west") insertDir="east";
                        else if(facing=="east") insertDir="west";
                        else if(facing=="up") insertDir="down";
                        else if(facing=="down") insertDir="up";
                        else insertDir="up";
                        ItemStack one = ItemStack::of(s.itemId,1);
                        if(beT->kind==BlockEntity::Kind::Furnace){
                            int trySlot = (insertDir=="up") ? 0 : 1;
                            if(trySlot==1 && !isFuelItem(s.itemId)) return false;
                            auto &dst = beT->furnace.slots[trySlot];
                            if(dst.empty()){
                                dst = one;
                                blockEntities_.dirty_.insert(posKey(tx,ty,tz));
                                return true;
                            } else if(dst.itemId==one.itemId && dst.count<64){
                                ++dst.count;
                                blockEntities_.dirty_.insert(posKey(tx,ty,tz));
                                return true;
                            } else return false;
                        }
                        if(beT->kind==BlockEntity::Kind::Brewing){
                            if(insertDir=="up"){
                                auto &dst = beT->brewing.slots[3];
                                if(dst.empty()){
                                    dst = one;
                                    blockEntities_.dirty_.insert(posKey(tx,ty,tz));
                                    return true;
                                } else if(dst.itemId==one.itemId && dst.count<64){
                                    ++dst.count;
                                    blockEntities_.dirty_.insert(posKey(tx,ty,tz));
                                    return true;
                                } else return false;
                            }
                            for(int idx : {0,1,2,4}){
                                auto &d = beT->brewing.slots[idx];
                                if(d.empty()){ d=one; blockEntities_.dirty_.insert(posKey(tx,ty,tz)); return true; }
                                if(d.itemId==one.itemId && d.count<64){ ++d.count; blockEntities_.dirty_.insert(posKey(tx,ty,tz)); return true; }
                            }
                            return false;
                        }
                        ItemStack* oslots=nullptr; int on=0;
                        switch(beT->kind){
                            case BlockEntity::Kind::Chest:
                            case BlockEntity::Kind::Barrel:
                            case BlockEntity::Kind::ShulkerBox:
                                oslots=beT->chest.slots; on=27; break;
                            case BlockEntity::Kind::Hopper:
                                oslots=beT->generic.slots; on=5; break;
                            case BlockEntity::Kind::Dispenser:
                            case BlockEntity::Kind::Dropper:
                                oslots=beT->generic.slots; on=9; break;
                            default: return false;
                        }
                        if(oslots){
                            for(int j=0;j<on;++j){
                                auto &d=oslots[j];
                                if(d.empty()){ d=one; blockEntities_.dirty_.insert(posKey(tx,ty,tz)); return true; }
                                if(d.itemId==one.itemId && d.count<64){ ++d.count; blockEntities_.dirty_.insert(posKey(tx,ty,tz)); return true; }
                            }
                        }
                        return false;
                    };

                    if(isDropper){
                        // Dropper: always try insert, else drop item (never projectile)
                        bool inserted = doDropperInsert();
                        if(!inserted){
                            spawnItemDrop(tx+0.5, ty+0.5, tz+0.5, s.itemId, 1, dx*0.25, 0.15, dz*0.25);
                        } else {
                            // play click sound variant?
                        }
                        if (--s.count <= 0) s = ItemStack::air();
                        broadcastSound("minecraft:block.dispenser.dispense", x+.5,y+.5,z+.5,1.f,1.f,"block");
                        blockEntities_.dirty_.insert(key);
                    } else {
                        bool handled = false;
                        // plan18 §6: shulker_box place (dispenser exception) — 16 colors, facing, container copy
                        if (iname.find("shulker_box") != std::string::npos) {
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            if (tSt==0) {
                                uint16_t belowSt = world_.getBlock(tx,ty-1,tz);
                                std::string shulkerFacing = (belowSt==0 ? facing : "up");
                                const gen::BlockDef* def = gen::blockByName(iname);
                                if (!def) def = gen::blockByName("minecraft:shulker_box");
                                if (def) {
                                    uint16_t ns = static_cast<uint16_t>(gen::stateWithProps(*def, {{"facing", shulkerFacing}}));
                                    world_.setBlock(tx,ty,tz, ns);
                                    broadcastBlockChange(tx,ty,tz, ns);
                                    auto* beN = blockEntities_.getAt(tx,ty,tz);
                                    if (!beN) beN = &blockEntities_.create(posKey(tx,ty,tz), BlockEntity::Kind::ShulkerBox);
                                    else beN->kind = BlockEntity::Kind::ShulkerBox;
                                    if (--s.count <= 0) s = ItemStack::air();
                                    blockEntities_.dirty_.insert(key);
                                    broadcastSound("minecraft:block.dispenser.dispense", x+.5,y+.5,z+.5,1.f,1.f,"block");
                                    handled = true;
                                }
                            }
                            if (!handled) {
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled = true;
                            }
                        }
                        // bucket fluid dispense
                        if(!handled && (iname=="minecraft:water_bucket" || iname=="minecraft:lava_bucket" || iname=="minecraft:powder_snow_bucket")){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            bool replaceable = (tSt==0);
                            // check replaceable: air or non-solid? simplified air only
                            if(replaceable){
                                std::string fluid = iname=="minecraft:lava_bucket" ? "minecraft:lava" : (iname=="minecraft:powder_snow_bucket" ? "minecraft:powder_snow" : "minecraft:water");
                                uint16_t fluidSt = 0;
                                if(fluid=="minecraft:powder_snow"){
                                    auto it=gen::blockNameToState().find(fluid);
                                    if(it!=gen::blockNameToState().end()) fluidSt=static_cast<uint16_t>(it->second);
                                } else {
                                    fluidSt = static_cast<uint16_t>(gen::stateWithPropsList(fluid, {{"level","0"}}));
                                    if(fluidSt==0){ auto it=gen::blockNameToState().find(fluid); if(it!=gen::blockNameToState().end()) fluidSt=static_cast<uint16_t>(it->second); }
                                }
                                // Nether water evaporates
                                if(fluid=="minecraft:water" && world_.dimensionId()==-1){
                                    // evaporate with particles/sound
                                    broadcastSound("minecraft:block.fire.extinguish", tx+0.5,ty+0.5,tz+0.5,0.5f,2.6f,"block");
                                } else {
                                    world_.setBlock(tx,ty,tz,fluidSt);
                                    broadcastBlockChange(tx,ty,tz,fluidSt);
                                    if(fluid=="minecraft:water" || fluid=="minecraft:lava"){
                                        if(fluidSim_) fluidSim_->touch(tx,ty,tz);
                                    }
                                }
                                // replace with empty bucket
                                s = ItemStack::ofName("minecraft:bucket",1);
                                handled=true;
                            } else {
                                // fallback drop
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            }
                        } else if(!handled && iname=="minecraft:bucket"){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            const gen::BlockDef* td = gen::blockByState(tSt);
                            bool isWater=false,isLava=false,isPowder=false;
                            if(td){
                                if(td->name=="minecraft:water"){
                                    for(auto&[k,v]: gen::propsOf(tSt)) if(k=="level"&&v=="0") isWater=true;
                                } else if(td->name=="minecraft:lava"){
                                    for(auto&[k,v]: gen::propsOf(tSt)) if(k=="level"&&v=="0") isLava=true;
                                } else if(td->name=="minecraft:powder_snow") isPowder=true;
                            }
                            if(isWater||isLava||isPowder){
                                world_.setBlock(tx,ty,tz,0);
                                broadcastBlockChange(tx,ty,tz,0);
                                std::string newName = isLava?"minecraft:lava_bucket":(isPowder?"minecraft:powder_snow_bucket":"minecraft:water_bucket");
                                s = ItemStack::ofName(newName,1);
                                handled=true;
                            }
                        } else if(!handled && (iname.find("splash_potion")!=std::string::npos || iname.find("lingering_potion")!=std::string::npos || iname=="minecraft:potion")){
                            // strict B23: potion projectile should be Potion entity, not Snowball
                            spawnProjectile(ProjectileKind::Potion, sx, sy, sz, dx*1.1, dy*0.2+0.12, dz*1.1, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled=true;
                        } else if(!handled && (iname.find("_helmet")!=std::string::npos || iname.find("_chestplate")!=std::string::npos || iname.find("_leggings")!=std::string::npos || iname.find("_boots")!=std::string::npos || iname.find("horse_armor")!=std::string::npos || iname=="minecraft:elytra" || iname=="minecraft:turtle_helmet" || iname=="minecraft:carved_pumpkin" || iname=="minecraft:skull")){
                            // strict B24: dispenser armor equip (vanilla Dispenser armor)
                            bool equipped=false;
                            // Try players at target
                            for(auto &pp : playersSnapshot()){
                                int px=(int)std::floor(pp->x), py=(int)std::floor(pp->y), pz=(int)std::floor(pp->z);
                                // target is tx,ty,tz; allow one block tolerance for standing entity (ty may be feet)
                                if( (px==tx && pz==tz && (py==ty || py==ty+1 || py==ty-1))){
                                    int slot=-1;
                                    if(iname.find("_helmet")!=std::string::npos || iname=="minecraft:turtle_helmet" || iname=="minecraft:carved_pumpkin" || iname.find("skull")!=std::string::npos) slot=8;
                                    else if(iname.find("_chestplate")!=std::string::npos || iname=="minecraft:elytra") slot=7;
                                    else if(iname.find("_leggings")!=std::string::npos) slot=6;
                                    else if(iname.find("_boots")!=std::string::npos) slot=5;
                                    else if(iname.find("horse_armor")!=std::string::npos) slot=-1; // not for player
                                    if(slot>=5 && slot<=8 && pp->inv[slot].empty()){
                                        pp->inv[slot]=ItemStack::of(s.itemId,1);
                                        equipped=true;
                                        syncPlayerArmorAttributes(*pp);
                                        broadcastPlayerEquipment(*pp);
                                        break;
                                    }
                                }
                            }
                            if(!equipped){
                                // Try mobs at target
                                std::lock_guard lk(entsMtx_);
                                for(auto &m: mobs_){
                                    int mx=(int)std::floor(m->x), my=(int)std::floor(m->y), mz=(int)std::floor(m->z);
                                    if(mx==tx && mz==tz && (my==ty || my==ty+1 || my==ty-1)){
                                        int eslot=-1;
                                        if(iname.find("_helmet")!=std::string::npos || iname=="minecraft:turtle_helmet" || iname=="minecraft:carved_pumpkin") eslot=5;
                                        else if(iname.find("_chestplate")!=std::string::npos || iname=="minecraft:elytra") eslot=4;
                                        else if(iname.find("_leggings")!=std::string::npos) eslot=3;
                                        else if(iname.find("_boots")!=std::string::npos) eslot=2;
                                        else if(iname.find("horse_armor")!=std::string::npos){
                                            if(m->kind==MobKind::Horse || m->kind==MobKind::Donkey || m->kind==MobKind::Mule){
                                                eslot=4;
                                            } else if(m->kind==MobKind::Llama || m->kind==MobKind::TraderLlama){
                                                eslot=4;
                                            }
                                        }
                                        if(eslot>=2 && eslot<=5 && m->equipment[eslot].empty()){
                                            m->equipment[eslot]=ItemStack::of(s.itemId,1);
                                            equipped=true;
                                            // NOTE(cleanup): mob SetEquipment 0x60 broadcast deferred
                                            // (no helper yet — state change above is authoritative; the
                                            // previously built-but-unsent WriteBuffer was dead code).
                                            break;
                                        }
                                    }
                                }
                            }
                            if(equipped){
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            } else {
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            }
                        } else if(!handled && iname.find("arrow") != std::string::npos) {
                            spawnProjectile(ProjectileKind::Arrow, sx, sy, sz, dx*1.2, dy*0.2+0.15, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("snowball") != std::string::npos) {
                            spawnProjectile(ProjectileKind::Snowball, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname == "minecraft:egg") {
                            spawnProjectile(ProjectileKind::Egg, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("ender_pearl") != std::string::npos) {
                            spawnProjectile(ProjectileKind::EnderPearl, sx, sy, sz, dx*1.2, dy*0.2+0.12, dz*1.2, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("fire_charge") != std::string::npos) {
                            spawnProjectile(ProjectileKind::Fireball, sx, sy, sz, dx*0.5, dy*0.5, dz*0.5, -1, false);
                            if(--s.count<=0) s=ItemStack::air();
                            handled = true;
                        } else if(!handled && iname.find("_spawn_egg") != std::string::npos) {
                            MobSpawner spawner2(*this);
                            if (spawner2.spawnFromDispenser(iname, x, y, z, facing)) {
                                if(--s.count<=0) s=ItemStack::air();
                            } else {
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                            }
                            handled = true;
                        } else if(!handled && iname=="minecraft:shears"){
                            // try shear sheep at target
                            bool sheared=false;
                            {
                                std::lock_guard lk(entsMtx_);
                                for(auto &m: mobs_){
                                    int mx=(int)std::floor(m->x), my=(int)std::floor(m->y), mz=(int)std::floor(m->z);
                                    if(mx==tx && my==ty && mz==tz && m->kind==MobKind::Sheep && !m->sheared){
                                        m->sheared=true;
                                        // drop wool 1-3 (use woolColor, D16 fix)
                                        {
                                            static const char* woolNamesD[] = {
                                                "minecraft:white_wool","minecraft:orange_wool","minecraft:magenta_wool","minecraft:light_blue_wool",
                                                "minecraft:yellow_wool","minecraft:lime_wool","minecraft:pink_wool","minecraft:gray_wool",
                                                "minecraft:light_gray_wool","minecraft:cyan_wool","minecraft:purple_wool","minecraft:blue_wool",
                                                "minecraft:brown_wool","minecraft:green_wool","minecraft:red_wool","minecraft:black_wool"
                                            };
                                            int colD = m->woolColor % 16;
                                            auto woolIt=gen::itemIdByName().find(woolNamesD[colD]);
                                            if(woolIt!=gen::itemIdByName().end()){
                                                int cnt=1+rand()%3;
                                                spawnItemDrop(m->x,m->y+0.8,m->z, woolIt->second, (uint8_t)cnt, (rand()/(double)RAND_MAX-.5)*0.12, 0.12, (rand()/(double)RAND_MAX-.5)*0.12);
                                            }
                                        }
                                        WriteBuffer md; md.varint(m->entityId); md.u8(17); md.u8(8); md.u8(1); md.u8(255);
                                        broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
                                        sheared=true;
                                        break;
                                    }
                                }
                            }
                            if(sheared){
                                if(s.applyDamage(1)) s=ItemStack::air();
                                handled=true;
                            } else {
                                // check for snow_golem/mooshroom simplified: just drop if not sheared
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                // don't consume? vanilla consumes durability only on success, but we treat as not consumed
                                handled=true; // don't double-decrement
                            }
                        } else if(!handled && iname=="minecraft:flint_and_steel"){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            uint16_t below = world_.getBlock(tx,ty-1,tz);
                            const gen::BlockDef* td=gen::blockByState(tSt);
                            const gen::BlockDef* bd=gen::blockByState(below);
                            bool isAir = tSt==0;
                            bool belowSolid = bd && td==nullptr; // simplified: any non-air below is solid
                            // also check for TNT, campfire, portal
                            bool handledFS=false;
                            if(td && std::string(td->name)=="minecraft:tnt"){
                                spawnPrimedTnt(tx+0.5, ty+0.5, tz+0.5, 0, 0.2, 0, 80);
                                broadcastSound("minecraft:entity.tnt.primed", tx+0.5, ty+0.5, tz+0.5, 1.f, 1.f, "block");
                                world_.setBlock(tx,ty,tz,0); broadcastBlockChange(tx,ty,tz,0);
                                handledFS=true;
                            } else if(td && (std::string(td->name)=="minecraft:campfire" || std::string(td->name)=="minecraft:soul_campfire")){
                                std::string lit=getPropStr(tSt,"lit");
                                if(lit=="false"){
                                    std::vector<std::pair<std::string_view,std::string_view>> props;
                                    for(auto&[k,v]: gen::propsOf(tSt)) if(k!="lit") props.emplace_back(k,v);
                                    props.emplace_back("lit","true");
                                    uint16_t ns=static_cast<uint16_t>(gen::stateWithProps(*td, props));
                                    world_.setBlock(tx,ty,tz,ns); broadcastBlockChange(tx,ty,tz,ns);
                                    handledFS=true;
                                }
                            } else if(isAir && belowSolid){
                                bool soulBase = false;
                                if (bd) {
                                    auto &tags = tagManager_.blockTags;
                                    auto it = tags.find("minecraft:soul_fire_base_blocks");
                                    if (it != tags.end()) {
                                        auto nit = gen::blockNameToState().find(std::string(bd->name));
                                        if (nit != gen::blockNameToState().end()) soulBase = it->second.count(static_cast<uint32_t>(nit->second))>0;
                                    }
                                    if (!soulBase) soulBase = std::string(bd->name)=="minecraft:soul_sand"||std::string(bd->name)=="minecraft:soul_soil";
                                }
                                std::string fn = soulBase?"minecraft:soul_fire":"minecraft:fire";
                                auto it=gen::blockNameToState().find(fn);
                                if(it!=gen::blockNameToState().end()){
                                    uint16_t fs=static_cast<uint16_t>(it->second);
                                    world_.setBlock(tx,ty,tz,fs); broadcastBlockChange(tx,ty,tz,fs);
                                    handledFS=true;
                                }
                            }
                            if(handledFS){
                                if(s.applyDamage(1)) s=ItemStack::air();
                                handled=true;
                            } else {
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                handled=true;
                            }
                        } else if(!handled && iname=="minecraft:bone_meal"){
                            uint16_t tSt = world_.getBlock(tx,ty,tz);
                            const gen::BlockDef* td=gen::blockByState(tSt);
                            bool fertilized=false;
                            if(td && blockTicks_){
                                auto* beh=blockTicks_->behaviorFor(std::string(td->name));
                                if(beh && beh->fertilize(world_, tx,ty,tz,tSt,this)){
                                    uint16_t ns=world_.getBlock(tx,ty,tz);
                                    broadcastBlockChange(tx,ty,tz,ns);
                                    broadcastSound("minecraft:item.bone_meal.use", tx+0.5,ty+0.5,tz+0.5,1.f,1.f,"block");
                                    fertilized=true;
                                }
                            }
                            if(fertilized){
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            } else {
                                spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                                if(--s.count<=0) s=ItemStack::air();
                                handled=true;
                            }
                        } else if(!handled && (iname=="minecraft:tnt" || iname.find("tnt") != std::string::npos)) {
                            spawnPrimedTnt(x + dx + 0.5, y + 0.3, z + dz + 0.5, dx*0.2, 0.2, dz*0.2, 80);
                            broadcastSound("minecraft:entity.tnt.primed", x+dx+0.5, y+dy+0.5, z+dz+0.5, 1.f, 1.f, "block");
                            if(--s.count<=0) s=ItemStack::air();
                            handled=true;
                        } else if(!handled) {
                            // default drop
                            spawnItemDrop(sx, sy, sz, s.itemId, 1, dx * .25, .15, dz * .25);
                            if(--s.count<=0) s=ItemStack::air();
                            handled=true;
                        }
                        if(handled){
                            broadcastSound("minecraft:block.dispenser.dispense", x + .5, y + .5, z + .5, 1.f, 1.f, "block");
                            blockEntities_.dirty_.insert(key);
                        }
                    }
                }
            }
            was = powered;
        }
    }
}
ItemStack* GameServer::containerAt(std::int32_t x, std::int32_t y,
                                   std::int32_t z, int& countOut,
                                   BlockEntity::Kind& kindOut) {
    auto* be = blockEntities_.getAt(x, y, z);
    if (!be) return nullptr;
    kindOut = be->kind;
    switch (be->kind) {
    case BlockEntity::Kind::Chest:
    case BlockEntity::Kind::Barrel:
    case BlockEntity::Kind::ShulkerBox: countOut = 27; return be->chest.slots;
    case BlockEntity::Kind::Hopper: countOut = 5; return be->generic.slots;
    case BlockEntity::Kind::Dispenser:
    case BlockEntity::Kind::Dropper: countOut = 9; return be->generic.slots;
    case BlockEntity::Kind::Furnace: countOut = 3; return be->furnace.slots;
    case BlockEntity::Kind::Brewing: countOut = 5; return be->brewing.slots;
    default: return nullptr;
    }
}
const std::vector<TradeOffer>& GameServer::tradeTable() {
    using TO = TradeOffer;
    static const std::vector<TradeOffer> table = [] {
        auto id = [](const char* n) {
            auto it = gen::itemIdByName().find(n);
            return it!=gen::itemIdByName().end()? it->second : 0u;
        };
        return std::vector<TO>{
            {id("minecraft:wheat"), 20, 0, 0, id("minecraft:emerald"), 1, 16, 2, 0.05f},
            {id("minecraft:coal"), 15, 0, 0, id("minecraft:emerald"), 1, 16, 2, 0.05f},
            {id("minecraft:emerald"), 1, 0, 0, id("minecraft:bread"), 4, 16, 2, 0.05f},
            {id("minecraft:emerald"), 3, 0, 0, id("minecraft:iron_pickaxe"), 1, 12, 10, 0.05f},
            {id("minecraft:porkchop"), 7, 0, 0, id("minecraft:emerald"), 1, 16, 5, 0.05f},
        };
    }();
    return table;
}
// B-10: 13 professions x 5 levels (minecraft-data villagerTrades -> plan37 §6)
static std::string professionToString(VillagerData::Profession p){
    switch(p){
        case VillagerData::ARMORER: return "minecraft:armorer";
        case VillagerData::BUTCHER: return "minecraft:butcher";
        case VillagerData::CARTOGRAPHER: return "minecraft:cartographer";
        case VillagerData::CLERIC: return "minecraft:cleric";
        case VillagerData::FARMER: return "minecraft:farmer";
        case VillagerData::FISHERMAN: return "minecraft:fisherman";
        case VillagerData::FLETCHER: return "minecraft:fletcher";
        case VillagerData::LEATHERWORKER: return "minecraft:leatherworker";
        case VillagerData::LIBRARIAN: return "minecraft:librarian";
        case VillagerData::MASON: return "minecraft:mason";
        case VillagerData::SHEPHERD: return "minecraft:shepherd";
        case VillagerData::TOOLSMITH: return "minecraft:toolsmith";
        case VillagerData::WEAPONSMITH: return "minecraft:weaponsmith";
        default: return "minecraft:farmer";
    }
}
static const std::unordered_map<std::string, std::array<std::vector<TradeOffer>,5>>& professionTrades(){
    using TO = TradeOffer;
    static std::unordered_map<std::string, std::array<std::vector<TO>,5>> m;
    static bool init=false;
    if(init) return m;
    init=true;
    auto id = [](const char* n)->uint32_t{
        auto it = gen::itemIdByName().find(n);
        return it!=gen::itemIdByName().end()? it->second : 0u;
    };
    auto emerald=id("minecraft:emerald");
    // farmer 13x5 example from plan37 §6
    m["minecraft:farmer"] = {{
        std::vector<TO>{{emerald,1,0,0,id("minecraft:bread"),6,16,2,0.05f},{id("minecraft:wheat"),20,0,0,emerald,1,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:pumpkin"),6,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:pumpkin_pie"),4,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:melon_slice"),4,0,0,emerald,1,16,7,0.05f},{emerald,1,0,0,id("minecraft:cookie"),18,12,10,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cake"),1,12,15,0.05f},{emerald,1,0,0,id("minecraft:suspicious_stew"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:golden_carrot"),3,12,30,0.05f},{emerald,1,0,0,id("minecraft:glistering_melon_slice"),3,12,30,0.05f}}
    }};
    m["minecraft:librarian"] = {{
        std::vector<TO>{{emerald,1,0,0,id("minecraft:bookshelf"),1,12,2,0.05f},{id("minecraft:paper"),24,0,0,emerald,1,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:book"),4,0,0,emerald,1,12,5,0.05f},{emerald,5,0,0,id("minecraft:clock"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:written_book"),1,0,0,emerald,1,12,10,0.05f},{emerald,5,0,0,id("minecraft:name_tag"),1,12,10,0.05f}},
        std::vector<TO>{{emerald,8,0,0,id("minecraft:enchanted_book"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,5,0,0,id("minecraft:enchanted_book"),1,12,30,0.20f}}
    }};
    m["minecraft:cleric"] = {{
        std::vector<TO>{{id("minecraft:rotten_flesh"),32,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:redstone"),2,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:gold_ingot"),3,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:glowstone"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit_foot"),1,0,0,emerald,1,12,10,0.05f},{emerald,1,0,0,id("minecraft:ender_pearl"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:scute"),4,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:bottle_o_enchanting"),1,12,15,0.05f}},
        std::vector<TO>{{id("minecraft:nether_wart"),1,0,0,emerald,1,12,30,0.05f},{emerald,1,0,0,id("minecraft:ender_pearl"),1,12,30,0.05f}}
    }};
    m["minecraft:armorer"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:iron_leggings"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_boots"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:lava_bucket"),1,0,0,emerald,1,12,10,0.05f},{emerald,4,0,0,id("minecraft:iron_helmet"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,6,0,0,id("minecraft:diamond_chestplate"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_chestplate"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_boots"),1,12,30,0.05f}}
    }};
    m["minecraft:weaponsmith"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:iron_axe"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_sword"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:iron_sword"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,8,0,0,id("minecraft:diamond_axe"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_sword"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_axe"),1,12,30,0.05f}}
    }};
    m["minecraft:toolsmith"] = {{
        std::vector<TO>{{id("minecraft:coal"),15,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:stone_axe"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:iron_ingot"),4,0,0,emerald,1,12,5,0.05f},{emerald,1,0,0,id("minecraft:iron_pickaxe"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:iron_pickaxe"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:diamond"),1,0,0,emerald,1,12,15,0.05f},{emerald,6,0,0,id("minecraft:diamond_pickaxe"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,7,0,0,id("minecraft:diamond_pickaxe"),1,12,30,0.05f},{emerald,8,0,0,id("minecraft:diamond_shovel"),1,12,30,0.05f}}
    }};
    m["minecraft:butcher"] = {{
        std::vector<TO>{{id("minecraft:chicken"),14,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:cooked_chicken"),8,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:porkchop"),7,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:cooked_porkchop"),5,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:mutton"),7,0,0,emerald,1,16,7,0.05f},{emerald,1,0,0,id("minecraft:cooked_mutton"),7,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit"),4,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:rabbit_stew"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cooked_rabbit"),5,12,30,0.05f},{emerald,1,0,0,id("minecraft:cooked_mutton"),7,12,30,0.05f}}
    }};
    m["minecraft:fisherman"] = {{
        std::vector<TO>{{id("minecraft:string"),20,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:cooked_cod"),6,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:coal"),10,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:cooked_salmon"),6,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:cod"),15,0,0,emerald,1,16,10,0.05f},{emerald,1,0,0,id("minecraft:fishing_rod"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:salmon"),13,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:bucket"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:cooked_cod"),6,12,30,0.05f},{emerald,1,0,0,id("minecraft:nautilus_shell"),1,12,30,0.05f}}
    }};
    m["minecraft:fletcher"] = {{
        std::vector<TO>{{id("minecraft:stick"),32,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:arrow"),16,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),10,0,0,emerald,1,16,5,0.05f},{emerald,2,0,0,id("minecraft:bow"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:string"),14,0,0,emerald,1,16,10,0.05f},{emerald,3,0,0,id("minecraft:crossbow"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:feather"),24,0,0,emerald,1,12,15,0.05f},{emerald,3,0,0,id("minecraft:bow"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,5,0,0,id("minecraft:arrow"),16,12,30,0.05f},{emerald,5,0,0,id("minecraft:tipped_arrow"),5,12,30,0.05f}}
    }};
    m["minecraft:leatherworker"] = {{
        std::vector<TO>{{id("minecraft:leather"),6,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:leather_leggings"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:flint"),26,0,0,emerald,1,12,5,0.05f},{emerald,2,0,0,id("minecraft:leather_chestplate"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:rabbit_hide"),9,0,0,emerald,1,12,10,0.05f},{emerald,3,0,0,id("minecraft:leather_helmet"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:scute"),6,0,0,emerald,1,12,15,0.05f},{emerald,5,0,0,id("minecraft:leather_chestplate"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,6,0,0,id("minecraft:saddle"),1,12,30,0.05f},{emerald,4,0,0,id("minecraft:leather_horse_armor"),1,12,30,0.05f}}
    }};
    m["minecraft:mason"] = {{
        std::vector<TO>{{id("minecraft:clay_ball"),10,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:brick"),10,16,2,0.05f}},
        std::vector<TO>{{id("minecraft:stone"),20,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:chiseled_stone_bricks"),4,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:granite"),16,0,0,emerald,1,16,10,0.05f},{emerald,1,0,0,id("minecraft:diorite"),1,16,10,0.05f}},
        std::vector<TO>{{id("minecraft:quartz"),12,0,0,emerald,1,12,15,0.05f},{emerald,1,0,0,id("minecraft:quartz_pillar"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,1,0,0,id("minecraft:quartz_block"),1,12,30,0.05f},{emerald,1,0,0,id("minecraft:clay"),10,12,30,0.05f}}
    }};
    m["minecraft:shepherd"] = {{
        std::vector<TO>{{id("minecraft:wool"),18,0,0,emerald,1,16,2,0.05f},{emerald,1,0,0,id("minecraft:shears"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:dye"),12,0,0,emerald,1,16,5,0.05f},{emerald,1,0,0,id("minecraft:bed"),1,16,5,0.05f}},
        std::vector<TO>{{id("minecraft:wool"),18,0,0,emerald,1,16,10,0.05f},{emerald,2,0,0,id("minecraft:banner"),1,12,10,0.05f}},
        std::vector<TO>{{id("minecraft:string"),14,0,0,emerald,1,12,15,0.05f},{emerald,3,0,0,id("minecraft:painting"),3,12,15,0.05f}},
        std::vector<TO>{{emerald,3,0,0,id("minecraft:bed"),1,12,30,0.05f},{emerald,2,0,0,id("minecraft:shears"),1,12,30,0.05f}}
    }};
    m["minecraft:cartographer"] = {{
        std::vector<TO>{{id("minecraft:paper"),24,0,0,emerald,1,16,2,0.05f},{emerald,7,0,0,id("minecraft:empty_map"),1,12,2,0.05f}},
        std::vector<TO>{{id("minecraft:glass_pane"),11,0,0,emerald,1,16,5,0.05f},{emerald,7,0,0,id("minecraft:explorer_map"),1,12,5,0.05f}},
        std::vector<TO>{{id("minecraft:compass"),1,0,0,emerald,1,12,10,0.05f},{emerald,7,0,0,id("minecraft:explorer_map"),1,12,10,0.05f}},
        std::vector<TO>{{emerald,12,0,0,id("minecraft:explorer_map"),1,12,15,0.05f},{emerald,7,0,0,id("minecraft:item_frame"),1,12,15,0.05f}},
        std::vector<TO>{{emerald,20,0,0,id("minecraft:explorer_map"),1,12,30,0.05f},{emerald,7,0,0,id("minecraft:banner"),1,12,30,0.05f}}
    }};
    return m;
}
bool GameServer::openTrading(Player& p, MobEntity& v) {
    if (!p.conn) return false;
    const int windowId = ++villagerWindowSeq_;
    WriteBuffer b;
    b.varint(windowId);
    b.varint(menus::kMerchant);
    nbt::writeTextComponent(b, "Villager");
    try { p.conn->sendPacket(proto::pl::sc::OpenScreen, b); } catch (...) {}
    // Trade List payload (plan37 B-10: profession 13x5, 2*level offers, Gossip priceMultiplier 0.05)
    WriteBuffer tl;
    tl.varint(windowId);
    // NITWIT: no trades
    if (v.villagerData.profession == VillagerData::NITWIT) {
        tl.varint(0);
        tl.varint(0); tl.varint(0);
        int lvl = std::clamp(v.villagerData.level,1,5);
        tl.varint(lvl); tl.i32(v.villagerXp); tl.boolean(false);
        try { p.conn->sendPacket(proto::pl::sc::TradeList, tl); } catch (...) {}
        return true;
    }
    int lvl = std::clamp(v.villagerData.level,1,5);
    if (v.villagerLevel != lvl) lvl = std::clamp(v.villagerLevel,1,5);
    std::string profStr = professionToString(v.villagerData.profession);
    const auto& pt = professionTrades();
    const std::vector<TradeOffer>* offersPtr = nullptr;
    std::vector<TradeOffer> fallback;
    auto it = pt.find(profStr);
    if (it != pt.end()) {
        offersPtr = &it->second[lvl-1];
    } else {
        // fallback to legacy table slice (should not happen)
        const auto& t = tradeTable();
        int num = std::min<int>((int)t.size(), lvl*2);
        if (num==0) num = std::min<int>((int)t.size(), 2);
        fallback.assign(t.begin(), t.begin()+num);
        offersPtr = &fallback;
    }
    const auto& offers = *offersPtr;
    int num = (int)offers.size();
    // lvl*2 slicing is already per-level vector size <=2*lvl, but enforce min 2 for lvl1 fallback display
    // For farmer lvl1 size 2 already OK
    tl.varint(static_cast<std::int32_t>(num));
    int gossipRep = v.gossip.get(p.uuid);
    for (int i=0;i<num;++i) {
        const auto& t = offers[i];
        // Gossip priceMultiplier: 0.05 discount scaled by gossip (plan37 §6: min 0.5 cap)
        float baseMult = t.priceMultiplier;
        float gossipDiscount = std::min(0.5f, gossipRep * 0.02f);
        // villager Workaround: priceMult stays baseMult, specialPrice carries discount
        float priceMult = baseMult;
        int specialPrice = - int(std::floor(gossipDiscount * t.inCount));
        // firstBuy
        tl.varint(static_cast<std::int32_t>(t.inItem));
        tl.varint(t.inCount);
        tl.varint(0);
        ItemStack::of(t.outItem, t.outCount).write(tl);
        if (t.inItem2 != 0) {
            tl.boolean(true);
            tl.varint(static_cast<std::int32_t>(t.inItem2));
            tl.varint(t.inCount2);
            tl.varint(0);
        } else {
            tl.boolean(false);
        }
        tl.boolean(false);
        tl.i32(0);
        tl.i32(t.maxUses);
        tl.i32(t.xp);
        tl.i32(specialPrice);
        tl.f32(priceMult);
        tl.i32(t.demand);
    }
    tl.varint(0);
    tl.varint(0);
    tl.varint(lvl);
    tl.i32(v.villagerXp);
    tl.boolean(true);
    try { p.conn->sendPacket(proto::pl::sc::TradeList, tl); } catch (...) {}
    return true;
}
bool GameServer::selectTrade(Player& p, std::int32_t index) {
    // B-10: resolve trade from professionTrades by nearby villager's profession/level, fallback to tradeTable
    TradeOffer t{};
    bool found=false;
    {
        std::lock_guard lk(entsMtx_);
        for (auto& m : mobs_) if (m->kind==MobKind::Villager) {
            double dx=m->x - p.x, dz=m->z - p.z;
            if (dx*dx+dz*dz < 128) {
                if (m->villagerData.profession == VillagerData::NITWIT) return false;
                std::string profStr = professionToString(m->villagerData.profession);
                auto it = professionTrades().find(profStr);
                if (it != professionTrades().end()) {
                    int lvl = std::clamp(m->villagerData.level,1,5);
                    if (m->villagerLevel != lvl) lvl = std::clamp(m->villagerLevel,1,5);
                    const auto& vec = it->second[lvl-1];
                    if (index>=0 && index < (int)vec.size()) { t = vec[index]; found=true; }
                }
                break;
            }
        }
    }
    if (!found) {
        const auto& trades = tradeTable();
        if (index < 0 || static_cast<std::size_t>(index) >= trades.size()) return false;
        t = trades[static_cast<std::size_t>(index)];
    }
    // verify first buy present
    int have = 0;
    for (auto& s : p.inv) if (!s.empty() && s.itemId == t.inItem) have += s.count;
    if (have < t.inCount) return false;
    if (t.inItem2 != 0) {
        int have2=0;
        for (auto& s: p.inv) if (!s.empty() && s.itemId == t.inItem2) have2+= s.count;
        if (have2 < t.inCount2) return false;
    }
    int need = t.inCount;
    for (auto& s : p.inv) {
        if (need <= 0) break;
        if (!s.empty() && s.itemId == t.inItem) {
            const int take = std::min<int>(s.count, need);
            s.count -= take; need -= take;
            if (s.count <= 0) s = ItemStack::air();
        }
    }
    if (t.inItem2 != 0) {
        int need2 = t.inCount2;
        for (auto& s: p.inv) {
            if (need2<=0) break;
            if (!s.empty() && s.itemId == t.inItem2) {
                const int take = std::min<int>(s.count, need2);
                s.count -= take; need2 -= take;
                if (s.count <= 0) s = ItemStack::air();
            }
        }
    }
    addToInventory(p, t.outItem, t.outCount);
    resendInventory(p);
    spawnXpOrbs(p.x, p.y + 1, p.z, 2, &p);
    // plan40 C-06: villager_trade advancement trigger
    {
        std::string soldName = "minecraft:emerald";
        for(auto& kv: gen::itemIdByName()) if(kv.second==t.inItem) { soldName=kv.first; break; }
        onVillagerTraded(p, soldName, t.inCount);
    }
    broadcastSound("minecraft:entity.villager.yes", p.x, p.y, p.z,
                   .8f, 1.f, "neutral");
    // Plan16: Villager XP, Gossip, level 1..5, restock 2/day (vanilla: 2 restocks per day at work site)
    {
        std::lock_guard lk(entsMtx_);
        for (auto& m : mobs_) if (m->kind==MobKind::Villager) {
            double dx=m->x - p.x, dz=m->z - p.z;
            if (dx*dx+dz*dz < 64) {
                m->villagerXp += 3 + (rand()%4);
                m->gossip.add(p.uuid, 2);
                // Level up check: every 10 xp -> level++ (vanilla xp thresholds 10,70 etc simplified)
                if (m->villagerXp >= m->villagerLevel * 10 && m->villagerLevel < 5) {
                    m->setVillagerLevel(m->villagerLevel+1);
                    broadcastSound("minecraft:entity.villager.levelup", m->x,m->y,m->z,1.f,1.f,"neutral");
                } else {
                    m->syncVillagerLevel();
                }
                // Restock: 2/day (vanilla: work POI, 6000-12000 ticks, max 2 per day)
                std::int64_t curDay = tickNo_ / 24000;
                if (curDay != m->villagerLastRestockDay) {
                    m->villagerRestocksToday = 0;
                    m->villagerLastRestockDay = curDay;
                }
                if (m->villagerRestocksToday >= 2) {
                    // already restocked twice today, schedule next day morning
                    m->restockUntil = (curDay+1)*24000 + 2000;
                } else {
                    if (m->restockUntil < tickNo_) {
                        // schedule next restock window (plan46 G-15: 2nd window
                        // auto-scheduled so 2/day is reachable without new trades)
                        m->restockUntil = tickNo_ + MobEntity::kRestockSecondWindowTicks + (rand()%2000);
                    }
                }
                break;
            }
        }
    }
    return true;
}
void GameServer::growResinNearHeart(int hx,int hy,int hz) {
    if (!isNight()) return;
    // find pale_oak_log within 8 of heart and place resin_clump on side
    for (int attempt=0; attempt<8; ++attempt) {
        int lx = hx + (rand()%17 - 8);
        int ly = hy + (rand()%9 - 4);
        int lz = hz + (rand()%17 - 8);
        uint16_t st = world_.getBlock(lx,ly,lz);
        auto* bd = gen::blockByState(st);
        if (!bd) continue;
        std::string n(bd->name);
        if (n!="minecraft:pale_oak_log" && n!="minecraft:stripped_pale_oak_log" && n!="minecraft:pale_oak_wood") continue;
        const int DX[4]={1,-1,0,0}, DZ[4]={0,0,1,-1};
        for (int d=0; d<4; ++d) {
            int rx=lx+DX[d], rz=lz+DZ[d];
            if (world_.getBlock(rx,ly,rz)!=0) continue;
            auto it = gen::blockNameToState().find("minecraft:resin_clump");
            if (it==gen::blockNameToState().end()) continue;
            uint16_t place = static_cast<uint16_t>(it->second);
            world_.setBlock(rx,ly,rz,place);
            broadcastBlockChange(rx,ly,rz,place);
            broadcastSound("minecraft:block.resin.place", rx+0.5, ly+0.5, rz+0.5, 1.f, 1.f, "block");
            return;
        }
    }
}
void GameServer::itemsTick() {
    struct Pickup { std::shared_ptr<ItemEntity> ent; Player* collector; };
    std::vector<Pickup> pickups;
    std::vector<std::uint8_t> none;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = itemDrops_.begin(); it != itemDrops_.end();) {
            auto& e = *it;
            ++e->ageTicks;
            if (e->ageTicks > 6000) { it = itemDrops_.erase(it); continue; }
            // gravity-lite
            e->vy -= 0.04; if (e->vy < -0.5) e->vy = -0.5;
            e->y += e->vy; e->x += e->vx; e->z += e->vz;
            // crude ground clamp
            world_.generateChunkIfMissing(static_cast<std::int32_t>(e->x)>>4,
                                   static_cast<std::int32_t>(e->z)>>4);
            int col=4;
            world_.withChunk(static_cast<std::int32_t>(e->x)>>4,
                      static_cast<std::int32_t>(e->z)>>4,[&](const Chunk& c){
                for (int ry=kSectionsPerChunk*16-1; ry>=0; --ry)
                    if (c.blocks[Chunk::index(ry>>4,ry&15,
                        static_cast<std::int32_t>(e->z)&15,
                        static_cast<std::int32_t>(e->x)&15)]!=0){col=ry+1;break;}
            });
            const double gy = kMinY + col + 0.25;
            if (e->y < gy) { e->y = gy; e->vy = 0; e->vx *= 0.6; e->vz *= 0.6; }

            if (e->ageTicks > 10) {
                for (auto& pp : playersSnapshot()) {
                    auto* pl = pp.get();
                    if (!pl->inPlay || pl->dead) continue;
                    double dx=pl->x-e->x, dy=(pl->y+0.9)-e->y, dz=pl->z-e->z;
                    if (dx*dx+dy*dy+dz*dz < 2.0) {
                        pickups.push_back({e, pl});
                        break;
                    }
                }
            }
            ++it;
        }
    }
    for (auto& pk : pickups) {
        if (addToInventory(*pk.collector, pk.ent->itemId, pk.ent->count)) {
            onItemObtained(*pk.collector,
                           ItemStack::of(pk.ent->itemId, pk.ent->count),
                           "picked_up");
            WriteBuffer c;
            c.varint(pk.ent->entityId);
            c.varint(pk.collector->entityId);
            c.varint(pk.ent->count);
            broadcastPacketExcept(nullptr, 0x76 /*collect*/, c);
            resendInventory(*pk.collector);
            std::lock_guard lk(entsMtx_);
            pk.ent->collected = true;
            itemDrops_.erase(std::remove_if(itemDrops_.begin(), itemDrops_.end(),
                [&](const std::shared_ptr<ItemEntity>& x){ return x.get()==pk.ent.get(); }),
                itemDrops_.end());
            WriteBuffer rm;
            rm.varint(1); rm.varint(pk.ent->entityId);
            broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
        }
    }
}
void GameServer::spawnItemDrop(double x,double y,double z,std::uint32_t itemId,std::uint8_t cnt,
                               double vx,double vy,double vz) {
    ItemStack s = (itemId==0 || cnt==0) ? ItemStack::air() : ItemStack::of(itemId, cnt);
    spawnItemDrop(x, y, z, s, vx, vy, vz);
}
void GameServer::spawnItemDrop(double x,double y,double z,const ItemStack& stack,
                               double vx,double vy,double vz) {
    auto e = std::make_shared<ItemEntity>();
    e->entityId = nextEntityId();
    e->x=x; e->y=y; e->z=z; e->vx=vx; e->vy=vy; e->vz=vz;
    e->setStack(stack);
    {
        std::lock_guard lk(entsMtx_);
        itemDrops_.push_back(e);
    }
    broadcastSpawnItem(*e);
}
void GameServer::broadcastSpawnItem(const ItemEntity& it) {
    WriteBuffer b;
    b.varint(it.entityId);
    std::uint8_t zero[16] = {};
    b.uuid(zero);
    b.varint(static_cast<std::int32_t>(gen::entityTypeIdByName().at("minecraft:item")));
    b.f64(it.x); b.f64(it.y); b.f64(it.z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(1);                                        // objectData = 1 (item w/ stack)
    b.i16(static_cast<std::int16_t>(it.vx*8000));
    b.i16(static_cast<std::int16_t>(it.vy*8000));
    b.i16(static_cast<std::int16_t>(it.vz*8000));
    broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
    // D11 (plan26 §4): metadata index 8 type 7 Slot must carry full ItemStack payload
    // via ItemStack::write (count,varint itemId, added, removed, components).
    // Old code wrote minimal `0,0` and mishandled air (count 0 wrote itemId 0).
    WriteBuffer md;
    md.varint(it.entityId);
    md.u8(8); md.u8(7);
    WriteBuffer slot;
    ItemStack s = it.asStack();
    s.write(slot);
    md.raw(slot.data.data(), slot.data.size());
    md.u8(255);
    broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md);
}
bool GameServer::addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count) {
    // merge into existing stacks (hotbar 36..44, main 9..35)
    for (int pass = 0; pass < 2; ++pass) {
        for (int i : (pass == 0 ? std::initializer_list<int>{36,37,38,39,40,41,42,43,44}
                                : std::initializer_list<int>{9,10,11,12,13,14,15,16,17,18,19,
                                                             20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35})) {
            auto& s = p.inv[i];
            if (pass == 0 && s.itemId == itemId && s.count > 0 && s.count < 64) {
                const auto take = std::min<int16_t>((int16_t)(64 - s.count), (int16_t)count);
                s.count += take; count -= take;
                if (count == 0) return true;
            } else if (pass == 1 && s.count == 0) {
                s.itemId = itemId; s.count = std::min<int16_t>(64, (int16_t)count);
                count -= s.count;
                if (count == 0) return true;
            }
        }
    }
    return false;                                       // inventory full: stays on ground
}
void GameServer::resendInventory(Player& p) {
    WriteBuffer b;
    b.varint(0);                                            // window 0
    b.varint(++p.invStateId);
    b.varint(46);
    for (int i = 0; i < 46; ++i) p.inv[i].write(b);
    ItemStack::air().write(b);                          // carried
    try { p.conn->sendPacket(pl::sc::ContainerSetContent, b); } catch (...) {}
}
void GameServer::sendSetExperience(Player& p) {
    WriteBuffer b;
    b.f32(p.xp.progress);
    b.varint(p.xp.level);
    b.varint(p.xp.totalXp);
    try { p.conn->sendPacket(pl::sc::SetExperience, b); } catch (...) {}
}
void GameServer::effectsTick() {
    for (auto& pp : playersSnapshot()) {
        auto* p = pp.get();
        if (!p->inPlay || p->effects.empty()) continue;
        bool changed = false;
        for (auto it = p->effects.begin(); it != p->effects.end();) {
            if (it->type == effects::InstantHealth && !it->expired()) {
                p->health = std::min(20.f, p->health + 4.f * (it->amplifier + 1));
                sendSetHealth(*p);
                it = p->effects.erase(it);
                changed = true;
                continue;
            }
            if (it->type == effects::InstantDamage && !it->expired()) {
                applyDamage(*p, 6.f * (it->amplifier + 1), "magic");
                it = p->effects.erase(it);
                changed = true;
                continue;
            }
            --it->durationTicks;
            if (it->expired()) {
                WriteBuffer b;
                b.varint(p->entityId);
                b.varint(it->type);
                try { p->conn->sendPacket(pl::sc::RemoveMobEffect, b); }
                catch (...) {}
                it = p->effects.erase(it);
                changed = true;
                continue;
            }
            if (it->type == effects::Regeneration &&
                tickNo_ % std::max(1, 50 >> it->amplifier) == 0)
                p->health = std::min(20.f, p->health + 1.f), sendSetHealth(*p);
            // plan23 §7 strict: poison 25>>amp (was 40), wither 40>>amp, poison does not kill below 1
            if (it->type == effects::Poison &&
                tickNo_ % std::max(1, 25 >> it->amplifier) == 0) {
                if (p->health > 1.0f) applyDamage(*p, 1.f, "poison");
            }
            if (it->type == effects::Wither &&
                tickNo_ % std::max(1, 40 >> it->amplifier) == 0)
                applyDamage(*p, 1.f, "wither");
            if (it->type == effects::Saturation && tickNo_ % std::max(1, 2 >> it->amplifier) == 0) {
                addFoodAndSaturation(*p, 1, float(it->amplifier + 1));
            }
            if (it->type == effects::Hunger) {
                addHungerExhaustion(*p, 0.005f * float(it->amplifier + 1));
            }
            ++it;
        }
        if (changed) onEffectsChanged(p);
        // per-tick metadata effects: invisibility/glowing/levitation/slow-falling
        // plan29 §7 polish: vanilla Levitation vy = 0.05*(amp+1) with lerp 0.2, fallDistance reset, ignore when swimming/riding
        {
            static thread_local std::unordered_map<std::int32_t,double> levVy;
            int levAmp = amplifierFor(p->effects, effects::Levitation);
            if (levAmp >= 0 && !p->isSwimming && p->vehicleId == -1) {
                double target = levitationVelocity(levAmp);
                double &vy = levVy[p->entityId];
                vy += (target - vy) * 0.2;
                p->y += vy;
                p->fallDist = 0;
                p->prevFeetY = p->y;
                if (p->conn) {
                    WriteBuffer lev;
                    lev.varint(p->entityId);
                    lev.f64(p->x); lev.f64(p->y); lev.f64(p->z);
                    lev.i8((int8_t)(p->yaw*256.f/360.f)); lev.i8((int8_t)(p->pitch*256.f/360.f));
                    lev.boolean(p->onGround);
                    try { broadcastPacketExcept(nullptr, pl::sc::EntityTeleport, lev); } catch(...) {}
                }
            } else if (levAmp >= 0) {
                // levitating but swimming/riding -> still suppress fall damage
                p->fallDist = 0;
                levVy.erase(p->entityId);
            } else {
                levVy.erase(p->entityId);
                if (hasEffect(p->effects, effects::SlowFalling)) {
                    if (p->fallDist > 0) p->fallDist *= 0.9;
                }
            }
        }
    }
    for (auto &pp2 : playersSnapshot()) {
        auto* p2 = pp2.get();
        if (!p2->inPlay || !p2->conn) continue;
        p2->attributes.applyEffectModifiers(p2->effects);
        if (tickNo_ % 20 == 0 && (!p2->effects.empty() || p2->attributes.getValue(Attribute::MOVEMENT_SPEED) != 0.10
            || p2->attributes.getValue(Attribute::MAX_HEALTH) != 20.0
            || p2->attributes.getValue(Attribute::ARMOR) != 0
            || p2->attributes.getValue(Attribute::ATTACK_DAMAGE) != 1.0)) {
            WriteBuffer ab;
            p2->attributes.writeUpdate(ab, p2->entityId);
            try { p2->conn->sendPacket(pl::sc::UpdateAttributes, ab); } catch(...) {}
            try { broadcastPacketExcept(p2, pl::sc::UpdateAttributes, ab); } catch(...) {}
        }
        // sync invisibility/glowing metadata: index 0 flags, index 6 pose already
        if (tickNo_ % 20 == 0) {
            bool invis = isInvisible(p2->effects);
            bool glow = isGlowing(p2->effects);
            if (invis || glow) {
                WriteBuffer md;
                md.varint(p2->entityId);
                if (invis) { md.u8(0); md.varint(0); md.u8(0x20); }
                if (glow) { md.u8(0); md.varint(0); md.u8(0x40); }
                md.u8(255);
                if (md.data.size() > 2) try { broadcastPacketExcept(nullptr, pl::sc::SetEntityMetadata, md); } catch(...) {}
            }
        }
    }
}
void GameServer::furnacesTick() {
    const auto& items = gen::itemIdByName();
    blockEntities_.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Furnace) return;
        FurnaceData& f = be.furnace;
        const std::int32_t x = posKeyUnpackX(key);
        const std::int32_t y = posKeyUnpackY(key);
        const std::int32_t z = posKeyUnpackZ(key);
        world_.generateChunkIfMissing(x >> 4, z >> 4);
        const std::uint16_t stateHere = world_.getBlock(x, y, z);

        // fuel consumption
        if (f.burnTicks > 0) --f.burnTicks;
        const Recipe* recipe =
            f.slots[FurnaceData::kInput].empty()
                ? nullptr
                : recipes_.findSmelting(f.slots[FurnaceData::kInput].itemId);
        const bool canSmelt =
            recipe && (!f.slots[FurnaceData::kOutput].empty() ||
                       true) /* output merge handled below */;
        if (f.burnTicks <= 0 && canSmelt && !f.slots[FurnaceData::kFuel].empty()) {
            const int ft = furnaceFuelTicks(f.slots[FurnaceData::kFuel].itemId);
            if (ft > 0) {
                f.burnDuration = static_cast<std::int16_t>(ft);
                f.burnTicks = f.burnDuration;
                ItemStack& fuel = f.slots[FurnaceData::kFuel];
                if (--fuel.count <= 0) fuel = ItemStack::air();
                blockEntities_.dirty_.insert(key);
            }
        }
        const bool burning = f.burnTicks > 0;
        if (canSmelt && burning) {
            if (++f.cookProgress >= f.cookTotal) {
                f.cookProgress = 0;
                auto out = recipe->result;
                auto& dst = f.slots[FurnaceData::kOutput];
                if (dst.empty()) dst = out;
                else if (dst.itemId == out.itemId) dst.count += out.count;
                else { f.cookProgress = f.cookTotal; return; }
                ItemStack& in = f.slots[FurnaceData::kInput];
                if (--in.count <= 0) in = ItemStack::air();
                blockEntities_.dirty_.insert(key);
                // xp orbs on manual collection only; skip here
            }
        } else {
            f.cookProgress = 0;
        }

        // lit-state block update (vanilla swaps furnace[lit=...])
        static const gen::BlockDef* fdef = gen::blockByName("minecraft:furnace");
        if ((fdef && stateHere == fdef->defaultState) || stateHere == 4351) {
            // NOTE(cleanup): parenthesized as evaluated — (fdef && stateHere == default) || stateHere == 4351.
            const std::uint16_t want = gen::stateWithPropsList("minecraft:furnace",
                {{"lit", burning ? "true" : "false"}});
            if (stateHere != want) {
                world_.setBlock(x, y, z, want);
                broadcastBlockChange(x, y, z, want);
            }
        }
        (void)items;
    });
}
void GameServer::brewingTick() {
    // Brewing stand: fuel (blaze powder -> kFuelPerBlaze) + brewTime kBrewTicks.
    const auto itBlaze = gen::itemIdByName().find("minecraft:blaze_powder");
    const std::uint32_t blazeId = itBlaze != gen::itemIdByName().end() ? itBlaze->second : 0;
    blockEntities_.forEach([&](std::int64_t key, BlockEntity& be) {
        if (be.kind != BlockEntity::Kind::Brewing) return;
        BrewingData& b = be.brewing;
        // replenish fuel from blaze powder in slot 4
        if (b.fuel <= 0 && !b.slots[4].empty() && (blazeId == 0 || b.slots[4].itemId == blazeId)) {
            if (--b.slots[4].count <= 0) b.slots[4] = ItemStack::air();
            b.fuel = BrewingData::kFuelPerBlaze;
            blockEntities_.dirty_.insert(key);
        }
        if (b.brewTime > 0) {
            --b.brewTime;
            blockEntities_.dirty_.insert(key);
            if (b.brewTime == 0) {
                // brew complete: consume ingredient slot 3 and transform potions (strict audit MEDIUM I7)
                // plan19 inventory: full PotionBrewing transforms (water->awkward, awkward->effect, splash/lingering, redstone/glowstone)
                if (!b.slots[3].empty()) {
                    std::uint32_t ingId = b.slots[3].itemId;
                    if (--b.slots[3].count <= 0) b.slots[3] = ItemStack::air();
                    auto idOf = [&](const char* n)->std::uint32_t{
                        auto it = gen::itemIdByName().find(n);
                        return it != gen::itemIdByName().end() ? it->second : 0;
                    };
                    std::uint32_t potionId = idOf("minecraft:potion");
                    std::uint32_t splashId = idOf("minecraft:splash_potion");
                    std::uint32_t lingeringId = idOf("minecraft:lingering_potion");
                    std::uint32_t gunpowderId = idOf("minecraft:gunpowder");
                    std::uint32_t dragonBreathId = idOf("minecraft:dragon_breath");
                    for (int pi = 0; pi < 3; ++pi) {
                        auto &stk = b.slots[pi];
                        if (stk.empty()) continue;
                        // handle gunpowder -> splash and dragon breath -> lingering via itemId change (PotionBrewing splash transform)
                        if (ingId == gunpowderId && potionId != 0 && splashId != 0) {
                            if (stk.itemId == potionId) {
                                std::vector<std::uint8_t> saved;
                                for (auto &pr : stk.components) if (pr.first==ItemStack::kPotionContentsComponentId) saved = pr.second;
                                stk.itemId = splashId;
                                if (!saved.empty()) {
                                    bool has=false;
                                    for (auto &pr: stk.components) if(pr.first==ItemStack::kPotionContentsComponentId) has=true;
                                    if (!has) stk.components.emplace_back(ItemStack::kPotionContentsComponentId, saved);
                                }
                                continue;
                            }
                        }
                        if (ingId == dragonBreathId && splashId != 0 && lingeringId != 0) {
                            if (stk.itemId == splashId) {
                                std::vector<std::uint8_t> saved;
                                for (auto &pr : stk.components) if (pr.first==ItemStack::kPotionContentsComponentId) saved = pr.second;
                                stk.itemId = lingeringId;
                                if (!saved.empty()) {
                                    bool has=false;
                                    for (auto &pr: stk.components) if(pr.first==ItemStack::kPotionContentsComponentId) has=true;
                                    if (!has) stk.components.emplace_back(ItemStack::kPotionContentsComponentId, saved);
                                }
                                continue;
                            }
                        }
                        bool isPotionItem = (stk.itemId == potionId || stk.itemId == splashId || stk.itemId == lingeringId);
                        if (!isPotionItem) continue;
                        // plan23 §5 I7: use PotionBrewing::mix for transform (water->awkward, awkward->effect, redstone/glowstone)
                        int curId = stk.getPotionId();
                        bool hasContents = stk.hasPotionContents();
                        int target = PotionBrewing::mix(curId, hasContents, ingId);
                        if (target >= 0) {
                            stk.setPotionId(target);
                        }
                    }
                    blockEntities_.dirty_.insert(key);
                } else {
                    // no ingredient but timer expired? just reset
                    b.brewTime = 0;
                }
                // send ContainerSetData to viewers of this brewing stand
                // fuel and brewTime will be synced via dirty flag and next interaction,
                // but also broadcast to any player with menu open on this block
                for (auto& p : playersSnapshot()) {
                    // find sessions? we broadcast via block entity dirty; menu content sync
                    // will happen on next click; for now we just mark dirty.
                    (void)p;
                }
            }
        } else {
            // idle: try to start brewing if we have ingredient + at least one potion and fuel
            bool hasIngredient = !b.slots[3].empty();
            bool hasPotion = !b.slots[0].empty() || !b.slots[1].empty() || !b.slots[2].empty();
            if (hasIngredient && hasPotion && b.fuel > 0) {
                // consume 1 fuel per operation
                --b.fuel;
                b.brewTime = BrewingData::kBrewTicks;
                blockEntities_.dirty_.insert(key);
            }
        }
    });
}
void GameServer::spawnXpOrbs(double x, double y, double z, int totalPoints,
                             Player* directTo) {
    // split into vanilla-ish orb sizes (plan16: include 2477 for dragon 12000)
    static const int kSizes[] = {1, 3, 7, 17, 37, 73, 149, 307, 617, 1237, 2477};
    std::vector<int> orbs;
    while (totalPoints > 0) {
        int pick = 0;
        for (int i = 0; i < 11; ++i)
            if (kSizes[i] <= totalPoints) pick = i;
        if (pick == 0 && totalPoints < 1) break;
        const int v = kSizes[pick];
        orbs.push_back(std::min(v, totalPoints));
        totalPoints -= std::min(v, totalPoints);
        if (orbs.size() >= 16) break;                    // sanity cap
    }
    if (orbs.empty()) return;
    std::vector<std::shared_ptr<XpOrbEntity>> created;
    {
        std::lock_guard lk(entsMtx_);
        for (int v : orbs) {
            auto e = std::make_shared<XpOrbEntity>();
            e->entityId = nextEntityId();
            e->value = static_cast<std::uint16_t>(v);
            e->x = x + ((rand() % 5) - 2) * 0.1;
            e->y = y; e->z = z + ((rand() % 5) - 2) * 0.1;
            e->vy = 0.08;
            xpOrbs_.push_back(e);
            created.push_back(e);
        }
    }
    for (auto& e : created) {
        WriteBuffer b;
        b.varint(e->entityId);
        b.f64(e->x); b.f64(e->y); b.f64(e->z);
        b.i16(static_cast<std::int16_t>(e->value));
        broadcastPacketExcept(nullptr, pl::sc::SpawnExperienceOrb, b);
    }
}
void GameServer::xpOrbsTick() {
    struct Pickup { std::shared_ptr<XpOrbEntity> orb; Player* p; };
    std::vector<Pickup> pickups;
    {
        std::lock_guard lk(entsMtx_);
        for (auto it = xpOrbs_.begin(); it != xpOrbs_.end();) {
            auto& e = *it;
            ++e->ageTicks;
            if (e->ageTicks > 6000) { it = xpOrbs_.erase(it); continue; }
            e->vy -= 0.03; if (e->vy < -0.4) e->vy = -0.4;
            e->y += e->vy;
            world_.generateChunkIfMissing(static_cast<std::int32_t>(e->x)>>4,
                                   static_cast<std::int32_t>(e->z)>>4);
            int col=4;
            world_.withChunk(static_cast<std::int32_t>(e->x)>>4,
                      static_cast<std::int32_t>(e->z)>>4,[&](const Chunk& c){
                for (int ry=kSectionsPerChunk*16-1; ry>=0; --ry)
                    if (c.blocks[Chunk::index(ry>>4,ry&15,
                        static_cast<std::int32_t>(e->z)&15,
                        static_cast<std::int32_t>(e->x)&15)]!=0){col=ry+1;break;}
            });
            const double gy = kMinY + col + 0.25;
            if (e->y < gy) { e->y = gy; e->vy = 0; }
            if (e->ageTicks > 10) {
                for (auto& pp : playersSnapshot()) {
                    auto* pl = pp.get();
                    if (!pl->inPlay || pl->dead || pl->gamemode != 0) continue;
                    double dx=pl->x-e->x, dy=(pl->y+0.9)-e->y, dz=pl->z-e->z;
                    if (dx*dx+dy*dy+dz*dz < 2.5) { pickups.push_back({e, pl}); break; }
                }
            }
            ++it;
        }
    }
    for (auto& pk : pickups) {
        Player& p = *pk.p;
        int xp = pk.orb->value;
        // plan13 §4: Mending – repair equipped item with Mending before adding to XP
        {
            std::vector<int> mendingSlots;
            for (int i=0;i<46;++i) if(!p.inv[i].empty() && p.inv[i].mendingLevel()>0 && p.inv[i].getDamage()>0) mendingSlots.push_back(i);
            if(!mendingSlots.empty() && xp>0){
                int pick = mendingSlots[rand() % mendingSlots.size()];
                ItemStack &target = p.inv[pick];
                int dmg = target.getDamage();
                int repair = std::min(dmg, xp * 2);
                target.setDamage(dmg - repair);
                xp -= repair / 2;
                resendInventory(p);
                if(pick>=5 && pick<=8) syncPlayerArmorAttributes(p);
            }
        }
        if(xp>0) p.xp.addPoints(xp);
        sendSetExperience(p);
        WriteBuffer c;
        c.varint(pk.orb->entityId);
        c.varint(p.entityId);
        c.varint(1);
        broadcastPacketExcept(nullptr, pl::sc::Collect, c);
        WriteBuffer rm;
        rm.varint(1); rm.varint(pk.orb->entityId);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
        std::lock_guard lk(entsMtx_);
        xpOrbs_.erase(std::remove_if(xpOrbs_.begin(), xpOrbs_.end(),
            [&](const std::shared_ptr<XpOrbEntity>& x){
                return x.get()==pk.orb.get(); }),
            xpOrbs_.end());
    }
}
std::shared_ptr<ProjectileEntity> GameServer::spawnProjectile(ProjectileKind kind, double x, double y,
                                 double z, double vx, double vy, double vz,
                                 std::int32_t ownerId, bool ownerIsPlayer, bool charged) {
    auto e = std::make_shared<ProjectileEntity>();
    e->entityId = nextEntityId();
    e->kind = kind;
    e->x = x; e->y = y; e->z = z;
    e->vx = vx; e->vy = vy; e->vz = vz;
    e->ownerId = ownerId;
    e->ownerIsPlayer = ownerIsPlayer;
    e->charged = charged;
    projectiles_.push_back(e);
    // plan28 finish: this was an EMPTY entsMtx_ lock_guard — spawnProjectile is
    // called from BehaviorTree actions while GameServer::mobsTick already holds
    // entsMtx_ (mob AI runs under it); re-locking the non-recursive mutex was a
    // SELF-DEADLOCK that froze the tick forever (dragon breath -> fireball).
    // projectiles_ is only mutated on the tick thread (spawn + projectilesTick).
    const auto& types = gen::entityTypeIdByName();
    static const char* kNames[] = {"minecraft:arrow", "minecraft:snowball",
                                   "minecraft:egg", "minecraft:ender_pearl",
                                   "minecraft:potion", "minecraft:wither_skull",
                                   "minecraft:fireball", "minecraft:dragon_fireball",
                                   "minecraft:trident", "minecraft:wind_charge", "minecraft:breeze_wind_charge"};
    int idx = static_cast<int>(kind);
    const char* entName = (idx >=0 && idx < (int)(sizeof(kNames)/sizeof(kNames[0]))) ? kNames[idx] : "minecraft:snowball";
    auto ti = types.find(entName);
    WriteBuffer b;
    b.varint(e->entityId);
    std::uint8_t zero[16] = {};
    b.uuid(zero);
    b.varint(ti != types.end() ? static_cast<std::int32_t>(ti->second) : 0);
    b.f64(x); b.f64(y); b.f64(z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(1);                                        // objectData: velocity
    b.i16(static_cast<std::int16_t>(vx * 8000));
    b.i16(static_cast<std::int16_t>(vy * 8000));
    b.i16(static_cast<std::int16_t>(vz * 8000));
    broadcastPacketExcept(nullptr, pl::sc::SpawnEntity, b);
    return e;
}
void GameServer::projectilesTick() {
    struct Hit { std::shared_ptr<ProjectileEntity> p; Player* player; std::shared_ptr<MobEntity> mob; float dmg; };
    std::vector<Hit> hits;
    std::vector<std::int32_t> despawn;
    {
        for (auto it = projectiles_.begin(); it != projectiles_.end();) {
            auto& pr = *it;
            ++pr->ageTicks;
            if ((pr->ageTicks > 1200) || (pr->stuck && pr->ageTicks > 600 + 1200)) {
                despawn.push_back(pr->entityId);
                it = projectiles_.erase(it);
                continue;
            }
            // plan44 §3 G-09 loyalty: returning trident homes to its owner (passes through blocks)
            if (pr->returningToOwner && pr->kind == ProjectileKind::Trident) {
                double tx=0, ty=0, tz=0; bool found=false;
                if (pr->ownerIsPlayer) {
                    for (auto& pp : playersSnapshot())
                        if (pp->entityId == pr->ownerId && !pp->dead) { tx=pp->x; ty=pp->y+1.0; tz=pp->z; found=true; break; }
                } else {
                    std::lock_guard lk2(entsMtx_);
                    for (auto& mb : mobs_)
                        if (mb->entityId == pr->ownerId && !mb->dead) { tx=mb->x; ty=mb->y+0.8; tz=mb->z; found=true; break; }
                }
                if (found) {
                    double dx=tx-pr->x, dy=ty-pr->y, dz=tz-pr->z;
                    double d = std::sqrt(dx*dx+dy*dy+dz*dz);
                    if (d < 1.5) { // caught by owner (thrown trident used durability, not consumed)
                        broadcastSound("minecraft:item.trident.return", tx, ty, tz, 1.f, 1.f, "player");
                        despawn.push_back(pr->entityId);
                        it = projectiles_.erase(it);
                        continue;
                    }
                    double sp = std::min(d, 1.5);
                    pr->vx = dx/d*sp; pr->vy = dy/d*sp; pr->vz = dz/d*sp;
                    pr->x += pr->vx; pr->y += pr->vy; pr->z += pr->vz;
                    ++it;
                    continue;
                }
                // owner gone: fall through to normal physics (ages out)
            }
            if (!pr->stuck) {
                // plan16: Fireball gravity 0 (vanilla FireballEntity, WitherSkull, DragonFireball have no gravity)
                // plan34 §3 wind_charge / breeze_wind_charge gravity 0 + breeze deflect
                double g = 0.03;
                if (pr->kind == ProjectileKind::Arrow) g = 0.05;
                else if (pr->kind == ProjectileKind::Fireball || pr->kind == ProjectileKind::WitherSkull || pr->kind == ProjectileKind::DragonFireball || pr->kind == ProjectileKind::WindCharge || pr->kind == ProjectileKind::BreezeWindCharge) g = 0.0;
                // plan34 breeze deflect: reverse non-wind projectiles within 1.5 of a breeze
                if (pr->kind != ProjectileKind::BreezeWindCharge && pr->kind != ProjectileKind::WindCharge) {
                    std::lock_guard lk2(entsMtx_);
                    for (auto& mb : mobs_) if (mb->kind==MobKind::Breeze) {
                        double bdx=mb->x - pr->x, bdy=(mb->y+1.0)-pr->y, bdz=mb->z - pr->z;
                        if (bdx*bdx+bdy*bdy+bdz*bdz < 2.25) { // 1.5²
                            pr->vx = -pr->vx; pr->vy = -pr->vy*0.6 + 0.2; pr->vz = -pr->vz;
                            broadcastSound("minecraft:entity.breeze.deflect", mb->x, mb->y, mb->z, 1.f, 1.f, "hostile");
                            break;
                        }
                    }
                }
                pr->vy -= g;
                pr->x += pr->vx; pr->y += pr->vy; pr->z += pr->vz;
                world_.generateChunkIfMissing(
                    static_cast<std::int32_t>(pr->x) >> 4,
                    static_cast<std::int32_t>(pr->z) >> 4);
                // block collision (plan44 G-09: returning loyalty tridents pass through blocks)
                if (!pr->returningToOwner && world_.getBlock(static_cast<std::int32_t>(pr->x),
                                    static_cast<std::int32_t>(pr->y),
                                    static_cast<std::int32_t>(pr->z)) != 0) {
                    if (pr->kind == ProjectileKind::Arrow) {
                        pr->stuck = true;
                    } else if (pr->kind == ProjectileKind::Trident && pr->loyaltyLevel > 0) {
                        pr->returningToOwner = true; // loyalty: bounce off blocks back to owner
                    } else if (pr->kind == ProjectileKind::EnderPearl) {
                        // pearl teleport: find owner player and teleport
                        Player* owner = nullptr;
                        for (auto &pp : playersSnapshot()) if (pp->entityId == pr->ownerId && pr->ownerIsPlayer) { owner = pp.get(); break; }
                        if (owner) {
                            double tx = pr->x + 0.5;
                            double ty = pr->y + 0.5;
                            double tz = pr->z + 0.5;
                            // clamp to avoid inside block: raise by 0.5
                            owner->x = tx; owner->y = ty; owner->z = tz;
                            // teleport packet
                            if (owner->conn) {
                                WriteBuffer tb;
                                tb.varint(0); // teleport id not tracked for pearl? use 0
                                tb.f64(tx); tb.f64(ty); tb.f64(tz);
                                tb.f64(0); tb.f64(0); tb.f64(0);
                                tb.f32(owner->yaw); tb.f32(owner->pitch);
                                tb.u32(0);
                                try { owner->conn->sendPacket(proto::pl::sc::PlayerPosition, tb); } catch(...) {}
                            }
                            // broadcast to others
                            {
                                WriteBuffer tp;
                                tp.varint(owner->entityId);
                                tp.f64(tx); tp.f64(ty); tp.f64(tz);
                                tp.i8(static_cast<int8_t>(owner->yaw*256.f/360.f));
                                tp.i8(static_cast<int8_t>(owner->pitch*256.f/360.f));
                                tp.boolean(false);
                                broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
                            }
                            applyDamage(*owner, 5.f, "fall");
                            owner->lastEnderPearlTick = tickNo_;
                            // cooldown packet plan17 LOW: vanilla 20t (1 sec), was 60
                            if (owner->conn) {
                                auto pid = gen::itemIdByName().find("minecraft:ender_pearl");
                                if (pid != gen::itemIdByName().end()) {
                                    WriteBuffer cd;
                                    cd.varint(static_cast<int32_t>(pid->second));
                                    cd.varint(20); // 1 sec vanilla
                                    try { owner->conn->sendPacket(proto::pl::sc::SetCooldown, cd); } catch(...) {}
                                }
                            }
                        }
                        despawn.push_back(pr->entityId);
                        it = projectiles_.erase(it); continue;
                    } else {
                        despawn.push_back(pr->entityId);
                        it = projectiles_.erase(it); continue;
                    }
                } else {
                    // entity collision
                    bool hitSomething = false;
                    for (auto& pp : playersSnapshot()) {
                        if (pr->ownerIsPlayer && pp->entityId == pr->ownerId)
                            continue;
                        if (pp->dead || !pp->inPlay) continue;
                        const double dx = pp->x - pr->x;
                        const double dy = pp->y + 0.9 - pr->y;
                        const double dz = pp->z - pr->z;
                        if (dx*dx + dy*dy + dz*dz < 0.55) {
                            // plan44 §3 G-08: shield blocks frontal arrows/tridents (piercing bypasses shields)
                            if ((pr->kind == ProjectileKind::Arrow || pr->kind == ProjectileKind::Trident) && pr->piercingLevel <= 0) {
                                DamageSource asrc(pr->kind == ProjectileKind::Arrow ? "arrow" : "trident");
                                if (CombatManager::tryShieldBlock(*this, *pp, asrc, pr->x, pr->z, false)) {
                                    hitSomething = true; break;
                                }
                            }
                            // plan44 §3 G-09: no double-hit while piercing through
                            if (std::find(pr->piercedIds.begin(), pr->piercedIds.end(), pp->entityId) != pr->piercedIds.end())
                                continue;
                            float dmg = 0;
                            if (pr->kind == ProjectileKind::Arrow) {
                                float base=6.f;
                                dmg = base * static_cast<float>(std::min(1.0, std::sqrt(pr->vx*pr->vx+pr->vy*pr->vy+pr->vz*pr->vz)/2.0));
                            } else if (pr->kind == ProjectileKind::Trident) {
                                dmg = 8.f; // plan44 G-09: vanilla thrown-trident damage vs players
                            } else if (pr->kind==ProjectileKind::BreezeWindCharge || pr->kind==ProjectileKind::WindCharge) {
                                dmg = 1.f;
                            }
                            if (dmg > 0)
                                hits.push_back({pr, pp.get(), nullptr, dmg});
                            else if (pr->kind==ProjectileKind::BreezeWindCharge || pr->kind==ProjectileKind::WindCharge) {
                                // wind charge knockback only even if dmg 1
                                hits.push_back({pr, pp.get(), nullptr, 1.f});
                            } else {
                                hitSomething = true; break;
                            }
                            // wind charge knockback
                            if (pr->kind==ProjectileKind::BreezeWindCharge || pr->kind==ProjectileKind::WindCharge) {
                                double inv=1.0/(std::sqrt(pr->vx*pr->vx+pr->vz*pr->vz)+1e-6);
                                double kx=pr->vx*inv*1.8, kz=pr->vz*inv*1.8;
                                WriteBuffer vel; vel.varint(pp->entityId); vel.i16((int16_t)(kx*8000)); vel.i16((int16_t)(0.35*8000)); vel.i16((int16_t)(kz*8000));
                                try{ pp->conn->sendPacket(proto::pl::sc::EntityVelocity, vel);}catch(...){}
                            }
                            // plan44 §3 G-09 piercing: arrows pass through (level = entity count), keep flying
                            if (pr->kind == ProjectileKind::Arrow && pr->piercingLevel > 0) {
                                pr->piercedIds.push_back(pp->entityId);
                                pr->piercingLevel--;
                                continue;
                            }
                            hitSomething = true;
                            break;
                        }
                    }
                    if (!hitSomething) {
                        std::lock_guard lk(entsMtx_);
                        for (auto& m : mobs_) {
                            if (!pr->ownerIsPlayer &&
                                m->entityId == pr->ownerId) continue;
                            const double dx = m->x - pr->x;
                            const double dy = m->y + 0.8 - pr->y;
                            const double dz = m->z - pr->z;
                            if (dx*dx + dy*dy + dz*dz < 0.55) {
                                // plan44 §3 G-09: no double-hit while piercing through
                                if (std::find(pr->piercedIds.begin(), pr->piercedIds.end(), m->entityId) != pr->piercedIds.end())
                                    continue;
                                float dmg = 5.f;
                                if (pr->kind==ProjectileKind::BreezeWindCharge || pr->kind==ProjectileKind::WindCharge) dmg=1.f;
                                hits.push_back({pr, nullptr, m, dmg});
                                // plan44 §3 G-09 piercing: arrows pass through mobs too, keep flying
                                if (pr->kind == ProjectileKind::Arrow && pr->piercingLevel > 0) {
                                    pr->piercedIds.push_back(m->entityId);
                                    pr->piercingLevel--;
                                    continue;
                                }
                                hitSomething = true;
                                break;
                            }
                        }
                    }
                    if (hitSomething) {
                        // plan44 §3 G-09 loyalty: trident returns to owner instead of despawning on entity hit
                        if (pr->kind == ProjectileKind::Trident && pr->loyaltyLevel > 0 && !pr->returningToOwner) {
                            pr->returningToOwner = true;
                            ++it;
                            continue;
                        }
                        despawn.push_back(pr->entityId);
                        it = projectiles_.erase(it);
                        continue;
                    }
                }
            }
            ++it;
        }
    }
    for (auto& h : hits) {
        // plan13 §7: Channeling trident spawns lightning when thundering and target in water/rain (plan37 B-11 thunder gate + canSeeSky)
        if (h.p->kind == ProjectileKind::Trident) {
            bool isThundering = thundering();
            if (isThundering) {
                double lx = h.player ? h.player->x : (h.mob ? h.mob->x : h.p->x);
                double ly = h.player ? h.player->y : (h.mob ? h.mob->y : h.p->y);
                double lz = h.player ? h.player->z : (h.mob ? h.mob->z : h.p->z);
                bool hasChannel=false;
                for(auto &pp: playersSnapshot()) if(pp->entityId==h.p->ownerId && h.p->ownerIsPlayer){
                    for(int i=36;i<=44;i++) if(!pp->inv[i].empty() && EnchantmentHelper::hasChanneling(pp->inv[i])) hasChannel=true;
                    for(int i=5;i<=8;i++) if(!pp->inv[i].empty() && EnchantmentHelper::hasChanneling(pp->inv[i])) hasChannel=true;
                    // also check held trident directly if owner inventory not found via helper already, but also direct check
                    break;
                }
                // plan37: channeling requires thundering && canSeeSky && hasChannel
                if (hasChannel && isThundering) {
                    // canSeeSky: check sky light or no opaque blocks above target
                    bool canSeeSky = false;
                    {
                        int tx = (int)std::floor(lx);
                        int ty = (int)std::floor(ly);
                        int tz = (int)std::floor(lz);
                        // simplified: if sky light 15 at target y, consider canSeeSky
                        try{
                            uint8_t sky = world_.getSkyLight(tx, ty, tz);
                            if(sky >= 15) canSeeSky = true;
                            else {
                                // fallback: scan up to maxY for non-air
                                bool blocked=false;
                                for(int y2=ty+1; y2<320; ++y2){
                                    if(world_.getBlock(tx, y2, tz)!=0){ blocked=true; break; }
                                }
                                canSeeSky = !blocked;
                            }
                        } catch(...){ canSeeSky = true; }
                    }
                    if(canSeeSky){
                        strikeLightning(lx, ly, lz);
                    }
                }
            }
        }
        if (h.player) {
            applyDamage(*h.player, h.dmg, "arrow");
            WriteBuffer de;
            de.varint(h.player->entityId);
            const auto dtid = gameData_.idOf("minecraft:damage_type",
                                             "minecraft:arrow");
            de.varint(dtid >= 0 ? dtid : 0);
            de.varint(0); de.varint(0);
            de.boolean(false);
            try { h.player->conn->sendPacket(pl::sc::DamageEvent, de); }
            catch (...) {}
        } else if (h.mob) {
            applyDamageToMob(*h.mob, h.dmg, "arrow");
            if (h.mob->dead) {
                WriteBuffer rm; rm.varint(1); rm.varint(h.mob->entityId);
                broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
                const auto drop = MobEntity::dropFor(h.mob->kind);
                if (drop.itemId)
                    spawnItemDrop(h.mob->x, h.mob->y + .4, h.mob->z,
                                  drop.itemId, drop.count);
                std::lock_guard lk(entsMtx_);
                mobAi_.erase(h.mob->entityId);
                mobs_.erase(std::remove(mobs_.begin(), mobs_.end(), h.mob),
                            mobs_.end());
            }
        }
    }
    for (auto id : despawn) {
        WriteBuffer rm; rm.varint(1); rm.varint(id);
        broadcastPacketExcept(nullptr, pl::sc::RemoveEntities, rm);
    }
}
void GameServer::minecartsTick() {
    // plan14 §5: minecart rail physics – powered_rail boost 0.06, detector/activator, gravity/friction
    std::vector<std::shared_ptr<MobEntity>> carts;
    {
        std::lock_guard lk(entsMtx_);
        for (auto &m : mobs_) if (m->kind == MobKind::Minecart) carts.push_back(m);
    }
    for (auto &cart : carts) {
        if (cart->dead) continue;
        // Find rail under or at cart pos (check y, y-1, y+1 per vanilla)
        int bx = static_cast<int>(std::floor(cart->x));
        int by = static_cast<int>(std::floor(cart->y));
        int bz = static_cast<int>(std::floor(cart->z));
        std::uint16_t railState = 0;
        const gen::BlockDef* railDef = nullptr;
        int rx=bx, ry=by, rz=bz;
        std::string railShape="north_south";
        std::string railName;
        bool found=false;
        for (int dy : {0,-1,1}) {
            std::uint16_t st = world_.getBlock(bx, by+dy, bz);
            const gen::BlockDef* bd = gen::blockByState(st);
            if (!bd) continue;
            std::string n(bd->name);
            if (n=="minecraft:rail" || n=="minecraft:powered_rail" || n=="minecraft:detector_rail" || n=="minecraft:activator_rail") {
                railState = st; railDef = bd; ry = by+dy; rx=bx; rz=bz; railName=n; found=true;
                for (auto &pr : gen::propsOf(st)) if (pr.first=="shape") railShape=std::string(pr.second);
                break;
            }
        }
        // Detector rail: powered when cart on it
        if (found && railName=="minecraft:detector_rail") {
            bool curPowered=false;
            for (auto &pr : gen::propsOf(railState)) if (pr.first=="powered" && pr.second=="true") curPowered=true;
            bool wantPowered = true; // cart present implies powered
            // Check distance: cart must be within 0.3 of center to count? Use 0.5 for simplicity always true if found
            if (curPowered != wantPowered) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for (auto &pr : gen::propsOf(railState)) if (pr.first!="powered") props.emplace_back(pr.first, pr.second);
                props.emplace_back("powered", wantPowered?"true":"false");
                std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*railDef, props));
                world_.setBlock(rx, ry, rz, ns);
                // analog output 15 implied via emissionLevel when powered true
            }
        } else if (found && railName=="minecraft:detector_rail") {
            // no-op
        }
        // If not on rail, apply free physics (gravity + friction)
        if (!found) {
            cart->velY -= 0.04; // gravity
            cart->velX *= 0.98; cart->velY *= 0.98; cart->velZ *= 0.98;
            // ground check
            if (world_.getBlock(bx, by-1, bz) != 0) {
                if (cart->velY < 0) cart->velY = 0;
                cart->velX *= 0.7; cart->velZ *= 0.7;
            }
            cart->x += cart->velX; cart->y += cart->velY; cart->z += cart->velZ;
        } else {
            // On rail: snap y to rail top and apply rail-directed movement
            double targetY = ry + 0.125 + 0.5; // rail top ~0.625 above block? vanilla 0.5; use 0.5
            if (std::abs(cart->y - targetY) > 0.1) cart->y = targetY;
            // Powered rail boost
            if (railName=="minecraft:powered_rail") {
                bool powered=false;
                for (auto &pr : gen::propsOf(railState)) if (pr.first=="powered" && pr.second=="true") powered=true;
                if (powered) {
                    // plan14 §5: accelerate along rail axis by 0.06 (powered_rail)
                    double ax=0, az=0;
                    if (railShape=="north_south" || railShape=="ascending_north" || railShape=="ascending_south") {
                        // Z axis
                        double dir = (cart->velZ >= 0) ? 1.0 : -1.0;
                        if (std::abs(cart->velZ) < 0.01) dir = 1.0; // default north->south
                        az = dir * 0.06;
                        // ascending adds Y
                        if (railShape=="ascending_north" || railShape=="ascending_south") cart->velY += 0.04;
                    } else if (railShape=="east_west" || railShape=="ascending_east" || railShape=="ascending_west") {
                        double dir = (cart->velX >= 0) ? 1.0 : -1.0;
                        if (std::abs(cart->velX) < 0.01) dir = 1.0;
                        ax = dir * 0.06;
                        if (railShape=="ascending_east" || railShape=="ascending_west") cart->velY += 0.04;
                    } else {
                        // curved: accelerate along dominant axis
                        if (std::abs(cart->velX) > std::abs(cart->velZ)) ax = (cart->velX >=0?1:-1)*0.06;
                        else az = (cart->velZ >=0?1:-1)*0.06;
                    }
                    cart->velX += ax; cart->velZ += az;
                    // clamp speed
                    double speed = std::sqrt(cart->velX*cart->velX + cart->velZ*cart->velZ);
                    if (speed > 0.4) { double f=0.4/speed; cart->velX*=f; cart->velZ*=f; }
                    // broadcast velocity for powered boost (plan11 spec EntityVelocity 0x5F)
                    {
                        WriteBuffer vb;
                        vb.varint(cart->entityId);
                        vb.i16(static_cast<std::int16_t>(cart->velX * 8000));
                        vb.i16(static_cast<std::int16_t>(cart->velY * 8000));
                        vb.i16(static_cast<std::int16_t>(cart->velZ * 8000));
                        broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vb);
                    }
                } else {
                    // unpowered powered rail slows down
                    cart->velX *= 0.5; cart->velZ *= 0.5;
                }
            }
            // Activator rail eject
            if (railName=="minecraft:activator_rail") {
                bool powered=false;
                for (auto &pr : gen::propsOf(railState)) if (pr.first=="powered" && pr.second=="true") powered=true;
                if (powered && cart->riderEntityId != -1) {
                    // eject rider
                    int rider = cart->riderEntityId;
                    cart->riderEntityId = -1;
                    // find rider mob/player and clear vehicleId
                    {
                        std::lock_guard lk(entsMtx_);
                        for (auto &m : mobs_) if (m->entityId==rider) m->vehicleId=-1;
                    }
                    for (auto &pp : playersSnapshot()) if (pp->entityId==rider) pp->vehicleId=-1;
                    broadcastSetPassengersEmpty(cart->entityId);
                    // also try to move rider slightly off
                }
            }
            // General rail friction and motion
            cart->velX *= 0.98; cart->velY *= 0.98; cart->velZ *= 0.98;
            // Apply movement along rail shape (constrain to rail axis)
            if (railShape=="north_south" || railShape=="ascending_north" || railShape=="ascending_south") {
                cart->velX *= 0.9; // damp X
                // keep Z
            } else if (railShape=="east_west" || railShape=="ascending_east" || railShape=="ascending_west") {
                cart->velZ *= 0.9;
            } else if (railShape=="south_east" || railShape=="north_west" || railShape=="south_west" || railShape=="north_east") {
                // curved: reduce speed a bit
                cart->velX *= 0.9; cart->velZ *= 0.9;
            }
            cart->x += cart->velX;
            cart->y += cart->velY;
            cart->z += cart->velZ;
            // Snap X/Z to rail center for straight rails
            if (railShape=="north_south") cart->x = rx + 0.5;
            else if (railShape=="east_west") cart->z = rz + 0.5;
            // for ascending, keep center as well
            if (railShape=="ascending_east" || railShape=="ascending_west") cart->z = rz + 0.5;
            if (railShape=="ascending_north" || railShape=="ascending_south") cart->x = rx + 0.5;
        }
        // Broadcast movement if moved
        if (!cart->hasSent || std::abs(cart->x-cart->sentX)+std::abs(cart->y-cart->sentY)+std::abs(cart->z-cart->sentZ) > 0.01) {
            WriteBuffer b;
            b.varint(cart->entityId);
            b.i16(static_cast<std::int16_t>((cart->x-cart->sentX)*4096));
            b.i16(static_cast<std::int16_t>((cart->y-cart->sentY)*4096));
            b.i16(static_cast<std::int16_t>((cart->z-cart->sentZ)*4096));
            b.i8(0); b.i8(0);
            b.boolean(true);
            broadcastPacketExcept(nullptr, proto::pl::sc::MoveEntityPosRot, b);
            cart->sentX = cart->x; cart->sentY = cart->y; cart->sentZ = cart->z; cart->hasSent = true;
        }
        // Handle detector rail unpower when cart left (scan nearby rails for no cart)
        // Do second pass for detector rails near previous position? Simplified: leave powered true while cart exists; will be cleared by next tick when no cart nearby if we scan.
    }
    // Clear detector rails that have no cart nearby (simple O(n) scan over nearby rails within 1 block of any cart)
    // For all detector rails in loaded chunks, check if any cart within 1 block; if not and powered true, power off.
    // To avoid scanning all chunks, just scan rails around carts' previous positions? We'll do a limited scan: for each cart's neighboring positions, check detector rail that is powered but no cart.
    // This is best-effort; full scan would be heavy but okay for small world.
    {
        std::unordered_set<std::int64_t> poweredDetectorKeys;
        // Collect detector rails that are powered near carts
        for (auto &cart : carts) {
            int bx = static_cast<int>(std::floor(cart->x));
            int by = static_cast<int>(std::floor(cart->y));
            int bz = static_cast<int>(std::floor(cart->z));
            for (int dx=-1; dx<=1; ++dx) for (int dy=-1; dy<=1; ++dy) for (int dz=-1; dz<=1; ++dz) {
                int nx=bx+dx, ny=by+dy, nz=bz+dz;
                std::uint16_t st = world_.getBlock(nx, ny, nz);
                const gen::BlockDef* bd = gen::blockByState(st);
                if (!bd || std::string(bd->name)!="minecraft:detector_rail") continue;
                bool p=false; for (auto &pr: gen::propsOf(st)) if (pr.first=="powered" && pr.second=="true") p=true;
                if (!p) continue;
                // check if any cart still on this rail
                bool hasCart=false;
                for (auto &c2: carts) {
                    int cbx=(int)std::floor(c2->x), cby=(int)std::floor(c2->y), cbz=(int)std::floor(c2->z);
                    for (int ddy : {0,-1,1}) if (cbx==nx && cby+ddy==ny && cbz==nz) hasCart=true;
                }
                if (!hasCart) {
                    // power off
                    std::vector<std::pair<std::string_view,std::string_view>> props;
                    for (auto &pr: gen::propsOf(st)) if (pr.first!="powered") props.emplace_back(pr.first, pr.second);
                    props.emplace_back("powered","false");
                    std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*bd, props));
                    world_.setBlock(nx, ny, nz, ns);
                }
            }
        }
    }
}
void GameServer::boatsTick() {
    // plan17 LOW: buoyancy 0.05 per Yarn BoatEntity (was 0.04), water friction 0.9 land 0.6 max 0.4
    std::vector<std::shared_ptr<MobEntity>> boats;
    {
        std::lock_guard lk(entsMtx_);
        for (auto &m : mobs_) if (MobEntity::isBoat(m->kind)) boats.push_back(m);
    }
    for (auto &b : boats) {
        if (b->dead) continue;
        int bx=(int)std::floor(b->x), by=(int)std::floor(b->y), bz=(int)std::floor(b->z);
        auto stBelow = world_.getBlock(bx, by-1, bz);
        const gen::BlockDef* dBelow = gen::blockByState(stBelow);
        // plan23 §3: use FluidState.isWater() for water detection (was block name string, fails for waterlogged/flowing)
        bool inWater = FluidSim::getFluidState(world_, bx, by, bz).isWater();
        auto st = world_.getBlock(bx, by, bz);
        bool underWater = inWater && [&]{
            for(auto &pr: gen::propsOf(st)) if(pr.first=="level" && pr.second=="0") return true;
            return false;
        }();
        bool onLand = false;
        if (!inWater && dBelow && dBelow->name!="minecraft:air" && dBelow->name!="minecraft:water") onLand=true;
        if (inWater) {
            double waterY = by + 0.35;
            if (b->y < waterY) b->velY += 0.05;
            else if (b->y > waterY+0.2) b->velY -= 0.05;
            else b->velY *= 0.6;
            b->velX *= 0.90; b->velZ *= 0.90;
            b->velY *= 0.90;
        } else if (onLand) {
            b->velY -= 0.05;
            b->velX *= 0.60; b->velZ *= 0.60;
            b->velY *= 0.6;
            if (world_.getBlock(bx, by-1, bz)!=0 && b->velY<0) b->velY=0;
        } else {
            b->velY -= 0.05;
            b->velX *= 0.98; b->velY *= 0.98; b->velZ *= 0.98;
        }
        double horiz = std::sqrt(b->velX*b->velX + b->velZ*b->velZ);
        if (horiz>0.4){ double f=0.4/horiz; b->velX*=f; b->velZ*=f; }
        b->x += b->velX; b->y += b->velY; b->z += b->velZ;
        if (inWater && std::abs(b->y - (by+0.35))<0.1) b->y = by+0.35;
        if (!b->hasSent || std::abs(b->x-b->sentX)+std::abs(b->y-b->sentY)+std::abs(b->z-b->sentZ)>0.01){
            WriteBuffer pkt;
            pkt.varint(b->entityId);
            pkt.i16((int16_t)((b->x-b->sentX)*4096));
            pkt.i16((int16_t)((b->y-b->sentY)*4096));
            pkt.i16((int16_t)((b->z-b->sentZ)*4096));
            pkt.i8(0); pkt.i8(0); pkt.boolean(true);
            broadcastPacketExcept(nullptr, proto::pl::sc::MoveEntityPosRot, pkt);
            b->sentX=b->x; b->sentY=b->y; b->sentZ=b->z; b->hasSent=true;
        }
        (void)underWater;
    }
}
} // namespace cppfm
