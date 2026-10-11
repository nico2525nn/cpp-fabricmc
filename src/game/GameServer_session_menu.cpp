#include "GameServer.hpp"
#include "Session.hpp"
#include "SessionMenuSupport.hpp"
#include "BundleContents.hpp"
#include "Messages.hpp"
#include "../generated/ItemIds.hpp"
#include "MenuInteraction.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppfm {
using namespace proto;

namespace {

using session_menu_detail::insertCompleteInventoryStack;

bool sameInventoryStack(const ItemStack& lhs, const ItemStack& rhs) {
    return !lhs.empty() && !rhs.empty() && lhs.sameItemData(rhs);
}

// Add a complete stack to a trial inventory.  Returning false leaves the
// caller's inventory untouched when the whole stack cannot fit; menu close
// and result-slot paths rely on that property to avoid partial-add + drop
// duplication.
struct SessionMenuIo : MenuIo {
    Session& s;
    explicit SessionMenuIo(Session& ss) : s(ss) {}
    using MenuIo::blockEntityChanged;

    // MenuLogic is called while the session owns the player/container model
    // locks.  Its callbacks are deliberately queued so that world mutation,
    // persistence/advancement hooks, and other extension-facing work never
    // run from inside that critical section.  The caller flushes after it
    // has released all model locks.
    enum class ActionKind { Drop, BlockEntityChanged, ItemObtained };
    struct PendingAction {
        ActionKind kind = ActionKind::BlockEntityChanged;
        Player* player = nullptr;
        std::int8_t dimension = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        std::int64_t key = 0;
        ItemStack stack = ItemStack::air();
        std::string how;
    };
    std::vector<PendingAction> pending;
    bool blockEntityChangePreAdmitted = false;

    bool hasBlockEntityChange() const {
        return std::any_of(pending.begin(), pending.end(), [](const auto& action) {
            return action.kind == ActionKind::BlockEntityChanged;
        });
    }

    void dropFromPlayer(Player& p, const ItemStack& stack, bool whole) override {
        ItemStack s2 = stack;
        if (!whole) s2.count = 1;
        PendingAction action;
        action.kind = ActionKind::Drop;
        action.player = &p;
        action.dimension = p.dimension;
        action.x = p.x;
        action.y = p.y + 1.2;
        action.z = p.z;
        action.stack = std::move(s2);
        pending.push_back(std::move(action));
    }
    void blockEntityChanged(std::int64_t key) override {
        PendingAction action;
        action.kind = ActionKind::BlockEntityChanged;
        action.dimension = s.dimension();
        action.key = key;
        pending.push_back(std::move(action));
    }
    bool preAdmitBlockEntityChange(std::int8_t dimension,
                                   std::optional<std::int64_t> key) {
        if (key &&
            !s.tryMarkBlockEntityDirty(dimension, *key))
            return false;
        blockEntityChangePreAdmitted = true;
        return true;
    }
    void itemCrafted(Player& p, const ItemStack& result) override {
        PendingAction action;
        action.kind = ActionKind::ItemObtained;
        action.player = &p;
        action.stack = result;
        action.how = "crafted";
        pending.push_back(std::move(action));
    }
    void itemSmelted(Player& p, const ItemStack& result) override {
        PendingAction action;
        action.kind = ActionKind::ItemObtained;
        action.player = &p;
        action.stack = result;
        action.how = "smelted";
        pending.push_back(std::move(action));
    }
    void flush() {
        auto actions = std::move(pending);
        pending.clear();
        for (auto& action : actions) {
            switch (action.kind) {
            case ActionKind::Drop:
                s.spawnMenuDrop(action.dimension, action.x, action.y,
                                action.z, action.stack);
                break;
            case ActionKind::BlockEntityChanged:
                if (!blockEntityChangePreAdmitted)
                    s.markBlockEntityDirty(action.dimension, action.key);
                break;
            case ActionKind::ItemObtained:
                if (action.player)
                    s.notifyMenuItemObtained(*action.player, action.stack,
                                             action.how.c_str());
                break;
            }
        }
    }
};

static std::unique_lock<std::recursive_mutex> lockMenuBlockEntity(Menu& menu) {
    if (!menu.blockEntityOwner || !menu.blockEntityOwner->stateMtx)
        return {};
    return std::unique_lock<std::recursive_mutex>(*menu.blockEntityOwner->stateMtx);
}

} // namespace

void Session::sendInitialInventory() {
    WriteBuffer b;
    std::shared_ptr<Connection> connection = conn_;
    std::unique_lock playerLock(self_->stateMtx);
    std::unique_lock packetLock(self_->inventoryPacketMtx);
    b.varint(0);                                       // window id: player inventory
    b.varint(++self_->invStateId);
    b.varint(static_cast<std::int32_t>(kPlayerInventorySlotCount));
    for (std::size_t i = 0; i < kPlayerInventorySlotCount; ++i)
        self_->inv[i].write(b);
    ItemStack::air().write(b);                         // carried item
    playerLock.unlock();
    connection->sendPacket(pl::sc::ContainerSetContent, b);
}

