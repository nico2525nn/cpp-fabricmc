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

namespace {
// Caller owns Player::stateMtx; this keeps the inventory-to-equipment slot
// projection identical for broadcasts and join-time targeted synchronization.
std::array<ItemStack, 6> equipmentFromInventoryLocked(const Player& player) {
    std::array<ItemStack, 6> equipment{};
    if (player.heldSlot >= 0 && player.heldSlot < 9)
        equipment[0] = player.inv[36 + player.heldSlot];
    equipment[1] = player.inv[45];
    equipment[2] = player.inv[5];
    equipment[3] = player.inv[6];
    equipment[4] = player.inv[7];
    equipment[5] = player.inv[8];
    return equipment;
}

} // namespace

void GameServer::sendEquipment(const MobEntity& mob) {
    std::array<ItemStack, 6> equipment;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        equipment = mob.equipment;
        entityId = mob.entityId;
        dimension = mob.dimension;
    }
    EquipmentComponent comp(equipment);
    if (!comp.hasAny()) return;
    WriteBuffer b;
    b.varint(entityId);
    comp.writePayload(b);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::SetEquipment, b);
}
void GameServer::sendEquipmentSlot(const MobEntity& mob, int slot) {
    if (slot<0||slot>=6) return;
    std::array<ItemStack, 6> equipment;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        equipment = mob.equipment;
        entityId = mob.entityId;
        dimension = mob.dimension;
    }
    EquipmentComponent comp(equipment);
    WriteBuffer b;
    b.varint(entityId);
    comp.writePayloadSingle(b, slot);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::SetEquipment, b);
}
void GameServer::broadcastPlayerEquipment(const Player& p, const bool force) {
    std::unique_lock equipmentPacketLock(p.equipmentPacketMtx);
    std::array<ItemStack, 6> equipment{};
    std::shared_ptr<Connection> connection;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        equipment = equipmentFromInventoryLocked(p);
        connection = p.conn;
        entityId = p.entityId;
        dimension = p.dimension;
    }
    if (!connection) return;
    WriteBuffer b;
    b.varint(entityId);
    // This is a complete equipment snapshot, not a list of non-empty slots:
    // explicit air stacks clear equipment removed since the previous update.
    EquipmentComponent(equipment).writePayload(b, true);
    const auto packetDimension = canonicalDimension(dimension);
    if (!force && p.hasLastEquipmentPacket &&
        p.lastEquipmentPacketDimension == packetDimension &&
        p.lastEquipmentPacket == b.data)
        return;
    p.lastEquipmentPacket = b.data;
    p.lastEquipmentPacketDimension = packetDimension;
    p.hasLastEquipmentPacket = true;
    broadcastPacketExceptInDimension(dimension, &p,
                                     proto::pl::sc::SetEquipment, b);
    connection->trySendPacket(proto::pl::sc::SetEquipment, b);
}
void GameServer::sendPlayerEquipmentTo(const Player& subject,
                                       const Player& viewer) {
    if (&subject == &viewer) return;

    std::unique_lock equipmentPacketLock(subject.equipmentPacketMtx);
    std::array<ItemStack, 6> equipment{};
    std::int32_t entityId = 0;
    std::int8_t subjectDimension = 0;
    {
        std::lock_guard subjectLock(subject.stateMtx);
        if (!subject.inPlay) return;
        equipment = equipmentFromInventoryLocked(subject);
        entityId = subject.entityId;
        subjectDimension = canonicalDimension(subject.dimension);
    }

    std::shared_ptr<Connection> connection;
    std::int8_t viewerDimension = 0;
    {
        std::lock_guard viewerLock(viewer.stateMtx);
        if (!viewer.inPlay) return;
        connection = viewer.conn;
        viewerDimension = canonicalDimension(viewer.dimension);
    }
    if (!connection || viewerDimension != subjectDimension) return;

    WriteBuffer packet;
    packet.varint(entityId);
    EquipmentComponent(equipment).writePayload(packet, true);
    connection->trySendPacket(proto::pl::sc::SetEquipment, packet);
}
void GameServer::syncEquipmentOnChange(Player& p){
    broadcastPlayerEquipment(p);
}

} // namespace cppfm
