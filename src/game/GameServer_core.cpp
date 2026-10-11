#include "GameServer.hpp"
#include "Session.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "EquipmentComponent.hpp"
#include "MetadataTypes.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "BehaviorTree.hpp"
#include "BehaviorTreeParser.hpp"
#include "RecipeBook.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
namespace cppfm {
using namespace proto;

namespace {

struct MobSpawnPacketState {
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    std::int32_t chunkX = 0;
    std::int32_t chunkZ = 0;
    MobKind kind = MobKind::Pig;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool isTamed = false;
    std::array<ItemStack, 6> equipment{};
    float horseMaxHealth = 0.0f;
    float horseMoveSpeed = 0.0f;
    float horseJumpStrength = 0.0f;
};

} // namespace

std::atomic<bool> g_stopRequested{false};

void GameServer::invalidateJvmMob(const std::shared_ptr<MobEntity>& mob) {
    if (jvmRuntime_ && mob && jvmRuntime_->onMobUnload(*mob))
        jvmRuntime_->invalidateEntity(*mob);
}

bool GameServer::addMob(const std::shared_ptr<MobEntity>& mob) {
    if (!mob) return false;
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    bool inserted = false;
    {
        std::lock_guard lock(entsMtx_);
        if (std::none_of(mobs_.begin(), mobs_.end(),
                         [&](const auto& candidate) {
                             return candidate == mob;
                         })) {
            mobs_.push_back(mob);
            inserted = true;
        }
    }
    if (!inserted) return false;
    {
        std::lock_guard entityLock(*mob->stateMtx);
        mob->clientSpawnReady = false;
    }
    try {
        if (jvmRuntime_) jvmRuntime_->onMobLoad(*mob);
    } catch (...) {
        // The callback runs outside entity locks and may throw after the mob
        // was inserted. Roll back only this exact registration; a re-entrant
        // removeMob may already have removed it.
        std::lock_guard lock(entsMtx_);
        const auto existing = std::find(mobs_.begin(), mobs_.end(), mob);
        if (existing != mobs_.end()) mobs_.erase(existing);
        throw;
    }
    // A load callback may discard the entity re-entrantly while the
    // simulation gate is released. Do not report successful registration to
    // callers that would emit follow-up effects for an entity no longer live.
    return containsMob(mob.get());
}

bool GameServer::removeMob(const std::shared_ptr<MobEntity>& mob) {
    if (!mob) return false;
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    if (!containsMob(mob.get())) return false;
    std::int32_t entityId = 0;
    {
        std::lock_guard entityLock(*mob->stateMtx);
        entityId = mob->entityId;
    }

    // Fabric entity-unload callbacks run while the mob is still in the
    // authoritative roster. Its Java handle is invalidated afterward.
    invalidateJvmMob(mob);
    bool removed = false;
    {
        std::lock_guard lock(entsMtx_);
        const auto oldSize = mobs_.size();
        mobs_.erase(std::remove(mobs_.begin(), mobs_.end(), mob), mobs_.end());
        removed = mobs_.size() != oldSize;
    }
    if (removed) broadcastMobRemoval(entityId);
    return removed;
}

bool GameServer::requestMobRemoval(
    const std::shared_ptr<MobEntity>& mob,
    const MobEntity::RemovalRequest reason) {
    if (!mob || reason == MobEntity::RemovalRequest::None) return false;
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    if (!containsMob(mob.get())) return false;
    return mob->requestRemoval(reason);
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

bool GameServer::isCurrentTickThread() const noexcept {
    std::lock_guard lock(serverThreadTasksMtx_);
    return tickThreadId_ != std::thread::id{} &&
           tickThreadId_ == std::this_thread::get_id();
}

bool GameServer::runFinalLifecycleCallback(
    const std::function<bool()>& callback) {
    if (!callback) return false;

    const auto currentThread = std::this_thread::get_id();
    std::thread::id previousServerThread;
    std::thread::id previousTickThread;
    std::thread::id previousFinalLifecycleThread;
    bool previousServerThreadAccepting = false;
    bool previousServerThreadHandoff = false;
    bool borrowedServerOwnership = false;
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        if (finalLifecycleCallbackDepth_ != 0 &&
            finalLifecycleThreadId_ != currentThread)
            return false;
        if (tickLoopActive_ && tickThreadId_ != currentThread)
            return false;
        if (!tickLoopActive_ && serverThreadId_ != currentThread) {
            if (inlineServerThreadTasks_ != 0) return false;
            // With no tick loop, terminal teardown may run on a different
            // thread. Transfer ownership only for this callback scope; no
            // tick or queued ingress can execute concurrently here.
            previousServerThread = serverThreadId_;
            previousTickThread = tickThreadId_;
            previousServerThreadAccepting = serverThreadAccepting_;
            previousServerThreadHandoff = serverThreadHandoff_;
            serverThreadAccepting_ = false;
            serverThreadHandoff_ = true;
            serverThreadId_ = currentThread;
            tickThreadId_ = currentThread;
            borrowedServerOwnership = true;
        }
        previousFinalLifecycleThread = finalLifecycleThreadId_;
        finalLifecycleThreadId_ = currentThread;
        ++finalLifecycleCallbackDepth_;
    }

    const auto restoreOwnership = [&] {
        std::lock_guard lock(serverThreadTasksMtx_);
        if (finalLifecycleCallbackDepth_ != 0)
            --finalLifecycleCallbackDepth_;
        if (finalLifecycleCallbackDepth_ == 0) {
            finalLifecycleThreadId_ = previousFinalLifecycleThread;
            if (borrowedServerOwnership) {
                serverThreadId_ = previousServerThread;
                tickThreadId_ = previousTickThread;
                serverThreadAccepting_ = previousServerThreadAccepting;
                serverThreadHandoff_ = previousServerThreadHandoff;
            }
        }
    };
    try {
        const bool result = callback();
        restoreOwnership();
        return result;
    } catch (...) {
        restoreOwnership();
        throw;
    }
}

void GameServer::claimServerThreadForBootstrap() noexcept {
    std::lock_guard lock(serverThreadTasksMtx_);
    serverThreadId_ = std::this_thread::get_id();
    serverThreadAccepting_ = true;
}

void GameServer::bindServerThread() {
    std::unique_lock lock(serverThreadTasksMtx_);
    serverThreadTaskIdleCv_.wait(lock, [this] {
        return inlineServerThreadTasks_ == 0;
    });
    serverThreadId_ = std::this_thread::get_id();
    tickThreadId_ = serverThreadId_;
    serverThreadAccepting_ = running_.load(std::memory_order_acquire) &&
                             !shutdownStarted_.load(std::memory_order_acquire);
    serverThreadHandoff_ = false;
}

void GameServer::releaseServerThread() noexcept {
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        if (serverThreadId_ != std::this_thread::get_id()) return;
        serverThreadAccepting_ = false;
        serverThreadHandoff_ = false;
        serverThreadId_ = {};
        tickThreadId_ = {};
    }
    cancelServerThreadTasks();
}

