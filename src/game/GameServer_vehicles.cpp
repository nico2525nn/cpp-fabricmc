#include "GameServer.hpp"
#include "BlockEntityDrops.hpp"
#include "BlockEntityKinds.hpp"
#include "EntitySupport.hpp"
#include "MetadataTypes.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "StairsHelper.hpp"
#include "EquipmentComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MeleeHelper.hpp"
#include "CombatManager.hpp"
#include "HungerManager.hpp"
#include "MobSpawner.hpp"
#include "MenuLogic.hpp"
#include "PotionBrewing.hpp"
#include "GameServerHelpers.hpp"

namespace cppfm {
using namespace proto;

void GameServer::handleMoveVehicle(Player& p, double x, double y, double z, float yaw, float pitch) {
    std::int32_t vehicleId = -1;
    std::int8_t playerDimension = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        vehicleId = p.vehicleId;
        playerDimension = canonicalDimension(p.dimension);
    }
    if (vehicleId == -1) return;
    std::shared_ptr<MobEntity> veh;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard entityLock(*candidate->stateMtx);
        if (candidate->entityId == vehicleId) {
            veh = candidate;
            break;
        }
    }
    if (!veh) return;

    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    MobKind vehicleKind = MobKind::Pig;
    {
        // Player and vehicle state are updated as one operation.  scoped_lock
        // is intentional here because the tick path may acquire them in the
        // opposite order while resolving a passenger.
        std::scoped_lock stateLock(p.stateMtx, *veh->stateMtx);
        if (p.vehicleId != vehicleId ||
            canonicalDimension(p.dimension) != playerDimension ||
            canonicalDimension(veh->dimension) != playerDimension)
            return;
        if (!isInsideBorder(x, z)) {
            veh->velX = 0;
            veh->velZ = 0;
            return;
        }
        const double dx = x - veh->x;
        const double dz = z - veh->z;
        veh->velX = dx * 0.5;
        veh->velZ = dz * 0.5;
        veh->x = x;
        veh->y = y;
        veh->z = z;
        veh->yaw = yaw;
        p.x = x;
        p.y = y;
        p.z = z;
        p.yaw = yaw;
        p.pitch = pitch;
        dimension = canonicalDimension(veh->dimension);
        entityId = veh->entityId;
        vehicleKind = veh->kind;
    }
    const WriteBuffer tp = makeEntityTeleportBody(
        entityId, x, y, z, 0.0, 0.0, 0.0, yaw, pitch, 0, true);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::EntityTeleport, tp);
    WriteBuffer vm;
    vm.f64(x); vm.f64(y); vm.f64(z);
    vm.f32(yaw); vm.f32(pitch);
    broadcastPacketExceptInDimension(dimension, &p,
                                     proto::pl::sc::VehicleMove, vm);
    if (vehicleKind==MobKind::Minecart || vehicleKind==MobKind::ChestMinecart || vehicleKind==MobKind::FurnaceMinecart || vehicleKind==MobKind::TntMinecart || vehicleKind==MobKind::HopperMinecart || vehicleKind==MobKind::CommandBlockMinecart || vehicleKind==MobKind::SpawnerMinecart) {
        broadcastMoveMinecart(entityId, x, y, z, yaw, pitch, &p);
    }
}
void GameServer::handleHorseJump(Player& p, int power) {
    std::int32_t vehicleId = -1;
    std::int8_t playerDimension = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        vehicleId = p.vehicleId;
        playerDimension = canonicalDimension(p.dimension);
    }
    if (vehicleId == -1) return;
    std::shared_ptr<MobEntity> veh;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard entityLock(*candidate->stateMtx);
        if (candidate->entityId == vehicleId) {
            veh = candidate;
            break;
        }
    }
    if (!veh) return;
    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double velocityZ = 0.0;
    {
        std::scoped_lock stateLock(p.stateMtx, *veh->stateMtx);
        if (p.vehicleId != vehicleId ||
            canonicalDimension(p.dimension) != playerDimension ||
            canonicalDimension(veh->dimension) != playerDimension ||
            (veh->kind != MobKind::Horse && veh->kind != MobKind::Llama &&
             veh->kind != MobKind::Pig))
            return;
        const float f = std::clamp(static_cast<float>(power) / 100.0F,
                                   0.0F, 1.0F);
        veh->velY = 0.42 + f*0.6;
        veh->velX *= 1.05; veh->velZ *= 1.05;
        if(veh->velY>1.2) veh->velY=1.2;
        dimension = canonicalDimension(veh->dimension);
        entityId = veh->entityId;
        velocityX = veh->velX;
        velocityY = veh->velY;
        velocityZ = veh->velZ;
        veh->lastTeleportTick = tickNo_;
    }
    WriteBuffer vb;
    vb.varint(entityId);
    vb.i16((int16_t)(velocityX*8000)); vb.i16((int16_t)(velocityY*8000)); vb.i16((int16_t)(velocityZ*8000));
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::EntityVelocity, vb);
}
void GameServer::broadcastSetPassengers(std::int32_t vehicleId) {
    std::shared_ptr<MobEntity> veh;
    std::int8_t dimension = 0;
    std::int32_t riderEntityId = -1;
    for (const auto& candidate : mobsSnapshot()) {
        if (!candidate) continue;
        std::lock_guard entityLock(*candidate->stateMtx);
        if (candidate->entityId != vehicleId) continue;
        veh = candidate;
        dimension = candidate->dimension;
        riderEntityId = candidate->riderEntityId;
            break;
    }
    if (!veh) return;
    WriteBuffer b;
    b.varint(vehicleId);
    if (riderEntityId != -1) {
        b.varint(1);
        b.varint(riderEntityId);
    } else {
        b.varint(0);
    }
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::SetPassengers, b);
}

