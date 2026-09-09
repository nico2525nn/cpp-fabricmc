#pragma once
#include <string>
#include <functional>
#include <filesystem>
#include <fstream>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>
#include "../core/NBTValue.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cppfm {

namespace persistence_detail {

// std::ofstream::flush() only hands bytes to the kernel.  Sync both the
// temporary file and its directory before considering a replacement durable.
inline bool syncFile(const std::filesystem::path& path) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        // FlushFileBuffers requires a writable handle for ordinary files.
        // The files are created by cppfm, so requesting write access here is
        // safe and makes the durability contract work on Windows as well as
        // POSIX instead of silently reducing it to a close-only flush.
        path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    return ok;
#elif defined(__unix__)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    int result = 0;
    do {
        result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
    const int closeResult = ::close(fd);
    return result == 0 && closeResult == 0;
#else
    (void)path;
    return true;
#endif
}

inline bool syncDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    (void)path;
    // replaceFile uses MOVEFILE_WRITE_THROUGH on Windows.
    return true;
#elif defined(__unix__)
    const auto directory = path.empty() ? std::filesystem::path(".") : path;
    const int fd = ::open(directory.c_str(), O_RDONLY);
    if (fd < 0) return false;
    int result = 0;
    do {
        result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
    const int closeResult = ::close(fd);
    return result == 0 && closeResult == 0;
#else
    (void)path;
    return true;
#endif
}

// Replacement of an existing destination is not consistently implemented by
// std::filesystem::rename on Windows.  Keep the operation atomic on both
// supported families instead of introducing a remove gap.
inline bool replaceFile(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

} // namespace persistence_detail

constexpr std::int32_t kCurrentDataVersion = 4189;

// vanilla LevelStorage (level.dat -> level.dat_old -> fresh generation).
enum class LevelSource { Dat, DatNew, DatOld, Fresh };
struct RecoveryResult {
    LevelSource src = LevelSource::Fresh;
    bool ok = false;                       // true if a usable level was loaded
    std::vector<std::string> logLines;     // human-readable startup log lines
    std::vector<std::string> droppedChunks;      // filled by chunk-level recovery
    std::vector<std::string> quarantinedPlayers; // filled by playerdata isolation
};
inline const char* levelSourceName(LevelSource s) {
    switch (s) {
        case LevelSource::Dat: return "level.dat";
        case LevelSource::DatNew: return "level.dat.new";
        case LevelSource::DatOld: return "level.dat_old";
        default: return "fresh";
    }
}

class WorldDataManager {
public:
    explicit WorldDataManager(std::string worldDir) : dir_(std::move(worldDir)) {}

    void setLevelStateProvider(std::function<void(nbt::Value&)> p, std::function<void(const nbt::Value&)> c) {
        provide_ = std::move(p);
        consume_ = std::move(c);
    }
    void setDirectory(std::string d) { dir_ = std::move(d); }
    const std::string& directory() const { return dir_; }

