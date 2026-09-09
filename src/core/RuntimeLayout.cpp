#include "RuntimeLayout.hpp"

#include <EmbeddedResources.hpp>
#include "../platform/FileLock.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#include <zlib.h>

namespace cppfm {
namespace {

constexpr std::string_view kMagic = "CPPFMRES1";
constexpr std::uint32_t kMaxEntries = 10000;
constexpr std::uint64_t kMaxResourceSize = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxExtractedSize = 256ULL * 1024ULL * 1024ULL;

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

bool readU32(const std::uint8_t*& cursor, const std::uint8_t* end,
             std::uint32_t& value) {
    if (static_cast<std::size_t>(end - cursor) < 4) return false;
    value = static_cast<std::uint32_t>(cursor[0]) |
            (static_cast<std::uint32_t>(cursor[1]) << 8) |
            (static_cast<std::uint32_t>(cursor[2]) << 16) |
            (static_cast<std::uint32_t>(cursor[3]) << 24);
    cursor += 4;
    return true;
}

bool readU64(const std::uint8_t*& cursor, const std::uint8_t* end,
             std::uint64_t& value) {
    if (static_cast<std::size_t>(end - cursor) < 8) return false;
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(*cursor++) << shift;
    return true;
}

bool safeResourcePath(std::string_view value) {
    if (value.empty() || value.front() == '/' || value.front() == '\\' ||
        value.find(':') != std::string_view::npos ||
        value.find('\\') != std::string_view::npos)
        return false;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto component = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (component.empty() || component == "." || component == "..") return false;
        begin = end == std::string_view::npos ? value.size() : end + 1;
    }
    return true;
}

std::string temporarySuffix() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return ".tmp-" + std::to_string(now) + "-" + std::to_string(thread);
}

bool regularNonSymlink(const std::filesystem::path& path,
                       std::error_code& error) {
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) return false;
    return std::filesystem::is_regular_file(status);
}

// Install a completed temporary file without ever replacing a file that may
// have appeared after the initial status check.  The hard-link paths are
// atomic and no-replace on the native filesystems used by the server.  The
// copy_file fallback is still explicitly SKIP_EXISTING, which preserves the
// same user-file invariant on filesystems that do not support hard links.
bool installWithoutReplace(const std::filesystem::path& temporary,
                           const std::filesystem::path& destination,
                           std::error_code& error) {
#ifdef _WIN32
    if (::CreateHardLinkW(destination.wstring().c_str(),
                          temporary.wstring().c_str(), nullptr)) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        if (removeError) {
            error = removeError;
            return false;
        }
        error.clear();
        return true;
    }
    const auto nativeError = static_cast<unsigned long>(::GetLastError());
    if (nativeError == ERROR_FILE_EXISTS || nativeError == ERROR_ALREADY_EXISTS)
        error = std::make_error_code(std::errc::file_exists);
    else
        error = std::error_code(static_cast<int>(nativeError),
                                std::system_category());
#else
    if (::link(temporary.c_str(), destination.c_str()) == 0) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        if (removeError) {
            error = removeError;
            return false;
        }
        error.clear();
        return true;
    }
    if (errno == EEXIST)
        error = std::make_error_code(std::errc::file_exists);
    else
        error = std::error_code(errno, std::generic_category());
#endif

    // Hard links can be disabled by a filesystem policy.  copy_file with
    // skip_existing is the portable fallback and, unlike rename, cannot
    // replace a file created by a user or another installer.
    std::error_code copyError;
    const bool copied = std::filesystem::copy_file(
        temporary, destination,
        std::filesystem::copy_options::skip_existing, copyError);
    if (copied) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        if (removeError) {
            error = removeError;
            return false;
        }
        error.clear();
        return true;
    }
    if (copyError) error = copyError;
    return false;
}

