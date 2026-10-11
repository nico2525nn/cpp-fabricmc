#include "GameServer.hpp"
#include "GameServerHelpers.hpp"
#include "BlockEntityPosition.hpp"
#include "BlockEntityKinds.hpp"
#include "BlockEntities.hpp"
#include "../generated/BlockStates.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace cppfm {
namespace {

template <class Behavior, class... Args>
void registerBehaviorFor(BlockTickScheduler& scheduler,
                         std::initializer_list<const char*> blockNames,
                         Args... args) {
    for (const char* blockName : blockNames)
        scheduler.registerBehavior(
            blockName, std::make_unique<Behavior>(args...));
}

} // namespace

void GameServer::init() {
    if (initialized_)
        throw std::logic_error("GameServer::init called more than once");
    shutdownStarted_.store(false, std::memory_order_release);
    shutdownComplete_.store(false, std::memory_order_release);
    shutdownLifecycleSucceeded_.store(true, std::memory_order_release);
    shutdownPersistenceSucceeded_.store(true, std::memory_order_release);
    shutdownSucceeded_.store(true, std::memory_order_release);

    // Bootstrap initially runs on the caller's thread. The periodic tick loop
    // takes ownership only after initialization has completed.
    claimServerThreadForBootstrap();
    bool heldByLiveOther = false;
    if (!sessionLock_.acquire(cfg_.worldDir, heldByLiveOther)) {
        if (heldByLiveOther)
            throw std::runtime_error("world is already in use: " + cfg_.worldDir);
        throw std::runtime_error("could not acquire world session lock: " +
                                 cfg_.worldDir);
    }

    initialized_ = true;
    loadStartupData();
    initializeDimensionEngines();
    spawnProtection_ = cfg_.spawnProtection;
    configurePrimaryPersistence();
    initializeJvmRuntime();
    initializePersistedWorld();
    initializeOptionalServices();
    if (jvmRuntime_) (void)jvmRuntime_->onServerWorldsLoaded();
}

void GameServer::loadStartupData() {
    data_.load(cfg_.assetsDir);
    gameData_.load(data_);
    whitelist_.load("whitelist.json");
    if (cfg_.whitelist) whitelist_.setEnabled(true);
    loadOps();
    loadBans();
    loadBannedIps();
    entityDataLoader_.loadDirectory("assets/entities");
    for (const auto& entry : entityDataLoader_.all()) {
        const auto& definition = entry.second;
        std::fprintf(stderr,
                     "[cppfm] entity data: %s max_health=%.1f speed=%.3f\n",
                     definition.type.c_str(), definition.max_health,
                     definition.movement_speed);
    }
    datapackManager_.loadAll(recipes_, "assets/data",
                             cfg_.worldDir + "/datapacks",
                             cfg_.recipesDir);
    functionEvaluator_.setServer(this);
    initCommands();
}

