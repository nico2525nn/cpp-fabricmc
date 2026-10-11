#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cppfm::datapack_archive {

namespace fs = std::filesystem;

// Limits are deliberately applied before allocation or decompression. Datapacks
// are administrator-installed input, but a malformed ZIP must not turn /reload
// into an unbounded memory or disk operation.
inline constexpr std::size_t kMaxEntries = 100'000;
inline constexpr std::size_t kMaxWorkspaceEntries = kMaxEntries;
inline constexpr std::uint64_t kMaxCentralDirectoryBytes = 64ULL * 1024 * 1024;
inline constexpr std::uint64_t kMaxEntryBytes = 64ULL * 1024 * 1024;
inline constexpr std::uint64_t kMaxArchiveBytes = 512ULL * 1024 * 1024;
inline constexpr std::uint64_t kMaxWorkspaceBytes = 1024ULL * 1024 * 1024;

#ifdef _WIN32
inline std::string windowsUnicodeCollisionKey(std::string_view utf8) {
    if (utf8.empty() || utf8.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error(
            "unable to build Windows Unicode collision key for datapack ZIP path");

    const auto utf16Length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (utf16Length <= 0)
        throw std::runtime_error(
            "unable to decode Windows Unicode datapack ZIP collision key");
    std::wstring utf16(static_cast<std::size_t>(utf16Length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
            static_cast<int>(utf8.size()), utf16.data(), utf16Length) !=
        utf16Length)
        throw std::runtime_error(
            "unable to decode Windows Unicode datapack ZIP collision key");

    const auto foldedLength = LCMapStringEx(
        LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, utf16.data(), utf16Length,
        nullptr, 0, nullptr, nullptr, 0);
    if (foldedLength <= 0)
        throw std::runtime_error(
            "unable to lowercase Windows Unicode datapack ZIP collision key");
    std::wstring folded(static_cast<std::size_t>(foldedLength), L'\0');
    if (LCMapStringEx(
            LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, utf16.data(), utf16Length,
            folded.data(), foldedLength, nullptr, nullptr, 0) != foldedLength)
        throw std::runtime_error(
            "unable to lowercase Windows Unicode datapack ZIP collision key");

    const auto utf8Length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, folded.data(), foldedLength,
        nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0)
        throw std::runtime_error(
            "unable to encode Windows Unicode datapack ZIP collision key");
    std::string result(static_cast<std::size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, folded.data(), foldedLength,
            result.data(), utf8Length, nullptr, nullptr) != utf8Length)
        throw std::runtime_error(
            "unable to encode Windows Unicode datapack ZIP collision key");
    return result;
}
#endif

inline bool isValidUtf8(std::string_view text) noexcept {
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto first = static_cast<unsigned char>(text[cursor]);
        if (first <= 0x7fU) {
            ++cursor;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        if (first >= 0xc2U && first <= 0xdfU) {
            length = 2;
            codePoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            length = 3;
            codePoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            length = 4;
            codePoint = first & 0x07U;
        } else {
            return false;
        }
        if (length > text.size() - cursor) return false;
        for (std::size_t i = 1; i < length; ++i) {
            const auto continuation =
                static_cast<unsigned char>(text[cursor + i]);
            if ((continuation & 0xc0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (continuation & 0x3fU);
        }
        if ((length == 2 && codePoint < 0x80U) ||
            (length == 3 && codePoint < 0x800U) ||
            (length == 4 && codePoint < 0x10000U) ||
            codePoint > 0x10ffffU ||
            (codePoint >= 0xd800U && codePoint <= 0xdfffU))
            return false;
        cursor += length;
    }
    return true;
}

inline void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
}

inline std::string decodeZipPath(std::string_view rawName,
                                std::uint16_t flags) {
    constexpr std::uint16_t kUtf8NameFlag = 0x0800U;
    if ((flags & kUtf8NameFlag) != 0) {
        if (!isValidUtf8(rawName))
            throw std::runtime_error("invalid UTF-8 path in datapack ZIP");
        return std::string(rawName);
    }

    // ZIP names without general-purpose bit 11 use the historical CP437
    // encoding. Convert it to UTF-8 before path validation and filesystem I/O.
    static constexpr std::array<std::uint16_t, 128> kCp437 = {
        0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,
        0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
        0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,
        0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
        0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,
        0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
        0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
        0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
        0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,
        0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
        0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,
        0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
        0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,
        0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
        0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,
        0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0,
    };
    std::string decoded;
    decoded.reserve(rawName.size());
    for (const unsigned char byte : rawName) {
        if (byte < 0x80U) decoded.push_back(static_cast<char>(byte));
        else appendUtf8(decoded, kCp437[byte - 0x80U]);
    }
    return decoded;
}

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    std::u8string nativeInput;
    nativeInput.reserve(utf8.size());
    for (const unsigned char byte : utf8)
        nativeInput.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(std::move(nativeInput));
}

