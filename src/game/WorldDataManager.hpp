#pragma once
#include <string>
#include <functional>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>
#include "../core/NBTValue.hpp"
#include "platform/StorageFileOps.hpp"

namespace cppfm {

namespace persistence_detail {
using storage_file_ops::replaceFile;
using storage_file_ops::syncDirectory;
using storage_file_ops::syncFile;

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
    enum class StorageRole { WorldRoot, Dimension };

    explicit WorldDataManager(
        std::string worldDir,
        StorageRole storageRole = StorageRole::WorldRoot)
        : dir_(std::move(worldDir)), storageRole_(storageRole) {}

    bool ownsLevelData() const noexcept {
        return storageRole_ == StorageRole::WorldRoot;
    }

    void setLevelStateProvider(std::function<void(nbt::Value&)> p, std::function<void(const nbt::Value&)> c) {
        provide_ = std::move(p);
        consume_ = std::move(c);
    }
    // A metadata template belongs to exactly one world directory. Reusing a
    // manager for another world must never carry opaque tags across worlds.
    void setDirectory(std::string d) {
        std::lock_guard fileLock(fileMutex());
        if (dir_ == d) return;
        dir_ = std::move(d);
        std::lock_guard rootLock(levelRootMtx_);
        loadedLevelRoot_ = nbt::Value::makeCompound();
        hasLoadedLevelRoot_ = false;
    }
    const std::string& directory() const { return dir_; }

    // Write a staging file first, publish it as .new only after it is durable,
    // then back up and replace level.dat. A recoverable .new is never opened
    // for truncating writes, so a failed later save cannot destroy it.
    bool atomicWrite(const std::string& path, const std::vector<std::uint8_t>& data) const {
        std::lock_guard lock(fileMutex());
        if (data.size() > kMaxLevelDataBytes ||
            data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            std::fprintf(stderr, "[WorldDataManager] level data exceeds size limit\n");
            return false;
        }
        const std::filesystem::path destination(path);
        const std::filesystem::path parent = destination.parent_path();
        const std::string candidate = path + ".new";
        const std::filesystem::path tmpBase = candidate + ".tmp";
        std::filesystem::path tmpPath;
        bool tmpOwned = false;
        const std::string old = path + "_old";
        const std::filesystem::path oldTmpBase = old + ".new";
        std::filesystem::path oldTmpPath;
        bool oldTmpOwned = false;
        try {
            if (!parent.empty()) {
                std::error_code directoryError;
                if (!storage_file_ops::createDirectoriesDurably(
                        parent, directoryError))
                    throw std::filesystem::filesystem_error(
                        "create durable world-data directory", parent,
                        directoryError);
            }
            std::error_code writeError;
            if (!storage_file_ops::writeFileToNewSibling(
                    tmpBase, data.data(), data.size(), tmpPath, writeError,
                    &tmpOwned, 0666))
                throw std::system_error(writeError,
                                        "create temporary level file");
            std::error_code candidateError;
            if (!persistence_detail::replaceFile(tmpPath, candidate,
                                                 candidateError))
                throw std::system_error(candidateError,
                                        "publish level.dat.new candidate");
            tmpOwned = false;
            if (!persistence_detail::syncDirectory(parent))
                throw std::runtime_error(
                    "cannot sync level directory after candidate publish");
            // W16: backup level.dat -> level.dat_old before rename (vanilla LevelStorage)
            std::error_code destinationError;
            const auto destinationStatus =
                std::filesystem::symlink_status(destination, destinationError);
            if (destinationError == std::errc::no_such_file_or_directory) {
                destinationError.clear();
            } else if (destinationError) {
                throw std::filesystem::filesystem_error(
                    "inspect level.dat before backup", destination,
                    destinationError);
            } else if (destinationStatus.type() ==
                       std::filesystem::file_type::symlink) {
                throw std::runtime_error(
                    "refusing to replace symlink level.dat");
            } else if (destinationStatus.type() !=
                           std::filesystem::file_type::not_found &&
                       destinationStatus.type() !=
                           std::filesystem::file_type::regular) {
                throw std::runtime_error(
                    "refusing to replace non-regular level.dat");
            }
            if (destinationStatus.type() == std::filesystem::file_type::regular) {
                std::error_code backupError;
                if (!storage_file_ops::copyFileToNewSibling(
                        destination, oldTmpBase, oldTmpPath, backupError,
                        &oldTmpOwned, 0666))
                    throw std::system_error(backupError, "backup level.dat");
                if (!persistence_detail::replaceFile(oldTmpPath, old,
                                                     backupError))
                    throw std::system_error(backupError, "replace level.dat_old");
                oldTmpOwned = false;
                if (!persistence_detail::syncDirectory(parent))
                    throw std::runtime_error("cannot sync level directory after backup");
            }
            // Atomic replacement.  On POSIX this replaces an existing file;
            // on Windows replaceFile uses MoveFileEx(REPLACE_EXISTING).
            std::error_code renameError;
            if (!persistence_detail::replaceFile(candidate, destination,
                                                 renameError))
                throw std::system_error(renameError, "replace level.dat");
            if (!persistence_detail::syncDirectory(parent))
                throw std::runtime_error("cannot sync level directory after save");
            return true;
        } catch (const std::exception& e) {
            std::error_code cleanupError;
            if (tmpOwned) std::filesystem::remove(tmpPath, cleanupError);
            if (oldTmpOwned) std::filesystem::remove(oldTmpPath, cleanupError);
            std::fprintf(stderr, "[WorldDataManager] atomic level save failed: %s\n", e.what());
            return false;
        } catch (...) {
            std::error_code cleanupError;
            if (tmpOwned) std::filesystem::remove(tmpPath, cleanupError);
            if (oldTmpOwned) std::filesystem::remove(oldTmpPath, cleanupError);
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

private:
    static std::recursive_mutex& fileMutex() {
        static std::recursive_mutex mutex;
        return mutex;
    }

    std::string dir_;
    StorageRole storageRole_;
    std::function<void(nbt::Value&)> provide_;
    std::function<void(const nbt::Value&)> consume_;
    mutable std::mutex levelRootMtx_;
    nbt::Value loadedLevelRoot_ = nbt::Value::makeCompound();
    bool hasLoadedLevelRoot_ = false;
    RecoveryResult lastRecovery_;

    static constexpr std::size_t kMaxLevelDataBytes = 8u * 1024u * 1024u;
};

} // namespace cppfm
