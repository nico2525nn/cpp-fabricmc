// BlockEvent: plan7 block interaction events (onBlockPlace, onBlockBreak, onBlockClicked, onEntityLand)
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include "../api/EventBus.hpp"

namespace cppfm {

// Generic BlockEvent carrier
struct BlockEvent {
    enum class Type { Place, Break, Clicked, EntityLand };
    Type type = Type::Place;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t oldState = 0;
    std::uint16_t newState = 0;
    void* player = nullptr;
    void* entity = nullptr;
    int face = 0;
};

// Specific structs for each hook (used by GameServer::fire*)
struct BlockPlaceBlockEvent : api::Cancelable {
    void* player = nullptr;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t oldState = 0;
    std::uint16_t newState = 0;
};

struct BlockBreakBlockEvent : api::Cancelable {
    void* player = nullptr;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t oldState = 0;
};

struct BlockClickedEvent : api::Cancelable {
    void* player = nullptr;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t state = 0;
    int face = 0;
    double cursorX = 0, cursorY = 0, cursorZ = 0;
};

struct EntityLandEvent {
    void* entity = nullptr; // Player* or MobEntity*
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t blockState = 0;
    double fallDistance = 0;
};

// Dispatcher that mirrors api::EventHook but also provides classic on* methods
class BlockEventDispatcher {
public:
    // Classic Fabric-style callbacks
    void onBlockPlace(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t oldState, std::uint16_t newState, void* player = nullptr) {
        BlockEvent ev; ev.type = BlockEvent::Type::Place; ev.x=x; ev.y=y; ev.z=z; ev.oldState=oldState; ev.newState=newState; ev.player=player;
        for (auto& h : placeHandlers_) h(ev);
        BlockPlaceBlockEvent cev; cev.player=player; cev.x=x; cev.y=y; cev.z=z; cev.oldState=oldState; cev.newState=newState;
        placeHook_.fire(cev);
    }
    void onBlockBreak(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t oldState, void* player = nullptr) {
        BlockEvent ev; ev.type = BlockEvent::Type::Break; ev.x=x; ev.y=y; ev.z=z; ev.oldState=oldState; ev.player=player;
        for (auto& h : breakHandlers_) h(ev);
        BlockBreakBlockEvent cev; cev.player=player; cev.x=x; cev.y=y; cev.z=z; cev.oldState=oldState;
        breakHook_.fire(cev);
    }
    void onBlockClicked(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, int face, void* player = nullptr) {
        BlockEvent ev; ev.type = BlockEvent::Type::Clicked; ev.x=x; ev.y=y; ev.z=z; ev.oldState=state; ev.newState=state; ev.player=player; ev.face=face;
        for (auto& h : clickedHandlers_) h(ev);
        BlockClickedEvent cev; cev.player=player; cev.x=x; cev.y=y; cev.z=z; cev.state=state; cev.face=face;
        clickedHook_.fire(cev);
    }
    void onEntityLand(void* entity, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t blockState, double fallDistance = 0) {
        BlockEvent ev; ev.type = BlockEvent::Type::EntityLand; ev.x=x; ev.y=y; ev.z=z; ev.oldState=blockState; ev.entity=entity; ev.face=0;
        (void)fallDistance;
        for (auto& h : landHandlers_) h(ev);
        EntityLandEvent lev; lev.entity=entity; lev.x=x; lev.y=y; lev.z=z; lev.blockState=blockState; lev.fallDistance=fallDistance;
        landHook_.fire(lev);
    }

    void addOnBlockPlaceHandler(std::function<void(const BlockEvent&)> h) { placeHandlers_.push_back(std::move(h)); }
    void addOnBlockBreakHandler(std::function<void(const BlockEvent&)> h) { breakHandlers_.push_back(std::move(h)); }
    void addOnBlockClickedHandler(std::function<void(const BlockEvent&)> h) { clickedHandlers_.push_back(std::move(h)); }
    void addOnEntityLandHandler(std::function<void(const BlockEvent&)> h) { landHandlers_.push_back(std::move(h)); }

    api::EventHook<BlockPlaceBlockEvent>& placeHook() { return placeHook_; }
    api::EventHook<BlockBreakBlockEvent>& breakHook() { return breakHook_; }
    api::EventHook<BlockClickedEvent>& clickedHook() { return clickedHook_; }
    api::EventHook<EntityLandEvent>& landHook() { return landHook_; }

private:
    std::vector<std::function<void(const BlockEvent&)>> placeHandlers_;
    std::vector<std::function<void(const BlockEvent&)>> breakHandlers_;
    std::vector<std::function<void(const BlockEvent&)>> clickedHandlers_;
    std::vector<std::function<void(const BlockEvent&)>> landHandlers_;
    api::EventHook<BlockPlaceBlockEvent> placeHook_;
    api::EventHook<BlockBreakBlockEvent> breakHook_;
    api::EventHook<BlockClickedEvent> clickedHook_;
    api::EventHook<EntityLandEvent> landHook_;
};

inline BlockEventDispatcher& blockEventDispatcher() {
    static BlockEventDispatcher inst;
    return inst;
}

} // namespace cppfm