void GameServer::cancelServerThreadTasks() noexcept {
    std::deque<std::shared_ptr<ServerThreadTask>> pending;
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        serverThreadAccepting_ = false;
        pending.swap(serverThreadTasks_);
        pendingServerThreadTaskCleanups_ += pending.size();
    }
    for (auto& request : pending) {
        if (!request) continue;
        auto expected = ServerThreadTask::State::Pending;
        if (request->state.compare_exchange_strong(
                expected, ServerThreadTask::State::Cancelled,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            request->task = {};
            request->waitCv.notify_all();
        }
    }
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        pendingServerThreadTaskCleanups_ -= pending.size();
    }
    serverThreadTaskIdleCv_.notify_all();
}

void GameServer::waitForServerThreadTasks() {
    if (isCurrentServerThread()) {
        bool terminalPump = false;
        {
            std::lock_guard lock(serverThreadTasksMtx_);
            terminalPump = shutdownStarted_.load(std::memory_order_acquire) &&
                finalLifecycleCallbackDepth_ != 0 &&
                finalLifecycleThreadId_ == std::this_thread::get_id();
        }
        if (!terminalPump) return;
        for (;;) {
            drainServerThreadTasks();
            std::unique_lock lock(serverThreadTasksMtx_);
            const auto idle = [this] {
                return activeServerThreadTasks_.load(std::memory_order_acquire) == 0 &&
                       inlineServerThreadTasks_ == 0 &&
                       serverThreadTasks_.empty() &&
                       pendingServerThreadTaskCleanups_ == 0;
            };
            if (idle()) return;
            serverThreadTaskIdleCv_.wait_for(lock, std::chrono::milliseconds(1), idle);
        }
    }
    std::unique_lock lock(serverThreadTasksMtx_);
    serverThreadTaskIdleCv_.wait(lock, [this] {
        return activeServerThreadTasks_.load(std::memory_order_acquire) == 0 &&
               inlineServerThreadTasks_ == 0 &&
               serverThreadTasks_.empty() &&
               pendingServerThreadTaskCleanups_ == 0;
    });
}