void Session::onSelectBundleItem(ReadBuffer& in) {
    try {
        const auto slotId = in.varint();
        const auto selectedIndex = in.varint();
        if (in.remaining() != 0 || slotId < 0 || selectedIndex < -1)
            return;

        const auto applySelection = [selectedIndex](ItemStack* stack) {
            if (!stack || stack->empty() ||
                stack->name() != "minecraft:bundle")
                return;
            if (selectedIndex == -1) {
                stack->selectedBundleStackIndex = -1;
                return;
            }
            const auto count = bundle_contents::size(*stack);
            stack->selectedBundleStackIndex =
                count && static_cast<std::size_t>(selectedIndex) < *count
                    ? selectedIndex : -1;
        };

        std::shared_ptr<Menu> activeMenu;
        {
            std::lock_guard stateLock(self_->stateMtx);
            activeMenu = openMenu_;
        }
        if (activeMenu) {
            auto entityLock = lockMenuBlockEntity(*activeMenu);
            std::unique_lock playerLock(self_->stateMtx);
            if (openMenu_ != activeMenu ||
                slotId >= activeMenu->totalSlots())
                return;
            applySelection(activeMenu->slotAt(
                slotId, self_->inv.data()));
            return;
        }

        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_ &&
            static_cast<std::size_t>(slotId) < self_->inv.size())
            applySelection(&self_->inv[static_cast<std::size_t>(slotId)]);
    } catch (const std::exception&) {
        in.skipRest();
    }
}

void Session::onWindowClick(ReadBuffer& in) {
    // Strict 1.21.4 (protocol 769) : `window_click` 0x10 windowId VarInt + stateId VarInt (I12).
    int windowId = 0;
    int stateId = 0;
    size_t mark = in.off;
    try {
        windowId = in.varint();
        stateId = in.varint();
    } catch (...) {
        in.off = mark;
        try {
            windowId = in.u8();
            stateId = in.varint();
        } catch (...) {
            in.off = mark;
            try { windowId = in.varint(); } catch (...) { return; }
            try { stateId = in.varint(); } catch (...) { stateId = 0; }
        }
    }
    const auto slotIdx = in.i16();
    const auto button = in.i8();
    const auto mode = in.varint();

    // changed slots array (client prediction; we recompute server-side)
    const auto nChanged = in.varint();
    if (nChanged < 0 || nChanged > 1024)
        throw std::runtime_error("window click changed-slot count out of range");
    for (std::int32_t i = 0; i < nChanged; ++i) {
        (void)in.i16();
        ItemStack::read(in);
    }
    ItemStack::read(in); // discard client cursor (server-authoritative)

    // Only hold stateMtx while deciding which authoritative view applies.
    // Menu handling and every resend below take their own short model
    // snapshot and perform network I/O after releasing the lock.  Keeping
    // this dispatcher lock-free across those calls also prevents a stale
    // click from acquiring the player lock and then waiting on a socket.
    std::shared_ptr<Menu> menu;
    bool playerWindow = false;
    bool staleRevision = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        staleRevision = stateId != self_->invStateId;
        if (windowId != 0 && openMenu_ && openMenu_->windowId == windowId) {
            menu = openMenu_;
        } else if (windowId == 0) {
            playerWindow = true;
        }
    }

    // The revision is the server's optimistic-concurrency guard.  Applying a
    // click built from an older menu snapshot can duplicate/lose items when a
    // tick or another packet has already changed the inventory.  Send the
    // current authoritative view and leave the click unapplied.
    if (staleRevision) {
        if (menu) sendMenuContent(*menu);
        else if (playerWindow) srv_.resendInventory(*self_);
        syncCursorItem();
        return;
    }

    if (menu) {
        handleMenuClick(*menu, slotIdx, button, mode);
        srv_.syncEquipmentOnChange(*self_);
        return;
    }
    if (playerWindow) {
        handlePlayerInventoryClick(slotIdx, button, mode);
    }
}

