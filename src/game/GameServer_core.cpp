#include "GameServer.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "BehaviorTree.hpp"
#include "BehaviorTreeParser.hpp"
namespace cppfm {
using namespace proto;

std::atomic<bool> g_stopRequested{false};

void GameServer::invalidateJvmMob(const std::shared_ptr<MobEntity>& mob) {
    if (jvmRuntime_ && mob) jvmRuntime_->invalidateEntity(*mob);
}

namespace {

bool hasAdvancementManager(const Player& player) {
    std::lock_guard lock(player.stateMtx);
    return player.advancements != nullptr;
}

bool hasAdvancement(const Player& player, const std::string& id) {
    std::lock_guard lock(player.stateMtx);
    return player.advancements && player.advancements->has(id);
}

} // namespace

bool GameServer::isCurrentServerThread() const noexcept {
    std::lock_guard lock(serverThreadTasksMtx_);
    return serverThreadId_ != std::thread::id{} &&
           serverThreadId_ == std::this_thread::get_id();
}

void GameServer::claimServerThreadForBootstrap() noexcept {
    std::lock_guard lock(serverThreadTasksMtx_);
    serverThreadId_ = std::this_thread::get_id();
    serverThreadAccepting_ = true;
}

void GameServer::bindServerThread() noexcept {
    std::lock_guard lock(serverThreadTasksMtx_);
    serverThreadId_ = std::this_thread::get_id();
    serverThreadAccepting_ = running_.load(std::memory_order_acquire) &&
                             !shutdownStarted_.load(std::memory_order_acquire);
}

void GameServer::releaseServerThread() noexcept {
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        if (serverThreadId_ != std::this_thread::get_id()) return;
        serverThreadAccepting_ = false;
        serverThreadId_ = {};
    }
    cancelServerThreadTasks();
}

void GameServer::cancelServerThreadTasks() noexcept {
    std::deque<std::shared_ptr<ServerThreadTask>> pending;
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        serverThreadAccepting_ = false;
        pending.swap(serverThreadTasks_);
    }
    for (auto& request : pending) {
        if (!request) continue;
        auto expected = ServerThreadTask::State::Pending;
        if (request->state.compare_exchange_strong(
                expected, ServerThreadTask::State::Cancelled,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            request->waitCv.notify_all();
        }
    }
    serverThreadTaskIdleCv_.notify_all();
}

void GameServer::waitForServerThreadTasks() {
    if (isCurrentServerThread()) return;
    std::unique_lock lock(serverThreadTasksMtx_);
    serverThreadTaskIdleCv_.wait(lock, [this] {
        return activeServerThreadTasks_.load(std::memory_order_acquire) == 0 &&
               serverThreadTasks_.empty();
    });
}

bool GameServer::runOnServerThread(std::function<void()> task,
                                   std::chrono::milliseconds timeout) {
    if (!task) return false;

    std::shared_ptr<ServerThreadTask> request;
    bool runInline = false;
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        const bool owner = serverThreadId_ != std::thread::id{} &&
                           serverThreadId_ == std::this_thread::get_id();
        if (owner && serverThreadAccepting_ &&
            !shutdownStarted_.load(std::memory_order_acquire)) {
            runInline = true;
        } else if (!serverThreadAccepting_ ||
                   serverThreadId_ == std::thread::id{} ||
                   !running_.load(std::memory_order_acquire) ||
                   shutdownStarted_.load(std::memory_order_acquire) ||
                   serverThreadTasks_.size() >= kMaxServerThreadTaskQueue) {
            return false;
        } else {
            try {
                request = std::make_shared<ServerThreadTask>(std::move(task));
                serverThreadTasks_.push_back(request);
            } catch (...) {
                return false;
            }
        }
    }

    if (runInline) {
        try {
            task();
            return true;
        } catch (...) {
            return false;
        }
    }

    if (timeout < std::chrono::milliseconds::zero())
        timeout = std::chrono::milliseconds::zero();
    std::unique_lock waitLock(request->waitMtx);
    const bool finished = request->waitCv.wait_for(
        waitLock, timeout, [&request] {
            const auto state = request->state.load(std::memory_order_acquire);
            return state == ServerThreadTask::State::Completed ||
                   state == ServerThreadTask::State::Failed ||
                   state == ServerThreadTask::State::Cancelled;
        });
    if (!finished) {
        auto expected = ServerThreadTask::State::Pending;
        if (request->state.compare_exchange_strong(
                expected, ServerThreadTask::State::Cancelled,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            request->waitCv.notify_all();
        }
        // If the task was already Running, it remains owned by the server
        // thread and will finish there.  The caller deliberately fails closed
        // at its deadline instead of extending a JNI call indefinitely.
        return false;
    }
    return request->state.load(std::memory_order_acquire) ==
           ServerThreadTask::State::Completed;
}