void GameServer::initializeDimensionEngines() {
    auto& overworld = dimensionRuntime(0);
    overworld.lightEngine = std::make_unique<LightEngine>(worldFor(0));
    worldFor(0).setBiomeCodec(
        [this](const std::string& key) { return data_.biomeIndex(key); },
        static_cast<std::int32_t>(data_.biomeIndex(cfg_.worldBiome)));

    // Chunk NBT stores biome registry names. Install the resolver on all
    // dimensions before any lazy load or generation can occur.
    for (const std::int8_t dimension : {std::int8_t{-1}, std::int8_t{1}}) {
        World& dimensionWorld = worldFor(dimension);
        dimensionWorld.setBiomeCodec(
            [this](const std::string& key) { return data_.biomeIndex(key); },
            static_cast<std::int32_t>(
                data_.biomeIndex(dimensionWorld.biomeKey())));
    }

    overworld.fluidSim = std::make_unique<FluidSim>(worldFor(0));
    overworld.redstone = std::make_unique<RedstoneEngine>(worldFor(0));
    overworld.blockTicks = std::make_unique<BlockTickScheduler>(
        worldFor(0), &gamerules_, this);
    registerDefaultBlockTickBehaviors(*overworld.blockTicks);

    for (const std::int8_t dimension : {std::int8_t{-1}, std::int8_t{1}}) {
        World& dimensionWorld = worldFor(dimension);
        auto& runtime = dimensionRuntime(dimension);
        runtime.lightEngine = std::make_unique<LightEngine>(dimensionWorld);
        runtime.fluidSim = std::make_unique<FluidSim>(dimensionWorld);
        runtime.redstone = std::make_unique<RedstoneEngine>(dimensionWorld);
        runtime.blockTicks = std::make_unique<BlockTickScheduler>(
            dimensionWorld, &gamerules_, this);
        registerDefaultBlockTickBehaviors(*runtime.blockTicks);
    }

    // Config was parsed once by main() before GameServer construction. Reading
    // server.properties here again would override CLI values for embedded use.
    constexpr std::array<std::int8_t, 3> dimensions{0, -1, 1};
    for (const auto dimension : dimensions)
        worldFor(dimension).setSimulationDistance(cfg_.simulationDistance);
    for (const auto dimension : dimensions) {
        worldFor(dimension).setSimulationDistanceCallback(
            [this, dimension](std::int32_t cx, std::int32_t cz) {
                return isChunkInSimulationDistanceFor(dimension, cx, cz);
            });
    }

    for (const auto dimension : dimensions)
        configureDimensionEngines(dimension);
}