void Session::handlePlayerInventoryClick(int slot, int button, int mode) {
    // Player inventory is represented by Player::inv for persistence and all
    // non-screen callers.  The screen adapter owns only the 2x2 recipe grid
    // and cached result, then copies those five protocol slots back before the
    // authoritative ContainerSetContent snapshot is sent.
    std::unique_lock playerLock(self_->stateMtx);
    Menu& menu = playerInventoryMenu_;
    menu.type = MenuType::Crafting;
    menu.playerInventory = true;
    menu.blockKey.reset();
    menu.container = nullptr;
    menu.containerCount = 0;
    menu.blockEntity = nullptr;
    menu.blockEntityOwner.reset();
    for (auto& stack : menu.craftGrid) stack = ItemStack::air();
    for (int i = 0; i < 4; ++i)
        menu.craftGrid[menu.craftGridIndex(i + 1)] =
            self_->inv[static_cast<std::size_t>(i + 1)];
    menu.craftResult = self_->inv[0];
    menu.refreshCraftResult(srv_.recipes());

    SessionMenuIo io(*this);
    (void)ClickLogic::apply(menu, *self_, srv_.recipes(), slot, button, mode,
                            cursorItem_, io);
    for (int i = 0; i < 4; ++i)
        self_->inv[static_cast<std::size_t>(i + 1)] =
            menu.craftGrid[menu.craftGridIndex(i + 1)];
    self_->inv[0] = menu.craftResult;
    playerLock.unlock();

    io.flush();
    srv_.resendInventory(*self_);
    syncCursorItem();
}
void Session::onEnchantItem(ReadBuffer& in) {
    // Packet `enchant_item` 0x0F: `windowId` VarInt (protocol.json 1.21.4, strict) + `button` VarInt.
    // Yarn `EnchantmentScreenHandler` uses VarInt for windowId; retain u8 fallback for leniency (vanilla client sends VarInt, some proxies u8).
    int windowId = 0;
    int button = 0;
    const std::size_t mark = in.off;
    try {
        windowId = in.varint();
        if (in.remaining() > 0) button = in.varint();
        else button = 0;
    } catch (...) {
        in.off = mark;
        try { windowId = in.u8(); button = in.u8(); } catch(...) { return; }
    }
    std::shared_ptr<Menu> menu;
    std::shared_ptr<BlockEntity> menuOwner;
    std::optional<std::int64_t> blockKey;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_ || openMenu_->windowId != windowId ||
            openMenu_->type != MenuType::Enchantment)
            return;
        menu = openMenu_;
        menuOwner = menu->blockEntityOwner;
        blockKey = menu->blockKey;
        dimension = self_->dimension;
    }

    // The bookshelf scan only needs the immutable menu position and world;
    // do it before acquiring the player lock so a world/chunk read cannot
    // block a concurrent player mutation while holding stateMtx.
    int bookshelves = 0;
    if (blockKey) {
        const int bx = posKeyUnpackX(*blockKey);
        const int by = posKeyUnpackY(*blockKey);
        const int bz = posKeyUnpackZ(*blockKey);
        bookshelves = CostCalculator::countBookshelves(
            srv_.worldFor(dimension), bx, by, bz);
    }

    auto mutationGate = srv_.worldFor(dimension).lockMutationGate();
    std::unique_lock<std::recursive_mutex> entityLock;
    if (menuOwner && menuOwner->stateMtx)
        entityLock = std::unique_lock<std::recursive_mutex>(*menuOwner->stateMtx);
    std::unique_lock playerLock(self_->stateMtx);
    if (openMenu_.get() != menu.get() || menu->windowId != windowId ||
        menu->type != MenuType::Enchantment)
        return;
    auto* logic = getMenuLogic(MenuType::Enchantment);
    auto* ench = logic ? dynamic_cast<EnchantmentMenuLogic*>(logic) : nullptr;
    if (!ench) return;

    ItemStack* input = menu->container ? &menu->container[0]
                                       : &menu->extraSlots[0];
    ItemStack* lapis = menu->container ? &menu->container[1]
                                       : &menu->extraSlots[1];
    const auto lapisId = gen::itemIdByName().find("minecraft:lapis_lazuli");
    if (button < 0 || button >= 3 || input->empty() || lapis->empty() ||
        lapisId == gen::itemIdByName().end() || lapis->itemId != lapisId->second)
        return;

    const auto offers = ench->offers(*menu, *self_, bookshelves);
    const EnchantmentOffer& choice =
        offers[static_cast<std::size_t>(button)];
    if (!choice.selectable() || lapis->count < choice.lapisCost)
        return;
    const int requiredLevel = button + 1;
    if (self_->gamemode == 0 &&
        (self_->xp.level < requiredLevel || self_->xp.level < choice.levelCost))
        return;

    SessionMenuIo io(*this);
    try {
        if (!io.preAdmitBlockEntityChange(dimension, blockKey)) {
            playerLock.unlock();
            if (entityLock.owns_lock()) entityLock.unlock();
            mutationGate.unlock();
            sendMenuContent(*menu);
            syncCursorItem();
            return;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[cppfm] enchant menu dirty admission failed: %s\n",
                     error.what());
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        mutationGate.unlock();
        sendMenuContent(*menu);
        syncCursorItem();
        return;
    } catch (...) {
        std::fprintf(stderr, "[cppfm] enchant menu dirty admission failed\n");
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        mutationGate.unlock();
        sendMenuContent(*menu);
        syncCursorItem();
        return;
    }

    // Apply the presented choice while the menu and player model are
    // serialized.  This is intentionally equivalent to
    // EnchantmentMenuLogic::onEnchantButton, but keeps its experience packet
    // out of the critical section.
    const std::string enchantedItemName = input->name();
    const int consumedLapis = choice.lapisCost;
    *input = choice.result;
    lapis->count = static_cast<std::int16_t>(lapis->count - choice.lapisCost);
    if (lapis->count <= 0) *lapis = ItemStack::air();
    const bool experienceChanged = self_->gamemode == 0;
    if (experienceChanged)
        self_->xp.level = std::max(0, self_->xp.level - choice.levelCost);
    std::uint32_t nextSeed = CostCalculator::splitmix32(
        static_cast<std::uint32_t>(self_->enchantmentSeed) +
        0x9e3779b9u + static_cast<std::uint32_t>(button));
    if (nextSeed == 0) nextSeed = 0x5a5a5a5au;
    self_->enchantmentSeed = static_cast<std::int32_t>(nextSeed);
    const std::int32_t enchantedWindowId = menu->windowId;

    io.blockEntityChanged(menu->blockKey);
    playerLock.unlock();
    if (entityLock.owns_lock()) entityLock.unlock();
    mutationGate.unlock();

    if (experienceChanged) GameServer::sendSetExperience(*self_);
    io.flush();
    // Advancement/JVM hooks may synchronously close or replace the menu.
    if (!enchantedItemName.empty())
        srv_.onItemEnchanted(*self_, enchantedItemName, consumedLapis);
    bool sameMenu = false;
    {
        std::lock_guard stateLock(self_->stateMtx);
        sameMenu = openMenu_.get() == menu.get() &&
                   openMenu_->windowId == enchantedWindowId;
    }
    if (sameMenu) {
        sendMenuContent(*menu);
        syncCursorItem();
    }
}
void Session::onTabComplete(ReadBuffer& in) {
    const auto transactionId = in.varint();
    const std::string text = in.string(65536);

    brigadier::CommandSource src;
    src.player = self_.get();
    src.name = self_->name;
    src.console = false;
    src.srcX = self_->x; src.srcY = self_->y; src.srcZ = self_->z;
    srv_.bindCommandSelector(src);

    std::string query = text;
    if (!query.empty() && query[0] == '/') query = query.substr(1);
    const auto suggestions = srv_.commands().suggest(query, std::move(src));

    // Strict token start: replace only the current token, not whole line.
    // Vanilla CommandSuggestions range is [start, start+length) covering the token being completed.
    std::int32_t start = 0;
    if (!text.empty()) {
        // find last space — token starts after it
        std::size_t lastSpace = text.rfind(' ');
        if (lastSpace != std::string::npos) {
            if (lastSpace + 1 >= text.size()) start = static_cast<std::int32_t>(text.size());
            else start = static_cast<std::int32_t>(lastSpace + 1);
        } else {
            // no space: for "/" prefixed commands, token starts after '/'
            if (text[0] == '/') start = 1;
            else start = 0;
        }
        // also handle trailing spaces already covered; for quoted or colon-separated
        // resource locations we keep the space-based token (vanilla includes "minecraft:" prefix).
    }
    std::int32_t length = static_cast<std::int32_t>(text.size()) - start;
    if (length < 0) length = 0;
    WriteBuffer b;
    b.varint(transactionId);
    b.varint(start);
    b.varint(length);
    b.varint(static_cast<std::int32_t>(suggestions.size()));
    for (auto& [match, tooltip] : suggestions) {
        b.string(match);
        b.boolean(false);
    }
    conn_->trySendPacket(pl::sc::CommandSuggestions, b);
}
void Session::sendSetSlot(std::int32_t windowId, std::int32_t stateId,
                          std::int16_t slot, const ItemStack& s) {
    WriteBuffer b;
    b.varint(windowId);
    b.varint(stateId);
    b.i16(slot);
    s.write(b);
    std::lock_guard packetLock(self_->inventoryPacketMtx);
    conn_->trySendPacket(pl::sc::ContainerSetSlot, b);
}
void Session::syncCursorItem() {
    WriteBuffer b;
    cursorItem_.write(b);
    std::lock_guard packetLock(self_->inventoryPacketMtx);
    conn_->trySendPacket(pl::sc::SetCursorItem, b);
}
void Session::handleMenuClick(Menu& m, int slot, int button, int mode) {
    Menu* const menuPtr = &m;
    const std::int32_t menuWindowId = m.windowId;
    const bool mayEditBlockEntity =
        m.blockKey.has_value() && m.blockEntity &&
        ((slot >= 0 && slot < menuLayoutFor(m.type).containerSlots) ||
         mode == 1 || mode == 5 || mode == 6);
    std::optional<MutationGateLease> mutationGate;
    if (mayEditBlockEntity) {
        std::int8_t dimension = 0;
        {
            std::lock_guard stateLock(self_->stateMtx);
            dimension = self_->dimension;
        }
        mutationGate.emplace(srv_.worldFor(dimension).lockMutationGate());
    }
    // A menu backed by a block entity must be updated atomically with the
    // player's inventory. Keep a consistent order: World gate -> block
    // entity -> player. Model and World locks are released before callbacks
    // or packet I/O.
    auto entityLock = lockMenuBlockEntity(m);
    std::unique_lock playerLock(self_->stateMtx);
    SessionMenuIo io(*this);
    std::vector<WriteBuffer> extraPackets;
    bool experienceChanged = false;
    std::optional<decltype(self_->inv)> inventoryBefore;
    std::optional<ItemStack> cursorBefore;
    std::optional<XpState> experienceBefore;
    std::optional<BlockEntity> blockEntityBefore;
    std::array<ItemStack, 27> extraSlotsBefore;
    std::array<ItemStack, 9> craftGridBefore;
    ItemStack craftResultBefore;
    std::vector<int> dragSlotsBefore;
    int dragButtonBefore = -1;
    if (mayEditBlockEntity) {
        inventoryBefore = self_->inv;
        cursorBefore = cursorItem_;
        experienceBefore = self_->xp;
        blockEntityBefore.emplace(*m.blockEntity);
        for (std::size_t i = 0; i < extraSlotsBefore.size(); ++i)
            extraSlotsBefore[i] = m.extraSlots[i];
        for (std::size_t i = 0; i < craftGridBefore.size(); ++i)
            craftGridBefore[i] = m.craftGrid[i];
        craftResultBefore = m.craftResult;
        dragSlotsBefore = m.dragSlots;
        dragButtonBefore = m.dragButton;
    }

    const auto menuStillOpen = [&]() {
        std::lock_guard playerStateLock(self_->stateMtx);
        return openMenu_ && openMenu_.get() == menuPtr &&
               openMenu_->windowId == menuWindowId;
    };
    const auto releaseModelLocks = [&]() {
        if (playerLock.owns_lock()) playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
    };
    const auto finish = [&]() {
        if (mayEditBlockEntity && io.hasBlockEntityChange()) {
            const auto blockKey = *m.blockKey;
            const auto cx = posKeyUnpackX(blockKey) >> 4;
            const auto cz = posKeyUnpackZ(blockKey) >> 4;
            if (!srv_.chunkFitsAnvilEntry(self_->dimension, cx, cz)) {
                // Do not acknowledge a menu edit that cannot be represented by
                // the Anvil entry limit. Restore every model touched by menu
                // logic before releasing locks; queued world/stat/drop hooks
                // are discarded with the rejected transaction.
                if (blockEntityBefore) *m.blockEntity = *blockEntityBefore;
                if (inventoryBefore) self_->inv = *inventoryBefore;
                if (cursorBefore) cursorItem_ = *cursorBefore;
                if (experienceBefore) self_->xp = *experienceBefore;
                for (std::size_t i = 0; i < extraSlotsBefore.size(); ++i)
                    m.extraSlots[i] = extraSlotsBefore[i];
                for (std::size_t i = 0; i < craftGridBefore.size(); ++i)
                    m.craftGrid[i] = craftGridBefore[i];
                m.craftResult = craftResultBefore;
                m.dragSlots = dragSlotsBefore;
                m.dragButton = dragButtonBefore;
                io.pending.clear();
                extraPackets.clear();
                experienceChanged = false;
                releaseModelLocks();
                if (mutationGate) mutationGate->unlock();
                if (menuStillOpen()) sendMenuContent(*menuPtr);
                syncCursorItem();
                return;
            }
        }
        releaseModelLocks();
        // MenuIo callbacks are extension/world work and must run after both
        // model locks have been released.  They may also close or replace the
        // menu, so all subsequent packet emission is guarded by
        // menuStillOpen().
        if (mutationGate) mutationGate->unlock();
        if (experienceChanged) GameServer::sendSetExperience(*self_);
        io.flush();
        if (!menuStillOpen()) return;
        sendMenuContent(*menuPtr);
        if (!menuStillOpen()) return;
        syncCursorItem();
        if (!menuStillOpen()) return;
        for (const auto& packet : extraPackets)
            conn_->trySendPacket(pl::sc::ContainerSetData, packet);
    };

    // Publish the dirty generation before menu logic mutates a live block
    // entity. If persistence refuses that publication, reject the click while
    // every model is still unchanged.
    if (mayEditBlockEntity) {
        try {
            if (!io.preAdmitBlockEntityChange(self_->dimension, m.blockKey)) {
                releaseModelLocks();
                if (mutationGate) mutationGate->unlock();
                if (menuStillOpen()) sendMenuContent(*menuPtr);
                syncCursorItem();
                return;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[cppfm] menu dirty admission failed: %s\n",
                         error.what());
            releaseModelLocks();
            if (mutationGate) mutationGate->unlock();
            if (menuStillOpen()) sendMenuContent(*menuPtr);
            syncCursorItem();
            return;
        } catch (...) {
            std::fprintf(stderr, "[cppfm] menu dirty admission failed\n");
            releaseModelLocks();
            if (mutationGate) mutationGate->unlock();
            if (menuStillOpen()) sendMenuContent(*menuPtr);
            syncCursorItem();
            return;
        }
    }

    const auto giveOutput = [&](const ItemStack& output) {
        if (output.empty()) return false;
        if (cursorItem_.empty()) {
            cursorItem_ = output;
            return true;
        }
        if (sameInventoryStack(cursorItem_, output)) {
            const int limit = maxStackFor(output);
            if (cursorItem_.count <= limit - output.count) {
                cursorItem_.count = static_cast<std::int16_t>(
                    cursorItem_.count + output.count);
                return true;
            }
        }
        auto trial = self_->inv;
        if (!insertCompleteInventoryStack(trial, output)) return false;
        self_->inv = std::move(trial);
        return true;
    };

    // Stonecutter output take (slot 1) - consume input, give result
    if (m.type == MenuType::Stonecutter && slot == 1 && mode == 0 && button == 0) {
        ItemStack* inp = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* out = m.container ? &m.container[1] : &m.extraSlots[1];
        if (!out->empty() && !inp->empty()) {
            if (!giveOutput(*out)) return;
            if (--inp->count <= 0) *inp = ItemStack::air();
            if (!inp->empty()) {
                const Recipe* r = srv_.recipes().findStonecutting(inp->itemId);
                if (r) *out = r->result;
                else *out = ItemStack::air();
            } else *out = ItemStack::air();
            finish();
            return;
        }
    }
    if (m.type == MenuType::Anvil && slot == 2 && mode == 0 && button == 0) {
        ItemStack* out = &m.extraSlots[2];
        if (!out->empty()) {
            std::string rename = m.anvilRename;
            int cost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], rename);
            if (cost < 0) cost = 0;
            bool tooExp = CostCalculator::isTooExpensive(cost, self_->gamemode==1);
            if ((self_->xp.level >= cost || self_->gamemode == 1) && cost > 0 && !tooExp) {
                if (!giveOutput(*out)) return;
                if (self_->gamemode == 0) {
                    self_->xp.level -= cost;
                    experienceChanged = true;
                }
                if (--m.extraSlots[0].count <= 0) m.extraSlots[0] = ItemStack::air();
                if (!m.extraSlots[1].empty() && --m.extraSlots[1].count <= 0) m.extraSlots[1] = ItemStack::air();
                *out = ItemStack::air();
                // refresh cost
                int newCost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], m.anvilRename);
                WriteBuffer pb;
                pb.varint(m.windowId);
                pb.i16(0);
                pb.i16(static_cast<std::int16_t>(newCost < 0 ? 0 : newCost));
                extraPackets.push_back(std::move(pb));
                finish();
                return;
            }
        }
    }
    // Yarn `CartographyTableScreenHandler` slots: 0 map, 1 paper, 2 result (filled_map clone). Take result consumes paper.
    if (m.type == MenuType::CartographyTable && slot == 2 && mode == 0 && button == 0) {
        ItemStack* mapIn = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* paperIn = m.container ? &m.container[1] : &m.extraSlots[1];
        ItemStack* out = m.container ? &m.container[2] : &m.extraSlots[2];
        if (!out->empty() && !mapIn->empty() && !paperIn->empty()) {
            // Validate map duplication recipe: filled_map + paper -> filled_map clone
            bool isFilledMap = out->name() == "minecraft:filled_map" || mapIn->name() == "minecraft:filled_map";
            bool isPaper = paperIn->name() == "minecraft:paper";
            if (isFilledMap && isPaper) {
                if (!giveOutput(*out)) return;
                if (--paperIn->count <= 0) *paperIn = ItemStack::air();
                // map slot is not consumed (vanilla duplicates map, not consumes); vanilla keeps map and only consumes paper
                // Duplicate output is single map copy already given; clear output and recompute
                *out = ItemStack::air();
                if (!mapIn->empty() && !paperIn->empty()) {
                    // recompute output: clone map (preserve components like map_id)
                    *out = *mapIn;
                    out->count = 1;
                }
                finish();
                return;
            }
        }
    }
    if (auto* logic = getMenuLogic(m.type)) {
        // Check if click is within container region; let logic handle it, fallback to generic for player inv
        int cont = m.totalSlots() - 36;
        if (slot >=0 && slot < cont) {
            bool handled = logic->onSlotClick(m, *self_, slot, button, mode, cursorItem_, io, srv_.recipes());
            if (handled) {
                logic->onContentChanged(m, *self_);
                if (m.type == MenuType::Crafting) m.refreshCraftResult(srv_.recipes());
                finish();
                return;
            }
        }
    }
    // crafting result refresh before interaction
    m.refreshCraftResult(srv_.recipes());
    (void)ClickLogic::apply(m, *self_, srv_.recipes(),
                            slot, button, mode, cursorItem_, io);
    if (m.type == MenuType::Crafting) m.refreshCraftResult(srv_.recipes());
    // Also notify MenuLogic of content change for result recomputation (e.g., Anvil)
    if (auto* logic2 = getMenuLogic(m.type)) logic2->onContentChanged(m, *self_);
    // Recompute dependent result slots before taking the authoritative menu
    // snapshot.  The previous order emitted a full snapshot and then a
    // separate slot packet, which allowed a newer revision to overtake that
    // slot packet on a concurrent send path.
    if (m.type == MenuType::Stonecutter) {
        ItemStack* inp = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* out = m.container ? &m.container[1] : &m.extraSlots[1];
        if (!inp->empty()) {
            const Recipe* r = srv_.recipes().findStonecutting(inp->itemId);
            if (r) *out = r->result;
            else *out = ItemStack::air();
        } else {
            *out = ItemStack::air();
        }
    }
    // vanilla: filled_map + paper -> filled_map copy (count 1); paper consumed on take, map preserved
    if (m.type == MenuType::CartographyTable) {
        ItemStack* mapIn = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* paperIn = m.container ? &m.container[1] : &m.extraSlots[1];
        ItemStack* out = m.container ? &m.container[2] : &m.extraSlots[2];
        bool canClone = false;
        if (!mapIn->empty() && !paperIn->empty()) {
            std::string mn = mapIn->name();
            std::string pn = paperIn->name();
            // allow filled_map + paper -> filled_map copy, also map + paper
            bool isMap = (mn == "minecraft:filled_map" || mn == "minecraft:map");
            bool isPaper = (pn == "minecraft:paper");
            canClone = isMap && isPaper;
        }
        if (canClone) {
            *out = *mapIn;
            out->count = 1;
            // preserve map_id etc via components already copied
        } else {
            *out = ItemStack::air();
        }
    }
    if (m.type == MenuType::Anvil) {
        std::string rename = m.anvilRename;
        int cost = CostCalculator::anvilCost(m.extraSlots[0], m.extraSlots[1], rename);
        WriteBuffer pb;
        pb.varint(m.windowId);
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cost < 0 ? 0 : cost));
        extraPackets.push_back(std::move(pb));
        bool tooExp = CostCalculator::isTooExpensive(cost, self_->gamemode==1);
        if (!m.extraSlots[0].empty() && cost > 0 && !tooExp) {
            m.extraSlots[2] = m.extraSlots[0];
            int nextCost = CostCalculator::nextRepairCost(m.extraSlots[0], m.extraSlots[1]);
            m.extraSlots[2].setRepairCost(nextCost);
            if(!rename.empty()) m.extraSlots[2].setCustomName(rename);
            if(!m.extraSlots[1].empty() && (m.extraSlots[1].hasEnchant("minecraft:protection") || m.extraSlots[1].hasEnchant("protection"))){
                int lvl = m.extraSlots[1].enchantLevel("protection");
                if(lvl==0) lvl = m.extraSlots[1].enchantLevel("minecraft:protection");
                if(lvl>0) ItemStack::addEnchant(m.extraSlots[2], "minecraft:protection", lvl);
            }
        } else {
            m.extraSlots[2] = ItemStack::air();
        }
    }
    if (m.type == MenuType::Brewing) {
        if (m.blockEntity && m.blockEntity->kind == BlockEntity::Kind::Brewing) {
            auto& b = m.blockEntity->brewing;
            for (int prop = 0; prop < 2; ++prop) {
                WriteBuffer pb;
                pb.varint(m.windowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(prop == 0 ? b.brewTime : b.fuel);
                extraPackets.push_back(std::move(pb));
            }
        }
    }
    if (cookingStationForMenu(m.type)) {
        if (m.blockEntity && m.blockEntity->kind == BlockEntity::Kind::Furnace) {
            auto& f = m.blockEntity->furnace;
            const int props[4] = {f.cookProgress, f.cookTotal, f.burnTicks, f.burnDuration};
            for (int prop = 0; prop < 4; ++prop) {
                WriteBuffer pb;
                pb.varint(m.windowId);
                pb.i16(static_cast<std::int16_t>(prop));
                pb.i16(static_cast<std::int16_t>(props[prop]));
                extraPackets.push_back(std::move(pb));
            }
        }
    }
    finish();
}
void Session::sendMenuContent(Menu& m) {
    WriteBuffer b;
    std::vector<WriteBuffer> propertyPackets;
    std::shared_ptr<Connection> connection = conn_;
    auto entityLock = lockMenuBlockEntity(m);
    // Keep the lock order consistent with handleMenuClick/onEnchantItem:
    // container first, then player.  The locks are used only to snapshot the
    // model; packet I/O happens after both are released.
    std::unique_lock playerLock(self_->stateMtx);
    std::unique_lock packetLock(self_->inventoryPacketMtx);
    // The caller may have released stateMtx before requesting this snapshot.
    // A close/reopen can therefore leave the owning handle alive while the
    // object is no longer the session's active screen.  Do not emit a stale
    // snapshot for that old screen.
    if (openMenu_.get() != &m)
        return;
    for (int i = 0; i < m.totalSlots(); ++i) {
        const ItemStack* stack = m.slotAt(i, self_->inv.data());
        if (stack && !stack->canWriteToProtocol()) {
            playerLock.unlock();
            if (entityLock.owns_lock()) entityLock.unlock();
            packetLock.unlock();
            closeOpenMenu(true);
            sendSystemText(
                "This menu contains item data this server cannot safely encode or edit.");
            return;
        }
    }
    if (!cursorItem_.canWriteToProtocol()) {
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        packetLock.unlock();
        closeOpenMenu(true);
        sendSystemText(
            "This menu contains item data this server cannot safely encode or edit.");
        return;
    }
    b.varint(m.windowId);
    b.varint(++self_->invStateId);
    b.varint(m.totalSlots());
    for (int i = 0; i < m.totalSlots(); ++i) {
        ItemStack* s = m.slotAt(i, self_->inv.data());
        if (s) s->write(b);
        else ItemStack::air().write(b);
    }
    cursorItem_.write(b);
    appendEnchantmentProperties(m, propertyPackets);
    playerLock.unlock();
    if (entityLock.owns_lock()) entityLock.unlock();
    connection->trySendPacket(pl::sc::ContainerSetContent, b);
    for (const auto& packet : propertyPackets)
        connection->trySendPacket(pl::sc::ContainerSetData, packet);
}
void Session::resyncOpenMenuAt(std::int8_t dimension,
                               std::int64_t blockKey) {
    std::shared_ptr<Menu> menu;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_ || !openMenu_->blockKey ||
            *openMenu_->blockKey != blockKey ||
            GameServer::canonicalDimension(self_->dimension) !=
                GameServer::canonicalDimension(dimension))
            return;
        menu = openMenu_;
    }

    const auto x = posKeyUnpackX(blockKey);
    const auto y = posKeyUnpackY(blockKey);
    const auto z = posKeyUnpackZ(blockKey);
    const auto* state = gen::blockByState(
        srv_.worldFor(dimension).getBlock(x, y, z));
    const auto owner = srv_.blockEntitiesFor(dimension).getShared(blockKey);
    bool stillSameContainer =
        state && state->name == menu->blockNameAtOpen &&
        owner == menu->blockEntityOwner;
    if (stillSameContainer && owner) {
        std::lock_guard entityLock(*owner->stateMtx);
        stillSameContainer = menu->blockEntityKindAtOpen &&
            owner->kind == *menu->blockEntityKindAtOpen;
    }
    if (!stillSameContainer) {
        closeOpenMenu(true);
        srv_.resendInventory(*self_);
        syncCursorItem();
        return;
    }
    sendMenuContent(*menu);
}

