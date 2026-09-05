#include "JvmRuntime.hpp"
#include "JvmContract.hpp"

#include "../game/GameServer.hpp"
#include "../game/World.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
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
    bool stopping = false;
    bool bootstrapInvoked = false;
    std::uint64_t serverHandle = 0;
    JvmProvider provider = JvmProvider::None;

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
    // providerClass is the application-loader KnotLauncher when the real
    // provider is selected.  dispatchClass is either KnotLauncher (its static
    // forwarding API) or the fallback CppModRuntime.
    jclass providerClass = nullptr;
    jobject providerLoader = nullptr;
    jclass dispatchClass = nullptr;
    jclass runtimeClass = nullptr;
    jclass bridgeClass = nullptr;
    jobject bridgeLoader = nullptr;
    jmethodID providerInstallBridge = nullptr;
    jmethodID providerBootstrap = nullptr;
    jmethodID bootstrap = nullptr;
    bool bootstrapHasClassesDir = false;
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
    jmethodID pluginMessage = nullptr;
#endif

    std::atomic<std::uint64_t> nativeDispatches{0};
    std::atomic<std::uint64_t> jvmDispatches{0};
    std::atomic<std::uint64_t> dispatchFailures{0};
    std::atomic<std::uint64_t> bridgeExceptions{0};

    explicit Impl(GameServer& s, JvmConfig c)
        : server(s), config(std::move(c)) {}
};

namespace {

std::atomic<JvmRuntime*> g_activeRuntime{nullptr};
// Read by Java-created worker threads while the server thread is inside a
// callback.  The callback path holds callMutex, so routing this read through
// NativeCallGuard would deadlock a worker that is joined by that callback.
std::atomic<std::int64_t> g_currentTick{0};

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

std::vector<std::uint8_t> fromJavaBytes(JNIEnv* env, jbyteArray value) {
    if (!env || !value) return {};
    const jsize length = env->GetArrayLength(value);
    if (length <= 0) return {};
    std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
    env->GetByteArrayRegion(value, 0, length,
                            reinterpret_cast<jbyte*>(result.data()));
    return result;
}

jbyteArray toJavaBytes(JNIEnv* env, const std::vector<std::uint8_t>& value) {
    if (!env) return nullptr;
    const jsize length = static_cast<jsize>(
        std::min<std::size_t>(value.size(), static_cast<std::size_t>(std::numeric_limits<jsize>::max())));
    jbyteArray result = env->NewByteArray(length);
    if (result && length > 0) {
        env->SetByteArrayRegion(result, 0, length,
                                reinterpret_cast<const jbyte*>(value.data()));
    }
    return result;
}

jstring toJavaString(JNIEnv* env, const std::string& value) {
    return env ? env->NewStringUTF(value.c_str()) : nullptr;
}

bool clearJavaException(JNIEnv* env, JvmRuntime* runtime, const char* where) {
    if (!env || !env->ExceptionCheck()) return false;
    if (runtime) {
        runtime->bridgeImpl().bridgeExceptions.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[cppfm][jvm] Java exception at %s\n", where);
        env->ExceptionDescribe();
        runtime->nativeLog("ERROR", std::string("Java exception at ") + where);
    }
    env->ExceptionClear();
    return true;
}

void throwJavaException(JNIEnv* env, const char* className,
                        const std::string& message) noexcept {
    if (!env) return;
    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass type = env->FindClass(className);
    if (!type) {
        env->ExceptionClear();
        return;
    }
    env->ThrowNew(type, message.c_str());
    env->DeleteLocalRef(type);
}

class NativeCallGuard {
public:
    explicit NativeCallGuard(bool allowBootstrap = false)
        : runtime_(g_activeRuntime.load(std::memory_order_acquire)) {
        if (!runtime_) return;
        auto& impl = runtime_->bridgeImpl();
        lock_ = std::unique_lock<std::recursive_mutex>(impl.callMutex);
        if (!impl.vm || impl.stopping || (!allowBootstrap && !impl.started)) {
            runtime_ = nullptr;
        }
    }

    JvmRuntime* get() const noexcept { return runtime_; }
    explicit operator bool() const noexcept { return runtime_ != nullptr; }

private:
    JvmRuntime* runtime_ = nullptr;
    std::unique_lock<std::recursive_mutex> lock_;
};

std::string dottedClassName(std::string name) {
    std::replace(name.begin(), name.end(), '/', '.');
    return name;
}

jobject classLoaderFor(JNIEnv* env, jclass type) {
    if (!env || !type) return nullptr;
    jclass classType = env->FindClass("java/lang/Class");
    if (!classType) { env->ExceptionClear(); return nullptr; }
    const jmethodID getLoader = env->GetMethodID(
        classType, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getLoader) { env->ExceptionClear(); env->DeleteLocalRef(classType); return nullptr; }
    jobject loader = env->CallObjectMethod(type, getLoader);
    env->DeleteLocalRef(classType);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        if (loader) env->DeleteLocalRef(loader);
        return nullptr;
    }
    return loader;
}

jclass loadClassFromLoader(JNIEnv* env, jobject loader, const std::string& internalName) {
    if (!env) return nullptr;
    if (!loader) {
        jclass result = env->FindClass(internalName.c_str());
        if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
        return result;
    }
    jclass loaderType = env->FindClass("java/lang/ClassLoader");
    if (!loaderType) { env->ExceptionClear(); return nullptr; }
    const jmethodID loadClass = env->GetMethodID(
        loaderType, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass) {
        env->ExceptionClear();
        env->DeleteLocalRef(loaderType);
        return nullptr;
    }
    const std::string dotted = dottedClassName(internalName);
    jstring name = env->NewStringUTF(dotted.c_str());
    jobject result = env->CallObjectMethod(loader, loadClass, name);
    if (name) env->DeleteLocalRef(name);
    env->DeleteLocalRef(loaderType);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return static_cast<jclass>(result);
}

bool isSameClass(JNIEnv* env, jclass left, jclass right) {
    return env && left && right && env->IsSameObject(left, right) == JNI_TRUE;
}

template <typename Result, typename Call>
Result withNativeRuntime(JNIEnv* env, const char* operation, Result fallback,
                         Call&& call, bool allowBootstrap = false) {
    NativeCallGuard guard(allowBootstrap);
    if (!guard) return fallback;
    try {
        return call(*guard.get());
    } catch (const std::exception& failure) {
        guard.get()->bridgeImpl().bridgeExceptions.fetch_add(1, std::memory_order_relaxed);
        throwJavaException(env, "java/lang/IllegalStateException",
                           std::string(operation) + ": " + failure.what());
    } catch (...) {
        guard.get()->bridgeImpl().bridgeExceptions.fetch_add(1, std::memory_order_relaxed);
        throwJavaException(env, "java/lang/IllegalStateException",
                           std::string(operation) + ": unknown native exception");
    }
    return fallback;
}

template <typename Call>
void withNativeRuntimeVoid(JNIEnv* env, const char* operation, Call&& call,
                           bool allowBootstrap = false) {
    NativeCallGuard guard(allowBootstrap);
    if (!guard) return;
    try {
        call(*guard.get());
    } catch (const std::exception& failure) {
        guard.get()->bridgeImpl().bridgeExceptions.fetch_add(1, std::memory_order_relaxed);
        throwJavaException(env, "java/lang/IllegalStateException",
                           std::string(operation) + ": " + failure.what());
    } catch (...) {
        guard.get()->bridgeImpl().bridgeExceptions.fetch_add(1, std::memory_order_relaxed);
        throwJavaException(env, "java/lang/IllegalStateException",
                           std::string(operation) + ": unknown native exception");
    }
}

HandleKind handleKindFromJava(jint raw) noexcept {
    switch (raw) {
    case static_cast<jint>(HandleKind::Server): return HandleKind::Server;
    case static_cast<jint>(HandleKind::World): return HandleKind::World;
    case static_cast<jint>(HandleKind::Player): return HandleKind::Player;
    case static_cast<jint>(HandleKind::Entity): return HandleKind::Entity;
    case static_cast<jint>(HandleKind::BlockState): return HandleKind::BlockState;
    case static_cast<jint>(HandleKind::ItemStack): return HandleKind::ItemStack;
    default: return HandleKind::Unknown;
    }
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
    withNativeRuntimeVoid(env, "nativeLog", [&](JvmRuntime& runtime) {
        runtime.nativeLog(fromJavaString(env, level), fromJavaString(env, message));
    }, true);
}

JNIEXPORT jlong JNICALL nativeServerHandle(JNIEnv* env, jclass) {
    return withNativeRuntime<jlong>(env, "nativeServerHandle", 0,
                                    [](JvmRuntime& runtime) {
        return static_cast<jlong>(runtime.nativeServerHandle());
    });
}

JNIEXPORT jlong JNICALL nativeCurrentTick(JNIEnv* env, jclass) {
    // This is deliberately a lock-free, snapshot-only bridge operation.  A
    // Java-created thread can call it while the main callback owns the
    // serialized JNI call lock and waits for that thread to join.
    if (!g_activeRuntime.load(std::memory_order_acquire)) return 0;
    return static_cast<jlong>(g_currentTick.load(std::memory_order_acquire));
}