void GameServer::configurePrimaryPersistence() {
    dimensionRuntime(0).persistence = std::make_unique<Persistence>(
        worldFor(0), cfg_.worldDir, cfg_.worldBiome);
    blockEntitiesFor(0).setDirtyCallback(
        [this](std::int32_t cx, std::int32_t cz) {
            if (auto* storage = persistenceFor(0))
                storage->markDirty(cx, cz);
            invalidateChunkCacheFor(0, cx, cz);
        });

    persistence().setDifficulty(difficulty_);

    std::unordered_map<std::uint16_t, std::string> biomeIdsToNames;
    const auto& biomeOrder = gameData_.order("minecraft:worldgen/biome");
    for (std::size_t i = 0; i < biomeOrder.size(); ++i)
        biomeIdsToNames.emplace(static_cast<std::uint16_t>(i), biomeOrder[i]);
    persistence().setBiomeCodec(
        std::move(biomeIdsToNames),
        static_cast<std::int32_t>(data_.biomeIndex(cfg_.worldBiome)));

    persistence().setChunkExtras(
        [this](std::int32_t cx, std::int32_t cz, nbt::Value& root) {
            nbt::Value entries = nbt::Value::makeList(nbt::Compound);
            blockEntitiesFor(0).writeChunkNbt(cx, cz, entries);
            if (!entries.list.empty())
                root.set("block_entities", std::move(entries));
        },
        {},
        [this](const nbt::Value& root) {
            const auto geometry = worldFor(0).verticalGeometry();
            BlockEntityStore::validateChunkNbt(
                root, geometry.minY, geometry.maxY());
        },
        [this](const nbt::Value& root) {
            return blockEntitiesFor(0).prepareChunkNbt(root);
        });
    persistence().setChunkSaveBarrier(
        [this](std::int32_t cx, std::int32_t cz) {
            // Persistence may invoke this on its worker. Keep redstone/world
            // callbacks on the same serialized simulation domain as ticks.
            SimulationDispatchGuard simulation(simulationDispatchMtx_);
            redstoneFor(0).flushPendingPistons(cx, cz);
        });
    persistence().setChunkSaveAdmission(
        [this](std::int32_t cx, std::int32_t cz) {
            const auto* structures = worldFor(0).structureManager();
            return !structures ||
                   !structures->hasPendingLootInChunk(cx, cz);
        });
    persistence().setChunkSaveComplete(
        [this](std::int32_t cx, std::int32_t cz) {
            blockEntitiesFor(0).clearRemovalTombstonesForChunk(cx, cz);
        });

    persistence().setLevelStateProvider(
        [this](nbt::Value& levelData) {
            nbt::Value gameRules = nbt::Value::makeCompound();
            for (const auto& [key, value] : gamerules_.all())
                gameRules.set(key, nbt::Value::makeString(value));
            levelData.set("GameRules", std::move(gameRules));
            levelData.set("raining", nbt::Value::makeByte(raining() ? 1 : 0));
            levelData.set(
                "rainTime",
                nbt::Value::makeInt(static_cast<std::int32_t>(
                    (weatherUntilTick_ - tickNo_) / 20)));
            levelData.set("thundering", nbt::Value::makeByte(0));
            levelData.set("thunderTime", nbt::Value::makeInt(6000));
            levelData.set("DayTime", nbt::Value::makeLong(dayTime()));
            levelData.set("Time", nbt::Value::makeLong(tickNo_));
            levelData.set("Difficulty", nbt::Value::makeString(difficulty_));
            levelData.set("GameType", nbt::Value::makeInt(defaultGameMode()));
            levelData.set("WanderingTraderSpawnDelay",
                          nbt::Value::makeInt(wanderingTraderSpawnDelay_));
            levelData.set("WanderingTraderSpawnChance",
                          nbt::Value::makeInt(wanderingTraderSpawnChance_));
            // Preserve the UUID already stored in an existing level.dat.
        },
        [this](const nbt::Value& levelData) {
            if (const auto* time = levelData.get("Time"))
                tickNo_ = time->l;
            if (const auto* dayTime = levelData.get("DayTime"))
                timeOffset_ += dayTime->l - this->dayTime();
            if (const auto* gameType = levelData.get("GameType")) {
                std::int32_t mode = -1;
                if (gameType->tag == nbt::Int) mode = gameType->i;
                else if (gameType->tag == nbt::Byte) mode = gameType->b;
                if (mode >= 0 && mode <= 3)
                    setDefaultGameMode(static_cast<std::uint8_t>(mode));
            }
            if (const auto* rain = levelData.get("raining"))
                weather_ = rain->b ? Weather::Rain : Weather::Clear;
            if (const auto* rainTime = levelData.get("rainTime"))
                weatherUntilTick_ = tickNo_ + rainTime->i * 20LL;
            if (const auto* gameRules = levelData.get("GameRules")) {
                for (const auto& [key, value] : gameRules->comp) {
                    std::string encoded;
                    if (value.tag == nbt::String) encoded = value.str;
                    else if (value.tag == nbt::Int)
                        encoded = std::to_string(value.i);
                    else if (value.tag == nbt::Long)
                        encoded = std::to_string(value.l);
                    else if (value.tag == nbt::Byte)
                        encoded = value.b ? "true" : "false";
                    else
                        encoded = value.str;
                    gamerules_.set(key, encoded, false);
                }
            }
            if (const auto* difficulty = levelData.get("Difficulty")) {
                if (difficulty->tag == nbt::String) {
                    difficulty_ = difficulty->str;
                } else if (difficulty->tag == nbt::Byte) {
                    switch (difficulty->b) {
                    case 0: difficulty_ = "peaceful"; break;
                    case 1: difficulty_ = "easy"; break;
                    case 2: difficulty_ = "normal"; break;
                    case 3: difficulty_ = "hard"; break;
                    default: break;
                    }
                }
            }
            if (const auto* delay = levelData.get("WanderingTraderSpawnDelay")) {
                if (delay->tag == nbt::Int) wanderingTraderSpawnDelay_ = delay->i;
                else if (delay->tag == nbt::Long)
                    wanderingTraderSpawnDelay_ = static_cast<int>(delay->l);
            }
            if (const auto* chance = levelData.get("WanderingTraderSpawnChance")) {
                if (chance->tag == nbt::Int) wanderingTraderSpawnChance_ = chance->i;
                else if (chance->tag == nbt::Long)
                    wanderingTraderSpawnChance_ = static_cast<int>(chance->l);
            }
        });

    persistence().loadLevelData();
}