ServerThreadTaskOutcome GameServer::runOnServerThreadDetailed(
    std::function<void()> task, std::chrono::milliseconds timeout,
    const ServerThreadTaskWaitPolicy waitPolicy,
    const bool allowDuringShutdown) {
    if (!task) return ServerThreadTaskOutcome::Rejected;

    std::shared_ptr<ServerThreadTask> request;
    bool runInline = false;
    bool countedInlineTask = false;
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        const bool owner = serverThreadId_ != std::thread::id{} &&
                           serverThreadId_ == std::this_thread::get_id();
        const bool finalLifecycleOwner =
            finalLifecycleThreadId_ == std::this_thread::get_id() &&
            finalLifecycleCallbackDepth_ != 0;
        const bool terminalOwnerAvailable =
            allowDuringShutdown &&
            shutdownStarted_.load(std::memory_order_acquire) &&
            finalLifecycleCallbackDepth_ != 0 &&
            finalLifecycleThreadId_ != std::thread::id{};
        if (owner &&
            (finalLifecycleOwner ||
             (!serverThreadHandoff_ && serverThreadAccepting_ &&
              (!shutdownStarted_.load(std::memory_order_acquire) ||
               allowDuringShutdown)))) {
            runInline = true;
            if (!finalLifecycleOwner) {
                ++inlineServerThreadTasks_;
                countedInlineTask = true;
            }
        } else if ((!serverThreadAccepting_ && !terminalOwnerAvailable) ||
                   serverThreadId_ == std::thread::id{} ||
                   (!running_.load(std::memory_order_acquire) &&
                    !terminalOwnerAvailable) ||
                   (shutdownStarted_.load(std::memory_order_acquire) &&
                    !allowDuringShutdown) ||
                   serverThreadTasks_.size() >= kMaxServerThreadTaskQueue) {
            return ServerThreadTaskOutcome::Rejected;
        } else {
            try {
                request = std::make_shared<ServerThreadTask>(
                    std::move(task), allowDuringShutdown);
                serverThreadTasks_.push_back(request);
            } catch (...) {
                return ServerThreadTaskOutcome::Rejected;
            }
        }
    }

    if (runInline) {
        const auto releaseInlineTask = [&] {
            if (!countedInlineTask) return;
            {
                std::lock_guard lock(serverThreadTasksMtx_);
                if (inlineServerThreadTasks_ != 0) --inlineServerThreadTasks_;
            }
            serverThreadTaskIdleCv_.notify_all();
        };
        try {
            task();
            releaseInlineTask();
            return ServerThreadTaskOutcome::Completed;
        } catch (...) {
            releaseInlineTask();
            return ServerThreadTaskOutcome::Rejected;
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
            request->task = {};
            request->waitCv.notify_all();
            return ServerThreadTaskOutcome::Rejected;
        }
        // A task that won the Pending -> Running race stays owned by the
        // server thread and may commit after this timeout. Callers with a
        // synchronous result contract wait for its terminal state; bounded
        // callers retain the explicit Indeterminate result.
        const auto state = request->state.load(std::memory_order_acquire);
        if (state == ServerThreadTask::State::Running &&
            waitPolicy == ServerThreadTaskWaitPolicy::WaitIfRunning) {
            request->waitCv.wait(waitLock, [&request] {
                const auto completed = request->state.load(
                    std::memory_order_acquire);
                return completed == ServerThreadTask::State::Completed ||
                       completed == ServerThreadTask::State::Failed ||
                       completed == ServerThreadTask::State::Cancelled;
            });
            return request->state.load(std::memory_order_acquire) ==
                           ServerThreadTask::State::Completed
                ? ServerThreadTaskOutcome::Completed
                : ServerThreadTaskOutcome::Rejected;
        }
        if (state == ServerThreadTask::State::Running)
            return ServerThreadTaskOutcome::Indeterminate;
        return state == ServerThreadTask::State::Completed
            ? ServerThreadTaskOutcome::Completed
            : ServerThreadTaskOutcome::Rejected;
    }
    return request->state.load(std::memory_order_acquire) ==
                   ServerThreadTask::State::Completed
        ? ServerThreadTaskOutcome::Completed
        : ServerThreadTaskOutcome::Rejected;
}

bool GameServer::runOnServerThread(std::function<void()> task,
                                   std::chrono::milliseconds timeout) {
    return runOnServerThreadDetailed(std::move(task), timeout) ==
           ServerThreadTaskOutcome::Completed;
}

