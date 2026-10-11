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

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif
#endif

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

#ifdef _WIN32
bool regularNonSymlink(const std::filesystem::path& path,
                       std::error_code& error) {
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) return false;
    return std::filesystem::is_regular_file(status);
}
#endif

#ifndef _WIN32
class ScopedFileDescriptor {
public:
    ScopedFileDescriptor() noexcept = default;
    explicit ScopedFileDescriptor(int value) noexcept : value_(value) {}
    ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor(ScopedFileDescriptor&& other) noexcept
        : value_(other.release()) {}
    ScopedFileDescriptor& operator=(ScopedFileDescriptor&& other) noexcept {
        if (this == &other) return *this;
        reset(other.release());
        return *this;
    }
    ~ScopedFileDescriptor() { reset(); }

    explicit operator bool() const noexcept { return value_ >= 0; }
    int get() const noexcept { return value_; }
    int release() noexcept {
        const int result = value_;
        value_ = -1;
        return result;
    }
    void reset(int next = -1) noexcept {
        if (value_ >= 0) (void)::close(value_);
        value_ = next;
    }

private:
    int value_ = -1;
};

ScopedFileDescriptor openSafeDirectoryTree(
    const std::filesystem::path& root, std::string_view relative,
    std::string& error, bool createMissing, bool allowAssetRootSymlink,
    bool* followedAssetRootSymlink) {
    if (followedAssetRootSymlink) *followedAssetRootSymlink = false;
    const int rootFd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (rootFd < 0) {
        error = "runtime root is not an accessible directory: " +
                root.string() + ": " + std::strerror(errno);
        return {};
    }
    ScopedFileDescriptor current(rootFd);
    bool followedAssetLink = false;
    std::size_t begin = 0;
    while (begin < relative.size()) {
        const auto end = relative.find('/', begin);
        const std::string component(relative.substr(begin,
            end == std::string_view::npos
                ? relative.size() - begin : end - begin));
        struct stat status {};
        if (::fstatat(current.get(), component.c_str(), &status,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno != ENOENT || !createMissing) {
                error = "could not inspect runtime directory " +
                        (root / std::filesystem::path(std::string(relative)))
                            .string() + ": " + std::strerror(errno);
                return {};
            }
            if (followedAssetLink) {
                error = "refusing to create runtime directory through "
                        "symlinked assets root: " + component;
                return {};
            }
            if (::mkdirat(current.get(), component.c_str(), 0755) != 0 &&
                errno != EEXIST) {
                error = "could not create runtime directory " +
                        (root / std::filesystem::path(std::string(relative)))
                            .string() + ": " + std::strerror(errno);
                return {};
            }
            if (::fstatat(current.get(), component.c_str(), &status,
                          AT_SYMLINK_NOFOLLOW) != 0) {
                error = "could not inspect created runtime directory " +
                        component + ": " + std::strerror(errno);
                return {};
            }
        }

        const bool allowedAssetLink = allowAssetRootSymlink && begin == 0 &&
                                      component == "assets" &&
                                      S_ISLNK(status.st_mode);
        if (S_ISLNK(status.st_mode) && !allowedAssetLink) {
            error = "runtime directory must not be a symlink: " +
                    (root / std::filesystem::path(std::string(relative)))
                        .string();
            return {};
        }
        if (!S_ISDIR(status.st_mode) && !allowedAssetLink) {
            error = "runtime path component is not a directory: " +
                    (root / std::filesystem::path(std::string(relative)))
                        .string();
            return {};
        }

        int flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
        if (!allowedAssetLink) flags |= O_NOFOLLOW;
        const int childFd = ::openat(current.get(), component.c_str(), flags);
        if (childFd < 0) {
            error = "could not open runtime directory component " + component +
                    ": " + std::strerror(errno);
            return {};
        }
        struct stat openedStatus {};
        const int statResult = ::fstat(childFd, &openedStatus);
        if (statResult != 0 || !S_ISDIR(openedStatus.st_mode)) {
            const int statusError = statResult != 0 ? errno : ENOTDIR;
            (void)::close(childFd);
            error = "runtime directory component is not a directory: " +
                    component + ": " + std::strerror(statusError);
            return {};
        }
        current.reset(childFd);
        if (allowedAssetLink) {
            followedAssetLink = true;
            if (followedAssetRootSymlink)
                *followedAssetRootSymlink = true;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return current;
}

int renameWithoutReplaceAt(int directoryFd, const char* source,
                           const char* destination) {
#if defined(__linux__) && defined(SYS_renameat2)
    constexpr unsigned int kRenameNoReplace = 1u;
    return static_cast<int>(::syscall(SYS_renameat2, directoryFd, source,
                                      directoryFd, destination,
                                      kRenameNoReplace));
#elif defined(__APPLE__)
    return ::renameatx_np(directoryFd, source, directoryFd, destination,
                          RENAME_EXCL);
#else
    (void)directoryFd;
    (void)source;
    (void)destination;
    errno = ENOTSUP;
    return -1;
#endif
}

bool writeIfMissingPosix(const std::filesystem::path& root,
                         std::string_view name,
                         const std::vector<std::uint8_t>& bytes,
                         std::string& error,
                         const std::filesystem::path* anchorRoot = nullptr) {
    const auto separator = name.find_last_of('/');
    const auto fileParent = separator == std::string_view::npos
        ? std::string_view{} : name.substr(0, separator);
    const std::string filename(name.substr(separator == std::string_view::npos
        ? 0 : separator + 1));
    bool followedAssetLink = false;
    ScopedFileDescriptor directory;
    if (anchorRoot) {
        const auto relativeRoot = root.lexically_relative(*anchorRoot);
        if (relativeRoot.empty() || relativeRoot.is_absolute()) {
            error = "embedded resource root is not below its safe anchor: " +
                    root.string();
            return false;
        }
        auto relativeParent = relativeRoot.generic_string();
        if (!fileParent.empty()) {
            relativeParent.push_back('/');
            relativeParent.append(fileParent);
        }
        if (!safeResourcePath(relativeParent)) {
            error = "embedded resource root has an unsafe relative path: " +
                    relativeParent;
            return false;
        }
        directory = openSafeDirectoryTree(*anchorRoot, relativeParent, error,
                                          true, false, nullptr);
    } else {
        directory = openSafeDirectoryTree(root, fileParent, error, true,
            name.starts_with("assets/"), &followedAssetLink);
    }
    if (!directory) return false;

    struct stat existing {};
    if (::fstatat(directory.get(), filename.c_str(), &existing,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISREG(existing.st_mode)) return true;
        error = "embedded resource target is not a regular file: " +
                (root / std::filesystem::path(std::string(name))).string();
        return false;
    }
    if (errno != ENOENT) {
        error = "could not inspect embedded resource " +
                (root / std::filesystem::path(std::string(name))).string() +
                ": " + std::strerror(errno);
        return false;
    }
    if (followedAssetLink) {
        error = "refusing to create embedded resource through symlinked "
                "assets root: " +
                (root / std::filesystem::path(std::string(name))).string();
        return false;
    }

    std::string temporaryName;
    ScopedFileDescriptor temporary;
    for (int attempt = 0; attempt < 8; ++attempt) {
        temporaryName = filename + temporarySuffix() + "-" +
                        std::to_string(attempt);
        const int fd = ::openat(directory.get(), temporaryName.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            temporary.reset(fd);
            break;
        }
        if (errno != EEXIST) {
            error = "could not create temporary embedded resource " +
                    temporaryName + ": " + std::strerror(errno);
            return false;
        }
    }
    if (!temporary) {
        error = "could not allocate a unique temporary embedded resource name";
        return false;
    }
    const auto removeTemporary = [&]() {
        if (::unlinkat(directory.get(), temporaryName.c_str(), 0) == 0 ||
            errno == ENOENT)
            return std::string{};
        return std::string("; cleanup also failed: ") + std::strerror(errno);
    };
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto written = ::write(temporary.get(), bytes.data() + offset,
                                     remaining);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            const std::string failure =
                "could not write temporary embedded resource " +
                temporaryName + ": " +
                std::strerror(written < 0 ? errno : EIO);
            error = failure + removeTemporary();
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::fsync(temporary.get()) != 0) {
        const std::string failure =
            "could not finish temporary embedded resource " +
            temporaryName + ": " + std::strerror(errno);
        error = failure + removeTemporary();
        return false;
    }
    temporary.reset();

    if (::linkat(directory.get(), temporaryName.c_str(), directory.get(),
                 filename.c_str(), 0) == 0) {
        if (::unlinkat(directory.get(), temporaryName.c_str(), 0) != 0) {
            error = "could not remove temporary embedded resource " +
                    temporaryName + ": " + std::strerror(errno);
            return false;
        }
        return true;
    }

    const int linkError = errno;
    int renameError = 0;
    if (linkError != EEXIST) {
        // Filesystems without hard-link support can still provide an atomic
        // exclusive rename (renameat2 on Linux, renameatx_np on Apple).
        if (renameWithoutReplaceAt(directory.get(), temporaryName.c_str(),
                                   filename.c_str()) == 0)
            return true;
        renameError = errno;
    }
    if ((linkError == EEXIST || renameError == EEXIST) &&
        ::fstatat(directory.get(), filename.c_str(), &existing,
                  AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(existing.st_mode)) {
        const auto cleanupError = removeTemporary();
        if (!cleanupError.empty()) {
            error = "could not accept concurrently installed embedded "
                    "resource " + filename + cleanupError;
            return false;
        }
        return true;
    }
    error = "could not install embedded resource " + filename +
            ": linkat failed: " + std::strerror(linkError);
    if (renameError != 0)
        error += "; exclusive rename failed: " +
                 std::string(std::strerror(renameError));
    error += removeTemporary();
    return false;
}
#endif

// Install a completed temporary file without ever replacing a file that may
// have appeared after the initial status check.
#ifdef _WIN32
bool installWithoutReplace(const std::filesystem::path& temporary,
                           const std::filesystem::path& destination,
                           std::error_code& error) {
    // The temporary and final names share a parent and volume. Omitting
    // MOVEFILE_REPLACE_EXISTING gives an atomic no-clobber rename without a
    // copy fallback that might leave a partial final file after a crash.
    if (::MoveFileExW(temporary.wstring().c_str(),
                      destination.wstring().c_str(),
                      MOVEFILE_WRITE_THROUGH)) {
        error.clear();
        return true;
    }
    const auto nativeError = static_cast<unsigned long>(::GetLastError());
    if (nativeError == ERROR_FILE_EXISTS || nativeError == ERROR_ALREADY_EXISTS)
        error = std::make_error_code(std::errc::file_exists);
    else
        error = std::error_code(static_cast<int>(nativeError),
                                std::system_category());
    return false;
}
#endif

bool ensureSafeDirectoryTree(const std::filesystem::path& root,
                             std::string_view relative,
                             std::string& error,
                             bool allowAssetRootSymlink = false,
                             bool* followedAssetRootSymlink = nullptr) {
    namespace fs = std::filesystem;
    if (followedAssetRootSymlink) *followedAssetRootSymlink = false;
    if (relative.empty()) return true;
    if (!safeResourcePath(relative)) {
        error = "runtime directory has an unsafe relative path: " +
                std::string(relative);
        return false;
    }
#ifndef _WIN32
    auto directory = openSafeDirectoryTree(root, relative, error, true,
        allowAssetRootSymlink, followedAssetRootSymlink);
    return static_cast<bool>(directory);
#else
    std::error_code ec;
    const auto rootStatus = fs::status(root, ec);
    if (ec || !fs::is_directory(rootStatus)) {
        error = "runtime root is not an accessible directory: " +
                root.string() + (ec ? ": " + ec.message() : std::string());
        return false;
    }

    fs::path current = root;
    bool followedAssetLink = false;
    std::size_t begin = 0;
    while (begin < relative.size()) {
        const auto end = relative.find('/', begin);
        current /= std::string(relative.substr(
            begin, end == std::string_view::npos
                ? relative.size() - begin : end - begin));
        ec.clear();
        auto status = fs::symlink_status(current, ec);
        if (ec == std::make_error_code(std::errc::no_such_file_or_directory) ||
            (!ec && status.type() == fs::file_type::not_found)) {
            if (followedAssetLink) {
                error = "refusing to create runtime directory through "
                        "symlinked assets root: " + current.string();
                return false;
            }
            ec.clear();
            fs::create_directory(current, ec);
            if (ec && ec != std::make_error_code(std::errc::file_exists)) {
                error = "could not create runtime directory " +
                        current.string() + ": " + ec.message();
                return false;
            }
            ec.clear();
            status = fs::symlink_status(current, ec);
        }
        if (ec) {
            error = "could not inspect runtime directory " + current.string() +
                    ": " + ec.message();
            return false;
        }
        if (fs::is_symlink(status)) {
            if (allowAssetRootSymlink && current == root / "assets") {
                const auto targetStatus = fs::status(current, ec);
                if (ec || !fs::is_directory(targetStatus)) {
                    error = "symlinked assets root is not an accessible "
                            "directory: " + current.string() +
                            (ec ? ": " + ec.message() : std::string());
                    return false;
                }
                followedAssetLink = true;
                if (followedAssetRootSymlink)
                    *followedAssetRootSymlink = true;
            } else {
                error = "runtime directory must not be a symlink: " +
                        current.string();
                return false;
            }
        }
        if (!fs::is_symlink(status) && !fs::is_directory(status)) {
            error = "runtime path component is not a directory: " +
                    current.string();
            return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
#endif
}

bool writeIfMissing(const std::filesystem::path& root, std::string_view name,
                   const std::vector<std::uint8_t>& bytes, std::string& error,
                   const std::filesystem::path* anchorRoot = nullptr) {
    if (!safeResourcePath(name)) {
        error = "embedded resource has an unsafe path: " + std::string(name);
        return false;
    }
#ifndef _WIN32
    return writeIfMissingPosix(root, name, bytes, error, anchorRoot);
#else
    const auto destination = root / std::filesystem::path(std::string(name));
    const auto separator = name.find_last_of('/');
    bool assetRootIsSymlink = false;
    if (separator != std::string_view::npos &&
        !ensureSafeDirectoryTree(root, name.substr(0, separator), error,
                                 name.starts_with("assets/"),
                                 &assetRootIsSymlink)) return false;
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
    if (assetRootIsSymlink) {
        error = "refusing to create embedded resource through symlinked "
                "assets root: " + destination.string();
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
#endif
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
        // zlib's length types are not guaranteed to be 64-bit on every
        // supported platform.  Validate before narrowing the untrusted pack
        // metadata; otherwise a large compressedSize could wrap and make
        // uncompress read a different amount than the bounds check allowed.
        if (compressedSize > static_cast<std::uint64_t>(
                std::numeric_limits<uLong>::max()) ||
            rawSize > static_cast<std::uint64_t>(
                std::numeric_limits<uLongf>::max())) {
            error = "embedded resource entry is too large for zlib: " + name;
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
        if (!writeIfMissing(destinationRoot, destinationName, bytes, error,
                            isClass ? &root : nullptr))
            return 0;
        extracted += rawSize;
    }
    return extracted;
}

bool ensureDirectory(const std::filesystem::path& root, std::string_view relative,
                     std::string& error) {
    return ensureSafeDirectoryTree(root, relative, error);
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
        "view-distance=10\n"
        "entity-broadcast-range-percentage=100\n"
        "simulation-distance=10\n"
        "level-type=minecraft:normal\n"
        "gamemode=survival\n"
        "force-gamemode=false\n"
        "difficulty=easy\n"
        "motd=A Minecraft Server\n"
        "level-seed=\n"
        "spawn-protection=16\n"
        "online-mode=true\n"
        "white-list=false\n"
        "pvp=true\n"
        "allow-flight=false\n"
        "hardcore=false\n"
        "enforce-secure-profile=true\n"
        "network-compression-threshold=256\n"
        "enable-rcon=false\n"
        "rcon.port=25575\n"
        "rcon.password=\n"
        "resource-pack=\n"
        "resource-pack-sha1=\n"
        "require-resource-pack=false\n"
        "jvm=true\n"
        "jvm-strict=false\n";
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
#ifdef _WIN32
    auto processLock = std::make_unique<platform::FileLock>(
        root / ".cppfm" / "server.lock", failure);
#else
    auto cppfmDirectory = openSafeDirectoryTree(
        root, ".cppfm", failure, false, false, nullptr);
    if (!cppfmDirectory) {
        if (error) *error = std::move(failure);
        return false;
    }
    auto processLock = std::make_unique<platform::FileLock>();
    processLock->acquireAt(cppfmDirectory.get(), "server.lock",
                           root / ".cppfm" / "server.lock", failure);
#endif
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
        const auto stageRelative = std::string(".cppfm/jvm/classes.staging") +
                                   temporarySuffix();
        classStage = root / stageRelative;
        auto stageDirectory = openSafeDirectoryTree(
            root, stageRelative, failure, true, false, nullptr);
        if (!stageDirectory) {
            failure = "could not create embedded JVM staging directory " +
                      classStage.string() + ": " + failure;
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