void GameServer::initializePersistedWorld() {
    // level.dat is authoritative for an existing world's seed. Propagate it
    // before either secondary dimension can generate its first chunk.
    cfg_.seed = worldFor(0).seed();
    cfg_.hashedSeed = static_cast<std::int64_t>(cfg_.seed);
    worldFor(-1).setSeed(cfg_.seed ^ 0x4E37ULL);
    worldFor(1).setSeed(cfg_.seed ^ 0xE11DULL);

    // Start loading before ensuring spawn chunks. Otherwise newly generated
    // terrain could hide saved chunks for the lifetime of this server.
    persistence().start();
    for (const char* dimensionDirectory : {"DIM-1", "DIM1"}) {
        const std::string dimensionPath =
            cfg_.worldDir + "/" + dimensionDirectory;
        for (const char* legacyFile : {"level.dat", "level.dat.new"}) {
            const auto path = dimensionPath + "/" + legacyFile;
            if (!std::filesystem::exists(path)) continue;
            if (std::string_view(legacyFile) == "level.dat")
                std::fprintf(stderr,
                    "[cppfm] found legacy %s, removing (single level.dat now)\n",
                    path.c_str());
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }

    // server.properties remains authoritative for difficulty; level.dat
    // supplies the persisted world-border interpolation state.
    difficulty_ = cfg_.difficulty;
    persistence().setDifficulty(difficulty_);

    const auto spawn = worldFor(0).spawnPoint();
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            const auto chunkX = (spawn.x >> 4) + dx;
            const auto chunkZ = (spawn.z >> 4) + dz;
            if (!worldFor(0).generateChunkIfMissing(chunkX, chunkZ)) {
                throw std::runtime_error(
                    "could not load or generate required spawn chunk " +
                    std::to_string(chunkX) + "," + std::to_string(chunkZ));
            }
            worldFor(0).addSpawnTicket(chunkX, chunkZ, tickNo_);
        }
    }

    for (int index = 0; index < 2; ++index) {
        const char* directory = index == 0 ? "DIM-1" : "DIM1";
        const std::int8_t dimension = index == 0 ? -1 : 1;
        auto& runtime = dimensionRuntime(dimension);
        World& dimensionWorld = worldFor(dimension);
        runtime.persistence = std::make_unique<Persistence>(
            dimensionWorld, cfg_.worldDir + "/" + directory, "",
            Persistence::StorageRole::Dimension);

        std::unordered_map<std::uint16_t, std::string> biomeIdsToNames;
        const auto& biomeOrder = gameData_.order("minecraft:worldgen/biome");
        for (std::size_t i = 0; i < biomeOrder.size(); ++i)
            biomeIdsToNames.emplace(static_cast<std::uint16_t>(i),
                                    biomeOrder[i]);
        auto& storage = *runtime.persistence;
        storage.setBiomeCodec(
            std::move(biomeIdsToNames),
            static_cast<std::int32_t>(
                data_.biomeIndex(dimensionWorld.biomeKey())));
        storage.setChunkExtras(
            [this, dimension](std::int32_t cx, std::int32_t cz,
                              nbt::Value& root) {
                nbt::Value entries = nbt::Value::makeList(nbt::Compound);
                blockEntitiesFor(dimension).writeChunkNbt(cx, cz, entries);
                if (!entries.list.empty())
                    root.set("block_entities", std::move(entries));
            },
            {},
            [this, dimension](const nbt::Value& root) {
                const auto geometry = worldFor(dimension).verticalGeometry();
                BlockEntityStore::validateChunkNbt(
                    root, geometry.minY, geometry.maxY());
            },
            [this, dimension](const nbt::Value& root) {
                return blockEntitiesFor(dimension).prepareChunkNbt(root);
            });
        storage.setChunkSaveBarrier(
            [this, dimension](std::int32_t cx, std::int32_t cz) {
                SimulationDispatchGuard simulation(simulationDispatchMtx_);
                redstoneFor(dimension).flushPendingPistons(cx, cz);
            });
        storage.setChunkSaveAdmission(
            [this, dimension](std::int32_t cx, std::int32_t cz) {
                const auto* structures =
                    worldFor(dimension).structureManager();
                return !structures ||
                       !structures->hasPendingLootInChunk(cx, cz);
            });
        storage.setChunkSaveComplete(
            [this, dimension](std::int32_t cx, std::int32_t cz) {
                blockEntitiesFor(dimension)
                    .clearRemovalTombstonesForChunk(cx, cz);
            });
        blockEntitiesFor(dimension).setDirtyCallback(
            [this, dimension](std::int32_t cx, std::int32_t cz) {
                if (auto* dimensionStorage = persistenceFor(dimension))
                    dimensionStorage->markDirty(cx, cz);
                invalidateChunkCacheFor(dimension, cx, cz);
            });
        storage.start();
    }
}