bool writeIfMissing(const std::filesystem::path& root, std::string_view name,
                   const std::vector<std::uint8_t>& bytes, std::string& error) {
    if (!safeResourcePath(name)) {
        error = "embedded resource has an unsafe path: " + std::string(name);
        return false;
    }
    const auto destination = root / std::filesystem::path(std::string(name));
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(destination, ec);
    if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        error = "could not inspect embedded resource " + destination.string() +
                ": " + ec.message();
        return false;
    }
    ec.clear();
    if (std::filesystem::exists(status)) {
        if (!std::filesystem::is_regular_file(status)) {
            error = "embedded resource target is not a regular file: " + destination.string();
            return false;
        }
        return true;
    }
    if (!std::filesystem::create_directories(destination.parent_path(), ec) && ec) {
        error = "could not create resource directory " + destination.parent_path().string() +
                ": " + ec.message();
        return false;
    }
    std::filesystem::path temporary = destination;
    temporary += temporarySuffix();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "could not write embedded resource: " + destination.string();
            return false;
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        if (!output) {
            std::filesystem::remove(temporary, ec);
            error = "could not finish embedded resource: " + destination.string();
            return false;
        }
    }
    std::error_code installError;
    if (installWithoutReplace(temporary, destination, installError)) return true;

    // A competing installer may have won the no-replace race.  Re-check with
    // symlink_status so a user-created symlink or directory is never accepted
    // as the immutable regular-file resource we need.
    std::error_code statusError;
    if (installError == std::make_error_code(std::errc::file_exists) &&
        regularNonSymlink(destination, statusError)) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        if (removeError) {
            error = "could not remove temporary embedded resource " +
                    temporary.string() + ": " + removeError.message();
            return false;
        }
        return true;
    }
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    error = "could not install embedded resource " + destination.string() +
            ": " + (statusError ? statusError.message() : installError.message());
    return false;
}

std::uint64_t embeddedResources(const std::filesystem::path& root,
                                std::string& error,
                                const std::filesystem::path* classesRoot = nullptr) {
    if (!embedded::kHasPack || embedded::kPackSize == 0) return 0;
    const auto* cursor = embedded::kPack;
    const auto* end = embedded::kPack + embedded::kPackSize;
    if (embedded::kPackSize < kMagic.size() ||
        std::memcmp(cursor, kMagic.data(), kMagic.size()) != 0) {
        error = "embedded resource header is invalid";
        return 0;
    }
    cursor += kMagic.size();
    std::uint32_t count = 0;
    if (!readU32(cursor, end, count) || count > kMaxEntries) {
        error = "embedded resource entry count is invalid";
        return 0;
    }
    std::uint64_t extracted = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t pathSize = 0;
        std::uint64_t rawSize = 0;
        std::uint64_t compressedSize = 0;
        if (!readU32(cursor, end, pathSize) ||
            !readU64(cursor, end, rawSize) ||
            !readU64(cursor, end, compressedSize) ||
            pathSize == 0 || rawSize > kMaxResourceSize ||
            compressedSize == 0 ||
            static_cast<std::size_t>(end - cursor) < pathSize) {
            error = "embedded resource entry header is invalid";
            return 0;
        }
        const std::string name(reinterpret_cast<const char*>(cursor), pathSize);
        cursor += pathSize;
        if (compressedSize > static_cast<std::uint64_t>(end - cursor) ||
            rawSize > kMaxExtractedSize - extracted) {
            error = "embedded resource entry exceeds bounds: " + name;
            return 0;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(rawSize));
        uLongf destinationSize = static_cast<uLongf>(rawSize);
        const auto result = uncompress(
            bytes.data(), &destinationSize, cursor,
            static_cast<uLong>(compressedSize));
        if (result != Z_OK || destinationSize != rawSize) {
            error = "could not decompress embedded resource: " + name;
            return 0;
        }
        cursor += static_cast<std::ptrdiff_t>(compressedSize);
        constexpr std::string_view classesPrefix = ".cppfm/jvm/classes/";
        const bool isClass = classesRoot && name.starts_with(classesPrefix);
        const auto& destinationRoot = isClass ? *classesRoot : root;
        const auto destinationName = isClass
            ? std::string_view(name).substr(classesPrefix.size())
            : std::string_view(name);
        if (!writeIfMissing(destinationRoot, destinationName, bytes, error)) return 0;
        extracted += rawSize;
    }
    return extracted;
}

