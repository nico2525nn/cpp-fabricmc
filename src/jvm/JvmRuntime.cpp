#include "JvmRuntime.hpp"

#include "../game/GameServer.hpp"
#include "../game/World.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(CPPFM_HAS_JNI)
#include <jni.h>
#endif

namespace cppfm::jvm {

struct JvmRuntime::Impl {
    GameServer& server;
    JvmConfig config;
    NativeHandleTable handles;
    JavaObjectCache objects;
    ModRoutingTable routing;
    mutable std::recursive_mutex callMutex;
    std::string lastError;
    bool started = false;
    std::uint64_t serverHandle = 0;

    std::atomic<std::uint64_t> ticks{0};
    std::atomic<std::uint64_t> joins{0};
    std::atomic<std::uint64_t> quits{0};
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::uint64_t> callbackErrors{0};
    std::atomic<std::size_t> discoveredMods{0};
    std::atomic<std::size_t> initializedEntrypoints{0};

#if defined(CPPFM_HAS_JNI)
    void* jvmLibrary = nullptr;
    JavaVM* vm = nullptr;
    jclass runtimeClass = nullptr;
    jclass bridgeClass = nullptr;
    jmethodID bootstrap = nullptr;
    jmethodID shutdown = nullptr;
    jmethodID serverTick = nullptr;
    jmethodID playerJoin = nullptr;
    jmethodID playerQuit = nullptr;
    jmethodID chat = nullptr;
    jmethodID blockBreak = nullptr;
    jmethodID blockPlace = nullptr;
    jmethodID blockClicked = nullptr;
    jmethodID command = nullptr;
    jmethodID entityDamage = nullptr;
    jmethodID mobSpawn = nullptr;
#endif

    explicit Impl(GameServer& s, JvmConfig c)
        : server(s), config(std::move(c)) {}
};

namespace {

std::atomic<JvmRuntime*> g_activeRuntime{nullptr};

#if defined(CPPFM_HAS_JNI)

class AttachedEnv {
public:
    explicit AttachedEnv(JavaVM* vm) : vm_(vm) {
        if (!vm_) return;
        void* raw = nullptr;
        const jint result = vm_->GetEnv(&raw, JNI_VERSION_1_8);
        if (result == JNI_OK) {
            env_ = static_cast<JNIEnv*>(raw);
            return;
        }
        if (result == JNI_EDETACHED &&
            vm_->AttachCurrentThread(reinterpret_cast<void**>(&env_), nullptr) == JNI_OK) {
            attached_ = true;
        }
    }

    ~AttachedEnv() {
        if (attached_ && vm_) vm_->DetachCurrentThread();
    }

    JNIEnv* get() const { return env_; }
    explicit operator bool() const { return env_ != nullptr; }

private:
    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

std::string envString(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string fromJavaString(JNIEnv* env, jstring value) {
    if (!env || !value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

jstring toJavaString(JNIEnv* env, const std::string& value) {
    return env ? env->NewStringUTF(value.c_str()) : nullptr;
}

bool clearJavaException(JNIEnv* env, JvmRuntime* runtime, const char* where) {
    if (!env || !env->ExceptionCheck()) return false;
    if (runtime) {
        std::fprintf(stderr, "[cppfm][jvm] Java exception at %s\n", where);
        env->ExceptionDescribe();
        runtime->nativeLog("ERROR", std::string("Java exception at ") + where);
    }
    env->ExceptionClear();
    return true;
}

std::filesystem::path findJvmLibrary(const JvmConfig& config) {
    std::vector<std::filesystem::path> candidates;
    if (!config.jvmLibrary.empty()) candidates.emplace_back(config.jvmLibrary);
    const auto explicitPath = envString("CPPFM_JVM_LIBRARY");
    if (!explicitPath.empty()) candidates.emplace_back(explicitPath);

    std::vector<std::string> homes;
    if (!config.javaHome.empty()) homes.push_back(config.javaHome);
    if (const auto home = envString("JAVA_HOME"); !home.empty()) homes.push_back(home);
    for (const auto& home : homes) {
        candidates.emplace_back(std::filesystem::path(home) / "lib/server/libjvm.so");
        candidates.emplace_back(std::filesystem::path(home) / "jre/lib/amd64/server/libjvm.so");
    }

    const std::array<std::filesystem::path, 3> roots = {
        "/usr/lib/jvm", "/opt/java", "/usr/java"
    };
    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec || !entry.is_directory(ec)) continue;
            candidates.emplace_back(entry.path() / "lib/server/libjvm.so");
            candidates.emplace_back(entry.path() / "jre/lib/amd64/server/libjvm.so");
        }
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

using CreateJvmFn = jint (*)(JavaVM**, void**, void*);

void setError(JvmRuntime::Impl& impl, std::string message) {
    impl.lastError = std::move(message);
    std::fprintf(stderr, "[cppfm][jvm] %s\n", impl.lastError.c_str());
}

bool registerBridge(JNIEnv* env, JvmRuntime* runtime, jclass bridgeClass);

JNIEXPORT void JNICALL nativeLog(JNIEnv* env, jclass, jstring level, jstring message) {
    if (auto* runtime = g_activeRuntime.load(std::memory_order_acquire)) {
        runtime->nativeLog(fromJavaString(env, level), fromJavaString(env, message));
    }
}

JNIEXPORT jlong JNICALL nativeServerHandle(JNIEnv*, jclass) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? static_cast<jlong>(runtime->nativeServerHandle()) : 0;
}

JNIEXPORT jlong JNICALL nativeCurrentTick(JNIEnv*, jclass) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? static_cast<jlong>(runtime->nativeCurrentTick()) : 0;
}

JNIEXPORT jstring JNICALL nativePlayerName(JNIEnv* env, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? toJavaString(env, runtime->nativePlayerName(static_cast<std::uint64_t>(handle))) : nullptr;
}

JNIEXPORT jstring JNICALL nativePlayerUuid(JNIEnv* env, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? toJavaString(env, runtime->nativePlayerUuid(static_cast<std::uint64_t>(handle))) : nullptr;
}

JNIEXPORT jint JNICALL nativePlayerEntityId(JNIEnv*, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativePlayerEntityId(static_cast<std::uint64_t>(handle)) : -1;
}

JNIEXPORT jint JNICALL nativePlayerGameMode(JNIEnv*, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativePlayerGameMode(static_cast<std::uint64_t>(handle)) : 0;
}

JNIEXPORT jboolean JNICALL nativePlayerSneaking(JNIEnv*, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && runtime->nativePlayerSneaking(static_cast<std::uint64_t>(handle))
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL nativeOnlinePlayerCount(JNIEnv*, jclass) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativeOnlinePlayerCount() : 0;
}

JNIEXPORT jlong JNICALL nativeOnlinePlayerHandle(JNIEnv*, jclass, jint index) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && index >= 0 ? static_cast<jlong>(runtime->nativeOnlinePlayerHandle(
        static_cast<std::size_t>(index))) : 0;
}

JNIEXPORT jint JNICALL nativePlayerHeldSlot(JNIEnv*, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativePlayerHeldSlot(static_cast<std::uint64_t>(handle)) : 0;
}

JNIEXPORT jint JNICALL nativePlayerInventoryItemId(JNIEnv*, jclass, jlong handle, jint slot) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativePlayerInventoryItemId(
        static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot)) : 0;
}