void Session::resyncOpenMenuForBlockChanges(
    std::int8_t dimension,
    const std::unordered_set<std::int64_t>& changedBlockKeys) {
    if (!self_ || changedBlockKeys.empty()) return;
    const auto targetDimension = GameServer::canonicalDimension(dimension);
    std::optional<std::int64_t> openBlockKey;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (openMenu_ && openMenu_->blockKey &&
            GameServer::canonicalDimension(self_->dimension) ==
                targetDimension)
            openBlockKey = *openMenu_->blockKey;
    }
    if (!openBlockKey || !changedBlockKeys.contains(*openBlockKey)) return;

    // resyncOpenMenuAt takes a fresh owning snapshot and revalidates the
    // current menu before querying world state or sending any packets.
    try {
        resyncOpenMenuAt(targetDimension, *openBlockKey);
    } catch (const std::exception& error) {
        std::fprintf(stderr,
                     "[cppfm] container resync at %lld failed: %s\n",
                     static_cast<long long>(*openBlockKey), error.what());
    } catch (...) {
        std::fprintf(stderr, "[cppfm] container resync at %lld failed\n",
                     static_cast<long long>(*openBlockKey));
    }
}

void Session::appendEnchantmentProperties(
    Menu& m, std::vector<WriteBuffer>& packets) {
    if (m.type != MenuType::Enchantment || !self_) return;

    const std::int8_t dimension = self_->dimension;
    int bookshelves = 0;
    if (m.blockKey) {
        const int bx = posKeyUnpackX(*m.blockKey);
        const int by = posKeyUnpackY(*m.blockKey);
        const int bz = posKeyUnpackZ(*m.blockKey);
        bookshelves = CostCalculator::countBookshelves(
            srv_.worldFor(dimension), bx, by, bz);
    }

    std::array<EnchantmentOffer, 3> offers{};
    std::int32_t enchantmentSeed = 0;
    if (auto* logic = getMenuLogic(MenuType::Enchantment)) {
        if (auto* enchantment = dynamic_cast<EnchantmentMenuLogic*>(logic)) {
            offers = enchantment->offers(m, *self_, bookshelves);
            enchantmentSeed = self_->enchantmentSeed;
        }
    }

    // EnchantmentScreenHandler's PropertyDelegate order in 1.21.4 is:
    // power[0..2], seed, enchantmentId[0..2], enchantmentLevel[0..2].
    auto appendProperty = [&](int property, int value) {
        WriteBuffer pb;
        pb.varint(m.windowId);
        pb.i16(static_cast<std::int16_t>(property));
        pb.i16(static_cast<std::int16_t>(value));
        packets.push_back(std::move(pb));
    };
    for (int i = 0; i < 3; ++i)
        appendProperty(i, offers[static_cast<std::size_t>(i)].levelCost);
    appendProperty(3, enchantmentSeed);
    for (int i = 0; i < 3; ++i)
        appendProperty(4 + i, offers[static_cast<std::size_t>(i)].enchantmentId);
    for (int i = 0; i < 3; ++i)
        appendProperty(7 + i, offers[static_cast<std::size_t>(i)].enchantmentLevel);
}