void GameServer::minecartsTick() {
    std::vector<std::shared_ptr<MobEntity>> carts;
    for (const auto& m : mobsSnapshot()) {
        if (!m) continue;
        std::lock_guard mobLock(*m->stateMtx);
        if (MobEntity::isMinecartKind(m->kind)) carts.push_back(m);
    }
    for (auto &cart : carts) {
        if (!cart) continue;
        std::unique_lock cartLock(*cart->stateMtx);
        if (cart->dead) continue;
        const auto dimension = canonicalDimension(cart->dimension);
        World& world = worldFor(dimension);
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
            std::uint16_t st = world.getBlock(bx, by+dy, bz);
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
            dimensionRuntime(dimension).poweredDetectorRails.insert(
                posKey(rx, ry, rz));
            bool curPowered=false;
            for (auto &pr : gen::propsOf(railState)) if (pr.first=="powered" && pr.second=="true") curPowered=true;
            bool wantPowered = true; // cart present implies powered
            // Check distance: cart must be within 0.3 of center to count? Use 0.5 for simplicity always true if found
            if (curPowered != wantPowered) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for (auto &pr : gen::propsOf(railState)) if (pr.first!="powered") props.emplace_back(pr.first, pr.second);
                props.emplace_back("powered", wantPowered?"true":"false");
                std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*railDef, props));
                world.setBlock(rx, ry, rz, ns);
                // analog output 15 implied via emissionLevel when powered true
            }
        }
        // If not on rail, apply free physics (gravity + friction)
        if (!found) {
            cart->velY -= 0.04; // gravity
            cart->velX *= 0.98; cart->velY *= 0.98; cart->velZ *= 0.98;
            // ground check
            if (world.getBlock(bx, by-1, bz) != 0) {
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
                    {
                        WriteBuffer vb;
                        vb.varint(cart->entityId);
                        vb.i16(static_cast<std::int16_t>(cart->velX * 8000));
                        vb.i16(static_cast<std::int16_t>(cart->velY * 8000));
                        vb.i16(static_cast<std::int16_t>(cart->velZ * 8000));
                        broadcastPacketExceptInDimension(dimension, nullptr,
                                                         proto::pl::sc::EntityVelocity, vb);
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
                    for (const auto& m : mobsSnapshot()) {
                        if (!m) continue;
                        std::lock_guard riderLock(*m->stateMtx);
                        if (m->entityId == rider) {
                            m->vehicleId = -1;
                            break;
                        }
                    }
                    for (const auto& pp : playersSnapshot()) {
                        if (!pp) continue;
                        std::lock_guard playerLock(pp->stateMtx);
                        if (pp->entityId == rider) {
                            pp->vehicleId = -1;
                            break;
                        }
                    }
                    WriteBuffer passengers;
                    passengers.varint(cart->entityId);
                    passengers.varint(0);
                    broadcastPacketExceptInDimension(dimension, nullptr,
                                                     proto::pl::sc::SetPassengers,
                                                     passengers);
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
            broadcastPacketExceptInDimension(dimension, nullptr,
                                             proto::pl::sc::MoveEntityPosRot, b);
            cart->sentX = cart->x; cart->sentY = cart->y; cart->sentZ = cart->z; cart->hasSent = true;
        }
        // Handle detector rail unpower when cart left (scan nearby rails for no cart)
        // Do second pass for detector rails near previous position? Simplified: leave powered true while cart exists; will be cleared by next tick when no cart nearby if we scan.
    }
    // Clear every detector rail we have touched when no cart remains on its
    // exact block.  This also handles the final cart being removed or moved
    // between ticks, which a scan around current cart positions cannot see.
    constexpr std::array<std::int8_t, 3> dimensions{0, -1, 1};
    for (const std::int8_t dimension : dimensions) {
        World& world = worldFor(dimension);
        auto& known = dimensionRuntime(dimension).poweredDetectorRails;
        for (auto it = known.begin(); it != known.end();) {
            const auto key = *it;
            const int nx = posKeyUnpackX(key);
            const int ny = posKeyUnpackY(key);
            const int nz = posKeyUnpackZ(key);
            const std::uint16_t st = world.getBlock(nx, ny, nz);
            const gen::BlockDef* bd = gen::blockByState(st);
            if (!bd || std::string(bd->name) != "minecraft:detector_rail") {
                it = known.erase(it);
                continue;
            }
            bool powered = false;
            for (auto& pr : gen::propsOf(st))
                if (pr.first == "powered" && pr.second == "true") powered = true;
            bool hasCart = false;
            for (const auto& cart : carts) {
                if (!cart) continue;
                std::lock_guard cartLock(*cart->stateMtx);
                if (canonicalDimension(cart->dimension) != dimension) continue;
                const int cbx = static_cast<int>(std::floor(cart->x));
                const int cby = static_cast<int>(std::floor(cart->y));
                const int cbz = static_cast<int>(std::floor(cart->z));
                for (int ddy : {0, -1, 1})
                    if (cbx == nx && cby + ddy == ny && cbz == nz)
                        hasCart = true;
            }
            if (!hasCart && powered) {
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for (auto& pr : gen::propsOf(st))
                    if (pr.first != "powered") props.emplace_back(pr.first, pr.second);
                props.emplace_back("powered", "false");
                const std::uint16_t ns = static_cast<std::uint16_t>(
                    gen::stateWithProps(*bd, props));
                world.setBlock(nx, ny, nz, ns);
            }
            ++it;
        }
    }
}
void GameServer::boatsTick() {
    std::vector<std::shared_ptr<MobEntity>> boats;
    for (const auto& m : mobsSnapshot()) {
        if (!m) continue;
        std::lock_guard mobLock(*m->stateMtx);
        if (MobEntity::isBoat(m->kind)) boats.push_back(m);
    }
    for (auto &b : boats) {
        if (!b) continue;
        std::unique_lock boatLock(*b->stateMtx);
        if (b->dead) continue;
        const auto dimension = canonicalDimension(b->dimension);
        World& world = worldFor(dimension);
        int bx=(int)std::floor(b->x), by=(int)std::floor(b->y), bz=(int)std::floor(b->z);
        auto stBelow = world.getBlock(bx, by-1, bz);
        const gen::BlockDef* dBelow = gen::blockByState(stBelow);
        bool inWater = FluidSim::getFluidState(world, bx, by, bz).isWater();
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
            if (world.getBlock(bx, by-1, bz)!=0 && b->velY<0) b->velY=0;
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
            broadcastPacketExceptInDimension(dimension, nullptr,
                                             proto::pl::sc::MoveEntityPosRot,
                                             pkt);
            b->sentX=b->x; b->sentY=b->y; b->sentZ=b->z; b->hasSent=true;
        }
    }
}

} // namespace cppfm