bool GameServer::startTickLoop() {
    std::unique_lock startupLock(lifecycleMtx_);
    if (shutdownStarted_.load(std::memory_order_acquire) ||
        stopRequested_.load(std::memory_order_seq_cst) ||
        !running_.load(std::memory_order_acquire))
        return false;
    {
        std::lock_guard lock(serverThreadTasksMtx_);
        if (tickLoopActive_)
            throw std::logic_error("tick loop is already running");
        tickLoopActive_ = true;
        serverThreadHandoff_ = true;
        serverThreadAccepting_ = false;
    }
    startupLock.unlock();
    bindServerThread();
    const auto finishTickLoop = [this] {
        // Fabric's final lifecycle events must run while the JNI bridge is
        // still published and while this thread remains the server owner.
        // JvmRuntime::stop() has an idempotent fallback for paths that never
        // create a tick loop (for example startup failure).
        if (jvmRuntime_) (void)jvmRuntime_->onServerStopping();
        releaseServerThread();
        {
            std::lock_guard lock(serverThreadTasksMtx_);
            tickLoopActive_ = false;
        }
        serverThreadTaskIdleCv_.notify_all();
    };

    using clock = std::chrono::steady_clock;
    auto next = clock::now() + std::chrono::milliseconds(50);
    while (running_.load(std::memory_order_acquire)) {
        try {
            // Accept work is non-blocking and shares the authoritative server
            // thread with initialization, Fabric callbacks, and world ticks.
            acceptPendingConnections();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[cppfm] fatal accept pass: %s\n", e.what());
            requestStop();
            break;
        } catch (...) {
            std::fprintf(stderr, "[cppfm] fatal accept pass: unknown exception\n");
            requestStop();
            break;
        }
        if (!running_.load(std::memory_order_acquire)) break;
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
    finishTickLoop();
    return true;
}
void GameServer::stopTickLoop() {
    std::unique_lock lock(serverThreadTasksMtx_);
    if (tickThreadId_ == std::this_thread::get_id()) return;
    if (!tickLoopActive_) return;
    std::fprintf(stderr, "[cppfm] waiting for the server tick loop to stop\n");
    serverThreadTaskIdleCv_.wait(lock, [this] { return !tickLoopActive_; });
}
void GameServer::runForever() {
    std::unique_lock startupLock(lifecycleMtx_);
    // stop() may have completed before a delayed runForever() call begins.
    // Do not reopen the listener or publish a janitor after teardown.
    if (shutdownStarted_.load(std::memory_order_acquire) ||
        stopRequested_.load(std::memory_order_seq_cst))
        return;
    if (running_.load(std::memory_order_acquire) ||
        platform::isValid(listenFd_.load(std::memory_order_acquire)))
        throw std::logic_error("server loop is already started");
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
    if (!platform::setSocketNonBlocking(fd)) {
        listenFd_.exchange(platform::invalid_socket, std::memory_order_acq_rel);
        platform::closeSocket(fd);
        throw std::runtime_error("could not make the listener non-blocking: " +
                                 platform::socketErrorText(platform::lastSocketError()));
    }
    if (stopRequested_.load(std::memory_order_seq_cst)) {
        const auto opened = listenFd_.exchange(platform::invalid_socket,
                                               std::memory_order_acq_rel);
        if (platform::isValid(opened)) platform::closeSocket(opened);
        return;
    }
    running_.store(true, std::memory_order_release);
    // requestStop() is also called directly by the signal handler and cannot
    // take lifecycleMtx_. Recheck after publication to close that narrow race.
    if (stopRequested_.load(std::memory_order_seq_cst)) requestStop();
    try {
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
        startupLock.unlock();
        // The caller owns the authoritative server thread. The listener is
        // polled non-blockingly from each loop pass, so no separate tick
        // thread can split Fabric callbacks from initialization.
        if (!startTickLoop()) return;
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
void GameServer::acceptPendingConnections() {
    // Bound work per pass so a continuously full listen queue cannot starve
    // the 20 Hz world tick.
    constexpr std::size_t kAcceptBatchLimit = 32;
    std::size_t acceptedCount = 0;
    while (running_.load(std::memory_order_acquire) &&
           acceptedCount < kAcceptBatchLimit) {
        sockaddr_in cli{};
        platform::socket_length_t cl = sizeof(cli);
        const auto listenFd = listenFd_.load(std::memory_order_acquire);
        if (!platform::isValid(listenFd)) break;
        const auto fd = platform::acceptSocket(
            listenFd, reinterpret_cast<sockaddr*>(&cli), &cl);
        if (!platform::isValid(fd)) {
            const int error = platform::lastSocketError();
            if (g_stopRequested || !running_.load(std::memory_order_acquire))
                return;
            if (platform::isInterrupted(error)) continue;
            if (platform::isWouldBlock(error)) return;
            std::fprintf(stderr, "[cppfm] accept failed: %s\n",
                         platform::socketErrorText(error).c_str());
            requestStop();
            return;
        }
        ++acceptedCount;
        // Winsock accept inherits listener properties, including its
        // nonblocking mode. Session I/O uses blocking sockets plus explicit
        // send/receive timeouts, so normalize accepted sockets on every OS.
        if (!platform::setSocketNonBlocking(fd, false)) {
            const int error = platform::lastSocketError();
            std::fprintf(stderr,
                         "[cppfm] could not configure accepted socket: %s\n",
                         platform::socketErrorText(error).c_str());
            platform::closeSocket(fd);
            continue;
        }
        std::fprintf(stderr, "[cppfm] accepted fd=%llu\n",
                     static_cast<unsigned long long>(platform::socketNumber(fd)));
        if (!acceptGate_.allow(steadyNowMs())) {
            std::fprintf(stderr, "[cppfm] accept gate: refusing fd=%llu (rate)\n",
                         static_cast<unsigned long long>(platform::socketNumber(fd)));
            platform::closeSocket(fd);
            continue;
        }
        auto admission = pendingSessionAdmissionGate_.tryAcquire();
        if (!admission) {
            std::fprintf(stderr, "[cppfm] session admission: refusing fd=%llu (capacity)\n",
                         static_cast<unsigned long long>(platform::socketNumber(fd)));
            platform::closeSocket(fd);
            continue;
        }
        try {
            auto finished = std::make_shared<std::atomic<bool>>(false);
            std::thread worker([this, fd, finished,
                                slot = std::move(*admission)]() mutable {
                SessionThreadScope sessionThreadScope(this);
                struct Completion {
                    std::shared_ptr<std::atomic<bool>> finished;
                    ~Completion() {
                        finished->store(true, std::memory_order_release);
                    }
                } completion{finished};
                (void)completion;
                std::optional<SessionAdmissionGate::Slot> sessionSlot(
                    std::move(slot));
                std::optional<SessionAdmissionGate::Slot> statusSessionSlot;
                std::shared_ptr<Connection> conn;
                bool registered = false;
                try {
                    conn = std::make_shared<Connection>(fd);
                    registerActiveConnection(conn);
                    registered = true;
                    conn->setNoDelay();
                    // Packet handling shares the simulation gate with the
                    // tick; bound a stalled client's socket hold time.  The
                    // sub-second deadline prevents one slow peer from holding
                    // the global simulation gate for multiple seconds.
                    conn->setSendTimeoutMs(250);
                    conn->setRecvTimeout(30);
                    conn->enableFloodBudget(true);
                    Session s(*this, conn, [this, fd, &sessionSlot,
                                            &statusSessionSlot] {
                        auto statusSlot = statusAdmissionGate_.tryAcquire();
                        if (!statusSlot) {
                            std::fprintf(stderr,
                                "[cppfm] status admission: refusing fd=%llu (capacity)\n",
                                static_cast<unsigned long long>(
                                    platform::socketNumber(fd)));
                            return false;
                        }
                        statusSessionSlot.emplace(std::move(*statusSlot));
                        sessionSlot.reset();
                        return true;
                    }, [&sessionSlot] { sessionSlot.reset(); });
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
            if (!registerSessionThread(std::move(worker), finished)) {
                requestStop();
                return;
            }
        } catch (const std::exception& e) {
            platform::closeSocket(fd);
            std::fprintf(stderr, "[cppfm] could not create session worker: %s\n", e.what());
            requestStop();
            return;
        } catch (...) {
            platform::closeSocket(fd);
            std::fprintf(stderr, "[cppfm] could not create session worker\n");
            requestStop();
            return;
        }
    }
}

bool GameServer::registerSessionThread(
    std::thread worker,
    const std::shared_ptr<std::atomic<bool>>& finished) {
    bool rejected = false;
    std::vector<std::thread> completed;
    {
        std::lock_guard lock(sessionThreadsMtx_);
        if (sessionThreadsStopping_) {
            rejected = true;
        } else {
            try {
                const auto finishedCount = static_cast<std::size_t>(std::count_if(
                    sessionThreads_.begin(), sessionThreads_.end(), [](const auto& entry) {
                        return entry.finished &&
                               entry.finished->load(std::memory_order_acquire);
                    }));
                completed.reserve(finishedCount);
                sessionThreads_.reserve(sessionThreads_.size() + 1);
                for (auto it = sessionThreads_.begin(); it != sessionThreads_.end();) {
                    if (it->finished && it->finished->load(std::memory_order_acquire)) {
                        completed.push_back(std::move(it->worker));
                        it = sessionThreads_.erase(it);
                    } else {
                        ++it;
                    }
                }
                sessionThreads_.push_back(SessionThread{std::move(worker), finished});
            } catch (...) {
                rejected = true;
            }
        }
    }
    // Never join a newly-created worker while holding the registry mutex: a
    // session's final unregisterActiveConnection() needs an unrelated lock
    // today, but keeping lifecycle locks independent prevents a future
    // shutdown callback from turning this into a lock inversion.
    for (auto& completedWorker : completed) {
        if (completedWorker.joinable()) completedWorker.join();
    }
    if (rejected && worker.joinable()) worker.join();
    return !rejected;
}

void GameServer::joinSessionThreads() {
    std::vector<SessionThread> workers;
    const auto self = std::this_thread::get_id();
    {
        std::lock_guard lock(sessionThreadsMtx_);
        sessionThreadsStopping_ = true;
        for (auto it = sessionThreads_.begin(); it != sessionThreads_.end();) {
            if (it->worker.joinable() && it->worker.get_id() == self) {
                ++it;
                continue;
            }
            workers.push_back(std::move(*it));
            it = sessionThreads_.erase(it);
        }
    }
    for (auto& session : workers) {
        if (!session.worker.joinable()) continue;
        while (session.finished &&
               !session.finished->load(std::memory_order_acquire)) {
            if (isCurrentServerThread()) drainServerThreadTasks();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        session.worker.join();
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
    std::lock_guard transitionLock(p.digTransitionMtx);
    std::int32_t digX = 0, digY = 0, digZ = 0;
    std::int8_t dimension = 0;
    {
        std::lock_guard digLock(p.digMtx);
        digX = p.digX;
        digY = p.digY;
        digZ = p.digZ;
    }
    {
        std::lock_guard playerLock(p.stateMtx);
        dimension = p.dimension;
    }
    broadcastDigStageAt(p, dimension, digX, digY, digZ, stage);
}
void GameServer::broadcastDigStageAt(Player& p, std::int8_t dimension,
                                     std::int32_t digX, std::int32_t digY,
                                     std::int32_t digZ,
                                     std::int8_t stage) {
    std::int32_t entityId = 0;
    {
        std::lock_guard playerLock(p.stateMtx);
        entityId = p.entityId;
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
bool GameServer::spawnMobFor(
    std::int8_t dimension, MobKind kind, double x, double y, double z,
    const std::function<void(MobEntity&)>& configureBeforePublish,
    std::shared_ptr<MobEntity>* spawnedMob) {
    if (spawnedMob) spawnedMob->reset();
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
    if (jvmRuntime_ && !jvmRuntime_->onMobSpawn(*mob, x, y, z)) return false;
    if (configureBeforePublish) configureBeforePublish(*mob);
    if (!addMob(mob)) return false;
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
    if (spawnedMob) *spawnedMob = mob;
    return true;
}
void GameServer::broadcastMobSpawn(MobEntity& mob) {
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, &mob] { broadcastMobSpawn(mob); });
        return;
    }
    if (!containsMob(&mob)) return;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        if (mob.dead) return;
        mob.clientSpawnReady = true;
        // The first relative entity-move packet is measured from the spawn
        // packet's absolute position, never from the struct's zero defaults.
        // In particular, a negative world Y can overflow the protocol's
        // signed 16-bit 1/4096-block delta if it is treated as an absolute.
        mob.recordPositionSent();
    }
    // A session only starts tracking this entity after its containing chunk
    // has been enqueued. A later chunk snapshot handles mobs which predate the
    // viewer, closing the join-vs-spawn race without sending entities ahead
    // of the chunk that establishes their client-side world.
    std::unordered_set<Player*> sessionPlayers;
    for (Session* session : activeSessions_) {
        if (!session || !session->self_) continue;
        sessionPlayers.insert(session->self_.get());
        // Keep the authoritative object identity here. The viewer path checks
        // that this mob is still in the server roster before publishing it;
        // passing the detached snapshot would fail that ownership check and
        // silently suppress every live spawn for already-loaded chunks.
        session->sendMobSpawnIfChunkLoaded(mob);
    }
    for (const auto& player : playersSnapshot())
        if (player && !sessionPlayers.contains(player.get()))
            sendMobSpawnToViewer(mob, *player);
}

void GameServer::broadcastMobPositionUpdate(
    const MobEntity& mob, const std::uint8_t packetId,
    const WriteBuffer& body, const Player* except) {
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, &mob, packetId, &body, except] {
            broadcastMobPositionUpdate(mob, packetId, body, except);
        });
        return;
    }
    if (!containsMob(&mob)) return;

    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    std::int32_t chunkX = 0;
    std::int32_t chunkZ = 0;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        if (mob.dead || !mob.clientSpawnReady) return;
        entityId = mob.entityId;
        dimension = canonicalDimension(mob.dimension);
        chunkX = static_cast<std::int32_t>(std::floor(mob.x)) >> 4;
        chunkZ = static_cast<std::int32_t>(std::floor(mob.z)) >> 4;
    }

    WriteBuffer remove;
    remove.varint(1);
    remove.varint(entityId);
    for (const auto& viewer : playersSnapshot()) {
        if (!viewer) continue;
        std::shared_ptr<Connection> connection;
        bool wasTracking = false;
        bool visible = false;
        {
            std::lock_guard playerLock(viewer->stateMtx);
            connection = viewer->conn;
            const auto viewerDimension = canonicalDimension(viewer->dimension);
            wasTracking = viewer->entityTracking.tracksEntity(entityId);
            visible = viewer->inPlay && viewer->spawned &&
                viewerDimension == dimension &&
                viewer->entityTracking.hasChunk(dimension, chunkX, chunkZ);
            if (wasTracking && visible &&
                !viewer->entityTracking.updateTrackedEntityChunk(
                    entityId, dimension, chunkX, chunkZ))
                visible = false;
            if (wasTracking && !visible)
                viewer->entityTracking.forgetEntity(entityId);
        }

        if (wasTracking && !visible) {
            if (connection && !connection->trySendPacketLowPriority(
                                  pl::sc::RemoveEntities, remove))
                connection->close();
            continue;
        }
        if (!wasTracking && visible) {
            sendMobSpawnToViewer(mob, *viewer);
            continue;
        }
        if (wasTracking && visible && connection && viewer.get() != except &&
            !connection->trySendPacketLowPriority(packetId, body)) {
            viewer->entityTracking.forgetEntity(entityId);
            connection->close();
        }
    }
}

void GameServer::broadcastMobRemoval(const std::int32_t entityId) {
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, entityId] {
            broadcastMobRemoval(entityId);
        });
        return;
    }

    WriteBuffer remove;
    remove.varint(1);
    remove.varint(entityId);
    for (const auto& viewer : playersSnapshot()) {
        if (!viewer || !viewer->entityTracking.forgetEntity(entityId)) continue;
        std::shared_ptr<Connection> connection;
        {
            std::lock_guard playerLock(viewer->stateMtx);
            connection = viewer->conn;
        }
        if (connection && !connection->trySendPacketLowPriority(
                              pl::sc::RemoveEntities, remove))
            connection->close();
    }
}