JNIEXPORT jint JNICALL nativePlayerInventoryItemCount(JNIEnv*, jclass, jlong handle, jint slot) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativePlayerInventoryItemCount(
        static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot)) : 0;
}

JNIEXPORT jstring JNICALL nativePlayerInventoryItemName(JNIEnv* env, jclass,
                                                        jlong handle, jint slot) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? toJavaString(env, runtime->nativePlayerInventoryItemName(
        static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot))) : nullptr;
}

JNIEXPORT jboolean JNICALL nativePlayerSetInventoryItemCount(JNIEnv*, jclass,
                                                              jlong handle, jint slot,
                                                              jint count) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && runtime->nativePlayerSetInventoryItemCount(
        static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot),
               static_cast<std::int32_t>(count)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL nativePlayerSetInventoryItem(JNIEnv*, jclass,
                                                         jlong handle, jint slot,
                                                         jint itemId, jint count) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && runtime->nativePlayerSetInventoryItem(
        static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot),
        static_cast<std::int32_t>(itemId), static_cast<std::int32_t>(count))
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jdouble JNICALL nativePlayerCoordinate(JNIEnv*, jclass, jlong handle, jint axis) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativePlayerCoordinate(static_cast<std::uint64_t>(handle), axis) : 0.0;
}

JNIEXPORT jboolean JNICALL nativePlayerSetPosition(JNIEnv*, jclass, jlong handle,
                                           jdouble x, jdouble y, jdouble z) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && runtime->nativePlayerSetPosition(static_cast<std::uint64_t>(handle), x, y, z)
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL nativePlayerSendMessage(JNIEnv* env, jclass, jlong handle,
                                            jstring message, jboolean overlay) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && runtime->nativePlayerSendMessage(
        static_cast<std::uint64_t>(handle), fromJavaString(env, message), overlay == JNI_TRUE)
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL nativePlayerWorld(JNIEnv*, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? static_cast<jlong>(runtime->nativePlayerWorld(static_cast<std::uint64_t>(handle))) : 0;
}

JNIEXPORT jlong JNICALL nativeServerWorld(JNIEnv*, jclass, jint dimension) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? static_cast<jlong>(runtime->nativeServerWorld(dimension)) : 0;
}

JNIEXPORT jstring JNICALL nativeWorldName(JNIEnv* env, jclass, jlong handle) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? toJavaString(env, runtime->nativeWorldName(static_cast<std::uint64_t>(handle))) : nullptr;
}

JNIEXPORT jint JNICALL nativeWorldBlock(JNIEnv*, jclass, jlong handle, jint x, jint y, jint z) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime ? runtime->nativeWorldBlock(static_cast<std::uint64_t>(handle), x, y, z) : 0;
}