bool ensureDirectory(const std::filesystem::path& root, std::string_view relative,
                     std::string& error) {
    std::error_code ec;
    const auto path = root / std::filesystem::path(std::string(relative));
    if (!std::filesystem::create_directories(path, ec) && ec) {
        error = "could not create server directory " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool ensureDefaultProperties(const std::filesystem::path& root, std::string& error) {
    const auto destination = root / "server.properties";
    std::error_code ec;
    const auto destinationStatus = std::filesystem::symlink_status(destination, ec);
    if (!ec && std::filesystem::exists(destinationStatus)) {
        if (!std::filesystem::is_regular_file(destinationStatus)) {
            error = "server.properties is not a regular file: " + destination.string();
            return false;
        }
        return true;
    }
    if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        error = "could not inspect server.properties: " + ec.message();
        return false;
    }
    // `symlink_status` reports ENOENT for the normal first-run case.  Clear
    // that expected miss before probing the optional example file; only
    // existing non-regular targets are rejected above.
    ec.clear();
    const auto example = root / "server.properties.example";
    if (std::filesystem::is_regular_file(example, ec)) {
        std::ifstream input(example, std::ios::binary);
        if (input) {
            const std::string contents((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());
            if (!input.bad()) {
                const std::vector<std::uint8_t> bytes(contents.begin(), contents.end());
                return writeIfMissing(root, "server.properties", bytes, error);
            }
        }
    }
    const std::string defaults =
        "server-port=25565\n"
        "max-players=20\n"
        "view-distance=6\n"
        "simulation-distance=10\n"
        "motd=CppFabricMC - C++ Minecraft 1.21.4 server\n";
    const std::vector<std::uint8_t> bytes(defaults.begin(), defaults.end());
    return writeIfMissing(root, "server.properties", bytes, error);
}

std::filesystem::path selectRoot(std::string& error) {
    const auto configured = environment("CPPFM_SERVER_DIR");
    std::error_code ec;
    auto root = configured.empty() ? std::filesystem::current_path(ec)
                                   : std::filesystem::absolute(configured, ec);
    if (ec || root.empty()) {
        error = "could not determine server directory: " + ec.message();
        return {};
    }
    return root.lexically_normal();
}

} // namespace

RuntimeLayout::~RuntimeLayout() = default;

RuntimeLayout& RuntimeLayout::processInstance() {
    // The executable has no caller-owned RuntimeLayout object: keep one
    // process instance so the server-directory lock remains held for the
    // entire lifetime of the running server.
    static RuntimeLayout instance;
    return instance;
}

bool RuntimeLayout::prepare(std::filesystem::path& root, std::string* error) {
    return processInstance().prepareForProcess(root, error);
}

bool RuntimeLayout::prepareForProcess(std::filesystem::path& root,
                                      std::string* error) {
    // RuntimeLayout is process-global because its lock must outlive the
    // server object.  Serialize callers as well: embedded users may invoke
    // preparation from more than one bootstrap path, and two simultaneous
    // first calls must not race while installing the same directory tree.
    std::lock_guard lock(prepareMutex_);
    std::string failure;
    const bool useEnvironmentRoot = root.empty();
    if (useEnvironmentRoot) {
        root = selectRoot(failure);
    } else {
        std::error_code absoluteError;
        root = std::filesystem::absolute(root, absoluteError).lexically_normal();
        if (absoluteError || root.empty()) {
            failure = "could not resolve server directory: " + absoluteError.message();
            if (error) *error = std::move(failure);
            return false;
        }
    }
    if (root.empty()) {
        if (error) *error = std::move(failure);
        return false;
    }

    if (prepared_) {
        std::error_code sameRootError;
        const bool sameRoot = root == root_ ||
            (std::filesystem::equivalent(root_, root, sameRootError) &&
             !sameRootError);
        if (sameRoot) {
            root = root_;
            return true;
        }
        failure = "runtime layout is already active for server directory " +
                  root_.string();
        if (error) *error = std::move(failure);
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::create_directories(root, ec) && ec) {
        failure = "could not create server directory " + root.string() + ": " + ec.message();
        if (error) *error = std::move(failure);
        return false;
    }
    if (useEnvironmentRoot && !environment("CPPFM_SERVER_DIR").empty()) {
        std::filesystem::current_path(root, ec);
        if (ec) {
            failure = "could not switch to server directory " + root.string() +
                      ": " + ec.message();
            if (error) *error = std::move(failure);
            return false;
        }
    }

    // Acquire the process-lifetime lock before creating any of the remaining
    // server layout.  A local lock protects setup until all work succeeds;
    // on success it is moved into the process instance below.
    if (!ensureDirectory(root, ".cppfm", failure)) {
        if (error) *error = std::move(failure);
        return false;
    }
    auto processLock = std::make_unique<platform::FileLock>(
        root / ".cppfm" / "server.lock", failure);
    if (!processLock->locked()) {
        if (error) *error = std::move(failure);
        return false;
    }

    constexpr std::array<std::string_view, 17> directories = {
        "world", "world/region", "world/entities", "world/poi",
        "world/playerdata", "world/stats", "world/advancements",
        "world/datapacks", "world/DIM-1", "world/DIM1", "mods", "config",
        "libraries", "logs", "crash-reports", "resourcepacks",
        ".cppfm/jvm/compile-stubs"};
    for (const auto directory : directories) {
        if (!ensureDirectory(root, directory, failure)) {
            if (error) *error = std::move(failure);
            return false;
        }
    }

    const auto classes = root / ".cppfm" / "jvm" / "classes";
    const auto classesBackup = root / ".cppfm" / "jvm" / "classes.previous";
    // Recover the old tree if a process was terminated between the two
    // directory renames below.  The backup is executable-owned and is never
    // considered a class path by the runtime.
    if (!std::filesystem::exists(classes, ec) &&
        std::filesystem::is_directory(classesBackup, ec)) {
        ec.clear();
        std::filesystem::rename(classesBackup, classes, ec);
        if (ec) {
            failure = "could not recover embedded JVM classes " + classes.string() +
                      ": " + ec.message();
            if (error) *error = std::move(failure);
            return false;
        }
    }

    if (!ensureDirectory(root, ".cppfm/jvm", failure)) {
        if (error) *error = std::move(failure);
        return false;
    }

    std::filesystem::path classStage;
    if (embedded::kHasPack && embedded::kPackSize != 0) {
        classStage = root / ".cppfm" / "jvm" /
                     ("classes.staging" + temporarySuffix());
        ec.clear();
        if (!std::filesystem::create_directories(classStage, ec) && ec) {
            failure = "could not create embedded JVM staging directory " +
                      classStage.string() + ": " + ec.message();
            if (error) *error = std::move(failure);
            return false;
        }
    } else if (!ensureDirectory(root, ".cppfm/jvm/classes", failure)) {
        if (error) *error = std::move(failure);
        return false;
    }

    const auto extracted = embeddedResources(
        root, failure, classStage.empty() ? nullptr : &classStage);
    if (!failure.empty()) {
        if (!classStage.empty()) std::filesystem::remove_all(classStage, ec);
        if (error) *error = std::move(failure);
        return false;
    }
    if (!ensureDefaultProperties(root, failure)) {
        if (!classStage.empty()) std::filesystem::remove_all(classStage, ec);
        if (error) *error = std::move(failure);
        return false;
    }

    if (!classStage.empty()) {
        // Install the complete class tree only after every embedded resource
        // has been decoded successfully.  A crash can leave `classes.previous`;
        // the recovery path above restores it on the next launch.
        ec.clear();
        if (std::filesystem::exists(classes, ec)) {
            std::filesystem::remove_all(classesBackup, ec);
            if (ec) {
                failure = "could not remove old embedded JVM class backup " +
                          classesBackup.string() + ": " + ec.message();
                std::filesystem::remove_all(classStage, ec);
                if (error) *error = std::move(failure);
                return false;
            }
            std::filesystem::rename(classes, classesBackup, ec);
            if (ec) {
                failure = "could not stage old embedded JVM classes " +
                          classes.string() + ": " + ec.message();
                std::filesystem::remove_all(classStage, ec);
                if (error) *error = std::move(failure);
                return false;
            }
        }
        std::filesystem::rename(classStage, classes, ec);
        if (ec) {
            std::error_code restoreError;
            if (std::filesystem::exists(classesBackup, restoreError))
                std::filesystem::rename(classesBackup, classes, restoreError);
            std::filesystem::remove_all(classStage, restoreError);
            failure = "could not install embedded JVM classes " + classes.string() +
                      ": " + ec.message();
            if (error) *error = std::move(failure);
            return false;
        }
        std::filesystem::remove_all(classesBackup, ec);
        if (ec) {
            // The active tree is already valid; retain the backup for the
            // next locked prepare so cleanup failure cannot corrupt startup.
            std::fprintf(stderr,
                         "[cppfm] warning: could not remove old JVM classes backup %s: %s\n",
                         classesBackup.string().c_str(), ec.message().c_str());
        }
    }
    if (extracted > 0)
        std::fprintf(stderr, "[cppfm] embedded runtime resources ready: %llu bytes\n",
                     static_cast<unsigned long long>(extracted));
    root_ = root;
    serverLock_ = std::move(processLock);
    prepared_ = true;
    return true;
}

} // namespace cppfm