inline std::uint16_t readU16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8U);
}

inline std::uint32_t readU32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

inline bool hasWindowsDeviceName(std::string_view component) {
    auto baseLength = component.find('.');
    if (baseLength == std::string_view::npos) baseLength = component.size();
    while (baseLength > 0 &&
           (component[baseLength - 1] == '.' || component[baseLength - 1] == ' '))
        --baseLength;
    const auto base = component.substr(0, baseLength);
    std::string folded;
    folded.reserve(base.size());
    for (const unsigned char byte : base) {
        folded.push_back(byte >= 'A' && byte <= 'Z'
            ? static_cast<char>(byte - 'A' + 'a')
            : static_cast<char>(byte));
    }
    if (folded == "con" || folded == "prn" || folded == "aux" ||
        folded == "nul" || folded == "conin$" || folded == "conout$")
        return true;
    if (folded.size() < 4 ||
        (!folded.starts_with("com") && !folded.starts_with("lpt")))
        return false;
    if (folded[3] >= '1' && folded[3] <= '9')
        return folded.size() == 4;
    if (folded.size() == 4 &&
        (folded[3] == static_cast<char>(0xb2) ||
         folded[3] == static_cast<char>(0xb3) ||
         folded[3] == static_cast<char>(0xb9)))
        return true;
    return folded.size() == 5 &&
        static_cast<unsigned char>(folded[3]) == 0xc2U &&
        (static_cast<unsigned char>(folded[4]) == 0xb2U ||
         static_cast<unsigned char>(folded[4]) == 0xb3U ||
         static_cast<unsigned char>(folded[4]) == 0xb9U);
}

inline bool isWindowsUnsafeComponent(std::string_view component) {
    if (component.empty() || component.back() == '.' || component.back() == ' ' ||
        hasWindowsDeviceName(component))
        return true;
    for (const unsigned char byte : component) {
        if (byte < 0x20U || byte == 0x7fU || byte == '<' || byte == '>' ||
            byte == ':' || byte == '"' || byte == '|' || byte == '?' ||
            byte == '*')
            return true;
    }
    return false;
}

inline std::string checkedRelativePath(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    if (value.empty() || value.front() == '/' ||
        value.find('\0') != std::string::npos)
        throw std::runtime_error("unsafe path in datapack ZIP");

    const bool directory = value.back() == '/';
    if (directory) value.pop_back();
    std::string normalized;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto length = (end == std::string::npos ? value.size() : end) - begin;
        const std::string_view component(value.data() + begin, length);
        if (component.empty() || component == "." || component == ".." ||
            isWindowsUnsafeComponent(component))
            throw std::runtime_error("unsafe path in datapack ZIP");
        if (!normalized.empty()) normalized.push_back('/');
        normalized.append(component);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (normalized.empty())
        throw std::runtime_error("empty path in datapack ZIP");
    if (directory) normalized.push_back('/');
    return normalized;
}

#if !defined(_WIN32)
namespace detail {

class FileDescriptor final {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept
        : value_(std::exchange(other.value_, -1)) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this == &other) return *this;
        if (value_ >= 0) (void)::close(value_);
        value_ = std::exchange(other.value_, -1);
        return *this;
    }
    ~FileDescriptor() {
        if (value_ >= 0) (void)::close(value_);
    }

    int get() const noexcept { return value_; }
    int release() noexcept { return std::exchange(value_, -1); }

private:
    int value_;
};

[[noreturn]] inline void throwPosixError(const char* operation,
                                         const fs::path& path,
                                         int error = errno) {
    throw fs::filesystem_error(operation, path,
        std::error_code(error, std::generic_category()));
}

inline FileDescriptor openDirectory(const fs::path& path) {
    const int descriptor = ::open(path.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) throwPosixError("open datapack directory", path);
    return FileDescriptor(descriptor);
}

// A private directory may safely be made writable by its owner after mkdirat:
// its initial mode is already a subset of 0700, and a shared parent is accepted
// only when the sticky bit prevents another uid from replacing the new entry.
inline void requireSafeParent(int descriptor, const fs::path& path) {
    struct stat status {};
    if (::fstat(descriptor, &status) != 0)
        throwPosixError("inspect datapack parent directory", path);
    if (!S_ISDIR(status.st_mode) ||
        ((status.st_mode & (S_IWGRP | S_IWOTH)) != 0 &&
         (status.st_mode & S_ISVTX) == 0))
        throw fs::filesystem_error("unsafe shared datapack parent directory",
            path, std::make_error_code(std::errc::permission_denied));
}

inline void removeEmptyDirectoryAt(int parent, const std::string& name) noexcept {
    (void)::unlinkat(parent, name.c_str(), AT_REMOVEDIR);
}