JNIEXPORT jboolean JNICALL nativeWorldSetBlock(JNIEnv*, jclass, jlong handle,
                                       jint x, jint y, jint z, jint state) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && runtime->nativeWorldSetBlock(
        static_cast<std::uint64_t>(handle), x, y, z, state) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL nativeExecuteCommand(JNIEnv* env, jclass, jstring command) {
    auto* runtime = g_activeRuntime.load(std::memory_order_acquire);
    return runtime && runtime->nativeExecuteCommand(fromJavaString(env, command))
               ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL nativeSetModStats(JNIEnv*, jclass, jint discovered, jint initialized) {
    if (auto* runtime = g_activeRuntime.load(std::memory_order_acquire))
        runtime->nativeSetModStats(static_cast<std::size_t>(std::max(0, discovered)),
                                   static_cast<std::size_t>(std::max(0, initialized)));
}

JNIEXPORT void JNICALL nativeRegisterTransformedMethod(JNIEnv* env, jclass,
                                               jstring owner, jstring name,
                                               jstring descriptor) {
    if (auto* runtime = g_activeRuntime.load(std::memory_order_acquire)) {
        runtime->nativeRegisterTransformedMethod(
            fromJavaString(env, owner), fromJavaString(env, name),
            fromJavaString(env, descriptor));
    }
}

bool registerBridge(JNIEnv* env, JvmRuntime* runtime, jclass bridgeClass) {
    static JNINativeMethod methods[] = {
        {const_cast<char*>("nativeLog"), const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;)V"), reinterpret_cast<void*>(nativeLog)},
        {const_cast<char*>("nativeServerHandle"), const_cast<char*>("()J"), reinterpret_cast<void*>(nativeServerHandle)},
        {const_cast<char*>("nativeCurrentTick"), const_cast<char*>("()J"), reinterpret_cast<void*>(nativeCurrentTick)},
        {const_cast<char*>("nativePlayerName"), const_cast<char*>("(J)Ljava/lang/String;"), reinterpret_cast<void*>(nativePlayerName)},
        {const_cast<char*>("nativePlayerUuid"), const_cast<char*>("(J)Ljava/lang/String;"), reinterpret_cast<void*>(nativePlayerUuid)},
        {const_cast<char*>("nativePlayerEntityId"), const_cast<char*>("(J)I"), reinterpret_cast<void*>(nativePlayerEntityId)},
        {const_cast<char*>("nativePlayerGameMode"), const_cast<char*>("(J)I"), reinterpret_cast<void*>(nativePlayerGameMode)},
        {const_cast<char*>("nativePlayerSneaking"), const_cast<char*>("(J)Z"), reinterpret_cast<void*>(nativePlayerSneaking)},
        {const_cast<char*>("nativeOnlinePlayerCount"), const_cast<char*>("()I"), reinterpret_cast<void*>(nativeOnlinePlayerCount)},
        {const_cast<char*>("nativeOnlinePlayerHandle"), const_cast<char*>("(I)J"), reinterpret_cast<void*>(nativeOnlinePlayerHandle)},
        {const_cast<char*>("nativePlayerHeldSlot"), const_cast<char*>("(J)I"), reinterpret_cast<void*>(nativePlayerHeldSlot)},
        {const_cast<char*>("nativePlayerInventoryItemId"), const_cast<char*>("(JI)I"), reinterpret_cast<void*>(nativePlayerInventoryItemId)},
        {const_cast<char*>("nativePlayerInventoryItemCount"), const_cast<char*>("(JI)I"), reinterpret_cast<void*>(nativePlayerInventoryItemCount)},
        {const_cast<char*>("nativePlayerInventoryItemName"), const_cast<char*>("(JI)Ljava/lang/String;"), reinterpret_cast<void*>(nativePlayerInventoryItemName)},
        {const_cast<char*>("nativePlayerSetInventoryItemCount"), const_cast<char*>("(JII)Z"), reinterpret_cast<void*>(nativePlayerSetInventoryItemCount)},
        {const_cast<char*>("nativePlayerSetInventoryItem"), const_cast<char*>("(JIII)Z"), reinterpret_cast<void*>(nativePlayerSetInventoryItem)},
        {const_cast<char*>("nativePlayerCoordinate"), const_cast<char*>("(JI)D"), reinterpret_cast<void*>(nativePlayerCoordinate)},
        {const_cast<char*>("nativePlayerSetPosition"), const_cast<char*>("(JDDD)Z"), reinterpret_cast<void*>(nativePlayerSetPosition)},
        {const_cast<char*>("nativePlayerSendMessage"), const_cast<char*>("(JLjava/lang/String;Z)Z"), reinterpret_cast<void*>(nativePlayerSendMessage)},
        {const_cast<char*>("nativePlayerWorld"), const_cast<char*>("(J)J"), reinterpret_cast<void*>(nativePlayerWorld)},
        {const_cast<char*>("nativeServerWorld"), const_cast<char*>("(I)J"), reinterpret_cast<void*>(nativeServerWorld)},
        {const_cast<char*>("nativeWorldName"), const_cast<char*>("(J)Ljava/lang/String;"), reinterpret_cast<void*>(nativeWorldName)},
        {const_cast<char*>("nativeWorldBlock"), const_cast<char*>("(JIII)I"), reinterpret_cast<void*>(nativeWorldBlock)},
        {const_cast<char*>("nativeWorldSetBlock"), const_cast<char*>("(JIIII)Z"), reinterpret_cast<void*>(nativeWorldSetBlock)},
        {const_cast<char*>("nativeExecuteCommand"), const_cast<char*>("(Ljava/lang/String;)Z"), reinterpret_cast<void*>(nativeExecuteCommand)},
        {const_cast<char*>("nativeSetModStats"), const_cast<char*>("(II)V"), reinterpret_cast<void*>(nativeSetModStats)},
        {const_cast<char*>("nativeRegisterTransformedMethod"), const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"), reinterpret_cast<void*>(nativeRegisterTransformedMethod)},
    };
    if (env->RegisterNatives(bridgeClass, methods,
                             static_cast<jint>(sizeof(methods) / sizeof(methods[0]))) != JNI_OK) {
        clearJavaException(env, runtime, "RegisterNatives");
        return false;
    }
    return true;
}

#endif // CPPFM_HAS_JNI

} // namespace

JvmRuntime::JvmRuntime(GameServer& server, JvmConfig config)
    : impl_(std::make_unique<Impl>(server, std::move(config))) {}

JvmRuntime::~JvmRuntime() { stop(); }

bool JvmRuntime::start(std::string* error) {
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    if (impl.started) return true;
    if (!impl.config.enabled) return true;

#if !defined(CPPFM_HAS_JNI)
    setError(impl, "cppfm was built without JNI headers; rebuild with a JDK");
    if (error) *error = impl.lastError;
    return false;
#else
    std::filesystem::path classesDir = impl.config.classesDir;
    if (classesDir.empty()) {
        if (const auto envPath = envString("CPPFM_JVM_CLASSES"); !envPath.empty())
            classesDir = envPath;
#ifdef CPPFM_DEFAULT_JVM_CLASSES
        if (classesDir.empty()) classesDir = CPPFM_DEFAULT_JVM_CLASSES;
#endif
        if (classesDir.empty()) classesDir = "jvm/classes";
    }
    std::error_code ec;
    classesDir = std::filesystem::absolute(classesDir, ec);
    if (ec || !std::filesystem::is_directory(classesDir, ec)) {
        setError(impl, "JVM classes directory does not exist: " + classesDir.string());
        if (error) *error = impl.lastError;
        return false;
    }

    const auto library = findJvmLibrary(impl.config);
    if (library.empty()) {
        setError(impl, "HotSpot libjvm.so was not found; set JAVA_HOME or CPPFM_JVM_LIBRARY");
        if (error) *error = impl.lastError;
        return false;
    }
    impl.jvmLibrary = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!impl.jvmLibrary) {
        setError(impl, "dlopen(" + library.string() + ") failed: " + dlerror());
        if (error) *error = impl.lastError;
        return false;
    }
    auto* create = reinterpret_cast<CreateJvmFn>(dlsym(impl.jvmLibrary, "JNI_CreateJavaVM"));
    if (!create) {
        setError(impl, "JNI_CreateJavaVM is missing from " + library.string());
        dlclose(impl.jvmLibrary);
        impl.jvmLibrary = nullptr;
        if (error) *error = impl.lastError;
        return false;
    }

    const std::filesystem::path modsDir =
        std::filesystem::absolute(impl.config.modsDir, ec);
    const std::filesystem::path configDir =
        std::filesystem::absolute(impl.config.configDir, ec);
    std::vector<std::string> optionStorage;
    optionStorage.emplace_back("-Djava.class.path=" + classesDir.string());
    optionStorage.emplace_back("-Dcppfm.mods.dir=" + modsDir.string());
    optionStorage.emplace_back("-Dcppfm.config.dir=" + configDir.string());
    optionStorage.emplace_back("-Dcppfm.game.version=1.21.4");
    optionStorage.emplace_back("-Dcppfm.protocol=769");
    optionStorage.emplace_back(std::string("-Dcppfm.jvm.strict=") +
                               (impl.config.strict ? "true" : "false"));
    optionStorage.emplace_back("-Xrs");
    std::vector<JavaVMOption> options;
    options.reserve(optionStorage.size());
    for (auto& value : optionStorage) options.push_back(JavaVMOption{value.data(), nullptr});

    JavaVMInitArgs args{};
    args.version = JNI_VERSION_1_8;
    args.nOptions = static_cast<jint>(options.size());
    args.options = options.data();
    args.ignoreUnrecognized = JNI_FALSE;
    JNIEnv* env = nullptr;
    const jint createResult = create(&impl.vm, reinterpret_cast<void**>(&env), &args);
    if (createResult != JNI_OK || !impl.vm || !env) {
        setError(impl, "JNI_CreateJavaVM failed with code " + std::to_string(createResult));
        if (impl.jvmLibrary) dlclose(impl.jvmLibrary);
        impl.jvmLibrary = nullptr;
        impl.vm = nullptr;
        if (error) *error = impl.lastError;
        return false;
    }

    jclass bridgeLocal = env->FindClass("cppfm/bridge/NativeBridge");
    if (!bridgeLocal || clearJavaException(env, this, "FindClass NativeBridge")) {
        setError(impl, "NativeBridge class is unavailable in JVM classes directory");
        stop();
        if (error) *error = impl.lastError;
        return false;
    }
    impl.bridgeClass = static_cast<jclass>(env->NewGlobalRef(bridgeLocal));
    env->DeleteLocalRef(bridgeLocal);
    if (!registerBridge(env, this, impl.bridgeClass)) {
        setError(impl, "failed to register NativeBridge methods");
        stop();
        if (error) *error = impl.lastError;
        return false;
    }

    jclass runtimeLocal = env->FindClass("cppfm/bridge/CppModRuntime");
    if (!runtimeLocal || clearJavaException(env, this, "FindClass CppModRuntime")) {
        setError(impl, "CppModRuntime class is unavailable in JVM classes directory");
        stop();
        if (error) *error = impl.lastError;
        return false;
    }
    impl.runtimeClass = static_cast<jclass>(env->NewGlobalRef(runtimeLocal));
    env->DeleteLocalRef(runtimeLocal);
    impl.bootstrap = env->GetStaticMethodID(impl.runtimeClass, "bootstrap",
                                             "(Ljava/lang/String;Ljava/lang/String;)Z");
    impl.shutdown = env->GetStaticMethodID(impl.runtimeClass, "shutdown", "()V");
    impl.serverTick = env->GetStaticMethodID(impl.runtimeClass, "onServerTick", "(J)V");
    impl.playerJoin = env->GetStaticMethodID(impl.runtimeClass, "onPlayerJoin", "(J)V");
    impl.playerQuit = env->GetStaticMethodID(impl.runtimeClass, "onPlayerQuit", "(J)V");
    impl.chat = env->GetStaticMethodID(impl.runtimeClass, "onChat", "(JLjava/lang/String;)Ljava/lang/String;");
    impl.blockBreak = env->GetStaticMethodID(impl.runtimeClass, "onBlockBreak", "(JIIII)Z");
    impl.blockPlace = env->GetStaticMethodID(impl.runtimeClass, "onBlockPlace", "(JIIII)Z");
    impl.blockClicked = env->GetStaticMethodID(impl.runtimeClass, "onBlockClicked", "(JIIIII)Z");
    impl.command = env->GetStaticMethodID(impl.runtimeClass, "onCommand", "(JLjava/lang/String;)Ljava/lang/String;");
    impl.entityDamage = env->GetStaticMethodID(impl.runtimeClass, "onEntityDamage", "(JJFLjava/lang/String;)Z");
    impl.mobSpawn = env->GetStaticMethodID(impl.runtimeClass, "onMobSpawn", "(JDDD)Z");
    if (clearJavaException(env, this, "resolve CppModRuntime methods") ||
        !impl.bootstrap || !impl.shutdown || !impl.serverTick || !impl.playerJoin ||
        !impl.playerQuit || !impl.chat || !impl.blockBreak || !impl.blockPlace ||
        !impl.blockClicked || !impl.command || !impl.entityDamage || !impl.mobSpawn) {
        setError(impl, "CppModRuntime is missing a required lifecycle method");
        stop();
        if (error) *error = impl.lastError;
        return false;
    }

    g_activeRuntime.store(this, std::memory_order_release);
    impl.serverHandle = impl.handles.registerObject(&impl.server, HandleKind::Server);
    const jstring mods = toJavaString(env, modsDir.string());
    const jstring config = toJavaString(env, configDir.string());
    const jboolean ok = env->CallStaticBooleanMethod(impl.runtimeClass, impl.bootstrap, mods, config);
    env->DeleteLocalRef(mods);
    env->DeleteLocalRef(config);
    if (clearJavaException(env, this, "CppModRuntime.bootstrap") || ok != JNI_TRUE) {
        setError(impl, "Java mod bootstrap failed");
        stop();
        if (error) *error = impl.lastError;
        return false;
    }
    impl.started = true;
    std::fprintf(stderr, "[cppfm][jvm] embedded HotSpot started; mods=%s classes=%s\n",
                 modsDir.string().c_str(), classesDir.string().c_str());
    return true;
#endif
}