void GameServer::sendMobSpawnToViewer(const MobEntity& mob, Player& viewer) {
    SimulationDispatchGuard simulation(simulationDispatchMtx_);
    if (mobStateLockOwnedByCurrentThread()) {
        runWithoutMobStateLock([this, &mob, &viewer] {
            sendMobSpawnToViewer(mob, viewer);
        });
        return;
    }
    if (!containsMob(&mob)) return;

    MobSpawnPacketState snapshot;
    {
        std::lock_guard entityLock(*mob.stateMtx);
        if (mob.dead || !mob.clientSpawnReady) return;
        snapshot.entityId = mob.entityId;
        snapshot.dimension = canonicalDimension(mob.dimension);
        snapshot.chunkX = static_cast<std::int32_t>(std::floor(mob.x)) >> 4;
        snapshot.chunkZ = static_cast<std::int32_t>(std::floor(mob.z)) >> 4;
        snapshot.kind = mob.kind;
        snapshot.x = mob.x;
        snapshot.y = mob.y;
        snapshot.z = mob.z;
        snapshot.isTamed = mob.isTamed;
        snapshot.equipment = mob.equipment;
        snapshot.horseMaxHealth = mob.horseMaxHealth;
        snapshot.horseMoveSpeed = mob.horseMoveSpeed;
        snapshot.horseJumpStrength = mob.horseJumpStrength;
    }

    std::shared_ptr<Connection> connection;
    {
        std::lock_guard playerLock(viewer.stateMtx);
        if (!viewer.inPlay || !viewer.spawned ||
            canonicalDimension(viewer.dimension) !=
                canonicalDimension(snapshot.dimension) ||
            !viewer.entityTracking.hasChunk(snapshot.dimension,
                                             snapshot.chunkX,
                                             snapshot.chunkZ) ||
            viewer.entityTracking.tracksEntity(snapshot.entityId))
            return;
        connection = viewer.conn;
    }
    if (!connection) return;

    WriteBuffer spawn;
    spawn.varint(snapshot.entityId);
    const auto uuid = entityUuidForId(snapshot.entityId);
    spawn.uuid(uuid.data());
    spawn.varint(static_cast<std::int32_t>(MobEntity::typeId(snapshot.kind)));
    spawn.f64(snapshot.x);
    spawn.f64(snapshot.y);
    spawn.f64(snapshot.z);
    spawn.i8(0);
    spawn.i8(0);
    spawn.i8(0);
    spawn.varint(0);
    spawn.i16(0);
    spawn.i16(0);
    spawn.i16(0);

    const auto enqueue = [&connection](std::uint8_t packetId,
                                       const WriteBuffer& body) {
        return connection->trySendPacketLowPriority(packetId, body);
    };
    bool queued = enqueue(pl::sc::SpawnEntity, spawn);
    if (queued && snapshot.isTamed &&
        MobEntity::usesHorseTemper(snapshot.kind)) {
        WriteBuffer metadata;
        metadata.varint(snapshot.entityId);
        meta::writeMetaByte(metadata, 17, 2);
        metadata.u8(255);
        queued = enqueue(pl::sc::SetEntityMetadata, metadata);
    }
    if (queued) {
        EquipmentComponent equipment(snapshot.equipment);
        if (equipment.hasAny()) {
            WriteBuffer body;
            body.varint(snapshot.entityId);
            equipment.writePayload(body);
            queued = enqueue(pl::sc::SetEquipment, body);
        }
    }
    if (queued && snapshot.kind == MobKind::Horse) {
        AttributeManager attributes;
        attributes.setBase(Attribute::MAX_HEALTH, snapshot.horseMaxHealth);
        attributes.setBase(Attribute::MOVEMENT_SPEED, snapshot.horseMoveSpeed);
        attributes.setBase(Attribute::JUMP_STRENGTH,
                           snapshot.horseJumpStrength);
        WriteBuffer body;
        attributes.writeUpdate(body, snapshot.entityId);
        queued = enqueue(pl::sc::UpdateAttributes, body);
    }
    if (!queued) {
        connection->close();
        return;
    }
    if (!viewer.entityTracking.tryTrackEntity(
            snapshot.entityId, snapshot.dimension,
            snapshot.chunkX, snapshot.chunkZ))
        connection->close();
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
                             std::int32_t attackerEntityId,
                             std::int32_t lootingLevel) {
    std::shared_ptr<MobAiEntry> entry;
    {
        std::lock_guard lock(mobAiMtx_);
        auto it = mobAi_.find(entityId);
        if (it != mobAi_.end()) entry = it->second;
    }
    if (!entry || !entry->ctx) return;
    const auto hurtTick = tickNo_.load(std::memory_order_acquire);
    entry->ctx->lastHurtTick.store(hurtTick, std::memory_order_release);
    // Environmental damage updates AI hurt memories but must not erase or
    // extend the separate vanilla last-player-hit credit window.
    if (attackerEntityId >= 0) {
        entry->ctx->lastPlayerHurtByEntityId.store(
            attackerEntityId, std::memory_order_release);
        entry->ctx->lastPlayerHurtTick.store(
            hurtTick, std::memory_order_release);
        entry->ctx->lastPlayerHurtLootingLevel.store(
            std::max(0, lootingLevel), std::memory_order_release);
    }
}

