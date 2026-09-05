// Stable Java-side ABI used by the embedded JVM boundary.
//
// Keep these names/descriptors in one native-owned contract so a later full
// Fabric/Knot implementation can implement the exact same surface without
// reverse engineering JNI call sites.  The dispatch methods belong to the
// KnotLauncher class loaded by the application loader; NativeBridge itself is
// loaded from the child-first loader and receives RegisterNatives on that exact
// Class object via installBridge(Class<?>).
#pragma once

namespace cppfm::jvm::contract {

inline constexpr char KnotLauncherClass[] = "cppfm/bridge/KnotLauncher";
inline constexpr char NativeBridgeClass[] = "cppfm/bridge/NativeBridge";
inline constexpr char CppModRuntimeClass[] = "cppfm/bridge/CppModRuntime";

inline constexpr char InstallBridgeName[] = "installBridge";
inline constexpr char InstallBridgeDescriptor[] = "(Ljava/lang/Class;)Z";

inline constexpr char KnotBootstrapName[] = "bootstrap";
inline constexpr char KnotBootstrapDescriptor[] =
    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z";
inline constexpr char FallbackBootstrapDescriptor[] =
    "(Ljava/lang/String;Ljava/lang/String;)Z";
inline constexpr char ShutdownName[] = "shutdown";
inline constexpr char ShutdownDescriptor[] = "()V";

inline constexpr char OnServerTickName[] = "onServerTick";
inline constexpr char OnServerTickDescriptor[] = "(J)V";
inline constexpr char OnPlayerJoinName[] = "onPlayerJoin";
inline constexpr char OnPlayerJoinDescriptor[] = "(J)V";
inline constexpr char OnPlayerQuitName[] = "onPlayerQuit";
inline constexpr char OnPlayerQuitDescriptor[] = "(J)V";
inline constexpr char OnChatName[] = "onChat";
inline constexpr char OnChatDescriptor[] =
    "(JLjava/lang/String;)Ljava/lang/String;";
inline constexpr char OnBlockBreakName[] = "onBlockBreak";
inline constexpr char OnBlockBreakDescriptor[] = "(JIIII)Z";
inline constexpr char OnBlockPlaceName[] = "onBlockPlace";
inline constexpr char OnBlockPlaceDescriptor[] = "(JIIII)Z";
inline constexpr char OnBlockClickedName[] = "onBlockClicked";
inline constexpr char OnBlockClickedDescriptor[] = "(JIIIII)Z";
inline constexpr char OnCommandName[] = "onCommand";
inline constexpr char OnCommandDescriptor[] =
    "(JLjava/lang/String;)Ljava/lang/String;";
inline constexpr char OnEntityDamageName[] = "onEntityDamage";
inline constexpr char OnEntityDamageDescriptor[] =
    "(JJFLjava/lang/String;)Z";
inline constexpr char OnMobSpawnName[] = "onMobSpawn";
inline constexpr char OnMobSpawnDescriptor[] = "(JDDD)Z";

inline constexpr char NativePluginMessageDescriptor[] =
    "(JILjava/lang/String;[B)V";

} // namespace cppfm::jvm::contract
