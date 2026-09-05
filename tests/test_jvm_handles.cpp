#include "jvm/NativeHandleTable.hpp"
#include "jvm/ModRoutingTable.hpp"

#include <cstdio>

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

    int object = 0;
    NativeHandleTable handles;
    const auto player = handles.registerObject(&object, HandleKind::Player);
    check(player != 0, "register player");
    check(handles.registerObject(&object, HandleKind::Player) == player,
          "same live object has stable identity");
    check(handles.valid(player, HandleKind::Player), "live handle resolves");
    check(!handles.valid(player, HandleKind::World), "kind mismatch fails closed");
    check(handles.invalidate(&object, HandleKind::Player), "invalidate live handle");
    check(!handles.valid(player), "stale handle fails after invalidation");
    const auto replacement = handles.registerObject(&object, HandleKind::Player);
    check(replacement != player, "address reuse gets a new opaque id");
    check(handles.valid(replacement, HandleKind::Player), "replacement resolves");

    ModRoutingTable routing;
    check(routing.path("net/minecraft/World", "tick", "()V") ==
              DispatchPath::NativeFast,
          "unmodified method uses native fast path");
    routing.markTransformed("net/minecraft/World", "tick", "()V");
    check(routing.path("net/minecraft/World", "tick", "()V") ==
              DispatchPath::JvmTransformed,
          "transformed method routes through JVM");
    routing.markNative("net/minecraft/World", "tick", "()V");
    check(routing.path("net/minecraft/World", "tick", "()V") ==
              DispatchPath::NativeFast,
          "native override restores fast path");

    std::printf("JVM handle/routing checks: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