inline FileDescriptor createPrivateDirectoryAt(
    int parent, const std::string& name, const fs::path& displayPath) {
    if (::mkdirat(parent, name.c_str(), 0700) != 0)
        throwPosixError("create private datapack directory", displayPath);

    try {
        // mkdirat's mode is constrained by umask. Restore 0700 while the
        // entry is still empty, then open it with O_NOFOLLOW and pin its inode.
        if (::fchmodat(parent, name.c_str(), 0700, 0) != 0)
            throwPosixError("protect datapack directory", displayPath);
        const int descriptor = ::openat(parent, name.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor < 0)
            throwPosixError("open private datapack directory", displayPath);
        FileDescriptor result(descriptor);
        if (::fchmod(descriptor, 0700) != 0)
            throwPosixError("set datapack directory permissions", displayPath);
        struct stat status {};
        if (::fstat(descriptor, &status) != 0)
            throwPosixError("verify datapack directory permissions", displayPath);
        if (!S_ISDIR(status.st_mode) || (status.st_mode & 0777) != 0700)
            throw fs::filesystem_error("datapack directory is not owner-only",
                displayPath, std::make_error_code(std::errc::permission_denied));
        return result;
    } catch (...) {
        removeEmptyDirectoryAt(parent, name);
        throw;
    }
}

inline FileDescriptor duplicateDirectory(int descriptor,
                                         const fs::path& displayPath) {
    const int duplicate = ::openat(descriptor, ".",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (duplicate < 0)
        throwPosixError("duplicate datapack directory handle", displayPath);
    return FileDescriptor(duplicate);
}

inline std::vector<std::string> pathComponents(std::string_view relative) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin < relative.size()) {
        const auto end = relative.find('/', begin);
        const auto length = (end == std::string_view::npos
            ? relative.size() : end) - begin;
        if (length == 0)
            throw std::runtime_error("empty component in datapack ZIP path");
        result.emplace_back(relative.substr(begin, length));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return result;
}

inline FileDescriptor ensureDirectoryTreeAt(
    int root, std::string_view relative, const fs::path& displayPath) {
    auto current = duplicateDirectory(root, displayPath);
    for (const auto& component : pathComponents(relative)) {
        bool created = false;
        if (::mkdirat(current.get(), component.c_str(), 0700) == 0) {
            created = true;
        } else if (errno != EEXIST) {
            throwPosixError("create datapack directory", displayPath);
        }
        if (created &&
            ::fchmodat(current.get(), component.c_str(), 0700, 0) != 0) {
            const int savedError = errno;
            removeEmptyDirectoryAt(current.get(), component);
            throwPosixError("protect datapack directory", displayPath,
                            savedError);
        }
        const int descriptor = ::openat(current.get(), component.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor < 0)
            throwPosixError("open datapack directory without following links",
                            displayPath);
        FileDescriptor next(descriptor);
        if (::fchmod(descriptor, 0700) != 0)
            throwPosixError("set datapack directory permissions", displayPath);
        current = std::move(next);
    }
    return current;
}

inline void writePrivateFileAt(int root, std::string_view relative,
                               const std::vector<std::uint8_t>& bytes,
                               const fs::path& displayPath) {
    const auto slash = relative.rfind('/');
    const auto leaf = std::string(slash == std::string_view::npos
        ? relative : relative.substr(slash + 1));
    if (leaf.empty())
        throw std::runtime_error("empty file name in datapack ZIP path");
    auto parent = slash == std::string_view::npos
        ? duplicateDirectory(root, displayPath)
        : ensureDirectoryTreeAt(root, relative.substr(0, slash), displayPath);
    const int descriptor = ::openat(parent.get(), leaf.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
        throwPosixError("create private extracted datapack file", displayPath);
    FileDescriptor output(descriptor);
    if (::fchmod(descriptor, 0600) != 0)
        throwPosixError("set extracted datapack file permissions", displayPath);

    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto amount = ::write(descriptor, bytes.data() + written,
                                    bytes.size() - written);
        if (amount < 0) {
            if (errno == EINTR) continue;
            throwPosixError("write extracted datapack file", displayPath);
        }
        if (amount == 0)
            throw fs::filesystem_error("short write of extracted datapack file",
                displayPath, std::make_error_code(std::errc::io_error));
        written += static_cast<std::size_t>(amount);
    }
    const int completedDescriptor = output.release();
    if (::close(completedDescriptor) != 0)
        throwPosixError("close extracted datapack file", displayPath);
}

} // namespace detail
#endif

class ExtractionCleanup final {
public:
    explicit ExtractionCleanup(fs::path root) : root_(std::move(root)) {}
    ExtractionCleanup(const ExtractionCleanup&) = delete;
    ExtractionCleanup& operator=(const ExtractionCleanup&) = delete;
    ~ExtractionCleanup() {
        if (!armed_) return;
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }
    void arm() noexcept { armed_ = true; }
    void release() noexcept { armed_ = false; }

private:
    fs::path root_;
    bool armed_ = false;
};