void GameServer::initializeJvmRuntime() {
    if (!cfg_.jvmEnabled) return;

    jvm::JvmConfig jvmConfig;
    jvmConfig.enabled = true;
    jvmConfig.strict = cfg_.jvmStrict;
    jvmConfig.classesDir = cfg_.jvmClassesDir;
    jvmConfig.modsDir = cfg_.jvmModsDir;
    jvmConfig.configDir = cfg_.jvmConfigDir;
    jvmConfig.javaHome = cfg_.jvmJavaHome;
    jvmConfig.jvmLibrary = cfg_.jvmLibrary;
    jvmConfig.jvmLibrariesDir = cfg_.jvmLibrariesDir;
    jvmRuntime_ = std::make_unique<jvm::JvmRuntime>(*this,
                                                    std::move(jvmConfig));
    std::string error;
    if (jvmRuntime_->start(&error)) return;

    std::fprintf(stderr, "[cppfm][jvm] disabled after startup failure: %s\n",
                 error.c_str());
    if (cfg_.jvmStrict)
        throw std::runtime_error("strict JVM startup failed: " + error);
}

void GameServer::initializeOptionalServices() {
    rconServer_ = std::make_unique<RconServer>(cfg_.rcon,
        [this](const std::string& command) { return dispatchConsole(command); });
    const bool rconStarted = rconServer_->start();
    std::fprintf(stderr, "[cppfm] RCON %s (enabled=%d port=%u)\n",
                 rconStarted ? "listening" : "not started",
                 static_cast<int>(cfg_.rcon.enabled), cfg_.rcon.port);

    bossAI_ = std::make_unique<BossAIManager>(*this);
}