void GameServer::startTickLoop() {
    if (tickThread_.joinable())
        throw std::logic_error("tick loop is already running");
    tickThread_ = std::thread([this] {
        bindServerThread();
        using clock = std::chrono::steady_clock;
        auto next = clock::now() + std::chrono::milliseconds(50);
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_until(next);
            next += std::chrono::milliseconds(50);
            if (!running_.load(std::memory_order_acquire)) break;
            ++tickNo_;
            try {
                tickOnce();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[cppfm] fatal tick %lld: %s\n",
                             static_cast<long long>(tickNo_), e.what());
                requestStop();
                break;
            } catch (...) {
                std::fprintf(stderr, "[cppfm] fatal tick %lld: unknown exception\n",
                             static_cast<long long>(tickNo_));
                requestStop();
                break;
            }
        }
        releaseServerThread();
    });
}
void GameServer::stopTickLoop() {
    if (tickThread_.joinable()) {
        if (tickThread_.get_id() == std::this_thread::get_id()) {
            // Keep ownership in the server.  An external stop/destructor can
            // join it after the tick callback returns; detaching here would
            // let the callback outlive the GameServer object.
            std::fprintf(stderr, "[cppfm] tick thread requested self-stop; deferred join\n");
            return;
        }
        std::fprintf(stderr, "[cppfm] joining tick thread\n");
        tickThread_.join();
        std::fprintf(stderr, "[cppfm] tick thread joined\n");
    }
}
void GameServer::runForever() {
    if (!platform::initializeSockets())
        throw std::runtime_error("could not initialize the platform socket layer");
    const auto fd = platform::createTcpSocket();
    if (!platform::isValid(fd)) {
        throw std::runtime_error("socket() failed: " +
                                 platform::socketErrorText(platform::lastSocketError()));
    }
    listenFd_.store(fd, std::memory_order_release);
    int one = 1;
    (void)platform::setSocketOption(
        fd, SOL_SOCKET, SO_REUSEADDR, &one,
        static_cast<platform::socket_length_t>(sizeof(one)));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg_.port);
    if (platform::bindSocket(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        listenFd_.exchange(platform::invalid_socket, std::memory_order_acq_rel);
        platform::closeSocket(fd);
        throw std::runtime_error("bind() failed: " +
                                 platform::socketErrorText(platform::lastSocketError()));
    }
    if (platform::listenSocket(fd, 64) != 0) {
        listenFd_.exchange(platform::invalid_socket, std::memory_order_acq_rel);
        platform::closeSocket(fd);
        throw std::runtime_error("listen() failed: " +
                                 platform::socketErrorText(platform::lastSocketError()));
    }
    running_.store(true, std::memory_order_release);
    try {
        startTickLoop();
        janitorThread_ = std::thread([this] {
            try {
                std::unique_lock lock(stopCvMtx_);
                while (running_.load(std::memory_order_acquire)) {
                    if (stopCv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                            return !running_.load(std::memory_order_acquire);
                        })) break;
                    lock.unlock();
                    try {
                        const auto now = nowMs();
                        for (auto& p : playersSnapshot()) {
                            if (!p) continue;
                            std::shared_ptr<Connection> connection;
                            enum class Action { None, CloseIdle, DisconnectTimeout,
                                                SendKeepAlive } action = Action::None;
                            std::int64_t keepAliveId = 0;
                            {
                                std::lock_guard playerLock(p->stateMtx);
                                if (!p->inPlay || !p->conn) continue;
                                connection = p->conn;
                                if (now - p->lastSeenMs > 60000) {
                                    action = Action::CloseIdle;
                                } else if (p->pendingKeepAlive != 0 &&
                                           now - p->lastSeenMs > 30000) {
                                    action = Action::DisconnectTimeout;
                                } else if (now - p->lastKeepAliveSentMs >= 10000) {
                                    keepAliveId = ++p->keepAliveCounter;
                                    p->pendingKeepAlive = keepAliveId;
                                    p->lastKeepAliveSentMs = now;
                                    action = Action::SendKeepAlive;
                                }
                            }
                            if (!connection) continue;
                            if (action == Action::CloseIdle) {
                                connection->close();
                            } else if (action == Action::DisconnectTimeout) {
                                WriteBuffer reason;
                                nbt::writeTextComponent(reason, "Timed out");
                                connection->trySendPacket(pl::sc::Disconnect, reason);
                                connection->close();
                            } else if (action == Action::SendKeepAlive) {
                                WriteBuffer b;
                                b.i64(keepAliveId);
                                connection->trySendPacket(pl::sc::KeepAlive, b);
                            }
                        }
                    } catch (const std::exception& e) {
                        std::fprintf(stderr, "[cppfm] janitor pass failed: %s\n", e.what());
                    } catch (...) {
                        std::fprintf(stderr, "[cppfm] janitor pass failed\n");
                    }
                    lock.lock();
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[cppfm] janitor stopped unexpectedly: %s\n", e.what());
                requestStop();
            } catch (...) {
                std::fprintf(stderr, "[cppfm] janitor stopped unexpectedly\n");
                requestStop();
            }
        });
        acceptLoop();
        requestStop();
    } catch (...) {
        requestStop();
        stopTickLoop();
        joinJanitorThread();
        if (const auto opened = listenFd_.exchange(platform::invalid_socket,
                                                   std::memory_order_acq_rel);
            platform::isValid(opened)) {
            platform::closeSocket(opened);
        }
        throw;
    }
}
void GameServer::acceptLoop() {
    while (running_.load(std::memory_order_acquire)) {
        sockaddr_in cli{};
        platform::socket_length_t cl = sizeof(cli);
        const auto listenFd = listenFd_.load(std::memory_order_acquire);
        if (!platform::isValid(listenFd)) break;
        const auto fd = platform::acceptSocket(
            listenFd, reinterpret_cast<sockaddr*>(&cli), &cl);
        if (!platform::isValid(fd)) {
            const int error = platform::lastSocketError();
            if (g_stopRequested || !running_.load(std::memory_order_acquire)) break;
            if (platform::isInterrupted(error)) continue;
            std::fprintf(stderr, "[cppfm] accept failed: %s\n",
                         platform::socketErrorText(error).c_str());
            break;
        }
        std::fprintf(stderr, "[cppfm] accepted fd=%llu\n",
                     static_cast<unsigned long long>(platform::socketNumber(fd)));
        if (!acceptGate_.allow(steadyNowMs())) {
            std::fprintf(stderr, "[cppfm] accept gate: refusing fd=%llu (rate)\n",
                         static_cast<unsigned long long>(platform::socketNumber(fd)));
            platform::closeSocket(fd);
            continue;
        }
        try {
            std::thread worker([this, fd] {
                std::shared_ptr<Connection> conn;
                bool registered = false;
                try {
                    conn = std::make_shared<Connection>(fd);
                    registerActiveConnection(conn);
                    registered = true;
                    conn->setNoDelay();
                    conn->setSendTimeout(15);
                    conn->setRecvTimeout(30);
                    conn->enableFloodBudget(true);
                    Session s(*this, conn);
                    s.run();
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "[cppfm] unhandled session exception: %s\n", e.what());
                } catch (...) {
                    std::fprintf(stderr, "[cppfm] unhandled session exception\n");
                }
                if (registered) {
                    try {
                        unregisterActiveConnection(conn);
                    } catch (...) {
                        std::fprintf(stderr, "[cppfm] could not unregister session connection\n");
                    }
                }
                if (conn) conn->close();
                else platform::closeSocket(fd);
            });
            if (!registerSessionThread(std::move(worker))) break;
        } catch (const std::exception& e) {
            platform::closeSocket(fd);
            std::fprintf(stderr, "[cppfm] could not create session worker: %s\n", e.what());
            requestStop();
            break;
        } catch (...) {
            platform::closeSocket(fd);
            std::fprintf(stderr, "[cppfm] could not create session worker\n");
            requestStop();
            break;
        }
    }
}

bool GameServer::registerSessionThread(std::thread worker) {
    bool rejected = false;
    {
        std::lock_guard lock(sessionThreadsMtx_);
        if (sessionThreadsStopping_) {
            rejected = true;
        } else {
            try {
                sessionThreads_.push_back(std::move(worker));
            } catch (...) {
                rejected = true;
            }
        }
    }
    // Never join a newly-created worker while holding the registry mutex: a
    // session's final unregisterActiveConnection() needs an unrelated lock
    // today, but keeping lifecycle locks independent prevents a future
    // shutdown callback from turning this into a lock inversion.
    if (rejected && worker.joinable()) worker.join();
    return !rejected;
}

void GameServer::joinSessionThreads() {
    std::vector<std::thread> workers;
    const auto self = std::this_thread::get_id();
    {
        std::lock_guard lock(sessionThreadsMtx_);
        sessionThreadsStopping_ = true;
        for (auto it = sessionThreads_.begin(); it != sessionThreads_.end();) {
            if (it->joinable() && it->get_id() == self) {
                ++it;
                continue;
            }
            workers.push_back(std::move(*it));
            it = sessionThreads_.erase(it);
        }
    }
    for (auto& worker : workers) {
        if (!worker.joinable()) continue;
        worker.join();
    }
    std::lock_guard lock(activeConnectionsMtx_);
    activeConnections_.clear();
}

void GameServer::registerActiveConnection(const std::shared_ptr<Connection>& connection) {
    std::lock_guard lock(activeConnectionsMtx_);
    activeConnections_.push_back(connection);
}

void GameServer::unregisterActiveConnection(const std::shared_ptr<Connection>& connection) {
    std::lock_guard lock(activeConnectionsMtx_);
    activeConnections_.erase(
        std::remove(activeConnections_.begin(), activeConnections_.end(), connection),
        activeConnections_.end());
}

void GameServer::stopClientConnections() {
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard lock(activeConnectionsMtx_);
        connections = activeConnections_;
    }
    for (auto& connection : connections) {
        if (connection) connection->abort();
    }
}