class Archive final {
public:
    explicit Archive(fs::path path) : path_(std::move(path)) {
        parseCentralDirectory();
    }

    bool hasDataRoot() const {
        return std::any_of(entries_.begin(), entries_.end(), [](const Entry& entry) {
            return entry.path == "data/" || entry.path.starts_with("data/");
        });
    }

    std::uint64_t declaredUncompressedBytes() const {
        std::uint64_t total = 0;
        for (const auto& entry : entries_) {
            if (entry.directory) continue;
            if (entry.uncompressedSize > kMaxArchiveBytes - total)
                throw std::runtime_error(
                    "datapack ZIP exceeds extraction limits");
            total += entry.uncompressedSize;
        }
        return total;
    }

    std::size_t entryCount() const noexcept { return entries_.size(); }

    void extractTo(const fs::path& destination) const {
        const fs::path extractionRoot = destination.lexically_normal();
        auto parentPath = extractionRoot.parent_path();
        if (parentPath.empty()) parentPath = ".";
        std::error_code error;
        std::ifstream input(path_, std::ios::binary);
        if (!input)
            throw fs::filesystem_error("open datapack ZIP", path_,
                std::make_error_code(std::errc::io_error));

        if (!fs::create_directories(parentPath, error) && error)
            throw fs::filesystem_error("create datapack extraction parent",
                                       parentPath, error);
        ExtractionCleanup cleanup(extractionRoot);
#if !defined(_WIN32)
        const auto rootName = extractionRoot.filename().string();
        if (rootName.empty() || rootName == "." || rootName == ".." ||
            rootName.find('/') != std::string::npos)
            throw std::runtime_error("invalid datapack extraction root path");
        auto parent = detail::openDirectory(parentPath);
        detail::requireSafeParent(parent.get(), parentPath);
        auto root = detail::createPrivateDirectoryAt(
            parent.get(), rootName, extractionRoot);
#else
        // SECURITY BOUNDARY: std::filesystem does not provide portable DACL
        // creation or handle-relative no-reparse traversal. Windows inherits
        // the parent ACL here; a current-user-only ACL and the POSIX no-follow
        // race guarantees are not claimed. Both require Win32 security/handle
        // APIs and are outside this portable implementation.
        if (extractionRoot.filename().empty() ||
            extractionRoot == extractionRoot.root_path())
            throw std::runtime_error("invalid datapack extraction root path");
        if (!fs::create_directory(extractionRoot, error)) {
            if (!error) error = std::make_error_code(std::errc::file_exists);
            throw fs::filesystem_error("create datapack extraction root",
                                       extractionRoot, error);
        }
#endif
        cleanup.arm();

        std::uint64_t totalOutput = 0;
        std::uint64_t totalCompressed = 0;
        for (const auto& entry : entries_) {
            const fs::path relative = pathFromUtf8(entry.path);
            const fs::path outputPath = extractionRoot / relative;
            const auto resolved = outputPath.lexically_normal();
            const auto relativeToRoot = resolved.lexically_relative(extractionRoot);
            if (relativeToRoot.empty() || relativeToRoot.is_absolute() ||
                *relativeToRoot.begin() == "..")
                throw std::runtime_error("datapack ZIP path escapes extraction root");

            if (entry.directory) {
                if (entry.uncompressedSize != 0 || entry.compressedSize != 0)
                    throw std::runtime_error("non-empty directory in datapack ZIP");
#if !defined(_WIN32)
                auto directoryPath = std::string_view(entry.path);
                if (!directoryPath.empty() && directoryPath.back() == '/')
                    directoryPath.remove_suffix(1);
                (void)detail::ensureDirectoryTreeAt(
                    root.get(), directoryPath, resolved);
#else
                if (!fs::create_directories(resolved, error) && error)
                    throw fs::filesystem_error("create datapack directory",
                                               resolved, error);
                error.clear();
#endif
                continue;
            }

            if (entry.uncompressedSize > kMaxEntryBytes ||
                entry.compressedSize > kMaxEntryBytes ||
                totalOutput > kMaxArchiveBytes - entry.uncompressedSize ||
                totalCompressed > kMaxArchiveBytes - entry.compressedSize)
                throw std::runtime_error("datapack ZIP exceeds extraction limits");
            totalOutput += entry.uncompressedSize;
            totalCompressed += entry.compressedSize;
            auto bytes = readEntry(input, entry);
#if !defined(_WIN32)
            detail::writePrivateFileAt(root.get(), entry.path, bytes, resolved);
#else
            if (!fs::create_directories(resolved.parent_path(), error) && error)
                throw fs::filesystem_error("create datapack parent directory",
                                           resolved.parent_path(), error);
            error.clear();
            std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
            if (!output)
                throw fs::filesystem_error("create extracted datapack file",
                    resolved, std::make_error_code(std::errc::io_error));
            if (!bytes.empty())
                output.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            if (!output)
                throw fs::filesystem_error("write extracted datapack file",
                    resolved, std::make_error_code(std::errc::io_error));
#endif
        }
        cleanup.release();
    }

private:
    struct Entry {
        std::string path;
        std::uint16_t flags = 0;
        std::uint16_t method = 0;
        std::uint32_t crc = 0;
        std::uint32_t compressedSize = 0;
        std::uint32_t uncompressedSize = 0;
        std::uint32_t localHeaderOffset = 0;
        bool directory = false;
    };