LootContext GameServer::mobLootContextForDeath(
    std::int32_t entityId, bool entityOnFire,
    float explosionRadius) const {
    LootContext context;
    context.hasEntityContext = true;
    context.entityOnFire = entityOnFire;
    context.explosionRadius = std::max(0.f, explosionRadius);

    std::shared_ptr<MobAiEntry> entry;
    {
        std::lock_guard lock(mobAiMtx_);
        const auto it = mobAi_.find(entityId);
        if (it != mobAi_.end()) entry = it->second;
    }
    if (!entry || !entry->ctx) return context;

    const auto playerId = entry->ctx->lastPlayerHurtByEntityId.load(
        std::memory_order_acquire);
    const auto playerHurtTick = entry->ctx->lastPlayerHurtTick.load(
        std::memory_order_acquire);
    const auto currentTick = tickNo_.load(std::memory_order_acquire);
    const bool recentPlayerDamage = playerId >= 0 && playerHurtTick >= 0 &&
        currentTick >= playerHurtTick &&
        currentTick - playerHurtTick <= 100;
    context.wasKilledByPlayer = recentPlayerDamage;
    context.lootingLevel = recentPlayerDamage
        ? entry->ctx->lastPlayerHurtLootingLevel.load(
              std::memory_order_acquire)
        : 0;
    return context;
}