void JvmRuntime::stop() {
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    if (!impl.vm) {
        impl.started = false;
        return;
    }
#if defined(CPPFM_HAS_JNI)
    g_activeRuntime.store(nullptr, std::memory_order_release);
    AttachedEnv attached(impl.vm);
    if (attached) {
        auto* env = attached.get();
        if (impl.started && impl.runtimeClass && impl.shutdown) {
            env->CallStaticVoidMethod(impl.runtimeClass, impl.shutdown);
            clearJavaException(env, this, "CppModRuntime.shutdown");
        }
        impl.objects.clear(env);
        if (impl.runtimeClass) env->DeleteGlobalRef(impl.runtimeClass);
        if (impl.bridgeClass) env->DeleteGlobalRef(impl.bridgeClass);
        impl.runtimeClass = nullptr;
        impl.bridgeClass = nullptr;
    }
    // DestroyJavaVM must run after all callbacks and global references are gone.
    impl.vm->DestroyJavaVM();
    impl.vm = nullptr;
    if (impl.jvmLibrary) dlclose(impl.jvmLibrary);
    impl.jvmLibrary = nullptr;
#endif
    impl.handles.clear();
    impl.routing.clear();
    impl.serverHandle = 0;
    impl.started = false;
}

bool JvmRuntime::started() const noexcept { return impl_->started; }