    static bool readAt(std::ifstream& input, std::uint64_t offset,
                       void* output, std::size_t size) {
        if (offset > static_cast<std::uint64_t>(
                         std::numeric_limits<std::streamoff>::max()) ||
            size > static_cast<std::size_t>(
                       std::numeric_limits<std::streamsize>::max()))
            return false;
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset));
        if (!input) return false;
        input.read(static_cast<char*>(output),
                   static_cast<std::streamsize>(size));
        return input.gcount() == static_cast<std::streamsize>(size);
    }

    void parseCentralDirectory() {
        std::ifstream input(path_, std::ios::binary | std::ios::ate);
        if (!input)
            throw fs::filesystem_error("open datapack ZIP", path_,
                std::make_error_code(std::errc::io_error));
        const auto end = input.tellg();
        if (end < 22)
            throw std::runtime_error("truncated datapack ZIP");
        const auto fileSize = static_cast<std::uint64_t>(end);
        const auto tailSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(fileSize, 22ULL + 65'535ULL));
        std::vector<std::uint8_t> tail(tailSize);
        if (!readAt(input, fileSize - tailSize, tail.data(), tail.size()))
            throw std::runtime_error("unable to read datapack ZIP directory");

        std::optional<std::size_t> endRecord;
        for (std::size_t cursor = tail.size() - 22;; --cursor) {
            if (readU32(tail.data() + cursor) == 0x06054b50U &&
                cursor + 22 + readU16(tail.data() + cursor + 20) == tail.size()) {
                endRecord = cursor;
                break;
            }
            if (cursor == 0) break;
        }
        if (!endRecord)
            throw std::runtime_error("datapack ZIP has no end-of-directory record");
        const auto* endBytes = tail.data() + *endRecord;
        if (readU16(endBytes + 4) != 0 || readU16(endBytes + 6) != 0 ||
            readU16(endBytes + 8) != readU16(endBytes + 10))
            throw std::runtime_error("multi-disk datapack ZIP is unsupported");
        const auto entryCount = readU16(endBytes + 10);
        const auto directorySize = readU32(endBytes + 12);
        const auto directoryOffset = readU32(endBytes + 16);
        if (entryCount == 0xffffU || directorySize == 0xffffffffU ||
            directoryOffset == 0xffffffffU)
            throw std::runtime_error("ZIP64 datapacks are unsupported");
        if (entryCount > kMaxEntries || directorySize > kMaxCentralDirectoryBytes)
            throw std::runtime_error("datapack ZIP directory exceeds limits");
        const std::uint64_t endOffset = fileSize - tailSize + *endRecord;
        if (directoryOffset > endOffset ||
            directorySize > endOffset - directoryOffset)
            throw std::runtime_error("invalid datapack ZIP directory bounds");
        centralDirectoryOffset_ = directoryOffset;

        std::vector<std::uint8_t> directory(directorySize);
        if (!readAt(input, directoryOffset, directory.data(), directory.size()))
            throw std::runtime_error("unable to read datapack ZIP directory");
        std::size_t cursor = 0;
        std::set<std::string> names;
        entries_.reserve(entryCount);
        for (std::size_t index = 0; index < entryCount; ++index) {
            constexpr std::size_t kCentralFixedBytes = 46;
            if (cursor > directory.size() ||
                directory.size() - cursor < kCentralFixedBytes ||
                readU32(directory.data() + cursor) != 0x02014b50U)
                throw std::runtime_error("invalid datapack ZIP entry directory");
            const auto* record = directory.data() + cursor;
            const auto madeBy = readU16(record + 4);
            const auto flags = readU16(record + 8);
            const auto method = readU16(record + 10);
            const auto crc = readU32(record + 16);
            const auto compressed = readU32(record + 20);
            const auto uncompressed = readU32(record + 24);
            const auto nameLength = readU16(record + 28);
            const auto extraLength = readU16(record + 30);
            const auto commentLength = readU16(record + 32);
            const auto diskStart = readU16(record + 34);
            const auto externalAttributes = readU32(record + 38);
            const auto localOffset = readU32(record + 42);
            const std::size_t variableBytes =
                static_cast<std::size_t>(nameLength) + extraLength + commentLength;
            if (variableBytes > directory.size() - cursor - kCentralFixedBytes)
                throw std::runtime_error("truncated datapack ZIP entry");
            if (diskStart != 0 || compressed == 0xffffffffU ||
                uncompressed == 0xffffffffU || localOffset == 0xffffffffU)
                throw std::runtime_error("ZIP64 or multi-disk entry is unsupported");
            if ((flags & (0x0001U | 0x0040U | 0x2000U)) != 0)
                throw std::runtime_error("encrypted datapack ZIP entries are unsupported");
            if (method != 0 && method != 8)
                throw std::runtime_error("unsupported datapack ZIP compression method");

            std::string rawName(
                reinterpret_cast<const char*>(record + kCentralFixedBytes),
                nameLength);
            const bool directoryEntry = !rawName.empty() &&
                                        (rawName.back() == '/' ||
                                         rawName.back() == '\\');
            // Unix-created symbolic links and special files must never become
            // filesystem objects in the extraction tree.
            const auto creatorSystem = madeBy >> 8U;
            const auto unixType = (externalAttributes >> 16U) & 0170000U;
            if (creatorSystem == 3 && unixType != 0 && unixType != 0100000U &&
                unixType != 0040000U)
                throw std::runtime_error("special file in datapack ZIP");

            Entry entry;
            entry.flags = flags;
            entry.method = method;
            entry.crc = crc;
            entry.compressedSize = compressed;
            entry.uncompressedSize = uncompressed;
            entry.localHeaderOffset = localOffset;
            entry.directory = directoryEntry ||
                              (externalAttributes & 0x10U) != 0 ||
                              unixType == 0040000U;
            entry.path = checkedRelativePath(decodeZipPath(rawName, flags));
            if (entry.directory && entry.path.back() != '/')
                entry.path.push_back('/');
            if (!names.insert(entry.path).second)
                throw std::runtime_error("duplicate path in datapack ZIP");
            std::string portableName = entry.path;
            if (entry.directory) portableName.pop_back();
            for (auto& byte : portableName) {
                const auto value = static_cast<unsigned char>(byte);
                if (value >= 'A' && value <= 'Z')
                    byte = static_cast<char>(value - 'A' + 'a');
            }
#ifdef _WIN32
            portableName = windowsUnicodeCollisionKey(portableName);
#endif
            if (!portableNames.insert(std::move(portableName)).second)
                throw std::runtime_error(
                    "case-insensitive duplicate path in datapack ZIP");
            entries_.push_back(std::move(entry));
            cursor += kCentralFixedBytes + variableBytes;
        }
        if (cursor != directory.size())
            throw std::runtime_error("unexpected data in datapack ZIP directory");
    }

    std::vector<std::uint8_t> readEntry(std::ifstream& input,
                                        const Entry& entry) const {
        std::array<std::uint8_t, 30> local{};
        if (!readAt(input, entry.localHeaderOffset, local.data(), local.size()) ||
            readU32(local.data()) != 0x04034b50U)
            throw std::runtime_error("invalid local entry in datapack ZIP");
        const auto localFlags = readU16(local.data() + 6);
        const auto localMethod = readU16(local.data() + 8);
        const auto nameLength = readU16(local.data() + 26);
        const auto extraLength = readU16(local.data() + 28);
        if ((localFlags & (0x0001U | 0x0040U | 0x2000U)) != 0 ||
            localFlags != entry.flags ||
            localMethod != entry.method)
            throw std::runtime_error("inconsistent local datapack ZIP entry");
        const auto localCrc = readU32(local.data() + 14);
        const auto localCompressedSize = readU32(local.data() + 18);
        const auto localUncompressedSize = readU32(local.data() + 22);
        if ((entry.flags & 0x0008U) == 0) {
            if (localCrc != entry.crc ||
                localCompressedSize != entry.compressedSize ||
                localUncompressedSize != entry.uncompressedSize)
                throw std::runtime_error(
                    "inconsistent local datapack ZIP CRC or sizes");
        } else if ((localCrc != 0 && localCrc != entry.crc) ||
                   (localCompressedSize != 0 &&
                    localCompressedSize != entry.compressedSize) ||
                   (localUncompressedSize != 0 &&
                    localUncompressedSize != entry.uncompressedSize)) {
            // General-purpose bit 3 permits the local values to be omitted
            // (zero) because they follow in the data descriptor. Some ZIP
            // writers also populate them eagerly; if present, they must agree
            // with the central directory just like the descriptor does.
            throw std::runtime_error(
                "inconsistent local datapack ZIP CRC or sizes");
        }
        std::vector<std::uint8_t> localName(nameLength);
        if (!readAt(input, static_cast<std::uint64_t>(entry.localHeaderOffset) +
                            local.size(), localName.data(), localName.size()))
            throw std::runtime_error("datapack ZIP entry names do not match");
        auto normalizedLocalName = checkedRelativePath(decodeZipPath(
            std::string_view(reinterpret_cast<const char*>(localName.data()),
                             localName.size()),
            localFlags));
        if (entry.directory && normalizedLocalName.back() != '/')
            normalizedLocalName.push_back('/');
        if (normalizedLocalName != entry.path)
            throw std::runtime_error("datapack ZIP entry names do not match");
        const std::uint64_t dataOffset =
            static_cast<std::uint64_t>(entry.localHeaderOffset) + local.size() +
            nameLength + extraLength;
        if (dataOffset > centralDirectoryOffset_ ||
            entry.compressedSize > centralDirectoryOffset_ - dataOffset)
            throw std::runtime_error("datapack ZIP entry overlaps its directory");
        const auto descriptorOffset = dataOffset + entry.compressedSize;
        if ((entry.flags & 0x0008U) != 0) {
            const auto available = centralDirectoryOffset_ - descriptorOffset;
            const auto descriptorBytes = static_cast<std::size_t>(
                std::min<std::uint64_t>(available, 16));
            std::array<std::uint8_t, 16> descriptor{};
            if (descriptorBytes < 12 ||
                !readAt(input, descriptorOffset, descriptor.data(),
                        descriptorBytes))
                throw std::runtime_error("truncated datapack ZIP data descriptor");
            const auto matches = [&](std::size_t offset) {
                return descriptorBytes >= offset + 12 &&
                    readU32(descriptor.data() + offset) == entry.crc &&
                    readU32(descriptor.data() + offset + 4) ==
                        entry.compressedSize &&
                    readU32(descriptor.data() + offset + 8) ==
                        entry.uncompressedSize;
            };
            const bool hasSignature =
                readU32(descriptor.data()) == 0x08074b50U && matches(4);
            if (!hasSignature && !matches(0))
                throw std::runtime_error(
                    "inconsistent datapack ZIP data descriptor");
        }
        std::vector<std::uint8_t> compressed(entry.compressedSize);
        if (!readAt(input, dataOffset, compressed.data(), compressed.size()))
            throw std::runtime_error("truncated datapack ZIP entry data");

        std::vector<std::uint8_t> output(entry.uncompressedSize);
        if (entry.method == 0) {
            if (entry.compressedSize != entry.uncompressedSize)
                throw std::runtime_error("invalid stored datapack ZIP entry");
            output = std::move(compressed);
        } else {
            z_stream stream{};
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                throw std::runtime_error("unable to initialize ZIP decompressor");
            stream.next_in = compressed.data();
            stream.avail_in = static_cast<uInt>(compressed.size());
            std::uint8_t overflowByte = 0;
            stream.next_out = output.empty() ? &overflowByte : output.data();
            stream.avail_out = static_cast<uInt>(
                output.empty() ? 1 : output.size());
            const int result = inflate(&stream, Z_FINISH);
            const auto totalInput = stream.total_in;
            const auto totalOutput = stream.total_out;
            inflateEnd(&stream);
            if (result != Z_STREAM_END || totalInput != compressed.size() ||
                totalOutput != output.size())
                throw std::runtime_error("invalid deflate stream in datapack ZIP");
        }
        const auto actualCrc = crc32(0, output.data(),
                                     static_cast<uInt>(output.size()));
        if (actualCrc != entry.crc)
            throw std::runtime_error("datapack ZIP entry CRC mismatch");
        return output;
    }

    fs::path path_;
    std::uint64_t centralDirectoryOffset_ = 0;
    std::vector<Entry> entries_;
    std::set<std::string> portableNames;
};