void GameServer::stop() {
    // A callback cannot destroy resources that its own stack still uses.
    // Leave final teardown to runForever's external owner or the destructor.
    bool insideInlineServerTask = false;
    {
        std::lock_guard taskLock(serverThreadTasksMtx_);
        insideInlineServerTask =
            serverThreadId_ == std::this_thread::get_id() &&
            inlineServerThreadTasks_ != 0;
    }
    if (isCurrentTickThread() || insideInlineServerTask ||
        sessionThreadOwner_ == this) {
        requestStop();
        cancelServerThreadTasks();
        stopCv_.notify_all();
        return;
    }

    // A mod's terminal callback may synchronously join a worker that asks
    // this server to stop. Waiting for the callback's tick thread here would
    // deadlock that join. Publish the stop request and let the runForever()
    // owner perform the synchronous teardown after the callback unwinds.
    bool deferredForLifecycleCallback = false;
    {
        std::lock_guard taskLock(serverThreadTasksMtx_);
        if (finalLifecycleCallbackDepth_ != 0 &&
            finalLifecycleThreadId_ != std::this_thread::get_id()) {
            requestStop();
            deferredForLifecycleCallback = true;
        }
    }
    if (deferredForLifecycleCallback) {
        cancelServerThreadTasks();
        stopCv_.notify_all();
        return;
    }

    // Serialize the synchronous owner before invoking extension code. The
    // foreign-callback deferral above must stay before this lock: a mod may
    // join a worker that calls stop(), and that worker must be able to return.
    std::unique_lock shutdownOwner(stopOwnerMtx_);
    if (shutdownComplete_.load(std::memory_order_acquire)) return;

    // A previous attempt may already have fenced ingress but failed to claim
    // terminal server-thread ownership. Keep that fence in place and allow a
    // later stop()/destructor call to retry the teardown instead of treating
    // the partial attempt as complete.
    bool initiatingShutdown = false;
    {
        std::lock_guard startupLock(lifecycleMtx_);
        initiatingShutdown =
            !shutdownStarted_.load(std::memory_order_acquire);
    }
    if (initiatingShutdown) {
        // Fabric SERVER_STOPPING runs while the network and worlds are still
        // available. The tick-loop terminal hook shares an idempotent JVM
        // lifecycle stage and cannot dispatch this event a second time.
        const bool stoppingSucceeded =
            !jvmRuntime_ || jvmRuntime_->onServerStopping();
        shutdownLifecycleSucceeded_.store(
            stoppingSucceeded, std::memory_order_release);
        {
            std::lock_guard startupLock(lifecycleMtx_);
            shutdownStarted_.store(true, std::memory_order_release);
            requestStop();
        }
    } else {
        requestStop();
    }

    // The signal/console path deliberately does not lock the task queue.
    cancelServerThreadTasks();
    stopCv_.notify_all();
    std::fprintf(stderr, "[cppfm] stopping tick loop\n");
    stopTickLoop();

    // All ordinary work was fenced above. Quiesce operations that were
    // already executing on the bootstrap/server owner before borrowing the
    // terminal callback owner; shutdown callbacks may then enqueue only the
    // explicitly allowed disconnect work.
    waitForServerThreadTasks();

    bool persistenceSucceeded = false;
    const bool teardownOwned = runFinalLifecycleCallback([&] {
        stopIngress();
        closeListeningSocket();
        stopWorkerThreads();

        // Fabric orders world unload after stopping/disconnect and
        // SERVER_STOPPED after persistence has closed. Keep all three native
        // world handles valid through their UNLOAD callbacks, then fence them
        // before STOPPED.
        bool lifecycleSucceeded = shutdownLifecycleSucceeded_.load(
            std::memory_order_acquire);
        if (jvmRuntime_)
            lifecycleSucceeded =
                jvmRuntime_->onWorldsUnloading() && lifecycleSucceeded;
        persistenceSucceeded = stopPersistenceAndFlush();
        releaseWorldLock();
        if (jvmRuntime_)
            lifecycleSucceeded =
                jvmRuntime_->onServerStopped() && lifecycleSucceeded;
        if (jvmRuntime_) {
            std::fprintf(stderr, "[cppfm] stopping embedded JVM\n");
            jvmRuntime_->stop();
        }
        shutdownLifecycleSucceeded_.store(
            lifecycleSucceeded, std::memory_order_release);
        shutdownSucceeded_.store(
            lifecycleSucceeded && persistenceSucceeded,
            std::memory_order_release);
        shutdownComplete_.store(true, std::memory_order_release);
        reportShutdownResult(persistenceSucceeded);
        return true;
    });
    if (!teardownOwned) {
        shutdownSucceeded_.store(false, std::memory_order_release);
        std::fprintf(stderr,
                     "[cppfm] refusing unsafe teardown: terminal server-thread ownership was unavailable\n");
    }
}

void GameServer::stopIngress() {
    stopClientConnections();
    // Drain command ingress before JVM teardown: an RCON worker can re-enter
    // the Java command boundary through dispatchConsole().
    std::fprintf(stderr, "[cppfm] stopping rcon\n");
    if (rconServer_) rconServer_->stop();
    waitForServerThreadTasks();
    closeListeningSocket();
}

