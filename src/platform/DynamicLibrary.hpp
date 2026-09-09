// Cross-platform dynamic-library loading for the optional HotSpot bridge.
#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cppfm::platform {

#ifdef _WIN32
using dynamic_library_t = HMODULE;
inline constexpr dynamic_library_t invalid_dynamic_library = nullptr;

inline dynamic_library_t openDynamicLibrary(const std::filesystem::path& path) noexcept {
    // LOAD_WITH_ALTERED_SEARCH_PATH makes dependencies next to a full-path
    // jvm.dll (notably the JDK's bin DLLs) discoverable even when the caller
    // started with a clean PATH.  The full path still prevents accidental
    // loading of a different JVM from the current directory.
    return ::LoadLibraryExW(path.wstring().c_str(), nullptr,
                            LOAD_WITH_ALTERED_SEARCH_PATH);
}

inline void* dynamicSymbol(dynamic_library_t library, const char* name) noexcept {
    return reinterpret_cast<void*>(::GetProcAddress(library, name));
}

inline void closeDynamicLibrary(dynamic_library_t library) noexcept {
    if (library) ::FreeLibrary(library);
}

inline std::string dynamicLibraryError() {
    return "Windows loader error " + std::to_string(GetLastError());
}
#else
using dynamic_library_t = void*;
inline constexpr dynamic_library_t invalid_dynamic_library = nullptr;

inline dynamic_library_t openDynamicLibrary(const std::filesystem::path& path) noexcept {
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

inline void* dynamicSymbol(dynamic_library_t library, const char* name) noexcept {
    return ::dlsym(library, name);
}

inline void closeDynamicLibrary(dynamic_library_t library) noexcept {
    if (library) ::dlclose(library);
}

inline std::string dynamicLibraryError() {
    const char* message = ::dlerror();
    return message ? std::string(message) : std::string("unknown loader error");
}
#endif

} // namespace cppfm::platform