void GameServer::spawnMobLootFor(MobKind kind, std::int8_t dimension,
                                 double x, double y, double z,
                                 const LootContext& context) {
    const std::string kindName = MobEntity::kindName(kind);
    const auto colon = kindName.find(':');
    const std::string nameSpace = colon == std::string::npos
        ? "minecraft" : kindName.substr(0, colon);
    const std::string base = colon == std::string::npos
        ? kindName : kindName.substr(colon + 1);
    const std::string tableId = nameSpace + ":entities/" + base;

    if (const auto* table = datapackManager_.lootTables.find(tableId)) {
        const auto evaluated = datapackManager_.lootTables.evaluateRegisteredTableBestEffort(
            tableId, {}, &context);
        if (evaluated) {
            for (const auto& stack : *evaluated) {
                if (stack.empty()) continue;
                spawnItemDropFor(
                    dimension, x, y, z, stack,
                    (nextRandom() / (double)RAND_MAX - .5) * .15, .1,
                    (nextRandom() / (double)RAND_MAX - .5) * .15);
            }
            return;
        }
        if (table->suppressDefaultDropFallback) return;
    }

    const auto fallback = MobEntity::dropFor(kind);
    if (!fallback.itemId) return;
    int count = fallback.count;
    if (context.lootingLevel > 0)
        count = std::min(64, count +
            static_cast<int>(nextRandom() % (context.lootingLevel + 1)));
    if (count <= 0) return;
    spawnItemDropFor(
        dimension, x, y, z, fallback.itemId,
        static_cast<std::uint8_t>(count),
        (nextRandom() / (double)RAND_MAX - .5) * .15, .1,
        (nextRandom() / (double)RAND_MAX - .5) * .15);
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
bool GameServer::savePlayerProgress(Player& p) {
    std::lock_guard playerLock(p.stateMtx);
    if (!p.stats || !p.advancements) return false;
    if (p.joinTick) {
        const std::int64_t ticks = tickNo_ - p.joinTick;
        p.stats->add("minecraft:custom|minecraft:play_time", ticks);
        p.joinTick = tickNo_;
    }
    const bool statsSaved = p.stats->save(uuidToHex(p.uuid));
    const bool advancementsSaved = p.advancements->save();
    return statsSaved && advancementsSaved;
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
void GameServer::sendRecipeBookToPlayers() {
    const auto packetBody = recipe_book::makeAddPacketBody(
        recipes_.all(), nullptr, true);
    for (const auto& player : playersSnapshot()) {
        if (!player) continue;
        std::shared_ptr<Connection> connection;
        {
            std::lock_guard playerLock(player->stateMtx);
            if (!player->inPlay) continue;
            connection = player->conn;
        }
        if (connection &&
            !connection->trySendPacket(pl::sc::RecipeBookAdd, packetBody))
            connection->close();
    }
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
    ctx.dayTime = dayTime();
    ctx.randomSeed = cfg_.seed ^
        (static_cast<std::uint64_t>(static_cast<std::int64_t>(ctx.x)) * 0x9E3779B97F4A7C15ULL) ^
        (static_cast<std::uint64_t>(static_cast<std::int64_t>(ctx.y)) * 0xBF58476D1CE4E5B9ULL) ^
        (static_cast<std::uint64_t>(static_cast<std::int64_t>(ctx.z)) * 0x94D049BB133111EBULL) ^
        static_cast<std::uint64_t>(ctx.dayTime);
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
void GameServer::publishPlayerEffectChanges(
    Player& player, const std::int32_t entityId, const std::int8_t dimension,
    const std::shared_ptr<Connection>& connection,
    const std::vector<std::uint8_t>& removed,
    const std::vector<EffectInstance>& updated, const bool stateChanged) {
    for (const auto effect : removed) {
        WriteBuffer packet;
        packet.varint(entityId);
        packet.varint(effect);
        if (connection)
            connection->trySendPacket(pl::sc::RemoveMobEffect, packet);
        broadcastPacketExceptInDimension(
            dimension, &player, pl::sc::RemoveMobEffect, packet);
    }
    for (const auto& effect : updated) {
        WriteBuffer packet;
        packet.varint(entityId);
        packet.varint(effect.type);
        packet.varint(effect.amplifier);
        packet.varint(effect.durationTicks);
        packet.u8(static_cast<std::uint8_t>(effectFlags(effect)));
        if (connection)
            connection->trySendPacket(pl::sc::EntityEffect, packet);
        broadcastPacketExceptInDimension(
            dimension, &player, pl::sc::EntityEffect, packet);
    }
    if (stateChanged) onEffectsChanged(&player);
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
        if (name == n) return spawnMobFor(dimension, kind, x, y, z);
    }
    if (name.find(':') == std::string::npos) {
        std::string full = "minecraft:" + name;
        for (int i = 0; i < kMobCount; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (full == MobEntity::kindName(kind)) {
                return spawnMobFor(dimension, kind, x, y, z);
            }
        }
        // also try without prefix via entityTypeId map (some callers pass short name)
        auto it2 = gen::entityTypeIdByName().find(full);
        if (it2 != gen::entityTypeIdByName().end()) {
            for (int i = 0; i < kMobCount; ++i) {
                auto kind = static_cast<MobKind>(i);
                if (MobEntity::typeId(kind) == it2->second) {
                    return spawnMobFor(dimension, kind, x, y, z);
                }
            }
        }
    }
    auto it = gen::entityTypeIdByName().find(name);
    if (it != gen::entityTypeIdByName().end()) {
        for (int i = 0; i < kMobCount; ++i) {
            auto kind = static_cast<MobKind>(i);
            if (MobEntity::typeId(kind) == it->second) {
                return spawnMobFor(dimension, kind, x, y, z);
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
                        return spawnMobFor(dimension, kind, x, y, z);
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
} // namespace cppfm