void GameServer::joinJanitorThread() {
    if (!janitorThread_.joinable()) return;
    if (janitorThread_.get_id() == std::this_thread::get_id()) {
        // See stopTickLoop(): retain the handle for an external join.
        std::fprintf(stderr, "[cppfm] janitor requested self-stop; deferred join\n");
        return;
    }
    janitorThread_.join();
}
void GameServer::broadcastDigStage(Player& p, std::int8_t stage) {
    std::int32_t entityId = 0;
    std::int32_t digX = 0, digY = 0, digZ = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        entityId = p.entityId;
        digX = p.digX;
        digY = p.digY;
        digZ = p.digZ;
        dimension = p.dimension;
    }
    WriteBuffer b;
    b.varint(entityId);
    b.position(digX, digY, digZ);
    b.i8(stage);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::BlockBreakAnimation, b);
}
void GameServer::sendSetHealth(Player& p) {
    std::shared_ptr<Connection> connection;
    WriteBuffer b;
    {
        std::lock_guard playerLock(p.stateMtx);
        connection = p.conn;
        if (!connection) return;
        b.f32(p.health);
        b.varint(p.food);
        b.f32(p.saturation);
    }
    connection->trySendPacket(pl::sc::SetHealth, b);
}
void GameServer::addHungerExhaustion(Player& p, float amount) {
    HungerManager::addExhaustion(p, amount);
}
void GameServer::addFoodAndSaturation(Player& p, int food, float sat) {
    HungerManager::addFoodAndSaturation(p, food, sat);
    sendSetHealth(p);
}
void GameServer::handleFoodConsume(Player& p, const std::string& itemName) {
    HungerManager::handleFoodConsume(p, itemName, *this);
}
void GameServer::broadcastPlayerChat(Player& sender, const std::string& message, int64_t timestamp) {
    std::array<std::uint8_t, 16> uuid{};
    std::string name;
    {
        std::lock_guard playerLock(sender.stateMtx);
        uuid = sender.uuid;
        name = sender.name;
    }
    WriteBuffer b;
    b.uuid(uuid.data());
    b.varint(0);
    b.boolean(false);
    b.string(message);
    b.i64(timestamp);
    b.i64(0);
    b.varint(0);
    b.boolean(false);
    b.varint(0);
    b.varint(0);
    nbt::writeTextComponent(b, name);
    b.boolean(false);
    broadcastPacketExcept(nullptr, proto::pl::sc::PlayerChat, b);
}
void GameServer::spawnMob(MobKind kind, double x, double y, double z) {
    spawnMobFor(0, kind, x, y, z);
}
void GameServer::spawnMobFor(std::int8_t dimension, MobKind kind, double x,
                             double y, double z) {
    auto mob = std::make_shared<MobEntity>();
    mob->entityId = nextEntityId();
    mob->dimension = canonicalDimension(dimension);
    mob->kind = kind;
    const auto& stats = mobStats(kind);
    mob->health = stats.maxHealth;
    if (auto *def = entityDataLoader_.get(MobEntity::kindName(kind))) {
        if (def->max_health > 0) mob->health = def->max_health;
        if (!def->equipment.empty()) {
            for (auto &kv : def->equipment) {
                int slot = kv.first;
                auto it = gen::itemIdByName().find(kv.second);
                if (it != gen::itemIdByName().end() && slot>=0 && slot<6) mob->equipment[slot] = ItemStack::of(it->second, 1);
            }
        }
    }
    if (kind==MobKind::Slime || kind==MobKind::MagmaCube) {
        mob->health = MobEntity::slimeHealthForSize(mob->slimeSize);
    }
    if (kind==MobKind::Horse) {
        std::uint64_t hseed = cfg_.seed ^ (static_cast<std::uint64_t>(static_cast<std::int32_t>(std::floor(x)))<<32)
            ^ static_cast<std::uint64_t>(static_cast<std::int32_t>(std::floor(z))) ^ (static_cast<std::uint64_t>(mob->entityId)*0x9E3779B97F4A7C15ULL);
        mob->applyHorseStats(MobEntity::randomizeHorseStats(hseed));
    }
    if (kind==MobKind::Villager) {
        mob->villagerData.type = static_cast<VillagerData::Type>(nextRandom()%7);
        if (nextRandom() % 12 == 0) mob->villagerData.profession = VillagerData::NITWIT;
        else mob->villagerData.profession = VillagerData::FARMER;
        mob->villagerData.level = 1;
        mob->villagerLevel = 1;
        mob->villagerXp = 0;
        mob->villagerRestocksToday = 0;
        mob->villagerLastRestockDay = -1;
        mob->restockUntil = 0;
    }
    if (kind==MobKind::Sheep) {
        int r = nextRandom() % 1000;
        if (r < 818) mob->woolColor = 0; // white 81.8%
        else if (r < 868) mob->woolColor = 15; // black 5%
        else if (r < 918) mob->woolColor = 7; // gray 5%
        else if (r < 968) mob->woolColor = 8; // light_gray 5%
        else if (r < 998) mob->woolColor = 12; // brown 3%
        else mob->woolColor = 6; // pink 0.2% (~0.164% vanilla)
        // set sheared metadata false initially
    }
    mob->x = x; mob->y = y; mob->z = z;
    mob->lastSeenMs = nowMs();
    if (jvmRuntime_ && !jvmRuntime_->onMobSpawn(*mob, x, y, z)) return;
    {
        std::lock_guard lk(entsMtx_);
        mobs_.push_back(mob);
    }
    broadcastMobSpawn(*mob);
    if (MobEntity::isBoss(kind) && bossAI_) {
        // BossAI also emits the boss-bar packet.  Give it a detached state
        // snapshot so a callback cannot race the published mob (or retain a
        // live reference while sending).  Merge the initialization fields
        // used by the current Wither/Dragon implementations afterwards.
        MobEntity callbackState;
        {
            std::lock_guard entityLock(*mob->stateMtx);
            callbackState = *mob;
        }
        callbackState.stateMtx = std::make_shared<std::recursive_mutex>();
        bossAI_->onSpawn(callbackState);
        {
            std::lock_guard entityLock(*mob->stateMtx);
            if (kind == MobKind::Wither)
                mob->witherSkullCooldown = callbackState.witherSkullCooldown;
            else if (kind == MobKind::EnderDragon) {
                mob->dragonPhase = callbackState.dragonPhase;
                mob->dragonPhaseUntil = callbackState.dragonPhaseUntil;
            }
        }
    }
}
void GameServer::broadcastMobSpawn(const MobEntity& mob) {
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, &mob] { broadcastMobSpawn(mob); });
        return;
    }
    MobEntity snapshot;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        snapshot = mob;
    }
    WriteBuffer b;
    b.varint(snapshot.entityId);
    static std::uint8_t zero[16] = {};
    b.uuid(zero);
    b.varint(static_cast<std::int32_t>(MobEntity::typeId(snapshot.kind)));
    b.f64(snapshot.x); b.f64(snapshot.y); b.f64(snapshot.z);
    b.i8(0); b.i8(0); b.i8(0);
    b.varint(0); b.i16(0); b.i16(0); b.i16(0);
    broadcastPacketExceptInDimension(snapshot.dimension, nullptr,
                                     pl::sc::SpawnEntity, b);
    sendEquipment(snapshot);
    // JUMP_STRENGTH) via UpdateAttributes 0x7C so clients see 15-30 HP, not the static default.
    if (snapshot.kind == MobKind::Horse) {
        AttributeManager am;
        am.setBase(Attribute::MAX_HEALTH, snapshot.horseMaxHealth);
        am.setBase(Attribute::MOVEMENT_SPEED, snapshot.horseMoveSpeed);
        am.setBase(Attribute::JUMP_STRENGTH, snapshot.horseJumpStrength);
        WriteBuffer ab;
        am.writeUpdate(ab, snapshot.entityId);
        broadcastPacketExceptInDimension(snapshot.dimension, nullptr,
                                         pl::sc::UpdateAttributes, ab);
    }
}
void GameServer::broadcastSetPassengersEmpty(std::int32_t vehicleId) {
    std::int8_t dimension = 0;
    {
        const auto mobs = mobsSnapshot();
        for (const auto& m : mobs) {
            if (!m) continue;
            std::lock_guard entityLock(*m->stateMtx);
            if (m->entityId == vehicleId) {
                dimension = m->dimension;
                break;
            }
        }
    }
    broadcastSetPassengersEmptyFor(dimension, vehicleId);
}
void GameServer::broadcastSetPassengersEmptyFor(std::int8_t dimension,
                                                std::int32_t vehicleId) {
    WriteBuffer b;
    b.varint(vehicleId);
    b.varint(0);
    broadcastPacketExceptInDimension(dimension, nullptr,
                                     proto::pl::sc::SetPassengers, b);
}
std::shared_ptr<GameServer::MobAiEntry>
GameServer::aiFor(const std::shared_ptr<MobEntity>& m) {
    if (!m) return nullptr;
    std::int32_t entityId = 0;
    MobKind kind = MobKind::Pig;
    {
        std::lock_guard entityLock(*m->stateMtx);
        entityId = m->entityId;
        kind = m->kind;
    }
    {
        std::lock_guard lock(mobAiMtx_);
        auto it = mobAi_.find(entityId);
        if (it != mobAi_.end()) return it->second;
    }

    // Build a new entry outside the map lock.  Tree construction is local and
    // can be relatively expensive; more importantly, no callback should ever
    // run while mobAiMtx_ is held.
    auto entry = std::make_shared<MobAiEntry>();
    entry->brain = std::make_unique<Brain>();
    entry->ctx = std::make_unique<AiContext>();
    if (auto* def = entityDataLoader_.get(MobEntity::kindName(kind))) {
        auto fresh = BehaviorTreeParser::parse(*def);
        if (fresh) entry->brain->setBehaviorTree(std::move(fresh));
    }
    if (!entry->brain->hasBehaviorTree()) {
        if (kind == MobKind::Enderman)
            entry->brain->setBehaviorTree(buildEndermanTree());
        else if (kind == MobKind::Wither)
            entry->brain->setBehaviorTree(buildWitherTree());
        else if (kind == MobKind::EnderDragon)
            entry->brain->setBehaviorTree(buildDragonTree());
    }

    // A concurrent first lookup may have won the race while the tree was
    // being constructed.  Reuse the canonical entry in that case.
    std::lock_guard lock(mobAiMtx_);
    auto it = mobAi_.find(entityId);
    if (it == mobAi_.end())
        it = mobAi_.emplace(entityId, std::move(entry)).first;
    return it->second;
}