bool Session::menuItemsSerializable(Menu& menu) {
    auto entityLock = lockMenuBlockEntity(menu);
    std::lock_guard playerLock(self_->stateMtx);
    if (!cursorItem_.canWriteToProtocol()) return false;
    for (int i = 0; i < menu.totalSlots(); ++i) {
        const ItemStack* stack = menu.slotAt(i, self_->inv.data());
        if (stack && !stack->canWriteToProtocol()) return false;
    }
    return true;
}
void Session::closeOpenMenu(bool sendPacketToClient) {
    struct PendingDrop {
        std::int8_t dimension = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        ItemStack stack = ItemStack::air();
    };
    std::int32_t windowId = 0;
    std::vector<PendingDrop> drops;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_) {
            tradingVillager_ = -1;
            return;
        }
        windowId = openMenu_->windowId;
        // Keep the original return-to-inventory transaction, but record full
        // inventory drops separately so world/entity mutation cannot happen
        // while stateMtx is held.
        const auto returnToPlayerOrQueueDrop = [this, &drops](const ItemStack& stack) {
            if (stack.empty()) return;
            auto trial = self_->inv;
            if (insertCompleteInventoryStack(trial, stack)) {
                self_->inv = std::move(trial);
                return;
            }
            PendingDrop drop;
            drop.dimension = self_->dimension;
            drop.x = self_->x;
            drop.y = self_->y + 0.5;
            drop.z = self_->z;
            drop.stack = stack;
            drops.push_back(std::move(drop));
        };

        if (openMenu_->type == MenuType::Crafting) {
            for (auto& s : openMenu_->craftGrid) {
                if (s.empty()) continue;
                returnToPlayerOrQueueDrop(s);
                s = ItemStack::air();
            }
            openMenu_->craftResult = ItemStack::air();
        } else if (openMenu_->container == openMenu_->extraSlots) {
            // These handlers use Menu::extraSlots as a per-screen temporary
            // inventory.  Their result slot is derived, never a real item to
            // return, but every input must survive an explicit close or a
            // replacement by another screen.
            int inputSlots = 0;
            switch (openMenu_->type) {
            case MenuType::Enchantment: inputSlots = 2; break;
            case MenuType::Anvil: inputSlots = 2; break;
            case MenuType::Stonecutter: inputSlots = 1; break;
            case MenuType::Grindstone: inputSlots = 2; break;
            case MenuType::Smithing: inputSlots = 3; break;
            case MenuType::Beacon: inputSlots = 1; break;
            case MenuType::Loom: inputSlots = 3; break;
            case MenuType::CartographyTable: inputSlots = 2; break;
            case MenuType::Lectern: inputSlots = 1; break;
            default: break;
            }
            inputSlots = std::min(inputSlots, openMenu_->containerCount);
            for (int i = 0; i < inputSlots; ++i) {
                ItemStack& s = openMenu_->extraSlots[i];
                if (s.empty()) continue;
                returnToPlayerOrQueueDrop(s);
                s = ItemStack::air();
            }
            // Result slots are previews.  Clear all remaining temporary
            // slots so a stale result cannot be carried into a later screen.
            for (int i = inputSlots; i < openMenu_->containerCount; ++i)
                openMenu_->extraSlots[i] = ItemStack::air();
        }
        if (!cursorItem_.empty()) {
            returnToPlayerOrQueueDrop(cursorItem_);
            cursorItem_ = ItemStack::air();
        }
        openMenu_.reset();
        tradingVillager_ = -1;
    }
    for (const auto& drop : drops) {
        srv_.spawnItemDropFor(drop.dimension, drop.x, drop.y, drop.z,
                              drop.stack, 0, 0.1, 0);
    }
    if (sendPacketToClient) {
        WriteBuffer b;
        b.varint(windowId);
        conn_->trySendPacket(pl::sc::CloseContainer, b);
    }
}
void Session::onCloseContainer() {
    bool hadMenu = false;
    {
        std::lock_guard stateLock(self_->stateMtx);
        hadMenu = openMenu_ != nullptr;
    }
    closeOpenMenu(false);
    if (hadMenu) srv_.resendInventory(*self_);
    syncCursorItem();
}
} // namespace cppfm