    // Atomic write helper: write and sync a sibling temp file, atomically
    // replace the backup, then atomically replace level.dat.  The fixed .new
    // name is intentional: if the process dies after the temp commit, the
    // next loader can validate and recover that complete candidate.
    bool atomicWrite(const std::string& path, const std::vector<std::uint8_t>& data) const {
        std::lock_guard lock(fileMutex());
        if (data.size() > kMaxLevelDataBytes ||
            data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            std::fprintf(stderr, "[WorldDataManager] level data exceeds size limit\n");
            return false;
        }
        const std::filesystem::path destination(path);
        const std::filesystem::path parent = destination.parent_path();
        const std::string tmp = path + ".new";
        const std::string old = path + "_old";
        const std::string oldTmp = old + ".new";
        try {
            if (!parent.empty()) std::filesystem::create_directories(parent);
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) throw std::runtime_error("cannot open temporary level file");
                f.write(reinterpret_cast<const char*>(data.data()),
                        static_cast<std::streamsize>(data.size()));
                f.flush();
                if (!f) throw std::runtime_error("cannot flush temporary level file");
            }
            if (!persistence_detail::syncFile(tmp))
                throw std::runtime_error("cannot sync temporary level file");
            // W16: backup level.dat -> level.dat_old before rename (vanilla LevelStorage)
            if (std::filesystem::exists(destination)) {
                std::error_code backupError;
                std::filesystem::copy_file(destination, oldTmp,
                                            std::filesystem::copy_options::overwrite_existing,
                                            backupError);
                if (backupError)
                    throw std::system_error(backupError, "backup level.dat");
                if (!persistence_detail::syncFile(oldTmp))
                    throw std::runtime_error("cannot sync level.dat_old backup");
                if (!persistence_detail::replaceFile(oldTmp, old, backupError))
                    throw std::system_error(backupError, "replace level.dat_old");
                if (!persistence_detail::syncDirectory(parent))
                    throw std::runtime_error("cannot sync level directory after backup");
            }
            // Atomic replacement.  On POSIX this replaces an existing file;
            // on Windows replaceFile uses MoveFileEx(REPLACE_EXISTING).
            std::error_code renameError;
            if (!persistence_detail::replaceFile(tmp, destination, renameError))
                throw std::system_error(renameError, "replace level.dat");
            if (!persistence_detail::syncDirectory(parent))
                throw std::runtime_error("cannot sync level directory after save");
            return true;
        } catch (const std::exception& e) {
            std::error_code cleanupError;
            std::filesystem::remove(tmp, cleanupError);
            std::filesystem::remove(oldTmp, cleanupError);
            std::fprintf(stderr, "[WorldDataManager] atomic level save failed: %s\n", e.what());
            return false;
        } catch (...) {
            std::error_code cleanupError;
            std::filesystem::remove(tmp, cleanupError);
            std::filesystem::remove(oldTmp, cleanupError);
            std::fprintf(stderr, "[WorldDataManager] atomic level save failed\n");
            return false;
        }
    }

    // DataFixerUpper-like version check
    bool needsFixup(std::int32_t fileVersion) const {
        return fileVersion < kCurrentDataVersion;
    }
    // Simple fixup: bump version and ensure required compounds exist
    void applyFixups(nbt::Value& root, std::int32_t fromVersion) const {
        if (fromVersion >= kCurrentDataVersion) return;
        // Example fixups: ensure Version compound, WorldBorder defaults, etc.
        auto* data = root.get("Data");
        if (!data) return;
        // In real DFU, would apply schemata. Here we just ensure DataVersion is updated.
        // Caller will set DataVersion to current before save.
    }

    bool checkAndFixVersion(nbt::Value& root) const {
        nbt::Value* data = nullptr;
        for (auto& [k,v] : root.comp) if (k=="Data") { data = &v; break; }
        if (!data) return false;
        std::int32_t ver = kCurrentDataVersion;
        if (auto* dv = data->get("DataVersion")) {
            if (dv->tag == nbt::Int) ver = dv->i;
            else if (dv->tag == nbt::Long) ver = static_cast<std::int32_t>(dv->l);
        } else {
            ver = 0;
        }
        if (needsFixup(ver)) {
            std::fprintf(stderr, "[WorldDataManager] DataFixerUpper: upgrading %d -> %d\n", ver, kCurrentDataVersion);
            applyFixups(root, ver);
            for (auto& [k,v] : data->comp) if (k=="DataVersion") { v.tag = nbt::Int; v.i = kCurrentDataVersion; return true; }
            data->set("DataVersion", nbt::Value::makeInt(kCurrentDataVersion));
        }
        return true;
    }

    // High-level save/load delegating to providers
    bool saveLevelData(nbt::Value root) {
        try {
            nbt::Value* data = nullptr;
            for (auto& [k,v] : root.comp) if (k=="Data") { data = &v; break; }
            if (data) {
                if (auto* version = data->get("DataVersion")) {
                    version->tag = nbt::Int;
                    version->i = kCurrentDataVersion;
                } else {
                    data->set("DataVersion", nbt::Value::makeInt(kCurrentDataVersion));
                }
            }
            WriteBuffer out;
            nbt::writeFileRoot(out, root);
            std::string path = dir_ + "/level.dat";
            return atomicWrite(path, out.data);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[WorldDataManager] level data serialization failed: %s\n", e.what());
            return false;
        } catch (...) {
            std::fprintf(stderr, "[WorldDataManager] level data serialization failed\n");
            return false;
        }
    }
    bool saveLevelDataWithProviders(std::int64_t worldTicks, std::int64_t dayTime, class World& world,
                                    const std::string& difficulty,
                                    double borderDiameter, double borderCX, double borderCZ,
                                    double borderLerpTarget = -1, std::int64_t borderLerpMs = -1);

    bool loadLevelData(class World& world, std::string& difficultyOut,
                       double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                       double* borderLerpTargetOut = nullptr, std::int64_t* borderLerpMsOut = nullptr);

    bool tryLoadFile(const std::string& path, class World& world, std::string& difficultyOut,
                     double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                     double* borderLerpTargetOut = nullptr, std::int64_t* borderLerpMsOut = nullptr);
    bool loadWithRecovery(class World& world, std::string& difficultyOut,
                          double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                          double* borderLerpTargetOut, std::int64_t* borderLerpMsOut,
                          RecoveryResult& out);
    const RecoveryResult& lastRecovery() const { return lastRecovery_; }

    // Raw load for testing: returns root
    bool loadRaw(nbt::Value& outRoot) const {
        std::lock_guard lock(fileMutex());
        try {
            std::string path = dir_ + "/level.dat";
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::error_code sizeError;
            const auto size = std::filesystem::file_size(path, sizeError);
            if (!sizeError && size > kMaxLevelDataBytes) return false;
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            // istreambuf_iterator reaches EOF without necessarily setting eofbit.
            if (bytes.empty() || bytes.size() > kMaxLevelDataBytes || f.bad()) return false;
            ReadBuffer in(bytes);
            nbt::Parser parser(in);
            outRoot = parser.readFileRoot();
            // version check
            checkAndFixVersion(outRoot);
            return true;
        } catch (...) { return false; }
    }

private:
    static std::recursive_mutex& fileMutex() {
        static std::recursive_mutex mutex;
        return mutex;
    }

    std::string dir_;
    std::function<void(nbt::Value&)> provide_;
    std::function<void(const nbt::Value&)> consume_;
    RecoveryResult lastRecovery_;

    static constexpr std::size_t kMaxLevelDataBytes = 8u * 1024u * 1024u;
};

} // namespace cppfm