const std::string& JvmRuntime::lastError() const noexcept { return impl_->lastError; }

JvmStats JvmRuntime::stats() const {
    const auto& impl = *impl_;
    return JvmStats{
        impl.started,
        impl.discoveredMods.load(std::memory_order_relaxed),
        impl.initializedEntrypoints.load(std::memory_order_relaxed),
        impl.handles.size(),
        impl.ticks.load(std::memory_order_relaxed),
        impl.joins.load(std::memory_order_relaxed),
        impl.quits.load(std::memory_order_relaxed),
        impl.callbacks.load(std::memory_order_relaxed),
        impl.callbackErrors.load(std::memory_order_relaxed),
        impl.routing.size()
    };
}

#if defined(CPPFM_HAS_JNI)
namespace {

template <typename Call>
bool invokeVoid(JvmRuntime& runtime, Call&& call, const char* name) {
    auto& impl = runtime.bridgeImpl();
    std::lock_guard lock(impl.callMutex);
    if (!impl.started || !impl.vm || !impl.runtimeClass) return true;
    AttachedEnv attached(impl.vm);
    if (!attached) return false;
    ++impl.callbacks;
    call(attached.get());
    if (clearJavaException(attached.get(), &runtime, name)) {
        ++impl.callbackErrors;
        return false;
    }
    return true;
}

template <typename Call>
bool invokeBoolean(JvmRuntime& runtime, Call&& call, const char* name,
                   bool defaultValue = true) {
    auto& impl = runtime.bridgeImpl();
    std::lock_guard lock(impl.callMutex);
    if (!impl.started || !impl.vm || !impl.runtimeClass) return defaultValue;
    AttachedEnv attached(impl.vm);
    if (!attached) return false;
    ++impl.callbacks;
    const jboolean result = call(attached.get());
    if (clearJavaException(attached.get(), &runtime, name)) {
        ++impl.callbackErrors;
        return defaultValue;
    }
    return result == JNI_TRUE;
}

} // namespace
#endif

