#include "jvm/JvmContract.hpp"
#include "jvm/ModRoutingTable.hpp"
#include "jvm/NativeHandleTable.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using cppfm::jvm::DispatchPath;
using cppfm::jvm::HandleKind;
using cppfm::jvm::ModRoutingTable;
using cppfm::jvm::NativeHandleTable;

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        if (!condition) {
            std::fprintf(stderr, "FAIL %s\n", name);
            ++failures;
        }
    };

    using namespace cppfm::jvm::contract;
    check(std::string(KnotLauncherClass) == "cppfm/bridge/KnotLauncher",
          "KnotLauncher internal name");
    check(std::string(InstallBridgeDescriptor) == "(Ljava/lang/Class;)Z",
          "installBridge descriptor");
    check(std::string(KnotBootstrapDescriptor) ==
              "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
          "Knot bootstrap descriptor");
    check(std::string(ShutdownDescriptor) == "()V", "shutdown descriptor");
    check(std::string(OnServerTickDescriptor) == "(J)V",
          "tick descriptor");
    check(std::string(OnChatDescriptor) ==
              "(JLjava/lang/String;)Ljava/lang/String;",
          "chat descriptor");
    check(std::string(OnBlockClickedDescriptor) == "(JIIIII)Z",
          "block click descriptor");
    check(std::string(OnEntityDamageDescriptor) ==
              "(JJFLjava/lang/String;)Z",
          "damage descriptor");
    check(std::string(OnMobSpawnDescriptor) == "(JDDD)Z",
          "mob spawn descriptor");

    int object = 0;
    NativeHandleTable handles;
    const auto first = handles.registerObject(&object, HandleKind::Entity);
    check(first != 0 && handles.valid(first, HandleKind::Entity),
          "first generation is live");
    check(handles.kind(first) == HandleKind::Entity, "handle kind lookup");
    check(handles.invalidateHandle(first), "invalidate by opaque handle");
    const auto second = handles.registerObject(&object, HandleKind::Entity);
    check(second != 0 && second != first && handles.valid(second),
          "address reuse cannot revive stale handle");
    check(!handles.valid(first), "stale generation fails closed");

    ModRoutingTable routing;
    const std::string owner = "net.minecraft.server.MinecraftServer";
    const std::string descriptor = "()V";
    const auto baseline = ModRoutingTable::stableHash(
        "net/minecraft/server/MinecraftServer", "tick", descriptor);
    routing.markBaseline(owner, "tick", descriptor, baseline);
    routing.markTransformed(owner, "tick", descriptor, baseline + 1);
    check(routing.path("net/minecraft/server/MinecraftServer", "tick", descriptor) ==
              DispatchPath::JvmTransformed,
          "transformed route is selected");
    check(routing.hash(owner, "tick", descriptor) == baseline + 1,
          "transformed hash is exposed");
    routing.markNative(owner, "tick", descriptor, baseline);
    check(routing.path(owner, "tick", descriptor) == DispatchPath::NativeFast,
          "native override restores fast route");
    routing.markTransformed(owner, "join", "*", 77);
    check(routing.path("net/minecraft/server/MinecraftServer", "join", "(J)V") ==
              DispatchPath::JvmTransformed,
          "wildcard descriptor route");
    check(routing.transformedCount() == 1, "transformed count tracks exact routes");

    std::printf("JVM native bridge contract/routing checks: %s\n",
                failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