class Workspace final {
public:
    explicit Workspace(
        std::uint64_t maxExtractedBytes = kMaxWorkspaceBytes,
        std::size_t maxEntries = kMaxWorkspaceEntries)
        : root_(createUniqueRoot()), maxExtractedBytes_(maxExtractedBytes),
          maxEntries_(maxEntries) {}
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    ~Workspace() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    fs::path extract(const fs::path& archivePath) {
        Archive archive(archivePath);
        if (!archive.hasDataRoot())
            throw std::runtime_error("ZIP does not contain a datapack data/ root");
        const auto archiveBytes = archive.declaredUncompressedBytes();
        const auto archiveEntries = archive.entryCount();
        std::lock_guard lock(mutex_);
        if (archiveBytes > maxExtractedBytes_ - extractedBytes_ ||
            archiveEntries > maxEntries_ - extractedEntries_)
            throw std::runtime_error(
                "enabled datapacks exceed workspace extraction limits");
        static std::atomic<std::uint64_t> nextPack{0};
        const auto destination = root_ / std::to_string(
            nextPack.fetch_add(1, std::memory_order_relaxed));
        try {
            archive.extractTo(destination);
            const auto dataRoot = destination / "data";
            reservations_.emplace(dataRoot.lexically_normal(),
                                  Reservation{archiveBytes, archiveEntries});
            extractedBytes_ += archiveBytes;
            extractedEntries_ += archiveEntries;
            return dataRoot;
        } catch (...) {
            std::error_code ignored;
            fs::remove_all(destination, ignored);
            throw;
        }
    }