bool JvmRuntime::onServerTick(std::int64_t tick) {
    impl_->ticks.fetch_add(1, std::memory_order_relaxed);
#if defined(CPPFM_HAS_JNI)
    return invokeVoid(*this, [&](JNIEnv* env) {
        env->CallStaticVoidMethod(impl_->runtimeClass, impl_->serverTick,
                                  static_cast<jlong>(tick));
    }, "onServerTick");
#else
    (void)tick;
    return true;
#endif
}

std::uint64_t JvmRuntime::playerHandle(Player& player) {
    return impl_->handles.registerObject(&player, HandleKind::Player);
}

std::uint64_t JvmRuntime::worldHandle(World& world) {
    return impl_->handles.registerObject(&world, HandleKind::World);
}

void JvmRuntime::invalidatePlayer(Player& player) {
    impl_->handles.invalidate(&player, HandleKind::Player);
}

void JvmRuntime::onPlayerJoin(Player& player) {
    const auto handle = playerHandle(player);
    impl_->joins.fetch_add(1, std::memory_order_relaxed);
#if defined(CPPFM_HAS_JNI)
    invokeVoid(*this, [&](JNIEnv* env) {
        env->CallStaticVoidMethod(impl_->runtimeClass, impl_->playerJoin,
                                  static_cast<jlong>(handle));
    }, "onPlayerJoin");
#else
    (void)handle;
#endif
}

void JvmRuntime::onPlayerQuit(Player& player) {
    const auto handle = playerHandle(player);
    impl_->quits.fetch_add(1, std::memory_order_relaxed);
#if defined(CPPFM_HAS_JNI)
    invokeVoid(*this, [&](JNIEnv* env) {
        env->CallStaticVoidMethod(impl_->runtimeClass, impl_->playerQuit,
                                  static_cast<jlong>(handle));
    }, "onPlayerQuit");
#endif
    invalidatePlayer(player);
}

bool JvmRuntime::onChat(Player& player, std::string& message) {
    const auto handle = playerHandle(player);
#if defined(CPPFM_HAS_JNI)
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    if (!impl.started || !impl.vm || !impl.runtimeClass) return true;
    AttachedEnv attached(impl.vm);
    if (!attached) return true;
    ++impl.callbacks;
    JNIEnv* env = attached.get();
    const jstring input = toJavaString(env, message);
    const jstring output = static_cast<jstring>(env->CallStaticObjectMethod(
        impl.runtimeClass, impl.chat, static_cast<jlong>(handle), input));
    env->DeleteLocalRef(input);
    if (clearJavaException(env, this, "onChat")) {
        ++impl.callbackErrors;
        return true;
    }
    if (!output) return false;
    message = fromJavaString(env, output);
    env->DeleteLocalRef(output);
#else
    (void)handle;
#endif
    return true;
}

bool JvmRuntime::onBlockBreak(Player& player, std::int32_t x, std::int32_t y,
                              std::int32_t z, std::uint16_t oldState) {
    const auto handle = playerHandle(player);
#if defined(CPPFM_HAS_JNI)
    return invokeBoolean(*this, [&](JNIEnv* env) {
        return env->CallStaticBooleanMethod(impl_->runtimeClass, impl_->blockBreak,
                                            static_cast<jlong>(handle), x, y, z,
                                            static_cast<jint>(oldState));
    }, "onBlockBreak");
#else
    (void)handle; (void)x; (void)y; (void)z; (void)oldState;
    return true;
#endif
}

bool JvmRuntime::onBlockPlace(Player& player, std::int32_t x, std::int32_t y,
                              std::int32_t z, std::uint16_t newState) {
    const auto handle = playerHandle(player);
#if defined(CPPFM_HAS_JNI)
    return invokeBoolean(*this, [&](JNIEnv* env) {
        return env->CallStaticBooleanMethod(impl_->runtimeClass, impl_->blockPlace,
                                            static_cast<jlong>(handle), x, y, z,
                                            static_cast<jint>(newState));
    }, "onBlockPlace");
#else
    (void)handle; (void)x; (void)y; (void)z; (void)newState;
    return true;
#endif
}

bool JvmRuntime::onBlockClicked(Player& player, std::int32_t x, std::int32_t y,
                                std::int32_t z, std::uint16_t state, int face) {
    const auto handle = playerHandle(player);
#if defined(CPPFM_HAS_JNI)
    return invokeBoolean(*this, [&](JNIEnv* env) {
        return env->CallStaticBooleanMethod(impl_->runtimeClass, impl_->blockClicked,
                                            static_cast<jlong>(handle), x, y, z,
                                            static_cast<jint>(state), face);
    }, "onBlockClicked");
#else
    (void)handle; (void)x; (void)y; (void)z; (void)state; (void)face;
    return true;
#endif
}

bool JvmRuntime::onCommand(Player* player, std::string& command) {
    const auto handle = player ? playerHandle(*player) : 0;
#if defined(CPPFM_HAS_JNI)
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    if (!impl.started || !impl.vm || !impl.runtimeClass) return true;
    AttachedEnv attached(impl.vm);
    if (!attached) return true;
    ++impl.callbacks;
    JNIEnv* env = attached.get();
    const jstring input = toJavaString(env, command);
    const jstring output = static_cast<jstring>(env->CallStaticObjectMethod(
        impl.runtimeClass, impl.command, static_cast<jlong>(handle), input));
    env->DeleteLocalRef(input);
    if (clearJavaException(env, this, "onCommand")) {
        ++impl.callbackErrors;
        return true;
    }
    if (!output) return false;
    command = fromJavaString(env, output);
    env->DeleteLocalRef(output);
#else
    (void)handle;
#endif
    return true;
}