void GameServer::stopWorkerThreads() {
    joinJanitorThread();
    joinSessionThreads();
    std::fprintf(stderr, "[cppfm] stopping I/O workers\n");
    ioPool_.shutdown();
}

bool GameServer::stopPersistenceAndFlush() {
    bool allSavesSucceeded = flushPendingStructureLootForShutdown();
    std::fprintf(stderr, "[cppfm] stopping persistence\n");
    const auto stopOne = [](Persistence& storage) {
        for (;;) {
            const auto result = storage.stopWithStatus();
            if (result == Persistence::StopStatus::StoppedCleanly)
                return true;
            if (result == Persistence::StopStatus::UnsavedData)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    for (const std::int8_t dimension :
         {std::int8_t{0}, std::int8_t{-1}, std::int8_t{1}}) {
        if (auto* storage = persistenceFor(dimension))
            allSavesSucceeded = stopOne(*storage) && allSavesSucceeded;
    }
    shutdownPersistenceSucceeded_.store(
        allSavesSucceeded, std::memory_order_release);
    return allSavesSucceeded;
}

void GameServer::closeListeningSocket() noexcept {
    if (const auto fd = listenFd_.exchange(platform::invalid_socket,
                                           std::memory_order_acq_rel);
        platform::isValid(fd)) {
        platform::closeSocket(fd);
    }
}

void GameServer::releaseWorldLock() noexcept {
    sessionLock_.release(); // plan46 §2 (O-08)
}

void GameServer::reportShutdownResult(
    const bool persistenceSucceeded) const noexcept {
    if (persistenceSucceeded)
        std::fprintf(stderr, "[cppfm] stopped cleanly\n");
    else
        std::fprintf(stderr,
                     "[cppfm] stopped with unsaved world state; inspect persistence errors\n");
}

void GameServer::registerDefaultBlockTickBehaviors(
    BlockTickScheduler& scheduler) {
    registerBehaviorFor<CropBehavior>(
        scheduler, {"minecraft:wheat", "minecraft:potatoes",
                    "minecraft:carrots", "minecraft:beetroots",
                    "minecraft:torchflower_crop", "minecraft:pitcher_crop"});
    registerBehaviorFor<SaplingBehavior>(
        scheduler, {"minecraft:oak_sapling", "minecraft:spruce_sapling",
                    "minecraft:birch_sapling", "minecraft:jungle_sapling"});
    registerBehaviorFor<StemBehavior>(
        scheduler, {"minecraft:bamboo"}, 16);
    registerBehaviorFor<StemBehavior>(
        scheduler, {"minecraft:sugar_cane", "minecraft:cactus"}, 4);
    registerBehaviorFor<FarmlandBehavior>(scheduler, {"minecraft:farmland"});
    registerBehaviorFor<CocoaBehavior>(scheduler, {"minecraft:cocoa"});
    registerBehaviorFor<SweetBerryBehavior>(
        scheduler, {"minecraft:sweet_berry_bush", "minecraft:sweet_berries"});
    registerBehaviorFor<NetherWartBehavior>(
        scheduler, {"minecraft:nether_wart"});
    registerBehaviorFor<ChorusFlowerBehavior>(
        scheduler, {"minecraft:chorus_flower"});
    registerBehaviorFor<KelpBehavior>(scheduler, {"minecraft:kelp"});
    registerBehaviorFor<SeagrassBehavior>(scheduler, {"minecraft:seagrass"});
    registerBehaviorFor<FireBehavior>(scheduler, {"minecraft:fire"});
    registerBehaviorFor<SoulFireBehavior>(scheduler, {"minecraft:soul_fire"});
    registerBehaviorFor<CampfireBehavior>(
        scheduler, {"minecraft:campfire", "minecraft:soul_campfire"});
    registerBehaviorFor<PortalAgeBehavior>(
        scheduler, {"minecraft:nether_portal"});
    registerBehaviorFor<PaleOakLeavesBehavior>(
        scheduler, {"minecraft:pale_oak_leaves"});
    registerBehaviorFor<CreakingHeartBehavior>(
        scheduler, {"minecraft:creaking_heart"});
}

void GameServer::configureDimensionEngines(std::int8_t dimension) {
    auto& runtime = dimensionRuntime(dimension);
    World& dimensionWorld = worldFor(dimension);
    auto& lightEngine = *runtime.lightEngine;
    auto& fluidSim = *runtime.fluidSim;
    auto& redstone = *runtime.redstone;
    auto& blockEntities = runtime.blockEntities;

    redstone.setBlockEntityStore(&blockEntities);
    redstone.setTickRef(&tickNo_);
    redstone.setBlockTickScheduler(runtime.blockTicks.get());
    redstone.setGameServer(this);
    redstone.setPistonChunkDirtyFn(
        [this, dimension](std::int32_t cx, std::int32_t cz) {
            if (auto* storage = persistenceFor(dimension))
                storage->markDirty(cx, cz);
        });
    redstone.setBroadcastFn(
        [this, dimension](std::int32_t x, std::int32_t y,
                          std::int32_t z, std::uint16_t state) {
            queueBlockChangeFor(dimension, x, y, z, state);
            invalidateChunkCacheFor(dimension, x >> 4, z >> 4);
        });
    dimensionWorld.setOnBlockChanged(
        [this, dimension, light = &lightEngine, fluids = &fluidSim,
         redstoneEngine = &redstone,
         blockEntityStore = &blockEntities](std::int32_t x, std::int32_t y,
                                           std::int32_t z,
                                           std::uint16_t oldState,
                                           std::uint16_t newState) {
            invalidateChunkCacheFor(dimension, x >> 4, z >> 4);
            const auto* oldBlock = gen::blockByState(oldState);
            const auto* newBlock = gen::blockByState(newState);
            if (oldBlock &&
                (!newBlock || oldBlock->name != newBlock->name)) {
                const auto key = posKey(x, y, z);
                // Piston animation stages its MovingPiston owner before
                // changing an ordinary block to moving_piston so the whole
                // block-entity batch is admitted before world mutation.
                // Preserve that new owner instead of treating it as stale.
                const bool installingMovingPiston = newBlock &&
                    newBlock->name == "minecraft:moving_piston" &&
                    blockEntityStore->hasCurrentKind(
                        key, BlockEntity::Kind::MovingPiston);
                const auto replacementKind =
                    blockEntityKindForBlockState(newState);
                const bool replacementEntityAlreadyStaged = replacementKind &&
                    (blockEntityStore->hasCurrentKind(key, *replacementKind) ||
                     blockEntityStore->hasOpaqueNbtAt(key));
                if (!installingMovingPiston &&
                    !replacementEntityAlreadyStaged) {
                    const bool dirtyAccepted =
                        blockEntityStore->removeAfterWorldBlockReplacement(key);
                    if (!dirtyAccepted)
                        std::fprintf(stderr,
                            "[cppfm] block entity at %d,%d,%d was removed after its dirty callback failed\n",
                            x, y, z);
                }
            }

            light->onBlockChanged(x, y, z, oldState, newState);
            fluids->touch(x, y, z);
            static constexpr int kNeighborX[6] = {1, -1, 0, 0, 0, 0};
            static constexpr int kNeighborY[6] = {0, 0, 1, -1, 0, 0};
            static constexpr int kNeighborZ[6] = {0, 0, 0, 0, 1, -1};
            for (int side = 0; side < 6; ++side) {
                fluids->touch(x + kNeighborX[side],
                              y + kNeighborY[side],
                              z + kNeighborZ[side]);
            }

            // The world invokes this hook while holding its mutation-order
            // gate. Never wait on RedstoneEngine::operationMutex_ here:
            // persistence drains redstone before acquiring the world gate.
            redstoneEngine->enqueueBlockChanged(x, y, z);
        });
}

} // namespace cppfm
