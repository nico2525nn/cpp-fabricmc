// ServerEvents: concrete event types fired through the EventBus (plan2.md
// "イベントバス"). Systems and future mods subscribe via api::EventHook.
#pragma once
#include "../api/EventBus.hpp"

namespace cppfm::api {

struct PlayerJoinEvent { void* player = nullptr; };
struct PlayerQuitEvent { void* player = nullptr; };
struct PlayerChatEvent : Cancelable {
    void* player = nullptr;
    std::string message;
};
struct BlockBreakEvent : Cancelable {
    void* player = nullptr;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t oldState = 0;
    bool dropItems = true;
};
struct BlockPlaceEvent : Cancelable {
    void* player = nullptr;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t newState = 0;
};
struct EntityDamageEvent : Cancelable {
    void* victimPlayer = nullptr;                    // Player* when player hit
    void* victimMob = nullptr;                       // MobEntity* when mob hit
    float amount = 0.f;
    std::string cause;
};
struct MobSpawnEvent : Cancelable {
    void* mob = nullptr;
    double x = 0, y = 0, z = 0;
};
struct ServerTickEvent { std::int64_t tick = 0; };
struct CommandExecuteEvent : Cancelable {
    std::string line;
    void* source = nullptr;
};
struct BlockClickedEvent : Cancelable {
    void* player = nullptr;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t state = 0;
    int face = 0;
};
struct EntityLandEvent {
    void* entity = nullptr;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t blockState = 0;
    double fallDistance = 0;
};

struct ServerEvents {
    EventHook<PlayerJoinEvent> join;
    EventHook<PlayerQuitEvent> quit;
    EventHook<PlayerChatEvent> chat;
    EventHook<BlockBreakEvent> blockBreak;
    EventHook<BlockPlaceEvent> blockPlace;
    EventHook<BlockClickedEvent> blockClicked;
    EventHook<EntityLandEvent> entityLand;
    EventHook<EntityDamageEvent> entityDamage;
    EventHook<MobSpawnEvent> mobSpawn;
    EventHook<ServerTickEvent> serverTick;
    EventHook<CommandExecuteEvent> commandExecute;
};

// Process-wide registry (mirrors Fabric API's global ServerLifecycleEvents).
inline ServerEvents& events() {
    static ServerEvents inst;
    return inst;
}

} // namespace cppfm::api
