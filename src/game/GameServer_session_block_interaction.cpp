#include "GameServer.hpp"
#include "Session.hpp"
#include "SessionInteractionSupport.hpp"
#include "SessionUseItemOnRequest.hpp"
#include "Messages.hpp"
#include "BlockEvent.hpp"
#include "../physics/Fluids.hpp"
#include "StairsHelper.hpp"
#include "HungerManager.hpp"
#include "../generated/BlockStates.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cppfm {
using namespace proto;

namespace {

bool withinBlockInteractionRange(
    const session_detail::SessionPlayerSnapshot& player,
    const std::int32_t x, const std::int32_t y, const std::int32_t z) {
    if (!player.inPlay || player.dead || !std::isfinite(player.x) ||
        !std::isfinite(player.y) || !std::isfinite(player.z))
        return false;
    const double range = player.gamemode == 1 ? 5.0 : 4.5;
    return session_detail::squaredDistanceToBox(
               player.x, player.y + 1.62, player.z, x, y, z,
               static_cast<double>(x) + 1.0,
               static_cast<double>(y) + 1.0,
               static_cast<double>(z) + 1.0) <= range * range + 1e-6;
}

} // namespace

bool Session::handleUseItemOnInteractions(const UseItemOnRequest& request) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto& ctx = request.context;
    if (request.playerState.dimension == 0 &&
        srv_.isSpawnProtected(tx, tz) && !srv_.isOp(self_->name)) {
        // check if placing a block (held is block item) – cancel
        const bool isBlockPlace = !request.heldItem.empty() &&
            gen::blockByName(request.heldItem.name()) != nullptr;
        if (isBlockPlace) {
            sendSystemText((msg::kRed + "Spawn protection prevents building here"));
            return true;
        }
    }

    {
        const std::uint16_t _clickedSt =
            srv_.worldFor(request.playerState.dimension).getBlock(x, y, z);
        if (!blockEventDispatcher().onBlockClicked(x, y, z, _clickedSt, d, self_.get())) return true;
        if (!session_detail::interactionContextUnchanged(
                *self_, request.playerState, request.heldItem,
                request.inventorySlot)) return true;
        api::BlockClickedEvent _bcev; _bcev.player=self_.get(); _bcev.x=x; _bcev.y=y; _bcev.z=z; _bcev.state=_clickedSt; _bcev.face=d;
        if (!api::events().blockClicked.fire(_bcev)) return true;
        if (!session_detail::interactionContextUnchanged(
                *self_, request.playerState, request.heldItem,
                request.inventorySlot)) return true;
        if (auto* runtime = srv_.jvmRuntime(); runtime &&
            !request.simulationLock.withLockReleased([&] {
                return runtime->onBlockClicked(
                    *self_, x, y, z, _clickedSt, d);
            })) return true;
        if (!session_detail::interactionContextUnchanged(
                *self_, request.playerState, request.heldItem,
                request.inventorySlot)) return true;
    }
    // right-click on interactive blocks opens menus (vanilla behaviour)
    {
        const std::uint16_t clickedState =
            srv_.worldFor(request.playerState.dimension).getBlock(x, y, z);
        const gen::BlockDef* bdef = gen::blockByState(clickedState);
        if (bdef) {
            const std::string bn(bdef->name);
            bool isMenuBlock = bn.find("chest") != std::string::npos ||
                bn == "minecraft:furnace" || bn == "minecraft:blast_furnace" ||
                bn == "minecraft:smoker" ||
                bn == "minecraft:hopper" || bn == "minecraft:dispenser" ||
                bn == "minecraft:dropper" ||
                bn == "minecraft:crafting_table" ||
                bn == "minecraft:enchanting_table" ||
                bn.find("anvil") != std::string::npos ||
                bn == "minecraft:brewing_stand" ||
                bn == "minecraft:stonecutter" ||
                bn == "minecraft:grindstone" ||
                bn.find("smithing") != std::string::npos ||
                bn == "minecraft:beacon" ||
                bn == "minecraft:loom" ||
                bn == "minecraft:barrel" ||
                bn.find("shulker_box") != std::string::npos ||
                bn == "minecraft:crafter" ||
                bn == "minecraft:cartography_table" ||
                bn == "minecraft:lectern";
            if (isMenuBlock) {
                // Allow opening from any face if not sneaking; ensure sneaking bypass
                if (ctx.isSneaking && !bn.empty()) {
                    // sneaking still places block, so skip menu
                } else {
                    openMenuAt(x, y, z, clickedState);
                    return true;
                }
            }
            // redstone interactables (lever / button / comparator) consume the click
            if (d >= 0 && d <= 5 &&
                (bn == "minecraft:lever" ||
                bn.find("_button") != std::string::npos ||
                bn.find("comparator") != std::string::npos)) {
                srv_.redstoneFor(request.playerState.dimension).onInteract(
                    x, y, z, srv_.tickNoForTest());
                return true;
            }
            if (d == 1) {
            if (bn.find("_bed") != std::string::npos &&
                bn.rfind("minecraft:", 0) == 0 && bn != "minecraft:bedrock") {
                const bool night = srv_.isNight();
                if (!night) {
                    sendSystemText((msg::kGray + "You can only sleep at night"));
                    return true;
                }
                self_->sleeping = true;
                self_->bedX = x; self_->bedY = y; self_->bedZ = z;
                self_->hasRespawnPoint = true;
                self_->respawnX = x; self_->respawnY = y; self_->respawnZ = z;
                self_->respawnDimension = request.playerState.dimension;
                self_->respawnAngle = self_->yaw;
                srv_.savePlayerData(GameServer::uuidToHex(self_->uuid), *self_);
                WriteBuffer sp;
                sp.position(x, y, z);
                sp.f32(self_->respawnAngle);
                conn_->trySendPacket(proto::pl::sc::SetDefaultSpawn, sp);
                int sleepingCount = 0, survivalCount = 0;
                for (auto& p : srv_.playersSnapshot()) {
                    if (!p->inPlay || p->gamemode != 0) continue;
                    ++survivalCount;
                    if (p->sleeping) ++sleepingCount;
                }
                if (sleepingCount >= survivalCount) {
                    srv_.setTimeOfDay(0);              // morning
                    if (srv_.raining()) srv_.forceWeatherClear();
                    for (auto& p : srv_.playersSnapshot())
                        if (p->sleeping) {
                            p->sleeping = false;
                            double wx = p->bedX + 1.5, wz = p->bedZ + 0.5;
                            srv_.teleportPlayer(*p, wx, p->bedY + 0.5, wz,
                                std::array<float, 2>{p->yaw, 0.0f});
                        }
                    srv_.broadcastSystemText((msg::kGray + "Good morning!"));
                } else {
                    sendSystemText((msg::kGray + "Sleeping... (" +
                                   std::to_string(sleepingCount) + "/" +
                                   std::to_string(survivalCount) + ")"));
                }
                return true;
            }
        }
    }

    {
        World& ww = srv_.worldFor(request.playerState.dimension);
        uint16_t cst = ww.getBlock(x,y,z);
        auto* cdef = gen::blockByState(cst);
        if (cdef && std::string(cdef->name)=="minecraft:cake") {
            if (HungerManager::handleCakeBlockConsume(srv_, *self_, x,y,z)) {
                return true;
            }
        }
    }
    }

    return false;
}
bool Session::handleUseItemOnToolActions(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto survival = request.survival;
    {
        InvSlot heldCopy = heldItem;
        const std::string heldNameForPortal = heldCopy.empty() ? std::string() : heldCopy.name();
        bool isFlint = heldNameForPortal == "minecraft:flint_and_steel";
        bool isFireCharge = heldNameForPortal == "minecraft:fire_charge";
        if ((isFlint || isFireCharge) && !heldCopy.empty()) {
            World& w = srv_.worldFor(self_->dimension);
            std::uint16_t clickedSt = w.getBlock(x, y, z);
            const gen::BlockDef* cd = gen::blockByState(clickedSt);
            bool clickedIsObsidian = cd && std::string(cd->name) == "minecraft:obsidian";
            if (clickedIsObsidian) {
                const auto& mp = gen::blockNameToState();
                auto obsIt = mp.find("minecraft:obsidian");
                std::uint16_t obsidian = obsIt != mp.end() ? static_cast<std::uint16_t>(obsIt->second) : 2397;
                const gen::BlockDef* portalDef = gen::blockByName("minecraft:nether_portal");
                bool ignited = false;
                auto fillInterior = [&](int orient, int ox, int oy, int oz) {
                    std::uint16_t portalState = 6033;
                    if (portalDef) {
                        if (orient == 0) portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","x"}}));
                        else portalState = static_cast<std::uint16_t>(gen::stateWithProps(*portalDef, {{"axis","z"}}));
                    } else {
                        auto it2 = mp.find("minecraft:nether_portal");
                        if (it2 != mp.end()) portalState = static_cast<std::uint16_t>(it2->second);
                    }
                    for (int dy=1; dy<=3; ++dy) for (int dx=1; dx<=2; ++dx) {
                        int32_t wx, wz;
                        if (orient==0) { wx = ox+dx; wz = oz; }
                        else { wx = ox; wz = oz+dx; }
                        int32_t wy = oy+dy;
                        w.setBlock(wx, wy, wz, portalState);
                        srv_.broadcastBlockChangeFor(self_->dimension, wx, wy, wz, portalState);
                        if (srv_.blockTicks()) srv_.blockTicks()->schedule(wx, wy, wz, srv_.tickNow() + 1 + (nextRandom()%20));
                    }
                    int32_t cxp = ox+1 + (orient==0?1:0);
                    int32_t czp = oz + (orient==1?1:0);
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:block.portal.ambient",
                                           cxp+0.5, oy+2, czp+0.5, 0.8f, 1.0f,
                                           "block");
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:item.flintandsteel.use",
                                           x+0.5, y+0.5, z+0.5, 1.f, 1.f,
                                           "block");
                };
                const auto geometry = w.verticalGeometry();
                for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                    for (int ox = x - 3; ox <= x && !ignited; ++ox) {
                        if (oy < geometry.minY || oy+4 >= geometry.maxY()) continue;
                        bool valid = true;
                        for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                            int32_t wx = ox+dx; int32_t wy = oy+dy; int32_t wz = z;
                            w.generateChunkIfMissing(wx>>4, wz>>4);
                            std::uint16_t st = w.getBlock(wx, wy, wz);
                            bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                            if (isBorder) { if (st != obsidian) valid=false; }
                            else { if (st != 0) valid=false; }
                        }
                        if (!valid) continue;
                        fillInterior(0, ox, oy, z);
                        ignited = true;
                    }
                }
                if (!ignited) {
                    for (int oy = y - 4; oy <= y && !ignited; ++oy) {
                        for (int oz = z - 3; oz <= z && !ignited; ++oz) {
                            if (oy < geometry.minY || oy+4 >= geometry.maxY()) continue;
                            bool valid = true;
                            for (int dy=0; dy<5 && valid; ++dy) for (int dx=0; dx<4 && valid; ++dx) {
                                int32_t wx = x; int32_t wy = oy+dy; int32_t wz = oz+dx;
                                w.generateChunkIfMissing(wx>>4, wz>>4);
                                std::uint16_t st = w.getBlock(wx, wy, wz);
                                bool isBorder = (dx==0 || dx==3 || dy==0 || dy==4);
                                if (isBorder) { if (st != obsidian) valid=false; }
                                else { if (st != 0) valid=false; }
                            }
                            if (!valid) continue;
                            fillInterior(1, x, oy, oz);
                            ignited = true;
                        }
                    }
                }
                if (ignited) {
                    if (self_->gamemode == 0) {
                        if (isFlint) {
                            auto* slot = &self_->inv[request.inventorySlot];
                            bool broken = slot->applyDamage(1);
                            if (broken) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        } else if (isFireCharge) {
                            auto* slot = &self_->inv[request.inventorySlot];
                            if (--slot->count <= 0) *slot = InvSlot::air();
                            srv_.resendInventory(*self_);
                        }
                    }
                    return true;
                }
            }
        }
    }

    if (!heldItem.empty() && (heldItem.name()=="minecraft:flint_and_steel" || heldItem.name()=="minecraft:fire_charge")) {
        std::uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x,y,z);
        const gen::BlockDef* cbd = gen::blockByState(clickedSt);
        if (cbd && std::string(cbd->name)=="minecraft:tnt") {
            srv_.worldFor(self_->dimension).setBlock(x,y,z,0);
            srv_.broadcastBlockChangeFor(self_->dimension, x,y,z,0);
            srv_.spawnPrimedTntFor(self_->dimension, x+0.5, y+0.5, z+0.5,
                                   0, 0.2, 0, 80);
            srv_.broadcastSoundFor(self_->dimension,
                                   "minecraft:entity.tnt.primed", x+0.5,
                                   y+0.5, z+0.5, 1.f, 1.f, "block");
            if (survival) {
                auto& mh = self_->inv[request.inventorySlot];
                if (heldItem.name()=="minecraft:flint_and_steel") { if (mh.applyDamage(1)) mh = ItemStack::air(); }
                else { if (--mh.count <= 0) mh = ItemStack::air(); }
                srv_.resendInventory(*self_);
            }
            return true;
        }
    }

    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName == "minecraft:water_bucket" || heldName == "minecraft:lava_bucket") {
            std::uint16_t target = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            bool replaceable = (target == 0);
            // also consider replaceable plants? treat only air for now
            World& targetWorld = srv_.worldFor(self_->dimension);
            if (replaceable && targetWorld.verticalGeometry().containsY(ty)) {
                std::string fluidName = (heldName == "minecraft:water_bucket") ? "minecraft:water" : "minecraft:lava";
                std::uint16_t fluidState = static_cast<std::uint16_t>(gen::stateWithPropsList(fluidName, {{"level","0"}}));
                if (fluidState==0) {
                    auto it = gen::blockNameToState().find(fluidName);
                    if (it != gen::blockNameToState().end()) fluidState = static_cast<std::uint16_t>(it->second);
                }
                if (!targetWorld.setBlock(tx, ty, tz, fluidState)) return true;
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, fluidState);
                if (survival) {
                    auto* mh = &self_->inv[request.inventorySlot];
                    *mh = ItemStack::ofName("minecraft:bucket", 1);
                    srv_.resendInventory(*self_);
                }
                srv_.broadcastSoundFor(self_->dimension,
                                       "minecraft:item.bucket.empty", tx+0.5,
                                       ty+0.5, tz+0.5, 1.f, 1.f, "block");
                return true;
            }
        } else if (heldName == "minecraft:bucket") {
            auto tryPick = [&](std::int32_t px,std::int32_t py,std::int32_t pz)->bool{
                std::uint16_t bs = srv_.worldFor(self_->dimension).getBlock(px,py,pz);
                const gen::BlockDef* bd = gen::blockByState(bs);
                if (!bd) return false;
                bool isWater=false,isLava=false;
                if (bd->name=="minecraft:water") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isWater=true;
                } else if (bd->name=="minecraft:lava") {
                    for (auto& [k,v]: gen::propsOf(bs)) if (k=="level" && v=="0") isLava=true;
                }
                if (!isWater && !isLava) return false;
                srv_.worldFor(self_->dimension).setBlock(px,py,pz, 0);
                srv_.broadcastBlockChangeFor(self_->dimension, px,py,pz, 0);
                if (survival) {
                    auto* mh = &self_->inv[request.inventorySlot];
                    std::string newName = isWater ? "minecraft:water_bucket" : "minecraft:lava_bucket";
                    *mh = ItemStack::ofName(newName, 1);
                    srv_.resendInventory(*self_);
                    srv_.onBucketFilled(*self_, newName);
                } else {
                    std::string newName = isWater ? "minecraft:water_bucket" : "minecraft:lava_bucket";
                    srv_.onBucketFilled(*self_, newName);
                }
                srv_.broadcastSoundFor(self_->dimension,
                                       "minecraft:item.bucket.fill", px+0.5,
                                       py+0.5, pz+0.5, 1.f, 1.f, "block");
                return true;
            };
            if (tryPick(x,y,z) || tryPick(tx,ty,tz)) {
                return true;
            }
        } else if (heldName == "minecraft:flint_and_steel" || heldName == "minecraft:fire_charge") {
            std::uint16_t target = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            if (target == 0) {
                if (!srv_.worldFor(self_->dimension).verticalGeometry().containsY(ty))
                    return true;
                bool canPlace = true;
                if (srv_.gameRules().contains("doFireTick") && !srv_.gameRules().getBool("doFireTick")) canPlace = false;
                if (canPlace) {
                    std::uint16_t belowSt = srv_.worldFor(self_->dimension).getBlock(tx, ty-1, tz);
                    const gen::BlockDef* belowDef = gen::blockByState(belowSt);
                    bool soulBase = false;
                    if (belowDef) {
                        auto &tags = srv_.datapackManager_.tagManager.blockTags;
                        auto it = tags.find("minecraft:soul_fire_base_blocks");
                        if (it != tags.end()) {
                            auto nit = gen::blockNameToState().find(std::string(belowDef->name));
                            if (nit != gen::blockNameToState().end()) {
                                uint32_t defId = static_cast<uint32_t>(nit->second);
                                soulBase = it->second.count(defId) > 0;
                            }
                        }
                        if (!soulBase) {
                            soulBase = std::string(belowDef->name)=="minecraft:soul_sand" || std::string(belowDef->name)=="minecraft:soul_soil";
                        }
                    }
                    std::string fireName = soulBase ? "minecraft:soul_fire" : "minecraft:fire";
                    auto it = gen::blockNameToState().find(fireName);
                    if (it == gen::blockNameToState().end()) it = gen::blockNameToState().find("minecraft:fire");
                    if (it != gen::blockNameToState().end()) {
                        std::uint16_t fireState = static_cast<std::uint16_t>(it->second);
                        if (!srv_.worldFor(self_->dimension).setBlock(
                                tx, ty, tz, fireState))
                            return true;
                        srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, fireState);
                        if (survival) {
                            auto* mh = &self_->inv[request.inventorySlot];
                            if (heldName=="minecraft:flint_and_steel") {
                                if (mh->applyDamage(1)) *mh = ItemStack::air();
                            } else {
                                if (--mh->count <=0) *mh = ItemStack::air();
                            }
                            srv_.resendInventory(*self_);
                        }
                        srv_.broadcastSoundFor(self_->dimension,
                                               "minecraft:item.flintandsteel.use",
                                               tx+0.5, ty+0.5, tz+0.5, 1.f,
                                               1.f, "block");
                    }
                }
                return true;
            }
        }
    }

    // ---- bone meal fertilize hook ----
    if (!heldItem.empty() && heldItem.name() == "minecraft:bone_meal") {
        const std::uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        if (clickedSt != 0) {
            const gen::BlockDef* cb = gen::blockByState(clickedSt);
            if (cb) {
                const std::string bn(cb->name);
                auto* beh = srv_.blockTicksFor(self_->dimension).behaviorFor(bn);
                if (beh && beh->fertilize(srv_.worldFor(self_->dimension), x, y, z, clickedSt, &srv_)) {
                    const std::uint16_t newSt = srv_.worldFor(self_->dimension).getBlock(x, y, z);
                    srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, newSt);
                    srv_.broadcastSoundFor(self_->dimension,
                                           "minecraft:item.bone_meal.use",
                                           x + 0.5, y + 0.5, z + 0.5);
                    if (survival) {
                        auto* mh = &self_->inv[request.inventorySlot];
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
                    return true;
                }
            }
        }
    }

    return false;
}
bool Session::handleUseItemOnDoorAndSlab(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto dir = request.face;
    const auto survival = request.survival;
    const auto& ctx = request.context;
    if (!heldItem.empty()) {
        const std::string heldName = heldItem.name();
        if (heldName.size() > 5 && heldName.rfind("_door", heldName.size() - 5) != std::string::npos) {
            auto& doorWorld = srv_.worldFor(self_->dimension);
            const auto geometry = doorWorld.verticalGeometry();
            if (!geometry.containsY(ty) || !geometry.containsY(ty + 1) ||
                !doorWorld.generateChunkIfMissing(tx >> 4, tz >> 4))
                return true;
            if (!hasSturdyTopFace(doorWorld.getBlock(tx, ty - 1, tz)))
                return true;
            const gen::BlockDef* ddef = gen::blockByName(heldName);
            if (ddef && doorWorld.getBlock(tx, ty, tz) == 0 &&
                doorWorld.getBlock(tx, ty + 1, tz) == 0) {
                float yaw = self_->yaw;
                const char* facing = "north";
                if (yaw >= 45.f && yaw < 135.f) facing = "west";
                else if (yaw >= 135.f && yaw < 225.f) facing = "south";
                else if (yaw >= 225.f && yaw < 315.f) facing = "east";
                // hinge logic via solid faces and neighboring doors (vanilla DoorBlock per Yarn 1.21.4: isFullCube count + hitPos tie - strict B5)
                std::string hingeStr = "left";
                {
                    auto isFullCubeAt = [&](int nx,int ny,int nz)->bool{
                        uint16_t s2 = srv_.worldFor(self_->dimension).getBlock(nx,ny,nz);
                        if(s2==0) return false;
                        auto* bd2 = gen::blockByState(s2);
                        if(!bd2) return false;
                        std::string n(bd2->name);
                        if(n.find("_slab")!=std::string::npos){
                            for(auto& [k,v]: gen::propsOf(s2)) if(k=="type" && v!="double") return false;
                        }
                        if(n.find("stairs")!=std::string::npos) return false;
                        return !bd2->transparent;
                    };
                    auto isDoorLowerAt = [&](int nx,int ny,int nz)->bool{
                        uint16_t s2 = srv_.worldFor(self_->dimension).getBlock(nx,ny,nz);
                        if(s2==0) return false;
                        auto* bd2 = gen::blockByState(s2);
                        if(!bd2 || std::string(bd2->name).find("_door")==std::string::npos) return false;
                        for(auto& [k,v]: gen::propsOf(s2)) if(k=="half" && v=="lower") return true;
                        return false;
                    };
                    int dxL=0, dzL=0, dxR=0, dzR=0;
                    std::string fs(facing);
                    if(fs=="north"){ dxL=-1; dzL=0; dxR=1; dzR=0; }
                    else if(fs=="south"){ dxL=1; dzL=0; dxR=-1; dzR=0; }
                    else if(fs=="west"){ dxL=0; dzL=1; dxR=0; dzR=-1; }
                    else if(fs=="east"){ dxL=0; dzL=-1; dxR=0; dzR=1; }
                    else { dxL=-1; dzL=0; dxR=1; dzR=0; }
                    int i = 0;
                    if(isFullCubeAt(tx+dxL, ty, tz+dzL)) i += -1;
                    if(isFullCubeAt(tx+dxL, ty+1, tz+dzL)) i += -1;
                    if(isFullCubeAt(tx+dxR, ty, tz+dzR)) i += 1;
                    if(isFullCubeAt(tx+dxR, ty+1, tz+dzR)) i += 1;
                    bool leftDoor = isDoorLowerAt(tx+dxL, ty, tz+dzL);
                    bool rightDoor = isDoorLowerAt(tx+dxR, ty, tz+dzR);
                    if((!leftDoor || rightDoor) && i <= 0){
                        if((!rightDoor || leftDoor) && i >= 0){
                            int j = (fs=="east"?1: fs=="west"?-1:0);
                            int k = (fs=="south"?1: fs=="north"?-1:0);
                            const double cursorX = ctx.cursor.x;
                            const double cursorZ = ctx.cursor.z;
                            const bool chooseLeft =
                                (j >= 0 || !(cursorZ < 0.5)) &&
                                (j <= 0 || !(cursorZ > 0.5)) &&
                                (k >= 0 || !(cursorX > 0.5)) &&
                                (k <= 0 || !(cursorX < 0.5));
                            hingeStr = chooseLeft ? "left" : "right";
                        } else {
                            hingeStr = "left";
                        }
                    } else {
                        hingeStr = "right";
                    }
                }
                bool powered = false;
                powered = srv_.redstoneFor(self_->dimension).isPoweredHere(tx,ty,tz) ||
                          srv_.redstoneFor(self_->dimension).isPoweredHere(tx,ty+1,tz);
                std::string openStr = powered ? "true" : "false";
                std::string poweredStr = powered ? "true" : "false";
                const auto lower =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","lower"},{"facing",facing},{"open",openStr},{"hinge",hingeStr},{"powered",poweredStr}}));
                const auto upper =
                    static_cast<std::uint16_t>(gen::stateWithProps(*ddef,
                        {{"half","upper"},{"facing",facing},{"open",openStr},{"hinge",hingeStr},{"powered",poweredStr}}));
                if (!blockEventDispatcher().onBlockPlace(tx, ty, tz, 0, lower, self_.get()) ||
                    !blockEventDispatcher().onBlockPlace(tx, ty + 1, tz, 0, upper, self_.get()))
                    return true;
                if (!session_detail::interactionContextUnchanged(
                        *self_, request.playerState, request.heldItem,
                        request.inventorySlot))
                    return true;
                api::BlockPlaceEvent lowerEvent;
                lowerEvent.player = self_.get(); lowerEvent.x = tx; lowerEvent.y = ty;
                lowerEvent.z = tz; lowerEvent.newState = lower;
                api::BlockPlaceEvent upperEvent;
                upperEvent.player = self_.get(); upperEvent.x = tx; upperEvent.y = ty + 1;
                upperEvent.z = tz; upperEvent.newState = upper;
                if (!srv_.events().blockPlace.fire(lowerEvent) ||
                    !srv_.events().blockPlace.fire(upperEvent)) return true;
                if (!session_detail::interactionContextUnchanged(
                        *self_, request.playerState, request.heldItem,
                        request.inventorySlot))
                    return true;
                if (auto* runtime = srv_.jvmRuntime(); runtime &&
                    !request.simulationLock.withLockReleased([&] {
                        return runtime->onBlockPlace(
                                   *self_, tx, ty, tz, lower) &&
                               runtime->onBlockPlace(
                                   *self_, tx, ty + 1, tz, upper);
                    }))
                    return true;
                if (!session_detail::interactionContextUnchanged(
                        *self_, request.playerState, request.heldItem,
                        request.inventorySlot))
                    return true;
                const std::array doorUpdates{
                    World::BlockUpdate{tx, ty, tz, lower},
                    World::BlockUpdate{tx, ty + 1, tz, upper},
                };
                const std::array doorPreconditions{
                    World::BlockStateCondition{tx, ty, tz, 0},
                    World::BlockStateCondition{tx, ty + 1, tz, 0},
                };
                if (!doorWorld.setBlockBatchIfStates(doorUpdates,
                                                     doorPreconditions))
                    return true;
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty, tz, lower);
                srv_.broadcastBlockChangeFor(self_->dimension, tx, ty + 1, tz, upper);
                if (survival) {
                    auto mh = &self_->inv[request.inventorySlot];
                    if (ItemStack::maxDamageFor(mh->itemId) > 0) {
                        if (mh->applyDamage(1)) *mh = ItemStack::air();
                        srv_.resendInventory(*self_);
                    } else {
                        if (--mh->count <= 0) *mh = InvSlot::air();
                        srv_.resendInventory(*self_);
                    }
                }
                return true;
            }
        }
    }

    if (!heldItem.empty()) {
        std::string hName = heldItem.name();
        if (hName.find("_slab") != std::string::npos) {
            uint16_t existing = srv_.worldFor(self_->dimension).getBlock(tx, ty, tz);
            const gen::BlockDef* ed = gen::blockByState(existing);
            if (ed && std::string(ed->name) == hName) {
                std::string curType = getPropStr(existing, "type");
                if (curType != "double") {
                    bool isBottom = curType=="bottom";
                    bool hittingOpposite = false;
                    if(isBottom) hittingOpposite = (d==1 || ctx.cursor.y > 0.5);
                    else /* top */ hittingOpposite = (d==0 || ctx.cursor.y < 0.5);
                    if(hittingOpposite){
                        std::vector<std::pair<std::string_view,std::string_view>> p;
                        for(auto&[k,v]: gen::propsOf(existing)) if(k!="type" && k!="waterlogged") p.emplace_back(k,v);
                        p.emplace_back("type","double");
                        // double slab must be waterlogged false
                        bool hasWl=false; for(int i=0;i<ed->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[ed->propsOff+i]]; if(pd.name=="waterlogged") hasWl=true; }
                        if(hasWl) p.emplace_back("waterlogged","false");
                        uint16_t dbl = static_cast<uint16_t>(gen::stateWithProps(*ed, p));
                        if (!blockEventDispatcher().onBlockPlace(tx, ty, tz, existing, dbl, self_.get())) return true;
                        if (!session_detail::interactionContextUnchanged(
                                *self_, request.playerState,
                                request.heldItem, request.inventorySlot)) return true;
                        api::BlockPlaceEvent ev2; ev2.player=self_.get(); ev2.x=tx; ev2.y=ty; ev2.z=tz; ev2.newState=dbl;
                        if (!srv_.events().blockPlace.fire(ev2)) return true;
                        if (!session_detail::interactionContextUnchanged(
                                *self_, request.playerState,
                                request.heldItem, request.inventorySlot)) return true;
                        auto* runtime = srv_.jvmRuntime();
                        if (!runtime || request.simulationLock.withLockReleased([&] {
                                return runtime->onBlockPlace(
                                    *self_, tx, ty, tz, dbl);
                            })) {
                            if (!session_detail::interactionContextUnchanged(
                                    *self_, request.playerState,
                                    request.heldItem, request.inventorySlot)) return true;
                            if (srv_.worldFor(self_->dimension).getBlock(tx, ty, tz) != existing)
                                return true;
                            if (!srv_.worldFor(self_->dimension).setBlock(
                                    tx, ty, tz, dbl))
                                return true;
                            srv_.broadcastBlockChangeFor(self_->dimension, tx,ty,tz,dbl);
                            if (survival) {
                                auto* mh=&self_->inv[request.inventorySlot];
                                if(--mh->count<=0) *mh=ItemStack::air();
                                srv_.resendInventory(*self_);
                            }
                            return true;
                        }
                    } else {
                        // same half → try adjacent placement (vanilla places single slab at offset)
                        const int adjX = tx + kBlockFaceOffsetX[d];
                        const int adjY = ty + kBlockFaceOffsetY[d];
                        const int adjZ = tz + kBlockFaceOffsetZ[d];
                        if (srv_.worldFor(self_->dimension).getBlock(adjX,adjY,adjZ)==0) {
                            const gen::BlockDef* sdef = gen::blockByName(hName);
                            if(sdef){
                                const char* newType;
                                if(d==1) newType="bottom";
                                else if(d==0) newType="top";
                                else newType = (ctx.cursor.y > 0.5 ? "top" : "bottom");
                                auto adjFs = FluidSim::getFluidState(srv_.worldFor(self_->dimension), adjX, adjY, adjZ);
                                bool wl = adjFs.isStillWater();
                                bool hasWlAdj=false; for(int i=0;i<sdef->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[sdef->propsOff+i]]; if(pd.name=="waterlogged") hasWlAdj=true; }
                                std::vector<std::pair<std::string_view,std::string_view>> ap;
                                ap.emplace_back("type", newType);
                                if(hasWlAdj) ap.emplace_back("waterlogged", wl?"true":"false");
                                uint16_t adjSt = static_cast<uint16_t>(gen::stateWithProps(*sdef, ap));
                                if (!blockEventDispatcher().onBlockPlace(adjX, adjY, adjZ, 0, adjSt, self_.get())) return true;
                                if (!session_detail::interactionContextUnchanged(
                                        *self_, request.playerState,
                                        request.heldItem,
                                        request.inventorySlot)) return true;
                                api::BlockPlaceEvent evA; evA.player=self_.get(); evA.x=adjX; evA.y=adjY; evA.z=adjZ; evA.newState=adjSt;
                                if (!srv_.events().blockPlace.fire(evA))
                                    return true;
                                if (!session_detail::interactionContextUnchanged(
                                        *self_, request.playerState,
                                        request.heldItem,
                                        request.inventorySlot)) return true;
                                auto* runtime = srv_.jvmRuntime();
                                if (!runtime ||
                                    request.simulationLock.withLockReleased([&] {
                                        return runtime->onBlockPlace(
                                            *self_, adjX, adjY, adjZ, adjSt);
                                    })) {
                                    if (!session_detail::interactionContextUnchanged(
                                            *self_, request.playerState,
                                            request.heldItem,
                                            request.inventorySlot)) return true;
                                    if (srv_.worldFor(self_->dimension).getBlock(adjX, adjY, adjZ) != 0)
                                        return true;
                                    if (!srv_.worldFor(self_->dimension).setBlock(
                                            adjX, adjY, adjZ, adjSt))
                                        return true;
                                    srv_.broadcastBlockChangeFor(self_->dimension, adjX,adjY,adjZ,adjSt);
                                    if(wl) srv_.fluidsFor(self_->dimension).touch(adjX,adjY,adjZ);
                                    if(survival){
                                        auto* mh=&self_->inv[request.inventorySlot];
                                        if(--mh->count<=0) *mh=ItemStack::air();
                                        srv_.resendInventory(*self_);
                                    }
                                    return true;
                                }
                            }
                        }
                        // same half but adjacent blocked → do not make double, fall through to normal handling (will ack without placing)
                        return true;
                    }
                }
            }
        }
        // also check placing slab onto existing slab at click position? vanilla allows placing slab on top of clicked slab to make double.
        // If tx is offset, also check clicked pos if it is slab and face is up/down
        if (hName.find("_slab") != std::string::npos) {
            uint16_t clickedSt = srv_.worldFor(self_->dimension).getBlock(x,y,z);
            const gen::BlockDef* cd = gen::blockByState(clickedSt);
            if (cd && std::string(cd->name) == hName) {
                std::string curType = getPropStr(clickedSt, "type");
                if (curType != "double") {
                    // Only double if clicking top of bottom slab or bottom of top slab
                    std::string chalf = getPropStr(clickedSt, "type");
                    bool canDouble = false;
                    if (chalf=="bottom" && dir==1) canDouble=true;
                    if (chalf=="top" && dir==0) canDouble=true;
                    if (canDouble) {
                        std::vector<std::pair<std::string_view,std::string_view>> p;
                        for(auto&[k,v]: gen::propsOf(clickedSt)) if(k!="type" && k!="waterlogged") p.emplace_back(k,v);
                        p.emplace_back("type","double");
                        bool hasWl=false; for(int i=0;i<cd->propCount;++i){ auto &pd=gen::kPropDefs[gen::kBlockPropsRun[cd->propsOff+i]]; if(pd.name=="waterlogged") hasWl=true; }
                        if(hasWl) p.emplace_back("waterlogged","false");
                        uint16_t dbl = static_cast<uint16_t>(gen::stateWithProps(*cd, p));
                        if (!srv_.worldFor(self_->dimension).setBlock(
                                x, y, z, dbl))
                            return true;
                        srv_.broadcastBlockChangeFor(self_->dimension, x,y,z,dbl);
                        if (survival) {
                            auto* mh=&self_->inv[request.inventorySlot];
                            if(--mh->count<=0) *mh=ItemStack::air();
                            srv_.resendInventory(*self_);
                        }
                        return true;
                    }
                }
            }
        }
    }

    return false;
}
bool Session::handleUseItemOnOccupied(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    if (srv_.worldFor(self_->dimension).getBlock(tx, ty, tz) != 0 || heldItem.empty()) {
        const std::uint16_t clickedState = srv_.worldFor(self_->dimension).getBlock(x, y, z);
        const gen::BlockDef* cdef = gen::blockByState(clickedState);
        if (cdef && cdef->name.size() > 5 &&
            cdef->name.rfind("_door", cdef->name.size() - 5) != std::string::npos) {
            bool isIron = std::string(cdef->name) == "minecraft:iron_door";
            if(isIron){
                return true;
            }
            bool open = false, upperHalf = false;
            bool powered = false;
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "open") open = v == "true";
                if (k == "half") upperHalf = v == "upper";
                if (k == "powered") powered = v == "true";
            }
            std::string facing;
            std::string hinge = "left";
            for (auto& [k, v] : gen::propsOf(clickedState)) {
                if (k == "facing") facing = std::string(v);
                if (k == "hinge") hinge = std::string(v);
            }
            const std::uint16_t st1 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "upper" : "lower"},
                     {"facing", facing}, {"hinge", hinge}, {"powered", powered?"true":"false"}}));
            const std::int32_t oy = upperHalf ? y - 1 : y + 1;
            const std::uint16_t st2 = static_cast<std::uint16_t>(
                gen::stateWithProps(*cdef,
                    {{"open", open ? "false" : "true"},
                     {"half", upperHalf ? "lower" : "upper"},
                     {"facing", facing}, {"hinge", hinge}, {"powered", powered?"true":"false"}}));
            auto& doorWorld = srv_.worldFor(self_->dimension);
            const std::uint16_t otherState = doorWorld.getBlock(x, oy, z);
            const auto* otherDef = gen::blockByState(otherState);
            if (!otherDef || std::string_view(otherDef->name) != cdef->name)
                return true;
            bool otherIsUpper = false;
            for (const auto& [key, value] : gen::propsOf(otherState))
                if (key == "half") otherIsUpper = value == "upper";
            if (otherIsUpper == upperHalf) return true;
            const std::array doorUpdates{
                World::BlockUpdate{x, y, z, st1},
                World::BlockUpdate{x, oy, z, st2},
            };
            const std::array doorPreconditions{
                World::BlockStateCondition{x, y, z, clickedState},
                World::BlockStateCondition{x, oy, z, otherState},
            };
            if (!doorWorld.setBlockBatchIfStates(doorUpdates,
                                                  doorPreconditions))
                return true;
            srv_.broadcastBlockChangeFor(self_->dimension, x, y, z, st1);
            srv_.broadcastBlockChangeFor(self_->dimension, x, oy, z, st2);
            srv_.broadcastSoundFor(self_->dimension,
                                   "minecraft:block.wooden_door.toggle",
                                   x + .5, y + .5, z + .5, 1.f,
                                   open ? 0.7f : 0.9f);
        }
        return true;
    }
    return false;
}
bool Session::handleUseItemOnEntityItems(const UseItemOnRequest& request, const InvSlot& heldItem) {
    const auto x = request.x;
    const auto y = request.y;
    const auto z = request.z;
    const auto tx = request.tx;
    const auto ty = request.ty;
    const auto tz = request.tz;
    const auto d = request.face;
    const auto survival = request.survival;
    // item id -> block name (block items share the name)
    std::string itemName = heldItem.name();
    {
        BlockPos hitPos{x, y, z};
        if (self_->heldSlot >= 0 && self_->heldSlot < 9) {
            ItemStack& stk = self_->inv[request.inventorySlot];
            if (!stk.empty() && stk.name().ends_with("_spawn_egg")) {
                if (srv_.trySpawnEgg(*self_, stk, hitPos, d)) return true;
            }
        }
        // keep itemName endsWith check for tooling/grep
        if (itemName.ends_with("_spawn_egg")) {
            // handled via trySpawnEgg above
        }
    }
    {
        if (!heldItem.empty()) {
            std::string hName = heldItem.name();
            bool isBoatItem = hName.ends_with("_boat") || hName.ends_with("_raft");
            if (isBoatItem) {
                auto kindOpt = [&]() -> std::optional<MobKind> {
                    if (hName=="minecraft:oak_boat") return MobKind::OakBoat;
                    if (hName=="minecraft:spruce_boat") return MobKind::SpruceBoat;
                    if (hName=="minecraft:birch_boat") return MobKind::BirchBoat;
                    if (hName=="minecraft:jungle_boat") return MobKind::JungleBoat;
                    if (hName=="minecraft:acacia_boat") return MobKind::AcaciaBoat;
                    if (hName=="minecraft:dark_oak_boat") return MobKind::DarkOakBoat;
                    if (hName=="minecraft:mangrove_boat") return MobKind::MangroveBoat;
                    if (hName=="minecraft:cherry_boat") return MobKind::CherryBoat;
                    if (hName=="minecraft:pale_oak_boat") return MobKind::PaleOakBoat;
                    if (hName=="minecraft:bamboo_raft") return MobKind::BambooRaft;
                    if (hName=="minecraft:oak_chest_boat") return MobKind::OakChestBoat;
                    if (hName=="minecraft:spruce_chest_boat") return MobKind::SpruceChestBoat;
                    if (hName=="minecraft:birch_chest_boat") return MobKind::BirchChestBoat;
                    if (hName=="minecraft:jungle_chest_boat") return MobKind::JungleChestBoat;
                    if (hName=="minecraft:acacia_chest_boat") return MobKind::AcaciaChestBoat;
                    if (hName=="minecraft:dark_oak_chest_boat") return MobKind::DarkOakChestBoat;
                    if (hName=="minecraft:mangrove_chest_boat") return MobKind::MangroveChestBoat;
                    if (hName=="minecraft:cherry_chest_boat") return MobKind::CherryChestBoat;
                    if (hName=="minecraft:pale_oak_chest_boat") return MobKind::PaleOakChestBoat;
                    if (hName=="minecraft:bamboo_chest_raft") return MobKind::BambooChestRaft;
                    return std::nullopt;
                }();
                if (kindOpt) {
                    double sx = tx + 0.5, sy = ty + 0.1, sz = tz + 0.5;
                    srv_.spawnMobFor(self_->dimension, *kindOpt, sx, sy, sz);
                    if (survival) {
                        auto* mh = &self_->inv[request.inventorySlot];
                        if (--mh->count <= 0) *mh = ItemStack::air();
                        srv_.resendInventory(*self_);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}
void Session::onUseItemOn(ReadBuffer& in,
                          SimulationDispatchGuard& simulationLock) {
    const std::int32_t hand = in.varint();
    std::int32_t x, y, z;
    in.position(x, y, z);
    const std::int32_t dir = in.varint();
    const float cursorX = in.f32();
    const float cursorY = in.f32();
    const float cursorZ = in.f32();
    (void)cursorX; (void)cursorZ;
    (void)in.boolean();                                 // inside block
    (void)in.boolean();                                 // world border hit
    const std::int32_t sequence = in.varint();

    if (hand != 0 && hand != 1) {
        ack(sequence);
        return;
    }

    const session_detail::SessionPlayerSnapshot playerState = session_detail::snapshotPlayerForSession(*self_);
    if (!withinBlockInteractionRange(playerState, x, y, z)) {
        // The sequence still needs an acknowledgement, but no interaction,
        // callback, item consumption, or placement may be driven by a block
        // outside the server-side interaction range.
        ack(sequence);
        return;
    }

    const int d = (dir >= 0 && dir < 6) ? dir : 0;
    const std::int32_t tx = x + kBlockFaceOffsetX[d], ty = y + kBlockFaceOffsetY[d], tz = z + kBlockFaceOffsetZ[d];

    ItemUseContext ctx;
    ctx.player = self_.get();
    ctx.world = &srv_.worldFor(playerState.dimension);
    ctx.hitPos = {x, y, z};
    ctx.placePos = {tx, ty, tz};
    ctx.face = d;
    ctx.cursor = {static_cast<double>(cursorX), static_cast<double>(cursorY), static_cast<double>(cursorZ)};
    ctx.yaw = playerState.yaw;
    ctx.isSneaking = playerState.sneaking;

    if (hand == 0 &&
        (playerState.heldSlot < 0 || playerState.heldSlot >= 9)) {
        ack(sequence);
        return;
    }
    const std::size_t inventorySlot = hand == 1
        ? std::size_t{45}
        : static_cast<std::size_t>(36 + playerState.heldSlot);
    InvSlot heldItem;
    {
        std::lock_guard playerLock(self_->stateMtx);
        heldItem = self_->inv[inventorySlot];
    }
    const UseItemOnRequest request{x, y, z, tx, ty, tz, d,
                                  inventorySlot, playerState.gamemode == 0,
                                  ctx, playerState, heldItem, simulationLock};
    if (handleUseItemOnInteractions(request)) {
        ack(sequence);
        return;
    }
    if (!session_detail::interactionContextUnchanged(
            *self_, playerState, heldItem, inventorySlot)) {
        ack(sequence);
        return;
    }
    if (!heldItem.empty()) {
        srv_.onItemUsedOnBlock(self_.get(), x, y, z, heldItem);
    }
    if (!session_detail::interactionContextUnchanged(
            *self_, playerState, heldItem, inventorySlot)) {
        ack(sequence);
        return;
    }
    if (handleUseItemOnToolActions(request, heldItem) ||
        handleUseItemOnDoorAndSlab(request, heldItem) ||
        handleUseItemOnOccupied(request, heldItem) ||
        handleUseItemOnEntityItems(request, heldItem)) {
        ack(sequence);
        return;
    }

    placeUseItemOnBlock(request, heldItem);
    ack(sequence);
}


} // namespace cppfm