bool JvmRuntime::onEntityDamage(Player* victimPlayer, MobEntity* victimMob,
                                float& amount, const std::string& cause) {
    const auto player = victimPlayer ? playerHandle(*victimPlayer) : 0;
    const auto mob = victimMob ? impl_->handles.registerObject(victimMob, HandleKind::Entity) : 0;
#if defined(CPPFM_HAS_JNI)
    return invokeBoolean(*this, [&](JNIEnv* env) {
        const jstring javaCause = toJavaString(env, cause);
        const jboolean result = env->CallStaticBooleanMethod(
            impl_->runtimeClass, impl_->entityDamage,
            static_cast<jlong>(player), static_cast<jlong>(mob),
            static_cast<jfloat>(amount), javaCause);
        if (javaCause) env->DeleteLocalRef(javaCause);
        return result;
    }, "onEntityDamage");
#else
    (void)player; (void)mob; (void)amount; (void)cause;
    return true;
#endif
}

bool JvmRuntime::onMobSpawn(MobEntity& mob, double x, double y, double z) {
    const auto handle = impl_->handles.registerObject(&mob, HandleKind::Entity);
#if defined(CPPFM_HAS_JNI)
    return invokeBoolean(*this, [&](JNIEnv* env) {
        return env->CallStaticBooleanMethod(impl_->runtimeClass, impl_->mobSpawn,
                                            static_cast<jlong>(handle), x, y, z);
    }, "onMobSpawn");
#else
    (void)handle; (void)x; (void)y; (void)z;
    return true;
#endif
}

std::uint64_t JvmRuntime::nativeServerHandle() const { return impl_->serverHandle; }
std::int64_t JvmRuntime::nativeCurrentTick() const { return impl_->server.tickNow(); }

std::string JvmRuntime::nativePlayerName(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    if (player) return player->name;
    const auto* mob = static_cast<const MobEntity*>(impl_->handles.resolve(handle, HandleKind::Entity));
    return mob ? std::string(MobEntity::kindName(mob->kind)) : std::string();
}

std::string JvmRuntime::nativePlayerUuid(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    return player ? GameServer::uuidToDashed(player->uuid) : std::string();
}

std::int32_t JvmRuntime::nativePlayerEntityId(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    if (player) return player->entityId;
    const auto* mob = static_cast<const MobEntity*>(impl_->handles.resolve(handle, HandleKind::Entity));
    return mob ? mob->entityId : -1;
}

std::int32_t JvmRuntime::nativePlayerGameMode(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    return player ? static_cast<std::int32_t>(player->gamemode) : 0;
}

bool JvmRuntime::nativePlayerSneaking(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    return player && player->isSneaking;
}

std::int32_t JvmRuntime::nativeOnlinePlayerCount() const {
    return static_cast<std::int32_t>(impl_->server.playerCount());
}

std::uint64_t JvmRuntime::nativeOnlinePlayerHandle(std::size_t index) {
    const auto players = impl_->server.playersSnapshot();
    if (index >= players.size() || !players[index]) return 0;
    return playerHandle(*players[index]);
}

namespace {

int nativeInventoryIndex(int slot) {
    // PlayerInventory's 36-slot main list is [main 27, hotbar 9], followed
    // by armor [boots..helmet] and offhand.  The native server stores the
    // same values in protocol slot order [crafting, armor, main, hotbar,
    // offhand], so keep the conversion in this boundary only.
    if (slot >= 0 && slot < 27) return 9 + slot;
    if (slot >= 27 && slot < 36) return 36 + (slot - 27);
    if (slot >= 36 && slot < 40) return 8 - (slot - 36);
    if (slot == 40) return 45;
    return -1;
}

const InvSlot* nativeInventorySlot(const Player* player, int slot) {
    const int index = nativeInventoryIndex(slot);
    return player && index >= 0 ? &player->inv[static_cast<std::size_t>(index)] : nullptr;
}

InvSlot* nativeInventorySlot(Player* player, int slot) {
    const int index = nativeInventoryIndex(slot);
    return player && index >= 0 ? &player->inv[static_cast<std::size_t>(index)] : nullptr;
}

} // namespace

std::int32_t JvmRuntime::nativePlayerHeldSlot(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    return player ? std::clamp(player->heldSlot, 0, 8) : 0;
}

std::int32_t JvmRuntime::nativePlayerInventoryItemId(std::uint64_t handle,
                                                    std::int32_t slot) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    const auto* item = nativeInventorySlot(player, slot);
    return item && !item->empty() ? static_cast<std::int32_t>(item->itemId) : 0;
}

std::int32_t JvmRuntime::nativePlayerInventoryItemCount(std::uint64_t handle,
                                                       std::int32_t slot) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    const auto* item = nativeInventorySlot(player, slot);
    return item && !item->empty() ? static_cast<std::int32_t>(item->count) : 0;
}

std::string JvmRuntime::nativePlayerInventoryItemName(std::uint64_t handle,
                                                      std::int32_t slot) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    const auto* item = nativeInventorySlot(player, slot);
    return item && !item->empty() ? item->name() : std::string("minecraft:air");
}

