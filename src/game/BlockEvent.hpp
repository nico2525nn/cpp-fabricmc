#pragma once
#include <cstdint>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
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
    bool onBlockPlace(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t oldState, std::uint16_t newState, void* player = nullptr) {
        BlockEvent ev; ev.type=BlockEvent::Type::Place; ev.x=x; ev.y=y; ev.z=z; ev.oldState=oldState; ev.newState=newState; ev.player=player;
        BlockPlaceBlockEvent clientEvent; clientEvent.player=player; clientEvent.x=x; clientEvent.y=y; clientEvent.z=z; clientEvent.oldState=oldState; clientEvent.newState=newState;
        return dispatchCancelable(placeHandlers_, ev, clientEvent, placeHook_);
    }
    bool onBlockBreak(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t oldState, void* player = nullptr) {
        BlockEvent ev; ev.type=BlockEvent::Type::Break; ev.x=x; ev.y=y; ev.z=z; ev.oldState=oldState; ev.player=player;
        BlockBreakBlockEvent clientEvent; clientEvent.player=player; clientEvent.x=x; clientEvent.y=y; clientEvent.z=z; clientEvent.oldState=oldState;
        return dispatchCancelable(breakHandlers_, ev, clientEvent, breakHook_);
    }
    bool onBlockClicked(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, int face, void* player = nullptr) {
        BlockEvent ev; ev.type=BlockEvent::Type::Clicked; ev.x=x; ev.y=y; ev.z=z; ev.oldState=state; ev.newState=state; ev.player=player; ev.face=face;
        BlockClickedEvent clientEvent; clientEvent.player=player; clientEvent.x=x; clientEvent.y=y; clientEvent.z=z; clientEvent.state=state; clientEvent.face=face;
        return dispatchCancelable(clickedHandlers_, ev, clientEvent, clickedHook_);
    }
    void onEntityLand(void* entity, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t blockState, double fallDistance = 0) {
        LegacyDispatchScope dispatchScope(*this);
        BlockEvent ev; ev.type = BlockEvent::Type::EntityLand; ev.x=x; ev.y=y; ev.z=z; ev.oldState=blockState; ev.entity=entity; ev.face=0;
        auto handlers = snapshotHandlers(landHandlers_);
        for (auto& h : handlers) h(ev);
        EntityLandEvent lev; lev.entity=entity; lev.x=x; lev.y=y; lev.z=z; lev.blockState=blockState; lev.fallDistance=fallDistance;
        landHook_.fire(lev);
    }

    void addOnBlockPlaceHandler(std::function<void(const BlockEvent&)> h) { addHandler(placeHandlers_, std::move(h)); }
    void addOnBlockBreakHandler(std::function<void(const BlockEvent&)> h) { addHandler(breakHandlers_, std::move(h)); }
    void addOnBlockClickedHandler(std::function<void(const BlockEvent&)> h) { addHandler(clickedHandlers_, std::move(h)); }
    void addOnEntityLandHandler(std::function<void(const BlockEvent&)> h) { addHandler(landHandlers_, std::move(h)); }

    // Legacy callbacks have no individual token for ABI compatibility. JVM
    // unload uses this bulk removal fence before invalidating mod handles.
    void clearLegacyHandlers() {
        std::unique_lock lock(legacyMtx_);
        const auto currentThread = std::this_thread::get_id();
        const auto currentIt = activeDispatchesByThread_.find(currentThread);
        const std::size_t currentDispatches =
            currentIt == activeDispatchesByThread_.end() ? 0 : currentIt->second;
        clearingLegacyHandlers_ = true;
        legacyCv_.wait(lock, [&] {
            return activeLegacyDispatches_ <= currentDispatches;
        });
        placeHandlers_.clear();
        breakHandlers_.clear();
        clickedHandlers_.clear();
        landHandlers_.clear();
        clearingLegacyHandlers_ = false;
        lock.unlock();
        legacyCv_.notify_all();
    }

    api::EventHook<BlockPlaceBlockEvent>& placeHook() { return placeHook_; }
    api::EventHook<BlockBreakBlockEvent>& breakHook() { return breakHook_; }
    api::EventHook<BlockClickedEvent>& clickedHook() { return clickedHook_; }
    api::EventHook<EntityLandEvent>& landHook() { return landHook_; }

private:
    using LegacyHandler = std::function<void(const BlockEvent&)>;

    class LegacyDispatchScope {
    public:
        explicit LegacyDispatchScope(BlockEventDispatcher& owner) : owner_(owner) {
            std::unique_lock lock(owner_.legacyMtx_);
            const auto currentThread = std::this_thread::get_id();
            owner_.legacyCv_.wait(lock, [&] {
                return !owner_.clearingLegacyHandlers_ ||
                       owner_.activeDispatchesByThread_.count(currentThread) != 0;
            });
            ++owner_.activeLegacyDispatches_;
            ++owner_.activeDispatchesByThread_[currentThread];
        }

        ~LegacyDispatchScope() {
            std::lock_guard lock(owner_.legacyMtx_);
            --owner_.activeLegacyDispatches_;
            const auto currentThread = std::this_thread::get_id();
            auto it = owner_.activeDispatchesByThread_.find(currentThread);
            if (it != owner_.activeDispatchesByThread_.end()) {
                if (--it->second == 0) owner_.activeDispatchesByThread_.erase(it);
            }
            owner_.legacyCv_.notify_all();
        }

        LegacyDispatchScope(const LegacyDispatchScope&) = delete;
        LegacyDispatchScope& operator=(const LegacyDispatchScope&) = delete;

    private:
        BlockEventDispatcher& owner_;
    };

    template <typename ClientEvent>
    bool dispatchCancelable(std::vector<LegacyHandler>& handlers,
                            BlockEvent& legacyEvent,
                            ClientEvent& clientEvent,
                            api::EventHook<ClientEvent>& hook) {
        LegacyDispatchScope dispatchScope(*this);
        try {
            auto snapshot = snapshotHandlers(handlers);
            for (auto& handler : snapshot) handler(legacyEvent);
            return hook.fire(clientEvent);
        } catch (...) {
            return false;
        }
    }

    template <typename Handler>
    void addHandler(std::vector<Handler>& handlers, Handler handler) {
        std::unique_lock lock(legacyMtx_);
        const auto currentThread = std::this_thread::get_id();
        legacyCv_.wait(lock, [&] {
            return !clearingLegacyHandlers_ ||
                   activeDispatchesByThread_.count(currentThread) != 0;
        });
        handlers.push_back(std::move(handler));
    }

    template <typename Handler>
    std::vector<Handler> snapshotHandlers(const std::vector<Handler>& handlers) const {
        std::lock_guard lock(legacyMtx_);
        return handlers;
    }

    mutable std::mutex legacyMtx_;
    std::condition_variable legacyCv_;
    std::size_t activeLegacyDispatches_ = 0;
    std::unordered_map<std::thread::id, std::size_t> activeDispatchesByThread_;
    bool clearingLegacyHandlers_ = false;
    std::vector<LegacyHandler> placeHandlers_;
    std::vector<LegacyHandler> breakHandlers_;
    std::vector<LegacyHandler> clickedHandlers_;
    std::vector<LegacyHandler> landHandlers_;
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