    void discardExtractedDataRoot(const fs::path& dataRoot) noexcept {
        const auto normalizedRoot = root_.lexically_normal();
        const auto normalizedData = dataRoot.lexically_normal();
        const auto destination = normalizedData.parent_path();
        if (normalizedData.filename() != "data" ||
            destination.parent_path() != normalizedRoot)
            return;
        std::lock_guard lock(mutex_);
        const auto reservation = reservations_.find(normalizedData);
        if (reservation != reservations_.end()) {
            extractedBytes_ -= reservation->second.bytes;
            extractedEntries_ -= reservation->second.entries;
            reservations_.erase(reservation);
        }
        std::error_code ignored;
        fs::remove_all(destination, ignored);
    }

private:
    struct Reservation {
        std::uint64_t bytes;
        std::size_t entries;
    };

    static fs::path createUniqueRoot() {
        std::error_code error;
        const auto tempRoot = fs::temp_directory_path(error);
        if (error)
            throw fs::filesystem_error("locate temporary directory", error);
#if !defined(_WIN32)
        auto parent = detail::openDirectory(tempRoot);
        detail::requireSafeParent(parent.get(), tempRoot);
        static std::atomic<std::uint64_t> nextRoot{0};
        const auto time = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            const auto suffix = time ^ nextRoot.fetch_add(
                1, std::memory_order_relaxed);
            const auto name = "cppfm-datapack-" + std::to_string(suffix);
            const auto candidate = tempRoot / name;
            if (::mkdirat(parent.get(), name.c_str(), 0700) != 0) {
                if (errno == EEXIST) continue;
                detail::throwPosixError(
                    "create temporary datapack directory", candidate);
            }
            try {
                // The initial mode is never broader than 0700; fchmodat restores
                // owner access even when a restrictive umask masked it at mkdir.
                if (::fchmodat(parent.get(), name.c_str(), 0700, 0) != 0)
                    detail::throwPosixError(
                        "protect temporary datapack directory", candidate);
                const int descriptor = ::openat(parent.get(), name.c_str(),
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                if (descriptor < 0)
                    detail::throwPosixError(
                        "open temporary datapack directory", candidate);
                detail::FileDescriptor root(descriptor);
                if (::fchmod(descriptor, 0700) != 0)
                    detail::throwPosixError(
                        "set temporary datapack directory permissions", candidate);
                struct stat status {};
                if (::fstat(descriptor, &status) != 0)
                    detail::throwPosixError(
                        "verify temporary datapack directory", candidate);
                if (!S_ISDIR(status.st_mode) || status.st_uid != ::geteuid() ||
                    (status.st_mode & 0777) != 0700)
                    throw fs::filesystem_error(
                        "temporary datapack directory is not owner-only",
                        candidate,
                        std::make_error_code(std::errc::permission_denied));
                return candidate;
            } catch (...) {
                detail::removeEmptyDirectoryAt(parent.get(), name);
                throw;
            }
        }
        throw std::runtime_error(
            "unable to reserve temporary datapack directory");
#else
        // Windows filesystem creation inherits the temporary directory's ACL.
        // The portable API does not install a current-user-only DACL; a strict
        // Windows guarantee needs explicit Win32 security-descriptor APIs.
        static std::atomic<std::uint64_t> nextRoot{0};
        const auto time = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            const auto suffix = time ^ nextRoot.fetch_add(
                1, std::memory_order_relaxed);
            const auto candidate = tempRoot /
                ("cppfm-datapack-" + std::to_string(suffix));
            error.clear();
            if (fs::create_directory(candidate, error)) return candidate;
            if (error && error != std::errc::file_exists)
                throw fs::filesystem_error("create temporary datapack directory",
                                           candidate, error);
        }
        throw std::runtime_error("unable to reserve temporary datapack directory");
#endif
    }

    fs::path root_;
    const std::uint64_t maxExtractedBytes_;
    const std::size_t maxEntries_;
    std::mutex mutex_;
    std::unordered_map<fs::path, Reservation> reservations_;
    std::uint64_t extractedBytes_ = 0;
    std::size_t extractedEntries_ = 0;
};

} // namespace cppfm::datapack_archive