JNIEXPORT jstring JNICALL nativePlayerName(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jstring>(env, "nativePlayerName", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativePlayerName(static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jstring JNICALL nativePlayerUuid(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jstring>(env, "nativePlayerUuid", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativePlayerUuid(static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jint JNICALL nativePlayerEntityId(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jint>(env, "nativePlayerEntityId", -1,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativePlayerEntityId(static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jint JNICALL nativePlayerGameMode(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jint>(env, "nativePlayerGameMode", 0,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativePlayerGameMode(static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jboolean JNICALL nativePlayerSneaking(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jboolean>(env, "nativePlayerSneaking", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativePlayerSneaking(static_cast<std::uint64_t>(handle))
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jint JNICALL nativeOnlinePlayerCount(JNIEnv* env, jclass) {
    return withNativeRuntime<jint>(env, "nativeOnlinePlayerCount", 0,
                                   [](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativeOnlinePlayerCount());
    });
}

JNIEXPORT jlong JNICALL nativeOnlinePlayerHandle(JNIEnv* env, jclass, jint index) {
    return withNativeRuntime<jlong>(env, "nativeOnlinePlayerHandle", 0,
                                    [&](JvmRuntime& runtime) {
        return index >= 0 ? static_cast<jlong>(runtime.nativeOnlinePlayerHandle(
            static_cast<std::size_t>(index))) : 0;
    });
}

JNIEXPORT jint JNICALL nativePlayerHeldSlot(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jint>(env, "nativePlayerHeldSlot", 0,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativePlayerHeldSlot(static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jint JNICALL nativePlayerInventoryItemId(JNIEnv* env, jclass, jlong handle, jint slot) {
    return withNativeRuntime<jint>(env, "nativePlayerInventoryItemId", 0,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativePlayerInventoryItemId(
            static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot)));
    });
}

JNIEXPORT jint JNICALL nativePlayerInventoryItemCount(JNIEnv* env, jclass, jlong handle, jint slot) {
    return withNativeRuntime<jint>(env, "nativePlayerInventoryItemCount", 0,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativePlayerInventoryItemCount(
            static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot)));
    });
}

JNIEXPORT jstring JNICALL nativePlayerInventoryItemName(JNIEnv* env, jclass,
                                                        jlong handle, jint slot) {
    return withNativeRuntime<jstring>(env, "nativePlayerInventoryItemName", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativePlayerInventoryItemName(
            static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot)));
    });
}

JNIEXPORT jboolean JNICALL nativePlayerSetInventoryItemCount(JNIEnv* env, jclass,
                                                              jlong handle, jint slot,
                                                              jint count) {
    return withNativeRuntime<jboolean>(env, "nativePlayerSetInventoryItemCount", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativePlayerSetInventoryItemCount(
            static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot),
            static_cast<std::int32_t>(count)) ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jboolean JNICALL nativePlayerSetInventoryItem(JNIEnv* env, jclass,
                                                         jlong handle, jint slot,
                                                         jint itemId, jint count) {
    return withNativeRuntime<jboolean>(env, "nativePlayerSetInventoryItem", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativePlayerSetInventoryItem(
            static_cast<std::uint64_t>(handle), static_cast<std::int32_t>(slot),
            static_cast<std::int32_t>(itemId), static_cast<std::int32_t>(count))
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jdouble JNICALL nativePlayerCoordinate(JNIEnv* env, jclass, jlong handle, jint axis) {
    return withNativeRuntime<jdouble>(env, "nativePlayerCoordinate", 0.0,
                                      [&](JvmRuntime& runtime) {
        return runtime.nativePlayerCoordinate(static_cast<std::uint64_t>(handle), axis);
    });
}

JNIEXPORT jboolean JNICALL nativePlayerSetPosition(JNIEnv* env, jclass, jlong handle,
                                           jdouble x, jdouble y, jdouble z) {
    return withNativeRuntime<jboolean>(env, "nativePlayerSetPosition", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativePlayerSetPosition(static_cast<std::uint64_t>(handle), x, y, z)
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jboolean JNICALL nativePlayerSendMessage(JNIEnv* env, jclass, jlong handle,
                                            jstring message, jboolean overlay) {
    return withNativeRuntime<jboolean>(env, "nativePlayerSendMessage", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativePlayerSendMessage(
            static_cast<std::uint64_t>(handle), fromJavaString(env, message),
            overlay == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jlong JNICALL nativePlayerWorld(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jlong>(env, "nativePlayerWorld", 0,
                                    [&](JvmRuntime& runtime) {
        return static_cast<jlong>(runtime.nativePlayerWorld(static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jlong JNICALL nativeServerWorld(JNIEnv* env, jclass, jint dimension) {
    return withNativeRuntime<jlong>(env, "nativeServerWorld", 0,
                                    [&](JvmRuntime& runtime) {
        return static_cast<jlong>(runtime.nativeServerWorld(dimension));
    });
}

JNIEXPORT jstring JNICALL nativeWorldName(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jstring>(env, "nativeWorldName", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativeWorldName(static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jint JNICALL nativeWorldBlock(JNIEnv* env, jclass, jlong handle,
                                        jint x, jint y, jint z) {
    return withNativeRuntime<jint>(env, "nativeWorldBlock", 0,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativeWorldBlock(
            static_cast<std::uint64_t>(handle), x, y, z));
    });
}

JNIEXPORT jboolean JNICALL nativeWorldSetBlock(JNIEnv* env, jclass, jlong handle,
                                               jint x, jint y, jint z, jint state) {
    return withNativeRuntime<jboolean>(env, "nativeWorldSetBlock", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativeWorldSetBlock(
            static_cast<std::uint64_t>(handle), x, y, z, state)
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jboolean JNICALL nativeExecuteCommand(JNIEnv* env, jclass, jstring command) {
    return withNativeRuntime<jboolean>(env, "nativeExecuteCommand", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativeExecuteCommand(fromJavaString(env, command))
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT void JNICALL nativeSetModStats(JNIEnv* env, jclass, jint discovered,
                                          jint initialized) {
    withNativeRuntimeVoid(env, "nativeSetModStats", [&](JvmRuntime& runtime) {
        runtime.nativeSetModStats(static_cast<std::size_t>(std::max(0, discovered)),
                                  static_cast<std::size_t>(std::max(0, initialized)));
    });
}

JNIEXPORT void JNICALL nativeRegisterTransformedMethod(JNIEnv* env, jclass,
                                                       jstring owner, jstring name,
                                                       jstring descriptor) {
    withNativeRuntimeVoid(env, "nativeRegisterTransformedMethod", [&](JvmRuntime& runtime) {
        runtime.nativeRegisterTransformedMethod(
            fromJavaString(env, owner), fromJavaString(env, name),
            fromJavaString(env, descriptor));
    });
}

JNIEXPORT void JNICALL nativeRegisterTransformedMethodHash(
    JNIEnv* env, jclass, jstring owner, jstring name, jstring descriptor,
    jlong transformedHash) {
    withNativeRuntimeVoid(env, "nativeRegisterTransformedMethodHash",
                          [&](JvmRuntime& runtime) {
        runtime.nativeRegisterTransformedMethod(
            fromJavaString(env, owner), fromJavaString(env, name),
            fromJavaString(env, descriptor), static_cast<std::uint64_t>(transformedHash));
    });
}

JNIEXPORT void JNICALL nativeRegisterMethodBaseline(
    JNIEnv* env, jclass, jstring owner, jstring name, jstring descriptor,
    jlong baselineHash, jlong transformedHash) {
    withNativeRuntimeVoid(env, "nativeRegisterMethodBaseline",
                          [&](JvmRuntime& runtime) {
        runtime.nativeRegisterMethodBaseline(
            fromJavaString(env, owner), fromJavaString(env, name),
            fromJavaString(env, descriptor), static_cast<std::uint64_t>(baselineHash),
            static_cast<std::uint64_t>(transformedHash));
    });
}

JNIEXPORT jboolean JNICALL nativeHandleValid(JNIEnv* env, jclass, jlong handle,
                                             jint expectedKind) {
    return withNativeRuntime<jboolean>(env, "nativeHandleValid", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativeHandleValid(
            static_cast<std::uint64_t>(handle), handleKindFromJava(expectedKind))
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jint JNICALL nativeHandleKind(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jint>(env, "nativeHandleKind", 0,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativeHandleKind(
            static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jboolean JNICALL nativeInvalidateHandle(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jboolean>(env, "nativeInvalidateHandle", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativeInvalidateHandle(static_cast<std::uint64_t>(handle))
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jstring JNICALL nativeEntityType(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jstring>(env, "nativeEntityType", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativeEntityType(
            static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jint JNICALL nativeEntityTypeId(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jint>(env, "nativeEntityTypeId", -1,
                                   [&](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativeEntityTypeId(
            static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jfloat JNICALL nativeEntityHealth(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jfloat>(env, "nativeEntityHealth", 0.0f,
                                     [&](JvmRuntime& runtime) {
        return runtime.nativeEntityHealth(static_cast<std::uint64_t>(handle));
    });
}

JNIEXPORT jboolean JNICALL nativeEntitySetHealth(JNIEnv* env, jclass, jlong handle,
                                                 jfloat health) {
    return withNativeRuntime<jboolean>(env, "nativeEntitySetHealth", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativeEntitySetHealth(
            static_cast<std::uint64_t>(handle), health) ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jboolean JNICALL nativeEntityDead(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jboolean>(env, "nativeEntityDead", JNI_TRUE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativeEntityDead(static_cast<std::uint64_t>(handle))
                   ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jlong JNICALL nativeEntityWorld(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jlong>(env, "nativeEntityWorld", 0,
                                    [&](JvmRuntime& runtime) {
        return static_cast<jlong>(runtime.nativeEntityWorld(
            static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jdouble JNICALL nativeEntityCoordinate(JNIEnv* env, jclass, jlong handle,
                                                  jint axis) {
    return withNativeRuntime<jdouble>(env, "nativeEntityCoordinate", 0.0,
                                      [&](JvmRuntime& runtime) {
        return runtime.nativeEntityCoordinate(static_cast<std::uint64_t>(handle), axis);
    });
}

JNIEXPORT jboolean JNICALL nativeEntitySetPosition(JNIEnv* env, jclass, jlong handle,
                                                   jdouble x, jdouble y, jdouble z) {
    return withNativeRuntime<jboolean>(env, "nativeEntitySetPosition", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativeEntitySetPosition(
            static_cast<std::uint64_t>(handle), x, y, z) ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jint JNICALL nativeEntityCount(JNIEnv* env, jclass) {
    return withNativeRuntime<jint>(env, "nativeEntityCount", 0,
                                   [](JvmRuntime& runtime) {
        return static_cast<jint>(runtime.nativeEntityCount());
    });
}

JNIEXPORT jlong JNICALL nativeEntityHandle(JNIEnv* env, jclass, jint index) {
    return withNativeRuntime<jlong>(env, "nativeEntityHandle", 0,
                                    [&](JvmRuntime& runtime) {
        return index < 0 ? 0 : static_cast<jlong>(runtime.nativeEntityHandle(
            static_cast<std::size_t>(index)));
    });
}

JNIEXPORT jlong JNICALL nativeWorldTime(JNIEnv* env, jclass, jlong handle) {
    return withNativeRuntime<jlong>(env, "nativeWorldTime", 0,
                                    [&](JvmRuntime& runtime) {
        return static_cast<jlong>(runtime.nativeWorldTime(
            static_cast<std::uint64_t>(handle)));
    });
}

JNIEXPORT jint JNICALL nativeRegistryItemId(JNIEnv* env, jclass, jstring name) {
    return withNativeRuntime<jint>(env, "nativeRegistryItemId", -1,
                                   [&](JvmRuntime& runtime) {
        return runtime.nativeRegistryItemId(fromJavaString(env, name));
    });
}

JNIEXPORT jstring JNICALL nativeRegistryItemName(JNIEnv* env, jclass, jint id) {
    return withNativeRuntime<jstring>(env, "nativeRegistryItemName", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativeRegistryItemName(id));
    });
}

JNIEXPORT jint JNICALL nativeRegistryBlockState(JNIEnv* env, jclass, jstring name) {
    return withNativeRuntime<jint>(env, "nativeRegistryBlockState", -1,
                                   [&](JvmRuntime& runtime) {
        return runtime.nativeRegistryBlockState(fromJavaString(env, name));
    });
}

JNIEXPORT jstring JNICALL nativeRegistryBlockName(JNIEnv* env, jclass, jint state) {
    return withNativeRuntime<jstring>(env, "nativeRegistryBlockName", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativeRegistryBlockName(state));
    });
}

JNIEXPORT jint JNICALL nativeRegistryEntryCount(JNIEnv* env, jclass, jstring registry) {
    return withNativeRuntime<jint>(env, "nativeRegistryEntryCount", 0,
                                   [&](JvmRuntime& runtime) {
        return runtime.nativeRegistryEntryCount(fromJavaString(env, registry));
    });
}

JNIEXPORT jstring JNICALL nativeRegistryEntryName(JNIEnv* env, jclass, jstring registry,
                                                  jint id) {
    return withNativeRuntime<jstring>(env, "nativeRegistryEntryName", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativeRegistryEntryName(
            fromJavaString(env, registry), id));
    });
}

JNIEXPORT jboolean JNICALL nativePlayerSendPluginMessage(
    JNIEnv* env, jclass, jlong handle, jstring channel, jbyteArray payload, jint phase) {
    return withNativeRuntime<jboolean>(env, "nativePlayerSendPluginMessage", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.nativePlayerSendPluginMessage(
            static_cast<std::uint64_t>(handle), fromJavaString(env, channel),
            fromJavaBytes(env, payload), phase) ? JNI_TRUE : JNI_FALSE;
    });
}

JNIEXPORT jstring JNICALL nativeServerSetting(JNIEnv* env, jclass, jstring key) {
    return withNativeRuntime<jstring>(env, "nativeServerSetting", nullptr,
                                      [&](JvmRuntime& runtime) {
        return toJavaString(env, runtime.nativeServerSetting(fromJavaString(env, key)));
    });
}

JNIEXPORT jint JNICALL nativeRoutePath(JNIEnv* env, jclass, jstring owner, jstring name,
                                       jstring descriptor) {
    return withNativeRuntime<jint>(env, "nativeRoutePath", 0,
                                   [&](JvmRuntime& runtime) {
        return runtime.nativeRoutePath(fromJavaString(env, owner), fromJavaString(env, name),
                                       fromJavaString(env, descriptor));
    });
}

JNIEXPORT jlong JNICALL nativeRouteHash(JNIEnv* env, jclass, jstring owner, jstring name,
                                        jstring descriptor) {
    return withNativeRuntime<jlong>(env, "nativeRouteHash", 0,
                                    [&](JvmRuntime& runtime) {
        return static_cast<jlong>(runtime.nativeRouteHash(
            fromJavaString(env, owner), fromJavaString(env, name),
            fromJavaString(env, descriptor)));
    });
}

JNIEXPORT jint JNICALL nativeTransformedMethodCount(JNIEnv* env, jclass) {
    return withNativeRuntime<jint>(env, "nativeTransformedMethodCount", 0,
                                   [](JvmRuntime& runtime) {
        return runtime.nativeTransformedMethodCount();
    });
}

JNIEXPORT jint JNICALL nativeNativeMethodCount(JNIEnv* env, jclass) {
    return withNativeRuntime<jint>(env, "nativeNativeMethodCount", 0,
                                   [](JvmRuntime& runtime) {
        return runtime.nativeNativeMethodCount();
    });
}

JNIEXPORT jboolean JNICALL nativeInstallBridge(JNIEnv* env, jclass, jclass bridgeClass) {
    return withNativeRuntime<jboolean>(env, "KnotLauncher.installBridge", JNI_FALSE,
                                       [&](JvmRuntime& runtime) {
        return runtime.installNativeBridge(env, bridgeClass) ? JNI_TRUE : JNI_FALSE;
    }, true);
}

struct BridgeNativeBinding {
    const char* name;
    const char* descriptor;
    void* function;
    bool required;
};

bool registerBridge(JNIEnv* env, JvmRuntime* runtime, jclass bridgeClass) {
    if (!env || !bridgeClass) return false;
    static const BridgeNativeBinding bindings[] = {
        {"nativeLog", "(Ljava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(nativeLog), true},
        {"nativeServerHandle", "()J", reinterpret_cast<void*>(nativeServerHandle), true},
        {"nativeCurrentTick", "()J", reinterpret_cast<void*>(nativeCurrentTick), true},
        {"nativePlayerName", "(J)Ljava/lang/String;", reinterpret_cast<void*>(nativePlayerName), true},
        {"nativePlayerUuid", "(J)Ljava/lang/String;", reinterpret_cast<void*>(nativePlayerUuid), true},
        {"nativePlayerEntityId", "(J)I", reinterpret_cast<void*>(nativePlayerEntityId), true},
        {"nativePlayerGameMode", "(J)I", reinterpret_cast<void*>(nativePlayerGameMode), true},
        {"nativePlayerSneaking", "(J)Z", reinterpret_cast<void*>(nativePlayerSneaking), true},
        {"nativeOnlinePlayerCount", "()I", reinterpret_cast<void*>(nativeOnlinePlayerCount), true},
        {"nativeOnlinePlayerHandle", "(I)J", reinterpret_cast<void*>(nativeOnlinePlayerHandle), true},
        {"nativePlayerHeldSlot", "(J)I", reinterpret_cast<void*>(nativePlayerHeldSlot), true},
        {"nativePlayerInventoryItemId", "(JI)I", reinterpret_cast<void*>(nativePlayerInventoryItemId), true},
        {"nativePlayerInventoryItemCount", "(JI)I", reinterpret_cast<void*>(nativePlayerInventoryItemCount), true},
        {"nativePlayerInventoryItemName", "(JI)Ljava/lang/String;", reinterpret_cast<void*>(nativePlayerInventoryItemName), true},
        {"nativePlayerSetInventoryItemCount", "(JII)Z", reinterpret_cast<void*>(nativePlayerSetInventoryItemCount), true},
        {"nativePlayerSetInventoryItem", "(JIII)Z", reinterpret_cast<void*>(nativePlayerSetInventoryItem), true},
        {"nativePlayerCoordinate", "(JI)D", reinterpret_cast<void*>(nativePlayerCoordinate), true},
        {"nativePlayerSetPosition", "(JDDD)Z", reinterpret_cast<void*>(nativePlayerSetPosition), true},
        {"nativePlayerSendMessage", "(JLjava/lang/String;Z)Z", reinterpret_cast<void*>(nativePlayerSendMessage), true},
        {"nativePlayerWorld", "(J)J", reinterpret_cast<void*>(nativePlayerWorld), true},
        {"nativeServerWorld", "(I)J", reinterpret_cast<void*>(nativeServerWorld), true},
        {"nativeWorldName", "(J)Ljava/lang/String;", reinterpret_cast<void*>(nativeWorldName), true},
        {"nativeWorldBlock", "(JIII)I", reinterpret_cast<void*>(nativeWorldBlock), true},
        {"nativeWorldSetBlock", "(JIIII)Z", reinterpret_cast<void*>(nativeWorldSetBlock), true},
        {"nativeExecuteCommand", "(Ljava/lang/String;)Z", reinterpret_cast<void*>(nativeExecuteCommand), true},
        {"nativeSetModStats", "(II)V", reinterpret_cast<void*>(nativeSetModStats), true},
        {"nativeRegisterTransformedMethod", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void*>(nativeRegisterTransformedMethod), true},
        {"nativeRegisterTransformedMethodHash", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V", reinterpret_cast<void*>(nativeRegisterTransformedMethodHash), false},
        {"nativeRegisterMethodBaseline", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;JJ)V", reinterpret_cast<void*>(nativeRegisterMethodBaseline), false},
        {"nativeHandleValid", "(JI)Z", reinterpret_cast<void*>(nativeHandleValid), false},
        {"nativeHandleKind", "(J)I", reinterpret_cast<void*>(nativeHandleKind), false},
        {"nativeInvalidateHandle", "(J)Z", reinterpret_cast<void*>(nativeInvalidateHandle), false},
        {"nativeEntityType", "(J)Ljava/lang/String;", reinterpret_cast<void*>(nativeEntityType), false},
        {"nativeEntityTypeId", "(J)I", reinterpret_cast<void*>(nativeEntityTypeId), false},
        {"nativeEntityHealth", "(J)F", reinterpret_cast<void*>(nativeEntityHealth), false},
        {"nativeEntitySetHealth", "(JF)Z", reinterpret_cast<void*>(nativeEntitySetHealth), false},
        {"nativeEntityDead", "(J)Z", reinterpret_cast<void*>(nativeEntityDead), false},
        {"nativeEntityWorld", "(J)J", reinterpret_cast<void*>(nativeEntityWorld), false},
        {"nativeEntityCoordinate", "(JI)D", reinterpret_cast<void*>(nativeEntityCoordinate), false},
        {"nativeEntitySetPosition", "(JDDD)Z", reinterpret_cast<void*>(nativeEntitySetPosition), false},
        {"nativeEntityCount", "()I", reinterpret_cast<void*>(nativeEntityCount), false},
        {"nativeEntityHandle", "(I)J", reinterpret_cast<void*>(nativeEntityHandle), false},
        {"nativeWorldTime", "(J)J", reinterpret_cast<void*>(nativeWorldTime), false},
        {"nativeRegistryItemId", "(Ljava/lang/String;)I", reinterpret_cast<void*>(nativeRegistryItemId), false},
        {"nativeRegistryItemName", "(I)Ljava/lang/String;", reinterpret_cast<void*>(nativeRegistryItemName), false},
        {"nativeRegistryBlockState", "(Ljava/lang/String;)I", reinterpret_cast<void*>(nativeRegistryBlockState), false},
        {"nativeRegistryBlockName", "(I)Ljava/lang/String;", reinterpret_cast<void*>(nativeRegistryBlockName), false},
        {"nativeRegistryEntryCount", "(Ljava/lang/String;)I", reinterpret_cast<void*>(nativeRegistryEntryCount), false},
        {"nativeRegistryEntryName", "(Ljava/lang/String;I)Ljava/lang/String;", reinterpret_cast<void*>(nativeRegistryEntryName), false},
        {"nativePlayerSendPluginMessage", "(JLjava/lang/String;[BI)Z", reinterpret_cast<void*>(nativePlayerSendPluginMessage), false},
        {"nativeServerSetting", "(Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void*>(nativeServerSetting), false},
        {"nativeRoutePath", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I", reinterpret_cast<void*>(nativeRoutePath), false},
        {"nativeRouteHash", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)J", reinterpret_cast<void*>(nativeRouteHash), false},
        {"nativeTransformedMethodCount", "()I", reinterpret_cast<void*>(nativeTransformedMethodCount), false},
        {"nativeNativeMethodCount", "()I", reinterpret_cast<void*>(nativeNativeMethodCount), false},
    };
    for (const auto& binding : bindings) {
        const jmethodID method = env->GetStaticMethodID(
            bridgeClass, binding.name, binding.descriptor);
        if (!method) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (binding.required) {
                std::fprintf(stderr, "[cppfm][jvm] NativeBridge missing %s%s\n",
                             binding.name, binding.descriptor);
                return false;
            }
            continue;
        }
        JNINativeMethod nativeMethod{
            const_cast<char*>(binding.name), const_cast<char*>(binding.descriptor),
            binding.function};
        if (env->RegisterNatives(bridgeClass, &nativeMethod, 1) != JNI_OK) {
            clearJavaException(env, runtime, binding.name);
            return false;
        }
    }
    return true;
}

bool registerKnotLauncher(JNIEnv* env, JvmRuntime* runtime, jclass launcherClass) {
    if (!env || !launcherClass) return false;
    const jmethodID install = env->GetStaticMethodID(
        launcherClass, contract::InstallBridgeName,
        contract::InstallBridgeDescriptor);
    if (!install) {
        clearJavaException(env, runtime, "KnotLauncher.installBridge descriptor");
        return false;
    }
    JNINativeMethod method{
        const_cast<char*>(contract::InstallBridgeName),
        const_cast<char*>(contract::InstallBridgeDescriptor),
        reinterpret_cast<void*>(nativeInstallBridge)};
    if (env->RegisterNatives(launcherClass, &method, 1) != JNI_OK) {
        clearJavaException(env, runtime, "KnotLauncher.RegisterNatives");
        return false;
    }
    return true;
}

jmethodID requiredStaticMethod(JNIEnv* env, JvmRuntime* runtime, jclass type,
                               const char* name, const char* descriptor) {
    const jmethodID method = env->GetStaticMethodID(type, name, descriptor);
    if (!method) {
        clearJavaException(env, runtime, name);
        return nullptr;
    }
    return method;
}

jmethodID optionalStaticMethod(JNIEnv* env, jclass type, const char* name,
                               const char* descriptor) {
    const jmethodID method = env->GetStaticMethodID(type, name, descriptor);
    if (!method && env->ExceptionCheck()) env->ExceptionClear();
    return method;
}

bool resolveDispatchMethods(JNIEnv* env, JvmRuntime* runtime, jclass dispatchClass,
                            bool knotProvider) {
    auto& impl = runtime->bridgeImpl();
    if (!dispatchClass) return false;
    impl.shutdown = requiredStaticMethod(env, runtime, dispatchClass,
                                         contract::ShutdownName,
                                         contract::ShutdownDescriptor);
    impl.serverTick = requiredStaticMethod(env, runtime, dispatchClass,
                                           contract::OnServerTickName,
                                           contract::OnServerTickDescriptor);
    impl.playerJoin = requiredStaticMethod(env, runtime, dispatchClass,
                                            contract::OnPlayerJoinName,
                                            contract::OnPlayerJoinDescriptor);
    impl.playerQuit = requiredStaticMethod(env, runtime, dispatchClass,
                                            contract::OnPlayerQuitName,
                                            contract::OnPlayerQuitDescriptor);
    impl.chat = requiredStaticMethod(env, runtime, dispatchClass,
                                     contract::OnChatName,
                                     contract::OnChatDescriptor);
    impl.blockBreak = requiredStaticMethod(env, runtime, dispatchClass,
                                            contract::OnBlockBreakName,
                                            contract::OnBlockBreakDescriptor);
    impl.blockPlace = requiredStaticMethod(env, runtime, dispatchClass,
                                            contract::OnBlockPlaceName,
                                            contract::OnBlockPlaceDescriptor);
    impl.blockClicked = requiredStaticMethod(env, runtime, dispatchClass,
                                              contract::OnBlockClickedName,
                                              contract::OnBlockClickedDescriptor);
    impl.command = requiredStaticMethod(env, runtime, dispatchClass,
                                        contract::OnCommandName,
                                        contract::OnCommandDescriptor);
    impl.entityDamage = requiredStaticMethod(env, runtime, dispatchClass,
                                              contract::OnEntityDamageName,
                                              contract::OnEntityDamageDescriptor);
    impl.mobSpawn = requiredStaticMethod(env, runtime, dispatchClass,
                                          contract::OnMobSpawnName,
                                          contract::OnMobSpawnDescriptor);
    // Network callbacks are optional until the full Java event facade exposes
    // them.  The native side still exposes sendPluginMessage immediately.
    impl.pluginMessage = optionalStaticMethod(
        env, dispatchClass, "onPluginMessage", contract::NativePluginMessageDescriptor);
    (void)knotProvider;
    return impl.shutdown && impl.serverTick && impl.playerJoin && impl.playerQuit &&
           impl.chat && impl.blockBreak && impl.blockPlace && impl.blockClicked &&
           impl.command && impl.entityDamage && impl.mobSpawn;
}

void deleteGlobal(JNIEnv* env, jobject& reference) {
    if (env && reference) env->DeleteGlobalRef(reference);
    reference = nullptr;
}

void deleteGlobal(JNIEnv* env, jclass& reference) {
    if (env && reference)
        env->DeleteGlobalRef(reinterpret_cast<jobject>(reference));
    reference = nullptr;
}

#endif // CPPFM_HAS_JNI

} // namespace

JvmRuntime::JvmRuntime(GameServer& server, JvmConfig config)
    : impl_(std::make_unique<Impl>(server, std::move(config))) {}

JvmRuntime::~JvmRuntime() { stop(); }

bool JvmRuntime::installNativeBridge(void* rawEnv, void* rawClass) {
#if !defined(CPPFM_HAS_JNI)
    (void)rawEnv;
    (void)rawClass;
    return false;
#else
    auto& impl = *impl_;
    auto* env = static_cast<JNIEnv*>(rawEnv);
    auto bridgeClass = static_cast<jclass>(rawClass);
    if (!env || !bridgeClass || !impl.vm || impl.stopping) return false;
    std::lock_guard lock(impl.callMutex);
    if (impl.bridgeClass) {
        if (isSameClass(env, impl.bridgeClass, bridgeClass)) return true;
        setError(impl, "NativeBridge was requested from a second classloader");
        return false;
    }
    // This is deliberately called with the Class object supplied by
    // KnotLauncher.  Do not replace it with FindClass: that would bind the
    // application-loader NativeBridge and break child-first transformed code.
    if (!registerBridge(env, this, bridgeClass)) return false;

    jobject loaderLocal = classLoaderFor(env, bridgeClass);
    jclass runtimeLocal = loadClassFromLoader(
        env, loaderLocal, contract::CppModRuntimeClass);
    if (!runtimeLocal) {
        if (loaderLocal) env->DeleteLocalRef(loaderLocal);
        setError(impl, "CppModRuntime is unavailable in NativeBridge classloader");
        return false;
    }
    jclass bridgeGlobal = static_cast<jclass>(env->NewGlobalRef(bridgeClass));
    jobject loaderGlobal = loaderLocal ? env->NewGlobalRef(loaderLocal) : nullptr;
    jclass runtimeGlobal = static_cast<jclass>(env->NewGlobalRef(runtimeLocal));
    if (loaderLocal) env->DeleteLocalRef(loaderLocal);
    env->DeleteLocalRef(runtimeLocal);
    if (!bridgeGlobal || !runtimeGlobal || (loaderLocal && !loaderGlobal)) {
        if (bridgeGlobal) env->DeleteGlobalRef(bridgeGlobal);
        if (runtimeGlobal) env->DeleteGlobalRef(runtimeGlobal);
        if (loaderGlobal) env->DeleteGlobalRef(loaderGlobal);
        setError(impl, "failed to retain Knot classloader bridge references");
        return false;
    }
    impl.bridgeClass = bridgeGlobal;
    impl.bridgeLoader = loaderGlobal;
    impl.runtimeClass = runtimeGlobal;
    return true;
#endif
}

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

    // Publish the runtime before any Java bootstrap code executes.  This is
    // required for both NativeBridge calls made by CppModRuntime and the
    // KnotLauncher.installBridge(Class<?>) callback.  `started` is made true
    // for the bootstrap window and is rolled back by stop() on failure.
    g_activeRuntime.store(this, std::memory_order_release);
    impl.serverHandle = impl.handles.registerObject(&impl.server, HandleKind::Server);

    jclass knotLocal = nullptr;
    if (impl.config.preferKnot) {
        knotLocal = env->FindClass(contract::KnotLauncherClass);
        if (!knotLocal && env->ExceptionCheck()) env->ExceptionClear();
    }

    auto fail = [&](const char* message) {
        setError(impl, message);
        stop();
        if (error) *error = impl.lastError;
        return false;
    };

    if (knotLocal) {
        impl.provider = JvmProvider::KnotLauncher;
        impl.providerClass = static_cast<jclass>(env->NewGlobalRef(knotLocal));
        jobject providerLoaderLocal = classLoaderFor(env, knotLocal);
        if (providerLoaderLocal)
            impl.providerLoader = env->NewGlobalRef(providerLoaderLocal);
        impl.dispatchClass = static_cast<jclass>(env->NewGlobalRef(knotLocal));
        env->DeleteLocalRef(knotLocal);
        if (providerLoaderLocal) env->DeleteLocalRef(providerLoaderLocal);
        if (!impl.providerClass || !impl.dispatchClass)
            return fail("failed to retain KnotLauncher class references");

        if (!registerKnotLauncher(env, this, impl.providerClass))
            return fail("KnotLauncher.installBridge(Class<?>) registration failed");
        impl.providerInstallBridge = env->GetStaticMethodID(
            impl.providerClass, contract::InstallBridgeName,
            contract::InstallBridgeDescriptor);
        impl.providerBootstrap = requiredStaticMethod(
            env, this, impl.providerClass, contract::KnotBootstrapName,
            contract::KnotBootstrapDescriptor);
        if (!impl.providerInstallBridge || !impl.providerBootstrap)
            return fail("KnotLauncher has an incompatible bootstrap ABI");
        if (!resolveDispatchMethods(env, this, impl.dispatchClass, true))
            return fail("KnotLauncher is missing a required dispatch method");

        impl.started = true;
        impl.bootstrapInvoked = true;
        const jstring classes = toJavaString(env, classesDir.string());
        const jstring mods = toJavaString(env, modsDir.string());
        const jstring config = toJavaString(env, configDir.string());
        const jboolean ok = env->CallStaticBooleanMethod(
            impl.providerClass, impl.providerBootstrap, classes, mods, config);
        if (classes) env->DeleteLocalRef(classes);
        if (mods) env->DeleteLocalRef(mods);
        if (config) env->DeleteLocalRef(config);
        if (clearJavaException(env, this, "KnotLauncher.bootstrap") || ok != JNI_TRUE)
            return fail("KnotLauncher bootstrap failed");
        if (!impl.bridgeClass || !impl.runtimeClass)
            return fail("KnotLauncher bootstrap did not install NativeBridge");
    } else {
        impl.provider = JvmProvider::CompatibilityFallback;
        jclass bridgeLocal = env->FindClass(contract::NativeBridgeClass);
        if (!bridgeLocal || clearJavaException(env, this, "FindClass NativeBridge"))
            return fail("NativeBridge class is unavailable in JVM classes directory");
        if (!registerBridge(env, this, bridgeLocal)) {
            env->DeleteLocalRef(bridgeLocal);
            return fail("failed to register NativeBridge methods");
        }
        jobject bridgeLoaderLocal = classLoaderFor(env, bridgeLocal);
        impl.bridgeClass = static_cast<jclass>(env->NewGlobalRef(bridgeLocal));
        if (bridgeLoaderLocal)
            impl.bridgeLoader = env->NewGlobalRef(bridgeLoaderLocal);
        jclass runtimeLocal = loadClassFromLoader(
            env, bridgeLoaderLocal, contract::CppModRuntimeClass);
        if (!runtimeLocal) runtimeLocal = env->FindClass(contract::CppModRuntimeClass);
        if (!runtimeLocal || clearJavaException(env, this, "FindClass CppModRuntime")) {
            if (bridgeLoaderLocal) env->DeleteLocalRef(bridgeLoaderLocal);
            env->DeleteLocalRef(bridgeLocal);
            return fail("CppModRuntime class is unavailable in JVM classes directory");
        }
        impl.runtimeClass = static_cast<jclass>(env->NewGlobalRef(runtimeLocal));
        impl.dispatchClass = static_cast<jclass>(env->NewGlobalRef(runtimeLocal));
        if (bridgeLoaderLocal) env->DeleteLocalRef(bridgeLoaderLocal);
        env->DeleteLocalRef(bridgeLocal);
        env->DeleteLocalRef(runtimeLocal);
        if (!impl.bridgeClass || !impl.runtimeClass || !impl.dispatchClass)
            return fail("failed to retain fallback JVM class references");

        impl.bootstrap = requiredStaticMethod(
            env, this, impl.runtimeClass, contract::KnotBootstrapName,
            contract::FallbackBootstrapDescriptor);
        impl.bootstrapHasClassesDir = false;
        if (!impl.bootstrap || !resolveDispatchMethods(env, this, impl.dispatchClass, false))
            return fail("CppModRuntime is missing a required lifecycle method");

        impl.started = true;
        impl.bootstrapInvoked = true;
        const jstring mods = toJavaString(env, modsDir.string());
        const jstring config = toJavaString(env, configDir.string());
        const jboolean ok = env->CallStaticBooleanMethod(
            impl.runtimeClass, impl.bootstrap, mods, config);
        if (mods) env->DeleteLocalRef(mods);
        if (config) env->DeleteLocalRef(config);
        if (clearJavaException(env, this, "CppModRuntime.bootstrap") || ok != JNI_TRUE)
            return fail("Java mod bootstrap failed");
    }
    std::fprintf(stderr, "[cppfm][jvm] embedded HotSpot started; provider=%d mods=%s classes=%s\n",
                 static_cast<int>(impl.provider), modsDir.string().c_str(),
                 classesDir.string().c_str());
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
    // Keep g_activeRuntime published while Java shutdown runs: the Java
    // provider is allowed to release handles and flush callbacks during its
    // own shutdown.  The recursive call mutex blocks every other native
    // caller, and is only marked stopping after that callback returns.
    {
        AttachedEnv attached(impl.vm);
        if (attached) {
            auto* env = attached.get();
            if (impl.bootstrapInvoked && impl.dispatchClass && impl.shutdown) {
                env->CallStaticVoidMethod(impl.dispatchClass, impl.shutdown);
                clearJavaException(env, this, "JvmProvider.shutdown");
            }
            impl.stopping = true;
            g_activeRuntime.store(nullptr, std::memory_order_release);
            impl.objects.clear(env);
            deleteGlobal(env, impl.dispatchClass);
            deleteGlobal(env, impl.runtimeClass);
            deleteGlobal(env, impl.bridgeClass);
            deleteGlobal(env, impl.providerClass);
            deleteGlobal(env, impl.providerLoader);
            deleteGlobal(env, impl.bridgeLoader);
        } else {
            impl.stopping = true;
            g_activeRuntime.store(nullptr, std::memory_order_release);
        }
    }
    // DestroyJavaVM must run after all callbacks, attached worker threads, and
    // global references are gone.
    impl.vm->DestroyJavaVM();
    impl.vm = nullptr;
    if (impl.jvmLibrary) dlclose(impl.jvmLibrary);
    impl.jvmLibrary = nullptr;
#endif
    impl.handles.clear();
    impl.routing.clear();
    impl.serverHandle = 0;
    impl.started = false;
    impl.stopping = false;
    impl.bootstrapInvoked = false;
    impl.provider = JvmProvider::None;
}

bool JvmRuntime::started() const noexcept { return impl_->started; }

JvmProvider JvmRuntime::provider() const noexcept { return impl_->provider; }

bool JvmRuntime::knotActive() const noexcept {
    return impl_->provider == JvmProvider::KnotLauncher && impl_->started;
}

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
        impl.routing.transformedCount(),
        impl.routing.nativeCount(),
        impl.provider,
        impl.nativeDispatches.load(std::memory_order_relaxed),
        impl.jvmDispatches.load(std::memory_order_relaxed),
        impl.dispatchFailures.load(std::memory_order_relaxed),
        impl.bridgeExceptions.load(std::memory_order_relaxed)
    };
}

#if defined(CPPFM_HAS_JNI)
namespace {

template <typename Call>
bool invokeVoid(JvmRuntime& runtime, Call&& call, const char* name) {
    auto& impl = runtime.bridgeImpl();
    std::lock_guard lock(impl.callMutex);
    if (!impl.started || !impl.vm || !impl.dispatchClass) return true;
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
    if (!impl.started || !impl.vm || !impl.dispatchClass) return defaultValue;
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

#if defined(CPPFM_HAS_JNI)
namespace {

struct ParsedJvmType {
    char primitive = 0; // 0 means an object/array descriptor.
    std::string descriptor;
};

struct ParsedJvmMethod {
    std::vector<ParsedJvmType> arguments;
    ParsedJvmType result;
};

bool parseJvmType(const std::string& descriptor, std::size_t& offset,
                  bool allowVoid, ParsedJvmType& result, std::string& error) {
    if (offset >= descriptor.size()) {
        error = "truncated JVM descriptor";
        return false;
    }
    const std::size_t begin = offset;
    bool array = false;
    while (offset < descriptor.size() && descriptor[offset] == '[') {
        array = true;
        ++offset;
    }
    if (offset >= descriptor.size()) {
        error = "truncated array descriptor";
        return false;
    }
    const char tag = descriptor[offset++];
    if (tag == 'V') {
        if (!allowVoid || array) {
            error = "void is only valid as a method return";
            return false;
        }
        result.primitive = 'V';
    } else if (tag == 'L') {
        const auto end = descriptor.find(';', offset);
        if (end == std::string::npos || end == offset) {
            error = "unterminated object descriptor";
            return false;
        }
        offset = end + 1;
        result.primitive = 0;
    } else if (std::string("ZBCSIJFD").find(tag) != std::string::npos) {
        if (array) {
            result.primitive = 0;
        } else {
            result.primitive = tag;
        }
    } else {
        error = "unknown JVM descriptor tag";
        return false;
    }
    result.descriptor = descriptor.substr(begin, offset - begin);
    return true;
}

bool parseJvmMethod(const std::string& descriptor, ParsedJvmMethod& result,
                    std::string& error) {
    if (descriptor.empty() || descriptor.front() != '(') {
        error = "method descriptor does not start with '('";
        return false;
    }
    std::size_t offset = 1;
    while (offset < descriptor.size() && descriptor[offset] != ')') {
        ParsedJvmType type;
        if (!parseJvmType(descriptor, offset, false, type, error)) return false;
        result.arguments.push_back(std::move(type));
    }
    if (offset >= descriptor.size() || descriptor[offset] != ')') {
        error = "method descriptor has no closing ')'";
        return false;
    }
    ++offset;
    if (!parseJvmType(descriptor, offset, true, result.result, error)) return false;
    if (offset != descriptor.size()) {
        error = "trailing bytes in JVM descriptor";
        return false;
    }
    return true;
}

std::string objectInternalName(const ParsedJvmType& type) {
    if (type.descriptor.size() < 3 || type.descriptor.front() != 'L' ||
        type.descriptor.back() != ';') return {};
    return type.descriptor.substr(1, type.descriptor.size() - 2);
}

bool isObjectType(const ParsedJvmType& type) {
    return !type.descriptor.empty() &&
           (type.descriptor.front() == 'L' || type.descriptor.front() == '[');
}

jobject wrapperForHandle(JNIEnv* env, JvmRuntime& runtime, std::uint64_t handle,
                         const ParsedJvmType& expected,
                         std::vector<jobject>& localReferences,
                         std::string& error) {
    auto& impl = runtime.bridgeImpl();
    if (!handle) return nullptr;
    if (!impl.handles.valid(handle)) {
        error = "invalid native handle";
        return nullptr;
    }
    const std::string internalName = objectInternalName(expected);
    if (internalName.empty()) {
        error = "handle argument requires an object descriptor";
        return nullptr;
    }
    jclass type = loadClassFromLoader(env, impl.bridgeLoader, internalName);
    if (!type) {
        error = "wrapper class is not available: " + internalName;
        return nullptr;
    }
    if (void* cached = impl.objects.get(handle, internalName)) {
        auto* cachedObject = static_cast<jobject>(cached);
        if (env->IsInstanceOf(cachedObject, type) == JNI_TRUE) {
            jobject local = env->NewLocalRef(cachedObject);
            env->DeleteLocalRef(type);
            if (local) localReferences.push_back(local);
            return local;
        }
    }

    jobject object = nullptr;
    const std::string exactFactory = "(J)L" + internalName + ";";
    jmethodID factory = env->GetStaticMethodID(type, "of", exactFactory.c_str());
    if (factory) {
        object = env->CallStaticObjectMethod(type, factory,
                                             static_cast<jlong>(handle));
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    // ServerWorld carries the MinecraftServer wrapper as a second argument;
    // it is the only built-in shadow wrapper whose factory is not (J)T.
    if (!object && internalName == "net/minecraft/server/world/ServerWorld") {
        jclass serverType = loadClassFromLoader(
            env, impl.bridgeLoader, "net/minecraft/server/MinecraftServer");
        if (serverType) {
            const jmethodID serverFactory = env->GetStaticMethodID(
                serverType, "of", "(J)Lnet/minecraft/server/MinecraftServer;");
            if (serverFactory) {
                jobject serverObject = env->CallStaticObjectMethod(
                    serverType, serverFactory, static_cast<jlong>(impl.serverHandle));
                if (serverObject) localReferences.push_back(serverObject);
                const jmethodID worldFactory = env->GetStaticMethodID(
                    type, "of",
                    "(JLnet/minecraft/server/MinecraftServer;)Lnet/minecraft/server/world/ServerWorld;");
                if (worldFactory && serverObject)
                    object = env->CallStaticObjectMethod(type, worldFactory,
                                                         static_cast<jlong>(handle),
                                                         serverObject);
                else if (env->ExceptionCheck()) env->ExceptionClear();
            } else if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(serverType);
        }
    }

    if (!object) {
        const jmethodID constructor = env->GetMethodID(type, "<init>", "(J)V");
        if (constructor) {
            object = env->NewObject(type, constructor, static_cast<jlong>(handle));
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(type);
        error = "wrapper factory threw for " + internalName;
        return nullptr;
    }
    if (!object) {
        env->DeleteLocalRef(type);
        error = "wrapper has no supported handle factory: " + internalName;
        return nullptr;
    }
    jobject global = env->NewGlobalRef(object);
    if (!global) {
        env->DeleteLocalRef(object);
        env->DeleteLocalRef(type);
        error = "could not create wrapper global reference: " + internalName;
        return nullptr;
    }
    impl.objects.put(env, handle, internalName, global);
    localReferences.push_back(object);
    env->DeleteLocalRef(type);
    return object;
}

bool convertJvmArgument(JNIEnv* env, JvmRuntime& runtime,
                        const ParsedJvmType& type, const JvmValue& value,
                        jvalue& out, std::vector<jobject>& localReferences,
                        std::string& error) {
    out = jvalue{};
    if (isObjectType(type)) {
        if (value.kind == JvmValueKind::Null) return true;
        if (type.descriptor == "Ljava/lang/String;" &&
            value.kind == JvmValueKind::String) {
            out.l = toJavaString(env, value.stringValue);
            if (out.l) localReferences.push_back(out.l);
            return out.l != nullptr;
        }
        if (type.descriptor == "[B" && value.kind == JvmValueKind::Bytes) {
            out.l = toJavaBytes(env, value.bytesValue);
            if (out.l) localReferences.push_back(out.l);
            return out.l != nullptr;
        }
        if (value.kind == JvmValueKind::Handle) {
            out.l = wrapperForHandle(env, runtime, value.handleValue, type,
                                     localReferences, error);
            return out.l != nullptr || value.handleValue == 0;
        }
        error = "JvmValue kind does not match object descriptor " + type.descriptor;
        return false;
    }
    switch (type.primitive) {
    case 'Z':
        if (value.kind != JvmValueKind::Boolean) { error = "expected boolean argument"; return false; }
        out.z = value.booleanValue ? JNI_TRUE : JNI_FALSE; return true;
    case 'B': case 'C': case 'S': case 'I':
        if (value.kind != JvmValueKind::Int) { error = "expected int argument"; return false; }
        out.i = static_cast<jint>(value.intValue); return true;
    case 'J':
        if (value.kind != JvmValueKind::Long) { error = "expected long argument"; return false; }
        out.j = static_cast<jlong>(value.longValue); return true;
    case 'F':
        if (value.kind != JvmValueKind::Float) { error = "expected float argument"; return false; }
        out.f = value.floatValue; return true;
    case 'D':
        if (value.kind != JvmValueKind::Double) { error = "expected double argument"; return false; }
        out.d = value.doubleValue; return true;
    default:
        error = "unsupported argument descriptor " + type.descriptor;
        return false;
    }
}

JvmValue readJvmResult(JNIEnv* env, jobject object,
                       const ParsedJvmType& type) {
    if (!object) return JvmValue::nullValue();
    jclass stringType = env->FindClass("java/lang/String");
    if (stringType && env->IsInstanceOf(object, stringType) == JNI_TRUE) {
        const auto result = JvmValue::string(fromJavaString(env, static_cast<jstring>(object)));
        env->DeleteLocalRef(stringType);
        env->DeleteLocalRef(object);
        return result;
    }
    if (stringType) env->DeleteLocalRef(stringType);
    if (type.descriptor == "[B") {
        auto* bytes = static_cast<jbyteArray>(object);
        const jsize length = env->GetArrayLength(bytes);
        std::vector<std::uint8_t> value;
        if (length > 0) {
            value.resize(static_cast<std::size_t>(length));
            env->GetByteArrayRegion(bytes, 0, length,
                                    reinterpret_cast<jbyte*>(value.data()));
        }
        env->DeleteLocalRef(object);
        return JvmValue::bytes(std::move(value));
    }
    // Native-backed wrappers expose nativeHandle(), allowing object results to
    // return to the C++ side without leaking a jobject across the API.
    jclass objectClass = env->GetObjectClass(object);
    const jmethodID handleMethod = objectClass
        ? env->GetMethodID(objectClass, "nativeHandle", "()J") : nullptr;
    if (objectClass && env->ExceptionCheck()) env->ExceptionClear();
    if (handleMethod) {
        const auto handle = env->CallLongMethod(object, handleMethod);
        if (!env->ExceptionCheck()) {
            env->DeleteLocalRef(objectClass);
            env->DeleteLocalRef(object);
            return JvmValue::handle(static_cast<std::uint64_t>(handle));
        }
        env->ExceptionClear();
    }
    if (objectClass) env->DeleteLocalRef(objectClass);
    env->DeleteLocalRef(object);
    return JvmValue::nullValue();
}

void deleteLocalReferences(JNIEnv* env, std::vector<jobject>& references) {
    for (auto* reference : references)
        if (reference) env->DeleteLocalRef(reference);
    references.clear();
}

} // namespace
#endif

bool JvmRuntime::onServerTick(std::int64_t tick) {
    impl_->ticks.fetch_add(1, std::memory_order_relaxed);
    g_currentTick.store(tick, std::memory_order_release);
#if defined(CPPFM_HAS_JNI)
    return invokeVoid(*this, [&](JNIEnv* env) {
        env->CallStaticVoidMethod(impl_->dispatchClass, impl_->serverTick,
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
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    const auto handle = impl.handles.registerObject(&player, HandleKind::Player);
    impl.handles.invalidateHandle(handle);
#if defined(CPPFM_HAS_JNI)
    if (impl.vm && !impl.stopping) {
        AttachedEnv attached(impl.vm);
        if (attached) impl.objects.erase(attached.get(), handle);
    }
#endif
}

void JvmRuntime::invalidateEntity(MobEntity& entity) {
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    const auto handle = impl.handles.registerObject(&entity, HandleKind::Entity);
    impl.handles.invalidateHandle(handle);
#if defined(CPPFM_HAS_JNI)
    if (impl.vm && !impl.stopping) {
        AttachedEnv attached(impl.vm);
        if (attached) impl.objects.erase(attached.get(), handle);
    }
#endif
}

void JvmRuntime::onPlayerJoin(Player& player) {
    const auto handle = playerHandle(player);
    impl_->joins.fetch_add(1, std::memory_order_relaxed);
#if defined(CPPFM_HAS_JNI)
    invokeVoid(*this, [&](JNIEnv* env) {
        env->CallStaticVoidMethod(impl_->dispatchClass, impl_->playerJoin,
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
        env->CallStaticVoidMethod(impl_->dispatchClass, impl_->playerQuit,
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
    if (!impl.started || !impl.vm || !impl.dispatchClass) return true;
    AttachedEnv attached(impl.vm);
    if (!attached) return true;
    ++impl.callbacks;
    JNIEnv* env = attached.get();
    const jstring input = toJavaString(env, message);
    const jstring output = static_cast<jstring>(env->CallStaticObjectMethod(
        impl.dispatchClass, impl.chat, static_cast<jlong>(handle), input));
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
        return env->CallStaticBooleanMethod(impl_->dispatchClass, impl_->blockBreak,
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
        return env->CallStaticBooleanMethod(impl_->dispatchClass, impl_->blockPlace,
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
        return env->CallStaticBooleanMethod(impl_->dispatchClass, impl_->blockClicked,
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
    if (!impl.started || !impl.vm || !impl.dispatchClass) return true;
    AttachedEnv attached(impl.vm);
    if (!attached) return true;
    ++impl.callbacks;
    JNIEnv* env = attached.get();
    const jstring input = toJavaString(env, command);
    const jstring output = static_cast<jstring>(env->CallStaticObjectMethod(
        impl.dispatchClass, impl.command, static_cast<jlong>(handle), input));
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
            impl_->dispatchClass, impl_->entityDamage,
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
    const bool allowed = invokeBoolean(*this, [&](JNIEnv* env) {
        return env->CallStaticBooleanMethod(impl_->dispatchClass, impl_->mobSpawn,
                                            static_cast<jlong>(handle), x, y, z);
    }, "onMobSpawn");
    if (!allowed) invalidateEntity(mob);
    return allowed;
#else
    (void)handle; (void)x; (void)y; (void)z;
    return true;
#endif
}

bool JvmRuntime::onPluginMessage(Player& player, int phase,
                                 const std::string& channel,
                                 const std::vector<std::uint8_t>& payload) {
    const auto handle = playerHandle(player);
#if defined(CPPFM_HAS_JNI)
    auto& impl = *impl_;
    if (!impl.pluginMessage) return true;
    return invokeVoid(*this, [&](JNIEnv* env) {
        const jstring javaChannel = toJavaString(env, channel);
        const jbyteArray javaPayload = toJavaBytes(env, payload);
        env->CallStaticVoidMethod(impl.dispatchClass, impl.pluginMessage,
                                  static_cast<jlong>(handle), static_cast<jint>(phase),
                                  javaChannel, javaPayload);
        if (javaChannel) env->DeleteLocalRef(javaChannel);
        if (javaPayload) env->DeleteLocalRef(javaPayload);
    }, "onPluginMessage");
#else
    (void)handle; (void)phase; (void)channel; (void)payload;
    return true;
#endif
}

std::uint64_t JvmRuntime::nativeServerHandle() const { return impl_->serverHandle; }
std::int64_t JvmRuntime::nativeCurrentTick() const { return impl_->server.tickNow(); }

bool JvmRuntime::nativeHandleValid(std::uint64_t handle,
                                   HandleKind expected) const {
    return impl_->handles.valid(handle, expected);
}

HandleKind JvmRuntime::nativeHandleKind(std::uint64_t handle) const {
    return impl_->handles.kind(handle);
}

bool JvmRuntime::nativeInvalidateHandle(std::uint64_t handle) {
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    const bool invalidated = impl.handles.invalidateHandle(handle);
#if defined(CPPFM_HAS_JNI)
    if (invalidated && impl.vm && !impl.stopping) {
        AttachedEnv attached(impl.vm);
        if (attached) impl.objects.erase(attached.get(), handle);
    }
#endif
    return invalidated;
}

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

std::string JvmRuntime::nativeEntityType(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(
        impl_->handles.resolve(handle, HandleKind::Player));
    if (player) return "minecraft:player";
    const auto* mob = static_cast<const MobEntity*>(
        impl_->handles.resolve(handle, HandleKind::Entity));
    return mob ? std::string(MobEntity::kindName(mob->kind)) : std::string();
}

std::int32_t JvmRuntime::nativeEntityTypeId(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(
        impl_->handles.resolve(handle, HandleKind::Player));
    if (player) return static_cast<std::int32_t>(gen::kPlayerEntityTypeId);
    const auto* mob = static_cast<const MobEntity*>(
        impl_->handles.resolve(handle, HandleKind::Entity));
    return mob ? static_cast<std::int32_t>(MobEntity::typeId(mob->kind)) : -1;
}

float JvmRuntime::nativeEntityHealth(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(
        impl_->handles.resolve(handle, HandleKind::Player));
    if (player) return player->health;
    const auto* mob = static_cast<const MobEntity*>(
        impl_->handles.resolve(handle, HandleKind::Entity));
    return mob ? static_cast<float>(mob->health) : 0.0f;
}

bool JvmRuntime::nativeEntitySetHealth(std::uint64_t handle, float health) {
    if (!std::isfinite(health) || health < 0.0f) return false;
    auto* player = static_cast<Player*>(
        impl_->handles.resolve(handle, HandleKind::Player));
    if (player) {
        player->health = health;
        player->dead = health <= 0.0f;
        return true;
    }
    auto* mob = static_cast<MobEntity*>(
        impl_->handles.resolve(handle, HandleKind::Entity));
    if (!mob) return false;
    mob->health = health;
    mob->dead = health <= 0.0f;
    return true;
}

bool JvmRuntime::nativeEntityDead(std::uint64_t handle) const {
    const auto* player = static_cast<const Player*>(
        impl_->handles.resolve(handle, HandleKind::Player));
    if (player) return player->dead || player->health <= 0.0f;
    const auto* mob = static_cast<const MobEntity*>(
        impl_->handles.resolve(handle, HandleKind::Entity));
    return !mob || mob->dead || mob->health <= 0.0;
}

std::uint64_t JvmRuntime::nativeEntityWorld(std::uint64_t handle) const {
    return nativePlayerWorld(handle);
}

double JvmRuntime::nativeEntityCoordinate(std::uint64_t handle, int axis) const {
    return nativePlayerCoordinate(handle, axis);
}

bool JvmRuntime::nativeEntitySetPosition(std::uint64_t handle, double x,
                                         double y, double z) {
    return nativePlayerSetPosition(handle, x, y, z);
}

std::int32_t JvmRuntime::nativeEntityCount() const {
    const auto players = impl_->server.playersSnapshot();
    const auto mobs = impl_->server.mobsSnapshot();
    const std::size_t count = players.size() + mobs.size();
    return static_cast<std::int32_t>(std::min<std::size_t>(
        count, static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
}

std::uint64_t JvmRuntime::nativeEntityHandle(std::size_t index) {
    const auto players = impl_->server.playersSnapshot();
    if (index < players.size() && players[index]) return playerHandle(*players[index]);
    index -= std::min(index, players.size());
    const auto mobs = impl_->server.mobsSnapshot();
    if (index >= mobs.size() || !mobs[index]) return 0;
    return impl_->handles.registerObject(mobs[index].get(), HandleKind::Entity);
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

std::int64_t JvmRuntime::nativeWorldTime(std::uint64_t handle) const {
    const auto* world = static_cast<const World*>(
        impl_->handles.resolve(handle, HandleKind::World));
    return world ? impl_->server.dayTime() : 0;
}

std::int32_t JvmRuntime::nativeRegistryItemId(const std::string& name) const {
    const auto it = gen::itemIdByName().find(name);
    return it == gen::itemIdByName().end() ? -1 : static_cast<std::int32_t>(it->second);
}

std::string JvmRuntime::nativeRegistryItemName(std::int32_t id) const {
    if (id < 0) return {};
    for (const auto& [name, value] : gen::kItems)
        if (value == static_cast<std::uint32_t>(id)) return std::string(name);
    return {};
}

std::int32_t JvmRuntime::nativeRegistryBlockState(const std::string& name) const {
    const auto it = gen::blockNameToState().find(name);
    return it == gen::blockNameToState().end() ? -1 : static_cast<std::int32_t>(it->second);
}

std::string JvmRuntime::nativeRegistryBlockName(std::int32_t state) const {
    if (state < 0) return {};
    const auto* block = gen::blockByState(static_cast<std::uint32_t>(state));
    return block ? std::string(block->name) : std::string();
}

std::int32_t JvmRuntime::nativeRegistryEntryCount(const std::string& registry) const {
    if (registry == "minecraft:item" || registry == "minecraft:items")
        return static_cast<std::int32_t>(gen::kItems.size());
    if (registry == "minecraft:block" || registry == "minecraft:block_state")
        return static_cast<std::int32_t>(gen::kBlocks.size());
    if (registry == "minecraft:entity_type")
        return static_cast<std::int32_t>(gen::kEntities.size());
    return static_cast<std::int32_t>(impl_->server.gameData_.order(registry).size());
}

std::string JvmRuntime::nativeRegistryEntryName(const std::string& registry,
                                                std::int32_t id) const {
    if (id < 0) return {};
    if (registry == "minecraft:item" || registry == "minecraft:items")
        return nativeRegistryItemName(id);
    if (registry == "minecraft:block" || registry == "minecraft:block_state")
        return nativeRegistryBlockName(id);
    if (registry == "minecraft:entity_type") {
        for (const auto& [name, value] : gen::kEntities)
            if (value == static_cast<std::uint32_t>(id)) return std::string(name);
        return {};
    }
    return impl_->server.gameData_.keyOf(registry, id);
}

bool JvmRuntime::nativePlayerSendPluginMessage(
    std::uint64_t handle, const std::string& channel,
    const std::vector<std::uint8_t>& payload, int phase) {
    auto* player = static_cast<Player*>(
        impl_->handles.resolve(handle, HandleKind::Player));
    if (!player || !player->conn || channel.empty() || (phase != 0 && phase != 1))
        return false;
    WriteBuffer body;
    body.string(channel);
    if (!payload.empty()) body.raw(payload.data(), payload.size());
    try {
        player->conn->sendPacket(phase == 0 ? proto::cf::sc::CustomPayload
                                            : proto::pl::sc::CustomPayload,
                                 body);
        return true;
    } catch (...) {
        return false;
    }
}

std::string JvmRuntime::nativeServerSetting(const std::string& key) const {
    const auto& cfg = impl_->server.config();
    if (key == "motd") return cfg.motd;
    if (key == "port") return std::to_string(cfg.port);
    if (key == "max-players" || key == "maxPlayers") return std::to_string(cfg.maxPlayers);
    if (key == "view-distance" || key == "viewDistance") return std::to_string(cfg.viewDistance);
    if (key == "simulation-distance" || key == "simulationDistance")
        return std::to_string(cfg.simulationDistance);
    if (key == "online-mode" || key == "onlineMode") return cfg.onlineMode ? "true" : "false";
    if (key == "enforce-secure-profile" || key == "enforces-secure-chat")
        return cfg.enforcesSecureChat ? "true" : "false";
    if (key == "pvp") return cfg.pvp ? "true" : "false";
    if (key == "allow-flight" || key == "allowFlight") return cfg.allowFlight ? "true" : "false";
    if (key == "hardcore") return cfg.hardcore ? "true" : "false";
    if (key == "spawn-protection" || key == "spawnProtection")
        return std::to_string(cfg.spawnProtection);
    if (key == "level-type" || key == "levelType") return cfg.levelType;
    if (key == "world-dir" || key == "worldDir") return cfg.worldDir;
    if (key == "seed") return std::to_string(cfg.seed);
    if (key == "protocol") return std::to_string(proto::kProtocolVersion);
    if (key == "game-version" || key == "version") return proto::kMinecraftVersion;
    if (key == "jvm.provider") {
        return impl_->provider == JvmProvider::KnotLauncher ? "knot" :
               impl_->provider == JvmProvider::CompatibilityFallback ? "fallback" : "none";
    }
    return {};
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

void JvmRuntime::nativeRegisterTransformedMethod(const std::string& owner,
                                                 const std::string& name,
                                                 const std::string& descriptor,
                                                 std::uint64_t transformedHash) {
    if (!owner.empty() && !name.empty() && !descriptor.empty())
        impl_->routing.markTransformed(owner, name, descriptor, transformedHash);
}

void JvmRuntime::nativeRegisterMethodBaseline(const std::string& owner,
                                              const std::string& name,
                                              const std::string& descriptor,
                                              std::uint64_t baselineHash,
                                              std::uint64_t transformedHash) {
    if (owner.empty() || name.empty() || descriptor.empty()) return;
    impl_->routing.markBaseline(owner, name, descriptor, baselineHash);
    if (transformedHash && transformedHash != baselineHash)
        impl_->routing.markTransformed(owner, name, descriptor, transformedHash);
    else
        impl_->routing.markNative(owner, name, descriptor, baselineHash);
}

bool JvmRuntime::shouldUseJvm(const std::string& owner, const std::string& name,
                              const std::string& descriptor) const {
    return impl_->routing.path(owner, name, descriptor) == DispatchPath::JvmTransformed;
}

int JvmRuntime::nativeRoutePath(const std::string& owner, const std::string& name,
                                const std::string& descriptor) const {
    return shouldUseJvm(owner, name, descriptor) ? 1 : 0;
}

std::uint64_t JvmRuntime::nativeRouteHash(const std::string& owner,
                                          const std::string& name,
                                          const std::string& descriptor) const {
    return impl_->routing.hash(owner, name, descriptor);
}

std::int32_t JvmRuntime::nativeTransformedMethodCount() const {
    return static_cast<std::int32_t>(impl_->routing.transformedCount());
}

std::int32_t JvmRuntime::nativeNativeMethodCount() const {
    return static_cast<std::int32_t>(impl_->routing.nativeCount());
}

JvmDispatchResult JvmRuntime::dispatchTransformed(
    const std::string& owner, const std::string& name,
    const std::string& descriptor, std::uint64_t receiverHandle,
    const std::vector<JvmValue>& arguments) {
    JvmDispatchResult result;
    const auto route = impl_->routing.route(owner, name, descriptor);
    if (!route || route->path == DispatchPath::NativeFast) {
        impl_->nativeDispatches.fetch_add(1, std::memory_order_relaxed);
        result.success = true;
        return result;
    }
    result.invoked = true;
#if !defined(CPPFM_HAS_JNI)
    result.success = false;
    result.error = "JVM transformed dispatch requested from a non-JNI build";
    impl_->dispatchFailures.fetch_add(1, std::memory_order_relaxed);
    return result;
#else
    auto& impl = *impl_;
    std::lock_guard lock(impl.callMutex);
    auto fail = [&](std::string message) {
        result.success = false;
        result.error = std::move(message);
        impl.dispatchFailures.fetch_add(1, std::memory_order_relaxed);
        return result;
    };
    if (!impl.started || !impl.vm || !impl.dispatchClass) {
        return fail("JVM transformed dispatch is not active");
    }
    ParsedJvmMethod parsed;
    std::string parseError;
    if (!parseJvmMethod(descriptor, parsed, parseError))
        return fail(parseError);
    if (parsed.arguments.size() != arguments.size())
        return fail("JVM dispatch argument count does not match descriptor");
    AttachedEnv attached(impl.vm);
    if (!attached) return fail("could not attach dispatch thread to JVM");
    JNIEnv* env = attached.get();
    const std::string canonicalOwner = ModRoutingTable::canonicalOwner(owner);
    jclass ownerClass = loadClassFromLoader(env, impl.bridgeLoader, canonicalOwner);
    if (!ownerClass) return fail("transformed owner class is unavailable: " + canonicalOwner);
    std::vector<jobject> localReferences;
    jobject receiver = nullptr;
    if (receiverHandle) {
        ParsedJvmType receiverType;
        receiverType.descriptor = "L" + canonicalOwner + ";";
        receiver = wrapperForHandle(env, *this, receiverHandle, receiverType,
                                    localReferences, result.error);
        if (!receiver) {
            env->DeleteLocalRef(ownerClass);
            return fail(result.error.empty() ? "could not create receiver wrapper" : result.error);
        }
    }
    const jmethodID method = receiverHandle
        ? env->GetMethodID(ownerClass, name.c_str(), descriptor.c_str())
        : env->GetStaticMethodID(ownerClass, name.c_str(), descriptor.c_str());
    if (!method) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        deleteLocalReferences(env, localReferences);
        env->DeleteLocalRef(ownerClass);
        return fail("transformed method is unavailable: " + canonicalOwner + "." + name + descriptor);
    }
    std::vector<jvalue> values(parsed.arguments.size());
    for (std::size_t i = 0; i < parsed.arguments.size(); ++i) {
        if (!convertJvmArgument(env, *this, parsed.arguments[i], arguments[i],
                                values[i], localReferences, result.error)) {
            deleteLocalReferences(env, localReferences);
            env->DeleteLocalRef(ownerClass);
            return fail(result.error.empty() ? "could not convert JVM dispatch argument" : result.error);
        }
    }
    impl.jvmDispatches.fetch_add(1, std::memory_order_relaxed);
    switch (parsed.result.primitive) {
    case 'V':
        if (receiverHandle) env->CallVoidMethodA(receiver, method, values.data());
        else env->CallStaticVoidMethodA(ownerClass, method, values.data());
        break;
    case 'Z':
        result.value = JvmValue::boolean(receiverHandle
            ? env->CallBooleanMethodA(receiver, method, values.data()) == JNI_TRUE
            : env->CallStaticBooleanMethodA(ownerClass, method, values.data()) == JNI_TRUE);
        break;
    case 'B':
        result.value = JvmValue::integer(static_cast<std::int32_t>(receiverHandle
            ? env->CallByteMethodA(receiver, method, values.data())
            : env->CallStaticByteMethodA(ownerClass, method, values.data())));
        break;
    case 'C':
        result.value = JvmValue::integer(static_cast<std::int32_t>(receiverHandle
            ? env->CallCharMethodA(receiver, method, values.data())
            : env->CallStaticCharMethodA(ownerClass, method, values.data())));
        break;
    case 'S':
        result.value = JvmValue::integer(static_cast<std::int32_t>(receiverHandle
            ? env->CallShortMethodA(receiver, method, values.data())
            : env->CallStaticShortMethodA(ownerClass, method, values.data())));
        break;
    case 'I':
        result.value = JvmValue::integer(static_cast<std::int32_t>(receiverHandle
            ? env->CallIntMethodA(receiver, method, values.data())
            : env->CallStaticIntMethodA(ownerClass, method, values.data())));
        break;
    case 'J':
        result.value = JvmValue::longInt(static_cast<std::int64_t>(receiverHandle
            ? env->CallLongMethodA(receiver, method, values.data())
            : env->CallStaticLongMethodA(ownerClass, method, values.data())));
        break;
    case 'F':
        result.value = JvmValue::floating(receiverHandle
            ? env->CallFloatMethodA(receiver, method, values.data())
            : env->CallStaticFloatMethodA(ownerClass, method, values.data()));
        break;
    case 'D':
        result.value = JvmValue::doubleFloat(receiverHandle
            ? env->CallDoubleMethodA(receiver, method, values.data())
            : env->CallStaticDoubleMethodA(ownerClass, method, values.data()));
        break;
    default: {
        jobject value = receiverHandle
            ? env->CallObjectMethodA(receiver, method, values.data())
            : env->CallStaticObjectMethodA(ownerClass, method, values.data());
        if (env->ExceptionCheck()) {
            deleteLocalReferences(env, localReferences);
            env->DeleteLocalRef(ownerClass);
            clearJavaException(env, this, "dispatchTransformed");
            return fail("transformed Java method threw an exception");
        }
        result.value = readJvmResult(env, value, parsed.result);
        break;
    }
    }
    if (env->ExceptionCheck()) {
        deleteLocalReferences(env, localReferences);
        env->DeleteLocalRef(ownerClass);
        clearJavaException(env, this, "dispatchTransformed");
        return fail("transformed Java method threw an exception");
    }
    deleteLocalReferences(env, localReferences);
    env->DeleteLocalRef(ownerClass);
    result.success = true;
    return result;
#endif
}

} // namespace cppfm::jvm