void GameServer::eraseMobAi(std::int32_t entityId) {
    std::lock_guard lock(mobAiMtx_);
    mobAi_.erase(entityId);
}

void GameServer::noteMobHurt(std::int32_t entityId,
                             std::int32_t attackerEntityId) {
    std::shared_ptr<MobAiEntry> entry;
    {
        std::lock_guard lock(mobAiMtx_);
        auto it = mobAi_.find(entityId);
        if (it != mobAi_.end()) entry = it->second;
    }
    if (!entry || !entry->ctx) return;
    entry->ctx->lastHurtTick.store(tickNo_, std::memory_order_release);
    entry->ctx->lastHurtByEntityId.store(attackerEntityId,
                                         std::memory_order_release);
}
std::shared_ptr<MobEntity> GameServer::findLovePartner(const MobEntity& seeker) {
    if (mobStateLockOwnedByCurrentThread()) {
        std::shared_ptr<MobEntity> result;
        runWithoutMobStateLock([this, &result, &seeker] {
            result = findLovePartner(seeker);
        });
        return result;
    }
    std::int8_t seekerDimension = 0;
    MobKind seekerKind = MobKind::Pig;
    double seekerX = 0.0, seekerZ = 0.0;
    {
        std::lock_guard seekerLock(*seeker.stateMtx);
        seekerDimension = seeker.dimension;
        seekerKind = seeker.kind;
        seekerX = seeker.x;
        seekerZ = seeker.z;
    }
    for (auto& other : mobsSnapshot()) {
        if (!other || other.get() == &seeker) continue;
        std::lock_guard entityLock(*other->stateMtx);
        if (canonicalDimension(other->dimension) !=
                canonicalDimension(seekerDimension) ||
            other->kind != seekerKind || !other->inLove ||
            MobEntity::isBaby(*other))
            continue;
        const double dx = other->x - seekerX, dz = other->z - seekerZ;
        if (dx * dx + dz * dz < 64) return other;
    }
    return nullptr;
}
void GameServer::initPlayerProgress(Player& p) {
    std::string hex;
    {
        std::lock_guard playerLock(p.stateMtx);
        hex = uuidToHex(p.uuid);
        p.stats = std::make_unique<StatsManager>(cfg_.worldDir);
        p.advancements = std::make_unique<AdvancementManager>(hex, cfg_.worldDir);
        p.stats->load(hex);
        p.advancements->load();
        p.joinTick = tickNo_;
    }
    grantAdvancement(p, "cppfm:root");
}
void GameServer::savePlayerProgress(Player& p) {
    std::lock_guard playerLock(p.stateMtx);
    if (!p.stats || !p.advancements) return;
    if (p.joinTick) {
        const std::int64_t ticks = tickNo_ - p.joinTick;
        p.stats->add("minecraft:custom|minecraft:play_time", ticks);
        p.joinTick = tickNo_;
    }
    p.stats->save(uuidToHex(p.uuid));
    p.advancements->save();
}
void GameServer::sendAdvancementsTo(Player& p, bool reset) {
    const auto merged = getMergedAdvancements();
    std::shared_ptr<Connection> connection;
    WriteBuffer b;
    {
        std::lock_guard playerLock(p.stateMtx);
        connection = p.conn;
        if (!connection || !p.advancements) return;
        writeAdvancementsPacket(b, reset, merged,
            [&](const std::string& id) {
                return p.advancements->has(id);
            });
    }
    connection->trySendPacket(pl::sc::UpdateAdvancements, b);
}
void GameServer::grantAdvancement(Player& p, const std::string& id) {
    bool granted = false;
    {
        std::lock_guard playerLock(p.stateMtx);
        if (!p.advancements) return;
        granted = p.advancements->grant(id);
    }
    if (!granted) return;
    sendAdvancementsTo(p, false);
    std::string tab = id;
    auto slash = tab.find("/");
    if (slash!=std::string::npos) tab = tab.substr(0, slash);
    if (tab.find(":")==std::string::npos) tab = "minecraft:" + tab;
    // special: cppfm:root -> minecraft:story/root tab, vanilla story root
    if (id=="cppfm:root") tab = "minecraft:story/root";
    else if (tab=="minecraft:story" || tab=="minecraft:adventure" || tab=="minecraft:nether" || tab=="minecraft:end" || tab=="minecraft:husbandry") { /* keep tab */ }
    else if (id.rfind("minecraft:",0)==0) tab = id;
    else tab = id;
    sendSelectAdvancementTab(p, tab);
}
std::vector<AdvancementDefOwned> GameServer::getMergedAdvancements() {
    std::lock_guard lk(advMergeMtx_);
    size_t cur = datapackManager_.advancements.size();
    if (!cachedMergedAdv_.empty() && cachedAdvRawSize_ == cur) return cachedMergedAdv_;
    cachedMergedAdv_ = mergedAdvancements(datapackManager_.advancements);
    cachedAdvRawSize_ = cur;
    return cachedMergedAdv_;
}
PredicateContext GameServer::basePredicateContext(Player& p) {
    PredicateContext ctx;
    std::int8_t dimension = 0;
    std::string name;
    std::string heldItemName;
    double x = 0.0, y = 0.0, z = 0.0;
    {
        std::lock_guard playerLock(p.stateMtx);
        dimension = p.dimension;
        name = p.name;
        if (p.heldSlot>=0 && p.heldSlot<9) {
            const auto& held = p.inv[36+p.heldSlot];
            if (!held.empty()) heldItemName = held.name();
        }
        x = p.x; y = p.y; z = p.z;
    }
    ctx.world = &worldFor(dimension);
    ctx.gamerules = &gamerules_;
    ctx.player = &p;
    ctx.playerName = std::move(name);
    ctx.heldItemName = std::move(heldItemName);
    ctx.scoreboard = &scoreboard;
    ctx.x = static_cast<int32_t>(x);
    ctx.y = static_cast<int32_t>(y);
    ctx.z = static_cast<int32_t>(z);
    return ctx;
}
void GameServer::evaluateTickAdvancements(Player& p) {
    if (!hasAdvancementManager(p)) return;
    auto merged = getMergedAdvancements();
    for (auto& adv : merged) {
        if (hasAdvancement(p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger == "minecraft:tick" || tr.trigger == "tick") {
                if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                    PredicateContext ctx = basePredicateContext(p);
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) continue;
                }
                grantAdvancement(p, adv.id);
                break;
            }
        }
    }
}
void GameServer::evaluateInventoryChanged(Player& p, const ItemStack& s) {
    if (!hasAdvancementManager(p) || s.empty()) return;
    std::string itemName = s.name();
    // normalize itemName for tag lookup
    std::string normHave = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
    auto norm = [](const std::string& s2){ return s2.find(':')==std::string::npos ? "minecraft:"+s2 : s2; };
    auto hasTagItem = [&](const std::string& tag, const std::string& haveNorm)->bool{
        // tag is like "minecraft:logs" (without #)
        std::string t = tag.find(':')==std::string::npos ? "minecraft:"+tag : tag;
        auto it = datapackManager_.tagManager.itemTags.find(t);
        if(it==datapackManager_.tagManager.itemTags.end()){
            // also try without minecraft: prefix
            auto it2 = datapackManager_.tagManager.itemTags.find(tag);
            if(it2==datapackManager_.tagManager.itemTags.end()) return false;
            it = it2;
        }
        auto iidIt = gen::itemIdByName().find(haveNorm);
        if(iidIt==gen::itemIdByName().end()){
            // fallback string contains
            return false;
        }
        return it->second.count(iidIt->second)>0;
    };
    auto merged = getMergedAdvancements();
    for (auto& adv : merged) {
        if (hasAdvancement(p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:inventory_changed" && tr.trigger != "inventory_changed") continue;
            bool match = false;
            if (tr.conditions.isNull() || tr.conditions.isObj()==false) {
                match = true;
            } else {
                if (auto* items = tr.conditions.find("items")) {
                    if (items->isArr()) {
                        for (auto& it : items->arr) {
                            std::string want;
                            if (it.isStr()) want = it.asStr();
                            else if (it.isObj()) {
                                if (auto* in = it.find("items")) {
                                    if (in->isStr()) want = in->asStr();
                                    else if (in->isArr() && !in->arr.empty() && in->arr[0].isStr()) want = in->arr[0].asStr();
                                } else if (auto* it2 = it.find("item")) want = it2->asStr();
                                else if (auto* id2 = it.find("id")) want = id2->asStr();
                                else if (auto* tag = it.find("tag")) {
                                    std::string tagStr=tag->asStr();
                                    if(!tagStr.empty() && tagStr[0]=='#') tagStr=tagStr.substr(1);
                                    if(hasTagItem(tagStr, normHave)) { match=true; break; }
                                    continue;
                                }
                            }
                            if (!want.empty()){
                                if(want[0]=='#'){
                                    std::string tag = want.substr(1);
                                    if(hasTagItem(tag, normHave)) { match = true; break; }
                                } else if (norm(want)==normHave) { match = true; break; }
                            }
                        }
                    } else if (items->isStr()) {
                        std::string want=items->asStr();
                        if(want[0]=='#'){
                            if(hasTagItem(want.substr(1), normHave)) match = true;
                        } else if (norm(want)==normHave) match = true;
                    } else if (items->isObj()) {
                        if(auto* tag = items->find("tag")){
                            std::string tagStr=tag->asStr();
                            if(!tagStr.empty() && tagStr[0]=='#') tagStr=tagStr.substr(1);
                            if(hasTagItem(tagStr, normHave)) match=true;
                        } else if(auto* inn = items->find("items")){
                            if(inn->isStr() && norm(inn->asStr())==normHave) match=true;
                            else if(inn->isArr()) for(auto& e: inn->arr) if(e.isStr() && norm(e.asStr())==normHave) { match=true; break; }
                        }
                    }
                } else {
                    match = true;
                }
            }
            if (match) {
                if (!tr.conditions.isNull() && tr.conditions.isObj() && tr.conditions.find("condition")) {
                    PredicateContext ctx = basePredicateContext(p);
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) match = false;
                }
                if (match) { grantAdvancement(p, adv.id); break; }
            }
        }
    }
}
void GameServer::evaluatePlayerKilledEntity(Player& p, MobKind kind) {
    if (!hasAdvancementManager(p)) return;
    std::string killed = MobEntity::kindName(kind);
    auto merged = getMergedAdvancements();
    // temporary victim entity for predicate context
    MobEntity victimTmp; victimTmp.kind = kind;
    for (auto& adv : merged) {
        if (hasAdvancement(p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:player_killed_entity" && tr.trigger != "player_killed_entity") continue;
            bool match = false;
            if (tr.conditions.isNull()) match = true;
            else {
                if (auto* ent = tr.conditions.find("entity")) {
                    if (ent->isArr()) {
                        for (auto& e : ent->arr) if (e.isObj()) if (auto* tp = e.find("type")) if (tp->asStr()==killed) match=true;
                    } else if (ent->isObj()) {
                        if (auto* tp = ent->find("type")) {
                            if (tp->asStr()==killed) match=true;
                        } else match = true;
                    } else if (ent->isStr()) {
                        if (ent->asStr()==killed) match=true;
                    }
                } else if (auto* pred = tr.conditions.find("predicate")) {
                    if (auto* tp = pred->find("type")) { if (tp->asStr()==killed) match=true; else match=true; }
                } else {
                    match = true;
                }
                if (!tr.conditions.find("entity") && !tr.conditions.find("predicate")) match = true;
                if (match && tr.conditions.isObj() && tr.conditions.find("condition")) {
                    PredicateContext ctx = basePredicateContext(p);
                    ctx.entity = &victimTmp;
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) match = false;
                }
            }
            if (match) { grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onBlockMined(Player& p, std::uint16_t oldState) {
    {
        std::lock_guard playerLock(p.stateMtx);
        if (!p.stats) return;
    }
    static thread_local std::unordered_map<std::uint32_t, std::string> inv;
    if (inv.empty())
        for (auto& [n, s] : gen::kBlocks) inv.emplace(s, std::string(n));
    auto it = inv.find(oldState);
    const std::string name = it != inv.end() ? it->second : "minecraft:air";
    {
        std::lock_guard playerLock(p.stateMtx);
        if (!p.stats) return;
        p.stats->add("minecraft:mined|" + name);
    }
    if (name == "minecraft:oak_log") grantAdvancement(p, "cppfm:wood");
    if (name == "minecraft:stone") { /* stone age analog */ }
    ItemStack dummy = ItemStack::ofName(name,1);
    if (!dummy.empty()) evaluateInventoryChanged(p, dummy);
}
void GameServer::onItemObtained(Player& p, const ItemStack& s,
                                const char* how) {
    ItemStack stack;
    std::string n;
    {
        std::lock_guard playerLock(p.stateMtx);
        if (!p.stats) return;
        stack = s;
        n = stack.name();
        p.stats->add(std::string("minecraft:") + how + "|" + n,
                     stack.count);
    }
    if (how == std::string("crafted")) {
        if (n == "minecraft:crafting_table") grantAdvancement(p, "cppfm:bench");
        if (n == "minecraft:stone_pickaxe") grantAdvancement(p, "cppfm:tools");
    }
    if (how == std::string("smelted")) {
        if (n == "minecraft:iron_ingot") grantAdvancement(p, "cppfm:iron");
        grantAdvancement(p, "cppfm:cook");
    }
    if (n == "minecraft:diamond") grantAdvancement(p, "cppfm:diamonds");
    evaluateInventoryChanged(p, stack);
}
void GameServer::onMobKilledBy(Player& p, MobKind kind) {
    {
        std::lock_guard playerLock(p.stateMtx);
        if (!p.stats) return;
        p.stats->add(std::string("minecraft:killed|") +
                     MobEntity::kindName(kind));
    }
    if (MobEntity::isHostile(kind)) grantAdvancement(p, "cppfm:hunter");
    evaluatePlayerKilledEntity(p, kind);
}
void GameServer::evaluateLocationTrigger(Player& p) {
    if (!hasAdvancementManager(p)) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx = basePredicateContext(p);
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (hasAdvancement(p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:location" && tr.trigger != "location") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                // otherwise treat as location predicate {location:{biome,...}}
                if (tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                } else {
                    // try location_check wrapping
                    json::Value wrapped = json::Value::object();
                    wrapped.set("condition", json::Value::ofString("minecraft:location_check"));
                    wrapped.set("predicate", tr.conditions);
                    bool foundLocation = tr.conditions.find("location") != nullptr;
                    if (foundLocation) {
                        if (!datapackManager_.evaluatePredicateValue(wrapped, ctx)) ok = false;
                    } else {
                        // generic predicate evaluation
                        if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                    }
                }
            }
            if (ok) { grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onPlacedBlock(Player& p, int x, int y, int z, std::uint16_t state) {
    if (!hasAdvancementManager(p)) return;
    std::string placedName;
    if (auto* bd = gen::blockByState(state)) placedName = bd->name;
    if (placedName.empty()) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx = basePredicateContext(p);
    ctx.x = x; ctx.y = y; ctx.z = z;
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (hasAdvancement(p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:placed_block" && tr.trigger != "placed_block") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* blk = tr.conditions.find("block")) {
                    std::string want = blk->asStr();
                    if (!want.empty() && want[0] != '#') {
                        std::string wantN = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                        std::string haveN = placedName.find(':')==std::string::npos ? "minecraft:"+placedName : placedName;
                        if (wantN != haveN) ok = false;
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                } else if (ok && !tr.conditions.isNull()) {
                    // evaluate other predicates like location_check inside placed_block try generic evaluation but ignore block which
                    // already checked only evaluate if condition key present
                }
            }
            if (ok) { grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onConsumeItem(Player& p, const ItemStack& stack) {
    if (!hasAdvancementManager(p) || stack.empty()) return;
    std::string itemName = stack.name();
    auto merged = getMergedAdvancements();
    PredicateContext ctx = basePredicateContext(p);
    ctx.heldItemName = itemName;
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (hasAdvancement(p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger != "minecraft:consume_item" && tr.trigger != "consume_item") continue;
            bool ok = false;
            if (tr.conditions.isNull()) ok = true;
            else if (tr.conditions.isObj()) {
                if (auto* item = tr.conditions.find("item")) {
                    std::vector<std::string> wants;
                    if (item->isStr()) wants.push_back(item->asStr());
                    else if (item->isObj()) {
                        if (auto* items = item->find("items")) {
                            if (items->isStr()) wants.push_back(items->asStr());
                            else if (items->isArr()) for (auto& v : items->arr) if (v.isStr()) wants.push_back(v.asStr());
                        } else if (auto* it = item->find("item")) {
                            if (it->isStr()) wants.push_back(it->asStr());
                        } else if (auto* id = item->find("id")) {
                            if (id->isStr()) wants.push_back(id->asStr());
                        }
                    } else if (item->isArr()) {
                        for (auto& v : item->arr) if (v.isStr()) wants.push_back(v.asStr());
                    }
                    if (wants.empty()) ok = true;
                    else for (auto& w : wants) {
                        std::string wn = w.find(':')==std::string::npos ? "minecraft:"+w : w;
                        std::string hn = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
                        if (wn == hn) { ok = true; break; }
                    }
                } else {
                    ok = true;
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok = false;
                }
            } else ok = true;
            if (ok) { grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onBredAnimals(Player* p) {
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, p] { onBredAnimals(p); });
        return;
    }
    if (!p || !hasAdvancementManager(*p)) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx = basePredicateContext(*p);
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (hasAdvancement(*p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:bred_animals" && tr.trigger!="bred_animals") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj() && tr.conditions.find("condition")) {
                if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onEnterBlock(Player* p, int x, int y, int z) {
    if (!p || !hasAdvancementManager(*p)) return;
    auto merged = getMergedAdvancements();
    PredicateContext ctx = basePredicateContext(*p);
    ctx.x = x; ctx.y = y; ctx.z = z;
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(p->stateMtx);
        dimension = p->dimension;
    }
    std::uint16_t st = worldFor(dimension).getBlock(x,y,z);
    std::string haveBlock;
    if (auto* bd = gen::blockByState(st)) haveBlock = bd->name;
    else haveBlock = "minecraft:air";
    for (auto& adv : merged) {
        if (hasAdvancement(*p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:enter_block" && tr.trigger!="enter_block") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* blk = tr.conditions.find("block")) {
                    std::string want = blk->asStr();
                    if (!want.empty() && want[0]!='#') {
                        std::string wn = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                        std::string hn = haveBlock.find(':')==std::string::npos ? "minecraft:"+haveBlock : haveBlock;
                        if (wn != hn) ok=false;
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
                }
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onItemUsedOnBlock(Player* p, int x, int y, int z, const ItemStack& item) {
    if (!p || !hasAdvancementManager(*p)) return;
    std::string itemName = item.name();
    if (itemName.empty()) itemName = "minecraft:air";
    auto merged = getMergedAdvancements();
    PredicateContext ctx = basePredicateContext(*p);
    ctx.heldItemName = itemName;
    ctx.x = x; ctx.y = y; ctx.z = z;
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (hasAdvancement(*p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:item_used_on_block" && tr.trigger!="item_used_on_block") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* it = tr.conditions.find("item")) {
                    std::vector<std::string> wants;
                    if (it->isStr()) wants.push_back(it->asStr());
                    else if (it->isObj()) {
                        if (auto* items = it->find("items")) {
                            if (items->isStr()) wants.push_back(items->asStr());
                            else if (items->isArr()) for (auto &v: items->arr) if (v.isStr()) wants.push_back(v.asStr());
                        } else if (auto* id = it->find("id")) wants.push_back(id->asStr());
                    }
                    if (!wants.empty()) {
                        bool any=false;
                        for (auto &w: wants) {
                            std::string wn = w.find(':')==std::string::npos ? "minecraft:"+w : w;
                            std::string hn = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
                            if (wn==hn) { any=true; break; }
                        }
                        if (!any) ok=false;
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
                }
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onEffectsChanged(Player* p) {
    if (!p || !hasAdvancementManager(*p)) return;
    std::vector<EffectInstance> effects;
    {
        std::lock_guard playerLock(p->stateMtx);
        effects = p->effects;
    }
    auto merged = getMergedAdvancements();
    PredicateContext ctx = basePredicateContext(*p);
    ctx.dayTime = dayTime();
    ctx.raining = raining();
    ctx.thundering = thundering();
    for (auto& adv : merged) {
        if (hasAdvancement(*p, adv.id)) continue;
        for (auto& tr : adv.triggers) {
            if (tr.trigger!="minecraft:effects_changed" && tr.trigger!="effects_changed") continue;
            bool ok = true;
            if (!tr.conditions.isNull() && tr.conditions.isObj()) {
                if (auto* effs = tr.conditions.find("effects")) {
                    if (effs->isArr()) {
                        for (auto &e : effs->arr) if (e.isObj()) {
                            if (auto* eff = e.find("effect")) {
                                std::string want = eff->asStr();
                                bool found=false;
                                for (auto &pe: effects) {
                                    std::string have = effects::nameOf(pe.type);
                                    if (want==have) { found=true; break; }
                                    if (want.find(':')==std::string::npos) {
                                        std::string shortHave = have.substr(have.find(':')+1);
                                        if (want==shortHave) found=true;
                                    }
                                }
                                if (!found) { ok=false; break; }
                            }
                        }
                    }
                }
                if (ok && tr.conditions.find("condition")) {
                    if (!datapackManager_.evaluatePredicateValue(tr.conditions, ctx)) ok=false;
                }
            }
            if (ok) { grantAdvancement(*p, adv.id); break; }
        }
    }
}
void GameServer::onItemEnchanted(Player& p, const std::string& itemName, int levels){
    if(!hasAdvancementManager(p)) return;
    auto merged=getMergedAdvancements();
    std::string normHave = itemName.find(':')==std::string::npos ? "minecraft:"+itemName : itemName;
    for(auto& adv: merged){
        if(hasAdvancement(p, adv.id)) continue;
        for(auto& tr: adv.triggers){
            if(tr.trigger!="minecraft:enchanted_item" && tr.trigger!="enchanted_item") continue;
            bool ok=true;
            if(!tr.conditions.isNull() && tr.conditions.isObj()){
                if(auto* it=tr.conditions.find("item")){
                    std::vector<std::string> wants;
                    if(it->isStr()) wants.push_back(it->asStr());
                    else if(it->isObj()){
                        if(auto* items=it->find("items")){
                            if(items->isStr()) wants.push_back(items->asStr());
                            else if(items->isArr()) for(auto& v: items->arr) if(v.isStr()) wants.push_back(v.asStr());
                        } else if(auto* id=it->find("id")) wants.push_back(id->asStr());
                    } else if(it->isArr()){
                        for(auto& v: it->arr) if(v.isStr()) wants.push_back(v.asStr());
                    }
                    if(!wants.empty()){
                        bool any=false;
                        for(auto& w: wants){
                            std::string wn=w.find(':')==std::string::npos?"minecraft:"+w:w;
                            if(wn==normHave) { any=true; break; }
                        }
                        if(!any) ok=false;
                    }
                }
                if(ok) if(auto* lv=tr.conditions.find("levels")){
                    int mn=1, mx=30;
                    if(lv->isNum()) mn=mx=lv->asInt(levels);
                    else if(lv->isObj()){
                        if(auto* mnV=lv->find("min")) mn=mnV->asInt(mn);
                        if(auto* mxV=lv->find("max")) mx=mxV->asInt(mx);
                    }
                    if(levels < mn || levels > mx) ok=false;
                }
            }
            if(ok){ grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onBucketFilled(Player& p, const std::string& filledName){
    if(!hasAdvancementManager(p)) return;
    auto merged=getMergedAdvancements();
    std::string normHave=filledName.find(':')==std::string::npos?"minecraft:"+filledName:filledName;
    for(auto& adv: merged){
        if(hasAdvancement(p, adv.id)) continue;
        for(auto& tr: adv.triggers){
            if(tr.trigger!="minecraft:filled_bucket" && tr.trigger!="filled_bucket") continue;
            bool ok=true;
            if(!tr.conditions.isNull() && tr.conditions.isObj()){
                if(auto* it=tr.conditions.find("item")){
                    std::string want;
                    if(it->isStr()) want=it->asStr();
                    else if(it->isObj()){
                        if(auto* items=it->find("items")){
                            if(items->isStr()) want=items->asStr();
                            else if(items->isArr() && !items->arr.empty() && items->arr[0].isStr()) want=items->arr[0].asStr();
                        } else if(auto* id=it->find("id")) want=id->asStr();
                    }
                    if(!want.empty()){
                        std::string wn=want.find(':')==std::string::npos?"minecraft:"+want:want;
                        if(wn!=normHave) ok=false;
                    }
                }
            }
            if(ok){ grantAdvancement(p, adv.id); break; }
        }
    }
}
void GameServer::onVillagerTraded(Player& p, const std::string& soldId, int count){
    if(!hasAdvancementManager(p)) return;
    auto merged=getMergedAdvancements();
    std::string normHave=soldId.find(':')==std::string::npos?"minecraft:"+soldId:soldId;
    for(auto& adv: merged){
        if(hasAdvancement(p, adv.id)) continue;
        for(auto& tr: adv.triggers){
            if(tr.trigger!="minecraft:villager_trade" && tr.trigger!="villager_trade") continue;
            bool ok=true;
            if(!tr.conditions.isNull() && tr.conditions.isObj()){
                if(auto* it=tr.conditions.find("item")){
                    if(it->isObj()){
                        if(auto* items=it->find("items")){
                            bool any=false;
                            if(items->isStr()){
                                std::string wn=items->asStr(); if(wn.find(':')==std::string::npos) wn="minecraft:"+wn; if(wn==normHave) any=true;
                            } else if(items->isArr()){
                                for(auto& v: items->arr) if(v.isStr()){
                                    std::string wn=v.asStr(); if(wn.find(':')==std::string::npos) wn="minecraft:"+wn; if(wn==normHave) { any=true; break; }
                                }
                            }
                            if(!any) ok=false;
                        }
                        if(ok) if(auto* cnt=it->find("count")) if(cnt->isObj()){
                            int mn=cnt->find("min")?cnt->at("min").asInt(1):1;
                            if(count < mn) ok=false;
                        }
                    } else if(it->isStr()){
                        std::string wn=it->asStr(); if(wn.find(':')==std::string::npos) wn="minecraft:"+wn;
                        if(wn!=normHave) ok=false;
                    }
                }
            }
            if(ok){ grantAdvancement(p, adv.id); break; }
        }
    }
}
bool GameServer::spawnMobByTypeName(const std::string& name, double x, double y,
                                     double z) {
    std::int8_t dimension = 0;
    if (brainTickGuard_) {
        std::lock_guard entityLock(*brainTickGuard_->stateMtx);
        dimension = brainTickGuard_->dimension;
    }
    return spawnMobByTypeNameFor(dimension, name, x, y, z);
}
bool GameServer::spawnMobByTypeNameFor(std::int8_t dimension,
                                       const std::string& name, double x,
                                       double y, double z) {
    // Use dynamic count via MobKind::WitherSkull+1 so future 149+ stays correct; also handle bare name + prefix fallback
    if (name=="minecraft:lightning_bolt" || name=="lightning_bolt" || name=="minecraft:lightning") {
        strikeLightningFor(dimension, x, y, z);
        return true;
    }
    constexpr int kMobCount = static_cast<int>(MobKind::WitherSkull) + 1; // 149 in 1.21.4
    for (int i = 0; i < kMobCount; ++i) {
        auto kind = static_cast<MobKind>(i);
        const char* n = MobEntity::kindName(kind);
        if (name == n) { spawnMobFor(dimension, kind, x, y, z); return true; }
    }
    if (name.find(':') == std::string::npos) {
        std::string full = "minecraft:" + name;
        for (int i = 0; i < kMobCount; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (full == MobEntity::kindName(kind)) {
                spawnMobFor(dimension, kind, x, y, z); return true;
            }
        }
        // also try without prefix via entityTypeId map (some callers pass short name)
        auto it2 = gen::entityTypeIdByName().find(full);
        if (it2 != gen::entityTypeIdByName().end()) {
            for (int i = 0; i < kMobCount; ++i) {
                auto kind = static_cast<MobKind>(i);
                if (MobEntity::typeId(kind) == it2->second) {
                    spawnMobFor(dimension, kind, x, y, z); return true;
                }
            }
        }
    }
    auto it = gen::entityTypeIdByName().find(name);
    if (it != gen::entityTypeIdByName().end()) {
        for (int i = 0; i < kMobCount; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (MobEntity::typeId(kind) == it->second) {
                spawnMobFor(dimension, kind, x, y, z); return true;
            }
        }
        // fallback: handle short name without minecraft: via map (e.g., "armadillo")
        if (name.find(':') != std::string::npos) {
            auto shortName = name.substr(name.find(':')+1);
            auto itS = gen::entityTypeIdByName().find(shortName);
            if (itS != gen::entityTypeIdByName().end()) {
                for (int i = 0; i < kMobCount; ++i) {
                    auto kind = static_cast<MobKind>(i);
                    if (MobEntity::typeId(kind) == itS->second) {
                        spawnMobFor(dimension, kind, x, y, z); return true;
                    }
                }
            }
        }
    }
    // also handle spawn_egg style name directly (e.g., "minecraft:armadillo" from "minecraft:armadillo_spawn_egg" already stripped)
    // but if caller passes the egg name itself, strip suffix and retry once
    if (name.ends_with("_spawn_egg")) {
        std::string base = name.substr(0, name.size()-std::string("_spawn_egg").size());
        if (base != name)
            return spawnMobByTypeNameFor(dimension, base, x, y, z);
    }
    return false;
}
bool GameServer::trySpawnEgg(Player& p, ItemStack& stack, BlockPos hitPos, int face) {
    std::string n;
    std::int8_t dimension = 0;
    std::uint8_t gamemode = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        n = stack.name();
        dimension = p.dimension;
        gamemode = p.gamemode;
    }
    if (!n.ends_with("_spawn_egg")) return false;
    BlockPos spawnPos = hitPos.offset(face);
    World& w = worldFor(dimension);
    {
        std::uint16_t st = w.getBlock(spawnPos.x, spawnPos.y, spawnPos.z);
        if (st != 0) {
            auto* def = gen::blockByState(st);
            bool replaceable = false;
            if (def) {
                std::string_view bn = def->name;
                // vanilla SpawnEggItem requires collision empty: short grass, fern, vines etc
                if (bn=="minecraft:short_grass"||bn=="minecraft:tall_grass"||bn=="minecraft:fern"||bn=="minecraft:large_fern"
                    ||bn=="minecraft:dead_bush"||bn=="minecraft:vine"||bn=="minecraft:snow"||bn=="minecraft:air"
                    ||bn=="minecraft:cave_air"||bn=="minecraft:void_air"||bn.find("water")!=std::string::npos) replaceable = true;
            }
            if (!replaceable) return false;
        }
    }
    if (!isInsideBorder(spawnPos.x + 0.5, spawnPos.z + 0.5)) return false;
    std::string mob = n.substr(0, n.size() - std::string("_spawn_egg").size());
    if (mob.empty()) return false;
    double sx = spawnPos.x + 0.5, sy = spawnPos.y, sz = spawnPos.z + 0.5;
    if (!spawnMobByTypeNameFor(dimension, mob, sx, sy, sz)) return false;
    if (gamemode != 1) {
        {
            std::lock_guard playerLock(p.stateMtx);
            if (--stack.count <= 0) stack = ItemStack::air();
        }
        resendInventory(p);
    }
    return true;
}
void GameServer::storeCookie(const std::array<std::uint8_t, 16>& uuid,
                             const std::string& key,
                             const std::vector<std::uint8_t>& value) {
    try {
        const std::string dir = cfg_.worldDir + "/data/cookies/" + uuidToHex(uuid);
        std::filesystem::create_directories(dir + "/../.." );
        std::filesystem::create_directories(dir.substr(0, dir.find_last_of('/')));
        // sanitize key into a file name
        std::string safe = key;
        for (auto& c : safe)
            if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
        std::ofstream f(dir + "/" + safe, std::ios::binary);
        f.write(reinterpret_cast<const char*>(value.data()),
                static_cast<std::streamsize>(value.size()));
    } catch (...) {}
}
void GameServer::eraseCookie(const std::array<std::uint8_t, 16>& uuid,
                             const std::string& key) {
    std::string safe = key;
    for (auto& c : safe)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    std::error_code ec;
    std::filesystem::remove(cfg_.worldDir + "/data/cookies/" +
                            uuidToHex(uuid) + "/" + safe, ec);
}
std::vector<std::uint8_t> GameServer::loadCookie(
    const std::array<std::uint8_t, 16>& uuid, const std::string& key) {
    std::string safe = key;
    for (auto& c : safe)
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    std::ifstream f(cfg_.worldDir + "/data/cookies/" + uuidToHex(uuid) + "/" + safe,
                    std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
}
bool GameServer::requestCookie(Player& p, const std::string& key) {
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard playerLock(p.stateMtx);
        connection = p.conn;
    }
    if (!connection) return false;
    WriteBuffer b;
    b.string(key);
    return connection->trySendPacket(proto::pl::sc::CookieRequest, b);
}
} // namespace cppfm