bool JvmRuntime::nativePlayerSetInventoryItemCount(std::uint64_t handle,
                                                   std::int32_t slot,
                                                   std::int32_t count) {
    auto* player = static_cast<Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    auto* item = nativeInventorySlot(player, slot);
    if (!item || count < 0 || count > 99) return false;
    if (count == 0) *item = ItemStack::air();
    else if (item->empty()) return false;
    else item->count = static_cast<std::int16_t>(count);
    impl_->server.resendInventory(*player);
    impl_->server.syncEquipmentOnChange(*player);
    return true;
}

bool JvmRuntime::nativePlayerSetInventoryItem(std::uint64_t handle,
                                              std::int32_t slot,
                                              std::int32_t itemId,
                                              std::int32_t count) {
    auto* player = static_cast<Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    auto* item = nativeInventorySlot(player, slot);
    if (!item || itemId < 0 || count < 0 || count > 99) return false;
    if (count == 0 || itemId == 0) {
        *item = ItemStack::air();
    } else if (item->itemId == static_cast<std::uint32_t>(itemId)) {
        item->count = static_cast<std::int16_t>(count);
    } else {
        *item = ItemStack::of(static_cast<std::uint32_t>(itemId),
                              static_cast<std::int16_t>(count));
    }
    impl_->server.resendInventory(*player);
    impl_->server.syncEquipmentOnChange(*player);
    return true;
}

double JvmRuntime::nativePlayerCoordinate(std::uint64_t handle, int axis) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    if (player) {
        if (axis == 0) return player->x;
        if (axis == 1) return player->y;
        if (axis == 2) return player->z;
    }
    const auto* mob = static_cast<const MobEntity*>(impl_->handles.resolve(handle, HandleKind::Entity));
    if (mob) {
        if (axis == 0) return mob->x;
        if (axis == 1) return mob->y;
        if (axis == 2) return mob->z;
    }
    return 0.0;
}

bool JvmRuntime::nativePlayerSetPosition(std::uint64_t handle, double x, double y,
                                         double z) {
    auto* player = static_cast<Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    if (player) {
        player->x = x; player->y = y; player->z = z;
        return true;
    }
    auto* mob = static_cast<MobEntity*>(impl_->handles.resolve(handle, HandleKind::Entity));
    if (!mob) return false;
    mob->x = x; mob->y = y; mob->z = z;
    return true;
}

bool JvmRuntime::nativePlayerSendMessage(std::uint64_t handle,
                                         const std::string& text, bool overlay) {
    auto* player = static_cast<Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    if (!player || !player->conn) return false;
    WriteBuffer body;
    nbt::writeTextComponent(body, text);
    body.boolean(overlay);
    try {
        player->conn->sendPacket(proto::pl::sc::SystemChat, body);
        return true;
    } catch (...) {
        return false;
    }
}

std::uint64_t JvmRuntime::nativePlayerWorld(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(impl_->handles.resolve(handle, HandleKind::Player));
    if (player)
        return const_cast<JvmRuntime*>(this)->worldHandle(impl_->server.worldFor(player->dimension));
    const auto* mob = static_cast<const MobEntity*>(impl_->handles.resolve(handle, HandleKind::Entity));
    return mob ? const_cast<JvmRuntime*>(this)->worldHandle(impl_->server.worldFor(0)) : 0;
}

std::uint64_t JvmRuntime::nativeServerWorld(int dimension) const {
    return const_cast<JvmRuntime*>(this)->worldHandle(
        impl_->server.worldFor(static_cast<std::int8_t>(dimension)));
}

std::string JvmRuntime::nativeWorldName(std::uint64_t handle) const {
    const auto* world = static_cast<const World*>(impl_->handles.resolve(handle, HandleKind::World));
    return world ? world->dimensionKey() : std::string();
}

std::int32_t JvmRuntime::nativeWorldBlock(std::uint64_t handle, std::int32_t x,
                                          std::int32_t y, std::int32_t z) const {
    const auto* world = static_cast<const World*>(impl_->handles.resolve(handle, HandleKind::World));
    return world ? static_cast<std::int32_t>(world->getBlock(x, y, z)) : 0;
}

bool JvmRuntime::nativeWorldSetBlock(std::uint64_t handle, std::int32_t x,
                                     std::int32_t y, std::int32_t z,
                                     std::int32_t state) {
    auto* world = static_cast<World*>(impl_->handles.resolve(handle, HandleKind::World));
    if (!world || state < 0 || state > 0xFFFF) return false;
    world->setBlock(x, y, z, static_cast<std::uint16_t>(state));
    return true;
}

bool JvmRuntime::nativeExecuteCommand(const std::string& command) {
    if (command.empty()) return false;
    try { return impl_->server.dispatchConsole(command).rfind("error:", 0) != 0; }
    catch (...) { return false; }
}

void JvmRuntime::nativeLog(const std::string& level, const std::string& message) const {
    FILE* stream = level == "ERROR" ? stderr : stdout;
    std::fprintf(stream, "[cppfm][jvm][%s] %s\n", level.c_str(), message.c_str());
    std::fflush(stream);
}

void JvmRuntime::nativeSetModStats(std::size_t discovered, std::size_t initialized) {
    impl_->discoveredMods.store(discovered, std::memory_order_relaxed);
    impl_->initializedEntrypoints.store(initialized, std::memory_order_relaxed);
}

void JvmRuntime::nativeRegisterTransformedMethod(const std::string& owner,
                                                 const std::string& name,
                                                 const std::string& descriptor) {
    if (!owner.empty() && !name.empty() && !descriptor.empty())
        impl_->routing.markTransformed(owner, name, descriptor);
}

} // namespace cppfm::jvm
