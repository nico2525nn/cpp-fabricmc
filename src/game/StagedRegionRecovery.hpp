#pragma once

#include "RegionFile.hpp"
#include "platform/StorageFileOps.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace cppfm {

// Repairs an interrupted full-region snapshot without allowing one bad chunk
// to discard valid staged siblings. The caller must hold the region's
// transaction lock for the duration of recover().
class StagedRegionRecovery final {
public:
    enum class ChunkState { Valid, Invalid, Unsupported };

    enum class Status { Recovered, Unsupported, Failed };

    struct Result final {
        Status status = Status::Failed;
        bool primaryRepaired = false;
        std::string error;
    };

    template <typename ValidateChunk>
    static Result recover(const std::filesystem::path& primaryPath,
                          const std::filesystem::path& stagedPath,
                          std::int32_t requestedCx,
                          std::int32_t requestedCz,
                          ValidateChunk&& validateChunk) {
        return recoverWithLoader(
            primaryPath, stagedPath, requestedCx, requestedCz,
            std::forward<ValidateChunk>(validateChunk),
            [](RegionFile& region, std::int32_t localX,
               std::int32_t localZ) {
                return region.load(localX, localZ);
            });
    }

    template <typename ValidateChunk, typename LoadChunk>
    static Result recoverWithLoader(
        const std::filesystem::path& primaryPath,
        const std::filesystem::path& stagedPath,
        std::int32_t requestedCx, std::int32_t requestedCz,
        ValidateChunk&& validateChunk, LoadChunk&& loadChunk) {
        Result result;
        std::error_code error;
        const auto inspectSource = [&](const std::filesystem::path& source,
                                       const char* label,
                                       bool required,
                                       bool& exists) {
            const auto status =
                std::filesystem::symlink_status(source, error);
            if (error == std::errc::no_such_file_or_directory) {
                error.clear();
                exists = false;
                if (!required) return true;
                result.error = std::string(label) +
                    " disappeared before recovery";
                return false;
            }
            if (error) {
                result.error = std::string("cannot inspect ") + label +
                               ": " + error.message();
                return false;
            }
            if (status.type() == std::filesystem::file_type::not_found) {
                exists = false;
                if (!required) return true;
                result.error = std::string(label) +
                    " disappeared before recovery";
                return false;
            }
            if (status.type() != std::filesystem::file_type::regular) {
                result.error = std::string("refusing non-regular ") + label;
                return false;
            }
            exists = true;
            return true;
        };
        bool primaryExists = false;
        if (!inspectSource(primaryPath, "primary region", false,
                           primaryExists))
            return result;
        bool stagedExists = false;
        if (!inspectSource(stagedPath, "staged region", true,
                           stagedExists))
            return result;

        auto recoveryPath = unusedSibling(stagedPath, ".recovering", error);
        if (error || recoveryPath.empty()) {
            result.error = error ? "cannot allocate recovery file: " +
                                       error.message()
                                 : "no recovery filename is available";
            return result;
        }
        TemporaryFile recoveryFile(std::filesystem::path{});
        bool recoveryFileCreated = false;
        const bool recoveryInitialized = writeEmptyRegion(
            recoveryPath, result.error, recoveryFileCreated);
        if (recoveryFileCreated)
            recoveryFile = TemporaryFile(recoveryPath);
        if (!recoveryInitialized) return result;

        try {
            RegionFile staged(stagedPath.string());
            std::optional<RegionFile> primary;
            bool primaryDamaged = false;
            if (primaryExists) {
                std::error_code primarySizeError;
                const auto primarySize = std::filesystem::file_size(
                    primaryPath, primarySizeError);
                if (primarySizeError) {
                    result.error = "cannot inspect primary region size: " +
                                   primarySizeError.message();
                    return result;
                }
                primaryDamaged = primarySize < 8192;
                if (!primaryDamaged)
                    primary.emplace(primaryPath.string());
            }
            RegionFile recovered(recoveryPath.string());

            std::error_code stagedSizeError;
            const auto stagedSize =
                std::filesystem::file_size(stagedPath, stagedSizeError);
            if (stagedSizeError) {
                result.error = "cannot inspect staged region size: " +
                               stagedSizeError.message();
                return result;
            }
            const bool stagedHeaderReadable =
                stagedSize >= 8192;
            bool stagedDamaged = !stagedHeaderReadable;
            const auto regionX = static_cast<std::int64_t>(requestedCx & ~31);
            const auto regionZ = static_cast<std::int64_t>(requestedCz & ~31);
            for (std::int32_t localZ = 0; localZ < 32; ++localZ) {
                for (std::int32_t localX = 0; localX < 32; ++localX) {
                    const auto chunkX = regionX + localX;
                    const auto chunkZ = regionZ + localZ;
                    if (chunkX < std::numeric_limits<std::int32_t>::min() ||
                        chunkX > std::numeric_limits<std::int32_t>::max() ||
                        chunkZ < std::numeric_limits<std::int32_t>::min() ||
                        chunkZ > std::numeric_limits<std::int32_t>::max()) {
                        result.error = "region chunk coordinates overflow";
                        return result;
                    }

                    std::vector<std::uint8_t> stagedBytes;
                    if (stagedHeaderReadable) {
                        try {
                            stagedBytes = loadChunk(staged, localX, localZ);
                        } catch (const RegionLoadError& failure) {
                            if (failure.kind() !=
                                RegionLoadFailureKind::Corruption) {
                                result.status = failure.kind() ==
                                        RegionLoadFailureKind::Unsupported
                                    ? Status::Unsupported : Status::Failed;
                                result.error = failure.what();
                                return result;
                            }
                            // Salvage readable staged siblings and the
                            // primary even when this particular Anvil slot is
                            // structurally broken. The damaged source is
                            // retained below before installing the rebuilt
                            // region.
                            stagedDamaged = true;
                        }
                    }
                    std::vector<std::uint8_t> primaryBytes;
                    if (primary) {
                        try {
                            primaryBytes = loadChunk(*primary, localX,
                                                     localZ);
                        } catch (const RegionLoadError& failure) {
                            if (failure.kind() !=
                                RegionLoadFailureKind::Corruption) {
                                result.status = failure.kind() ==
                                        RegionLoadFailureKind::Unsupported
                                    ? Status::Unsupported : Status::Failed;
                                result.error = failure.what();
                                return result;
                            }
                            // A malformed primary slot should not prevent
                            // valid staged chunks (or healthy primary
                            // siblings) from being salvaged. Mark the source
                            // for quarantine and rebuild it from the recovered
                            // snapshot after the scan.
                            primaryDamaged = true;
                        }
                    }
                    const auto stagedState = stagedBytes.empty()
                        ? ChunkState::Invalid
                        : validateChunk(stagedBytes,
                                        static_cast<std::int32_t>(chunkX),
                                        static_cast<std::int32_t>(chunkZ));
                    const auto primaryState = primaryBytes.empty()
                        ? ChunkState::Invalid
                        : validateChunk(primaryBytes,
                                        static_cast<std::int32_t>(chunkX),
                                        static_cast<std::int32_t>(chunkZ));

                    if (stagedState == ChunkState::Unsupported ||
                        primaryState == ChunkState::Unsupported) {
                        result.status = Status::Unsupported;
                        result.error = "recovery source contains unsupported chunk content at " +
                                       std::to_string(chunkX) + "," +
                                       std::to_string(chunkZ);
                        return result;
                    }
                    if (!stagedBytes.empty() &&
                        stagedState == ChunkState::Invalid)
                        stagedDamaged = true;
                    if (!primaryBytes.empty() &&
                        primaryState == ChunkState::Invalid)
                        primaryDamaged = true;

                    if (stagedState == ChunkState::Valid) {
                        recovered.store(localX, localZ, stagedBytes);
                    } else if (primaryState == ChunkState::Valid) {
                        recovered.store(localX, localZ, primaryBytes);
                    }
                }
            }

            if (!storage_file_ops::syncFile(recoveryPath)) {
                result.error = "cannot sync recovered region snapshot";
                return result;
            }
            if (!validateRecoveredRegion(recoveryPath, requestedCx,
                                         requestedCz, validateChunk,
                                         result.error))
                return result;

            std::filesystem::path primaryReplacement;
            TemporaryFile primaryReplacementFile(std::filesystem::path{});
            if (primaryDamaged) {
                primaryReplacement = unusedSibling(
                    primaryPath, ".recovering", error);
                if (error || primaryReplacement.empty()) {
                    result.error = error
                        ? "cannot allocate primary recovery file: " +
                              error.message()
                        : "no primary recovery filename is available";
                    return result;
                }
                bool primaryReplacementCreated = false;
                std::error_code copyError;
                const bool primaryReplacementReady =
                    storage_file_ops::copyFileExclusively(
                        recoveryPath, primaryReplacement, copyError,
                        &primaryReplacementCreated, 0600,
                        RegionFile::kMaxFileSizeBytes);
                if (!primaryReplacementReady)
                    result.error = "cannot create repaired primary: " +
                                   copyError.message();
                if (primaryReplacementCreated)
                    primaryReplacementFile =
                        TemporaryFile(primaryReplacement);
                if (!primaryReplacementReady) return result;
            }

            if (stagedDamaged && !backupCorruptFile(stagedPath, result.error))
                return result;
            if (primaryDamaged &&
                !backupCorruptFile(primaryPath, result.error))
                return result;

            const auto parent = stagedPath.parent_path();
            if (primaryDamaged) {
                if (!storage_file_ops::replaceFile(
                        primaryReplacement, primaryPath, error)) {
                    result.error = "cannot install repaired primary: " +
                                   error.message();
                    return result;
                }
                result.primaryRepaired = true;
                if (!storage_file_ops::syncDirectory(primaryPath.parent_path())) {
                    result.error = "cannot sync repaired primary directory";
                    return result;
                }
            }

            if (!storage_file_ops::replaceFile(recoveryPath, stagedPath,
                                                 error)) {
                result.error = "cannot install sanitized staged region: " +
                               error.message();
                return result;
            }
            if (!storage_file_ops::syncDirectory(parent)) {
                result.error = "cannot sync sanitized staged region directory";
                return result;
            }
            result.status = Status::Recovered;
            result.error.clear();
            return result;
        } catch (const std::exception& exception) {
            result.error = exception.what();
            return result;
        } catch (...) {
            result.error = "unknown staged-region recovery failure";
            return result;
        }
    }

private:
    class TemporaryFile final {
    public:
        explicit TemporaryFile(std::filesystem::path path)
            : path_(std::move(path)) {}
        ~TemporaryFile() {
            if (path_.empty()) return;
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
        TemporaryFile(const TemporaryFile&) = delete;
        TemporaryFile& operator=(const TemporaryFile&) = delete;
        TemporaryFile(TemporaryFile&& other) noexcept
            : path_(std::move(other.path_)) {
            other.path_.clear();
        }
        TemporaryFile& operator=(TemporaryFile&& other) noexcept {
            if (this == &other) return *this;
            remove();
            path_ = std::move(other.path_);
            other.path_.clear();
            return *this;
        }

    private:
        void remove() noexcept {
            if (path_.empty()) return;
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
        std::filesystem::path path_;
    };

    static std::filesystem::path unusedSibling(
        const std::filesystem::path& source, const char* suffix,
        std::error_code& error) {
        for (std::size_t index = 0; index < 10000; ++index) {
            auto candidate = source;
            candidate += suffix;
            if (index != 0) candidate += "." + std::to_string(index);
            const auto status = std::filesystem::symlink_status(
                candidate, error);
            if (error == std::errc::no_such_file_or_directory) {
                error.clear();
                return candidate;
            }
            if (error) return {};
            if (status.type() == std::filesystem::file_type::not_found)
                return candidate;
        }
        error = std::make_error_code(std::errc::file_exists);
        return {};
    }

    static bool writeEmptyRegion(const std::filesystem::path& path,
                                 std::string& error, bool& created) {
        std::error_code fileError;
        const std::array<char, 8192> emptyHeader{};
        if (!storage_file_ops::writeFileExclusively(
                path, emptyHeader.data(), emptyHeader.size(), fileError,
                &created, 0600)) {
            error = "cannot exclusively create recovery region: " +
                    fileError.message();
            return false;
        }
        error.clear();
        return true;
    }

    template <typename ValidateChunk>
    static bool validateRecoveredRegion(
        const std::filesystem::path& path, std::int32_t requestedCx,
        std::int32_t requestedCz, ValidateChunk&& validateChunk,
        std::string& error) {
        try {
            RegionFile region(path.string());
            const auto regionX = static_cast<std::int64_t>(requestedCx & ~31);
            const auto regionZ = static_cast<std::int64_t>(requestedCz & ~31);
            for (std::int32_t localZ = 0; localZ < 32; ++localZ) {
                for (std::int32_t localX = 0; localX < 32; ++localX) {
                    const auto bytes = region.load(localX, localZ);
                    if (bytes.empty()) continue;
                    const auto chunkX = regionX + localX;
                    const auto chunkZ = regionZ + localZ;
                    if (chunkX < std::numeric_limits<std::int32_t>::min() ||
                        chunkX > std::numeric_limits<std::int32_t>::max() ||
                        chunkZ < std::numeric_limits<std::int32_t>::min() ||
                        chunkZ > std::numeric_limits<std::int32_t>::max() ||
                        validateChunk(bytes,
                                      static_cast<std::int32_t>(chunkX),
                                      static_cast<std::int32_t>(chunkZ)) !=
                            ChunkState::Valid) {
                        error = "recovered region failed chunk validation";
                        return false;
                    }
                }
            }
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        } catch (...) {
            error = "unknown recovered region validation failure";
            return false;
        }
        error.clear();
        return true;
    }

    static bool backupCorruptFile(const std::filesystem::path& source,
                                  std::string& error) {
        std::error_code code;
        auto backupBase = source;
        backupBase += ".corrupt";
        std::filesystem::path backup;
        if (!storage_file_ops::copyFileToNewSibling(
                source, backupBase, backup, code, nullptr, 0666,
                RegionFile::kMaxFileSizeBytes)) {
            error = "cannot preserve corrupt source " + source.string() +
                    ": " + code.message();
            return false;
        }
        if (!storage_file_ops::syncDirectory(source.parent_path())) {
            error = "cannot sync corruption backup " + backup.string();
            return false;
        }
        error.clear();
        return true;
    }
};

} // namespace cppfm
