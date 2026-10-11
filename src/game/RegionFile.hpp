// Anvil region file (.mca) reader/writer. Records may be inline or use the
// Vanilla external-chunk stub and coordinate-named .mcc sidecar.
#pragma once
#include "platform/StorageFileOps.hpp"
#include "Lz4Block.hpp"
#include <array>
#include <cstdint>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include "../core/Zlib.hpp"

namespace cppfm {

enum class RegionLoadFailureKind { Io, Corruption, Unsupported };

class RegionLoadError final : public std::runtime_error {
public:
    RegionLoadError(RegionLoadFailureKind kind, const std::string& message)
        : std::runtime_error(message), kind_(kind) {}

    RegionLoadFailureKind kind() const noexcept { return kind_; }

private:
    RegionLoadFailureKind kind_;
};

class RegionFile {
    class ExternalSidecarWrite final {
    public:
        ExternalSidecarWrite(const std::filesystem::path& destination,
                             const std::vector<std::uint8_t>& payload,
                             std::size_t maxBytes,
                             bool replaceExisting = true,
                             bool transactionManaged = false)
            : destination_(destination),
              parent_(destination.parent_path()),
              replaceExisting_(replaceExisting),
              transactionManaged_(transactionManaged) {
            try {
                std::error_code inspectError;
                const auto status =
                    std::filesystem::symlink_status(destination_, inspectError);
                if (inspectError == std::errc::no_such_file_or_directory) {
                    inspectError.clear();
                } else if (inspectError) {
                    throw std::filesystem::filesystem_error(
                        "inspect external chunk sidecar", destination_,
                        inspectError);
                } else if (status.type() !=
                           std::filesystem::file_type::not_found) {
                    if (status.type() != std::filesystem::file_type::regular)
                        throw std::runtime_error(
                            "refusing non-regular external chunk sidecar: " +
                            destination_.string());
                    hadOriginal_ = true;
                }
                if (hadOriginal_ && !replaceExisting_)
                    throw std::filesystem::filesystem_error(
                        "staged external chunk sidecar already exists",
                        destination_,
                        std::make_error_code(std::errc::file_exists));

                std::error_code fileError;
                const auto temporaryBase =
                    std::filesystem::path(destination_.string() +
                                          (transactionManaged_
                                               ? ".cppfm-txn-tmp"
                                               : ".cppfm-tmp"));
                if (transactionManaged_) {
                    temporary_ = temporaryBase;
                    if (!storage_file_ops::writeFileExclusively(
                            temporary_, payload.data(), payload.size(),
                            fileError, &temporaryOwned_, 0666))
                        throw std::filesystem::filesystem_error(
                            "write transaction-managed external sidecar",
                            temporary_, fileError);
                } else if (replaceExisting_) {
                    if (!storage_file_ops::writeFileToNewSibling(
                            temporaryBase, payload.data(), payload.size(),
                            temporary_, fileError, &temporaryOwned_, 0666))
                        throw std::filesystem::filesystem_error(
                            "write temporary external chunk sidecar",
                            temporaryBase, fileError);
                } else {
                    temporary_ = temporaryBase;
                    if (!storage_file_ops::writeFileExclusively(
                            temporary_, payload.data(), payload.size(),
                            fileError, &temporaryOwned_, 0666))
                        throw std::filesystem::filesystem_error(
                            "write staged external chunk sidecar",
                            temporary_, fileError);
                }

                if (hadOriginal_) {
                    const auto backupBase =
                        std::filesystem::path(destination_.string() +
                                              (transactionManaged_
                                                   ? ".cppfm-txn-backup"
                                                   : ".cppfm-backup"));
                    const bool backedUp = transactionManaged_
                        ? (backup_ = backupBase,
                           storage_file_ops::copyFileExclusively(
                               destination_, backup_, fileError,
                               &backupOwned_, 0666, maxBytes))
                        : storage_file_ops::copyFileToNewSibling(
                              destination_, backupBase, backup_, fileError,
                              &backupOwned_, 0666, maxBytes);
                    if (!backedUp)
                        throw std::filesystem::filesystem_error(
                            "back up external chunk sidecar", destination_,
                            fileError);
                    if (!storage_file_ops::syncDirectory(parent_))
                        throw std::filesystem::filesystem_error(
                            "sync external chunk sidecar backup directory",
                            parent_, std::make_error_code(
                                std::errc::io_error));
                }

                if (replaceExisting_) {
                    if (!storage_file_ops::replaceFile(
                            temporary_, destination_, fileError))
                        throw std::filesystem::filesystem_error(
                            "publish external chunk sidecar", temporary_,
                            destination_, fileError);
                    temporaryOwned_ = false;
                    published_ = true;
                } else {
                    const auto moved = storage_file_ops::moveFileWithoutReplace(
                        temporary_, destination_, fileError);
                    if (moved !=
                        storage_file_ops::MoveWithoutReplaceStatus::Failed) {
                        published_ = true;
                        temporaryOwned_ = moved !=
                            storage_file_ops::MoveWithoutReplaceStatus::Moved;
                    }
                    if (moved !=
                        storage_file_ops::MoveWithoutReplaceStatus::Moved)
                        throw std::filesystem::filesystem_error(
                            "publish staged external chunk sidecar",
                            temporary_, destination_,
                            fileError ? fileError : std::make_error_code(
                                std::errc::io_error));
                }
                if (!storage_file_ops::syncDirectory(parent_)) {
                    std::error_code rollbackError;
                    if (!rollback(rollbackError))
                        throw std::runtime_error(
                            "cannot sync published external chunk sidecar and "
                            "rollback failed: " + rollbackError.message() +
                            backupDiagnostic());
                    throw std::filesystem::filesystem_error(
                        "sync published external chunk sidecar directory",
                        parent_, std::make_error_code(std::errc::io_error));
                }
            } catch (...) {
                if (published_ && rollbackAllowed_) {
                    std::error_code ignored;
                    rollback(ignored);
                }
                removeOwned(temporary_, temporaryOwned_);
                if (!published_)
                    removeOwned(backup_, backupOwned_);
                throw;
            }
        }

        ~ExternalSidecarWrite() {
            if (published_ && rollbackAllowed_) {
                std::error_code ignored;
                rollback(ignored);
            }
            removeOwned(temporary_, temporaryOwned_);
        }

        ExternalSidecarWrite(const ExternalSidecarWrite&) = delete;
        ExternalSidecarWrite& operator=(const ExternalSidecarWrite&) = delete;

        bool rollback(std::error_code& error) noexcept {
            error.clear();
            if (!published_) {
                return true;
            }
            if (hadOriginal_) {
                if (!backupOwned_ ||
                    !storage_file_ops::replaceFile(backup_, destination_,
                                                   error))
                    return false;
                backupOwned_ = false;
            } else {
                std::error_code inspectError;
                const auto status = std::filesystem::symlink_status(
                    destination_, inspectError);
                if (inspectError == std::errc::no_such_file_or_directory ||
                    (!inspectError && status.type() ==
                                          std::filesystem::file_type::not_found)) {
                    inspectError.clear();
                } else if (inspectError) {
                    error = inspectError;
                    return false;
                } else if (status.type() !=
                           std::filesystem::file_type::regular) {
                    error = std::make_error_code(std::errc::invalid_argument);
                    return false;
                } else {
                    if (!std::filesystem::remove(destination_, error) ||
                        error) {
                        if (!error)
                            error = std::make_error_code(
                                std::errc::no_such_file_or_directory);
                        return false;
                    }
                }
            }
            published_ = false;
            rollbackAllowed_ = false;
            if (!storage_file_ops::syncDirectory(parent_)) {
                error = std::make_error_code(std::errc::io_error);
                return false;
            }
            error.clear();
            return true;
        }

        void preservePublished() noexcept { rollbackAllowed_ = false; }

        std::string backupDiagnostic() const {
            return backupOwned_ ? "; previous sidecar retained at " +
                                      backup_.string()
                                : std::string{};
        }

        void commit() {
            rollbackAllowed_ = false;
            published_ = false;
            if (!backupOwned_) return;
            std::error_code removeError;
            if (!std::filesystem::remove(backup_, removeError) || removeError)
                throw std::filesystem::filesystem_error(
                    "remove committed external chunk sidecar backup", backup_,
                    removeError ? removeError : std::make_error_code(
                        std::errc::no_such_file_or_directory));
            backupOwned_ = false;
            if (!storage_file_ops::syncDirectory(parent_))
                throw std::filesystem::filesystem_error(
                    "sync committed external chunk sidecar cleanup", parent_,
                    std::make_error_code(std::errc::io_error));
        }

    private:
        static void removeOwned(const std::filesystem::path& path,
                                bool& owned) noexcept {
            if (!owned) return;
            std::error_code error;
            if (std::filesystem::remove(path, error) && !error) owned = false;
        }

        std::filesystem::path destination_;
        std::filesystem::path parent_;
        std::filesystem::path temporary_;
        std::filesystem::path backup_;
        bool replaceExisting_ = true;
        bool transactionManaged_ = false;
        bool hadOriginal_ = false;
        bool temporaryOwned_ = false;
        bool backupOwned_ = false;
        bool published_ = false;
        bool rollbackAllowed_ = true;
    };

public:
#ifdef CPPFM_REGION_EXTERNAL_TX_TESTING
    enum class ExternalTransactionTestPoint {
        SidecarPublished,
        RegionHeaderPublished,
        CommitMarkerPublished,
    };

    static void setExternalTransactionInterruptionForTesting(
        std::function<bool(ExternalTransactionTestPoint)> hook) {
        externalTransactionInterruptionForTesting() = std::move(hook);
    }
#endif

    // Anvil location entries use a 24-bit sector offset and an 8-bit sector
    // count. Recovery copies may span the last addressable sector range plus
    // the maximum chunk extent.
    static constexpr std::uint64_t kMaxFileSizeBytes =
        ((std::uint64_t{1} << 24) + 255) * 4096;

    explicit RegionFile(std::string path) : path_(std::move(path)) {}

    // Stable identity for callers that need to serialize a transaction made
    // up of multiple RegionFile operations. This identity is useful for
    // aliases, but can change when an atomic replacement installs a new inode.
    static std::string transactionKey(const std::string& path) {
        return fileIdentityKey(path);
    }

    // A transaction needs both a replacement-stable path key and the current
    // file identity. The path key serializes operations across atomic rename;
    // the identity key also joins existing hard-link aliases.
    static std::array<std::string, 2> transactionLockKeys(
        const std::string& path) {
        return {canonicalPathKey(path), fileIdentityKey(path)};
    }

    // Staged MCA snapshots may have one external `.mcc.new` payload per
    // chunk. Callers use this before promoting a snapshot so an interrupted
    // sidecar cleanup can be resumed before the MCA generation changes.
    bool hasSidecarGenerationFiles(std::string_view generationSuffix) const {
        for (std::int32_t lz = 0; lz < 32; ++lz) {
            for (std::int32_t lx = 0; lx < 32; ++lx) {
                const auto sidecar =
                    externalSidecarPath(lx, lz, generationSuffix);
                std::error_code inspectError;
                const auto status =
                    std::filesystem::symlink_status(sidecar, inspectError);
                if (inspectError == std::errc::no_such_file_or_directory)
                    continue;
                if (inspectError)
                    throw RegionLoadError(
                        RegionLoadFailureKind::Io,
                        "cannot inspect external chunk sidecar generation");
                if (status.type() != std::filesystem::file_type::not_found)
                    return true;
            }
        }
        return false;
    }

    // Remove a generation's sidecars only after the MCA no longer refers to
    // them. Refuse non-regular entries and sync the directory after unlinking
    // so a crash cannot resurrect a partially cleaned generation silently.
    bool removeSidecarGenerationFiles(std::string_view generationSuffix,
                                      std::string* error = nullptr) const {
        bool removedAny = false;
        for (std::int32_t lz = 0; lz < 32; ++lz) {
            for (std::int32_t lx = 0; lx < 32; ++lx) {
                const auto sidecar =
                    externalSidecarPath(lx, lz, generationSuffix);
                std::error_code inspectError;
                const auto status =
                    std::filesystem::symlink_status(sidecar, inspectError);
                if (inspectError == std::errc::no_such_file_or_directory)
                    continue;
                if (inspectError) {
                    if (error) *error = "cannot inspect " + sidecar.string() +
                                        ": " + inspectError.message();
                    return false;
                }
                if (status.type() == std::filesystem::file_type::not_found)
                    continue;
                if (status.type() != std::filesystem::file_type::regular) {
                    if (error) *error = "refusing non-regular external chunk sidecar " +
                                        sidecar.string();
                    return false;
                }
                std::error_code removeError;
                if (!std::filesystem::remove(sidecar, removeError) ||
                    removeError) {
                    if (error) *error = "cannot remove " + sidecar.string() +
                                        ": " + removeError.message();
                    return false;
                }
                removedAny = true;
            }
        }
        if (removedAny &&
            !storage_file_ops::syncDirectory(
                std::filesystem::path(path_).parent_path())) {
            if (error) *error = "cannot sync external sidecar directory";
            return false;
        }
        if (error) error->clear();
        return true;
    }

    // A recovery MCA uses a unique `.recovering[.N]` generation suffix. Its
    // sidecars are temporary and must not survive the owning MCA snapshot.
    bool removeOwnRecoverySidecarFiles(std::string* error = nullptr) const {
        const auto info = regionPathInfo();
        if (!info || !isRecoveryGenerationSuffix(info->generationSuffix)) {
            if (error) *error = "region path is not a recovery generation";
            return false;
        }
        return removeSidecarGenerationFiles(info->generationSuffix, error);
    }

    // Publish a sanitized recovery snapshot together with any external
    // payloads it references. Sidecar writes remain rollback-capable until
    // the replacement MCA is installed, so a failed install leaves the
    // destination generation unchanged.
    bool installRecoverySnapshotFrom(
        const std::filesystem::path& replacementMca,
        RegionFile& recoveryRegion, std::string* error = nullptr) {
        try {
            const auto destinationInfo = regionPathInfo();
            if (!destinationInfo ||
                (!destinationInfo->generationSuffix.empty() &&
                 destinationInfo->generationSuffix != ".new")) {
                if (error) *error =
                    "recovery destination is not a canonical or .new region";
                return false;
            }

            std::vector<std::unique_ptr<ExternalSidecarWrite>> sidecarWrites;
            for (std::int32_t lz = 0; lz < 32; ++lz) {
                for (std::int32_t lx = 0; lx < 32; ++lx) {
                    if (!recoveryRegion.externalStub(lx, lz)) continue;
                    const auto payload =
                        recoveryRegion.readExternalPayload(lx, lz);
                    sidecarWrites.emplace_back(
                        std::make_unique<ExternalSidecarWrite>(
                            externalSidecarPath(
                                lx, lz, destinationInfo->generationSuffix),
                            payload, kMaxExternalCompressedBytes, true));
                }
            }

            std::error_code replaceError;
            if (!storage_file_ops::replaceFile(
                    replacementMca, path_, replaceError)) {
                if (error) *error =
                    "cannot install recovered region: " +
                    replaceError.message();
                return false;
            }

            // Once the MCA points at the published sidecars, preserve that
            // complete generation even if the following directory sync is
            // uncertain. Backups remain available until commit succeeds.
            for (auto& sidecarWrite : sidecarWrites)
                sidecarWrite->preservePublished();
            const auto parent = std::filesystem::path(path_).parent_path();
            if (!storage_file_ops::syncDirectory(parent)) {
                if (error) *error =
                    "cannot sync recovered region directory";
                return false;
            }
            for (auto& sidecarWrite : sidecarWrites)
                sidecarWrite->commit();
            if (error) error->clear();
            return true;
        } catch (const std::exception& exception) {
            if (error) *error = exception.what();
            return false;
        } catch (...) {
            if (error) *error = "unknown recovered-region install failure";
            return false;
        }
    }

    // Check the compression and inline-or-external size limits used by
    // store(), without touching a region file. Gameplay can use this before
    // committing block-entity inventory edits that would otherwise become
    // impossible to persist.
    static bool canStoreBytes(const std::vector<std::uint8_t>& nbt) noexcept {
        try {
            if (nbt.empty() || nbt.size() > kMaxDecompressedBytes) return false;
            std::vector<std::uint8_t> compressed;
            compressRaw(nbt.data(), nbt.size(), compressed);
            (void)sectorsRequiredForCompressedSize(compressed.size());
            return compressed.size() <= kMaxExternalCompressedBytes;
        } catch (...) {
            return false;
        }
    }

    // Whether a valid compressed Anvil record needs the `.mcc` external-chunk
    // representation. Persistence uses this before creating its staged save
    // generation so MCA and sidecar publication can share one recovery record.
    static bool wouldStoreExternally(
        const std::vector<std::uint8_t>& nbt) {
        if (nbt.empty() || nbt.size() > kMaxDecompressedBytes)
            throw std::length_error(
                "chunk exceeds the bounded Anvil decode limit");
        std::vector<std::uint8_t> compressed;
        compressRaw(nbt.data(), nbt.size(), compressed);
        if (compressed.size() > kMaxExternalCompressedBytes)
            throw std::length_error(
                "compressed chunk exceeds the bounded external sidecar limit");
        return sectorsRequiredForCompressedSize(compressed.size()) >
               std::numeric_limits<std::uint8_t>::max();
    }

    std::filesystem::path externalSidecarPathForGeneration(
        std::int32_t lx, std::int32_t lz,
        std::string_view generationSuffix = {}) const {
        return externalSidecarPath(lx, lz, generationSuffix);
    }

    // Returns decompressed chunk bytes or empty if absent.
    std::vector<std::uint8_t> load(std::int32_t lx, std::int32_t lz) {
        std::scoped_lock lk(pathStripeMutex(path_), fileIdentityStripeMutex(path_), mtx_);
        recoverCanonicalExternalTransactionLocked();
        const std::size_t idx = index(lx, lz);
        storage_file_ops::RandomAccessRegularFile file;
        bool created = false;
        std::error_code fileError;
        if (!file.open(path_,
                       storage_file_ops::RandomAccessRegularFile::Access::ReadOnly,
                       false, created, fileError)) {
            if (fileError == std::errc::no_such_file_or_directory)
                return {};
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot open regular region file");
        }
        std::uint64_t fileSize = 0;
        if (!file.size(fileSize, fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot determine region file size");
        if (fileSize < 8192)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "truncated region header");
        std::uint8_t header[8192];
        if (!file.readAt(0, header, sizeof(header), fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region header");
        const std::uint32_t off = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        const std::uint8_t count = header[idx*4+3];
        if (off == 0 && count == 0) return {};
        if (off == 0 || count == 0)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "inconsistent region chunk location");
        const std::uint64_t regionEnd = (static_cast<std::uint64_t>(off) + count) * 4096u;
        if (off < 2 || regionEnd > fileSize)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "region chunk points outside the file");
        // `off` is a 24-bit Anvil sector address, but its declared type is
        // uint32_t.  Multiplying it by the unsuffixed 4096 used to perform
        // the arithmetic in 32 bits and wrap for perfectly valid region
        // files above the 4 GiB boundary.  That silently read a different
        // chunk and is also an easy way to turn a corrupt header into a
        // misleading decode error.
        const std::uint64_t byteOffset = static_cast<std::uint64_t>(off) * 4096u;
        if (byteOffset > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()))
            throw RegionLoadError(RegionLoadFailureKind::Unsupported,
                                  "region chunk offset is not seekable");
        std::uint8_t lenb[4];
        if (!file.readAt(byteOffset, lenb, sizeof(lenb), fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region chunk length");
        const std::uint32_t total = (static_cast<std::uint32_t>(lenb[0]) << 24) |
                                    (static_cast<std::uint32_t>(lenb[1]) << 16) |
                                    (static_cast<std::uint32_t>(lenb[2]) << 8) |
                                    static_cast<std::uint32_t>(lenb[3]);
        if (total < 1 || total > static_cast<std::uint32_t>(count) * 4096u - 4u ||
            total > 8u*1024u*1024u)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "invalid region chunk length");
        std::uint8_t comp = 0;
        if (!file.readAt(byteOffset + 4, &comp, sizeof(comp), fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region compression type");
        const bool external = (comp & 0x80U) != 0;
        const auto compression = static_cast<std::uint8_t>(comp & 0x7fU);
        std::vector<std::uint8_t> raw;
        if (external) {
            if (count != 1 || total != 1)
                throw RegionLoadError(
                    RegionLoadFailureKind::Corruption,
                    "external Anvil chunk has a malformed stub");
            raw = readExternalPayload(lx, lz);
        } else {
            if (total < 2)
                throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                      "inline region chunk has an empty payload");
            raw.resize(total - 1);
            if (!file.readAt(byteOffset + 5, raw.data(), raw.size(), fileError))
                throw RegionLoadError(RegionLoadFailureKind::Io,
                                      "cannot read region chunk payload");
        }
        return decodeChunkPayload(compression, std::move(raw));
    }

    // Ordinary load() intentionally remains fail-closed for staged files.
    // Recovery prefers a paired `.mcc.new`; it borrows the canonical sidecar
    // only when both MCA files contain the identical external stub.
    std::vector<std::uint8_t> loadWithCanonicalExternalReference(
        RegionFile& canonical, std::int32_t lx, std::int32_t lz) {
        const auto stagedStub = externalStub(lx, lz);
        if (!stagedStub) return load(lx, lz);
        if (!isStagedPeerOf(canonical))
            throw RegionLoadError(
                RegionLoadFailureKind::Unsupported,
                "staged external chunk has no canonical region peer; preserving source");

        const auto stagedSidecar = externalSidecarPath(lx, lz, ".new");
        std::error_code sidecarError;
        const auto stagedStatus =
            std::filesystem::symlink_status(stagedSidecar, sidecarError);
        const bool stagedSidecarExists =
            !sidecarError && stagedStatus.type() !=
                                 std::filesystem::file_type::not_found;
        if (sidecarError != std::errc::no_such_file_or_directory &&
            sidecarError)
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot inspect staged external sidecar");
        if (stagedSidecarExists)
            return decodeChunkPayload(
                stagedStub->compression,
                readExternalPayload(lx, lz, true));

        const auto canonicalStub = canonical.externalStub(lx, lz);
        if (!canonicalStub || *canonicalStub != *stagedStub)
            throw RegionLoadError(
                RegionLoadFailureKind::Unsupported,
                "staged external chunk has no matching primary stub or staged sidecar; preserving source");

        return canonical.load(lx, lz);
    }

    // Fill only empty slots from another region. This is used when two
    // independently recovered snapshots contain different sibling chunks:
    // existing staged slots remain authoritative, while primary-only chunks
    // are retained before the staged file replaces the primary.
    bool mergeMissingFrom(RegionFile& source, std::string* error = nullptr) {
        if (this == &source || path_ == source.path_) return true;
        try {
            for (std::int32_t lz = 0; lz < 32; ++lz) {
                for (std::int32_t lx = 0; lx < 32; ++lx) {
                    if (!load(lx, lz).empty()) continue;
                    auto sourceBytes = source.load(lx, lz);
                    if (!sourceBytes.empty()) store(lx, lz, sourceBytes);
                }
            }
        } catch (const std::exception& exception) {
            if (error) *error = exception.what();
            return false;
        } catch (...) {
            if (error) *error = "unknown region merge error";
            return false;
        }
        if (error) error->clear();
        return true;
    }

    void store(std::int32_t lx, std::int32_t lz, const std::vector<std::uint8_t>& nbt) {
        std::scoped_lock lk(pathStripeMutex(path_), fileIdentityStripeMutex(path_), mtx_);
        recoverCanonicalExternalTransactionLocked();
        const std::size_t idx = index(lx, lz);
        if (nbt.empty()) throw std::invalid_argument("cannot store an empty chunk");
        if (nbt.size() > kMaxDecompressedBytes)
            throw std::length_error("chunk exceeds the bounded Anvil decode limit");
        std::vector<std::uint8_t> comp;
        compressRaw(nbt.data(), nbt.size(), comp);
        const std::size_t requiredSectors =
            sectorsRequiredForCompressedSize(comp.size());
        const bool external = requiredSectors >
            std::numeric_limits<std::uint8_t>::max();
        if (comp.size() > kMaxExternalCompressedBytes)
            throw std::length_error(
                "compressed chunk exceeds the bounded external sidecar limit");
        const std::size_t needSectors = external ? 1 : requiredSectors;
        std::optional<std::filesystem::path> sidecarPath;
        std::optional<RegionPathInfo> externalRegionInfo;
        bool canonicalExternal = false;
        if (external) {
            externalRegionInfo = regionPathInfo();
            if (!externalRegionInfo ||
                (!externalRegionInfo->generationSuffix.empty() &&
                 externalRegionInfo->generationSuffix != ".new" &&
                 !isRecoveryGenerationSuffix(
                     externalRegionInfo->generationSuffix)))
                throw RegionLoadError(
                    RegionLoadFailureKind::Unsupported,
                    "external chunks can only be written to canonical, .new, or temporary recovery region generations");
            sidecarPath = externalSidecarPath(lx, lz,
                externalRegionInfo->generationSuffix);
            canonicalExternal =
                externalRegionInfo->generationSuffix.empty();
        }
        const auto parent = std::filesystem::path(path_).parent_path();
        if (!parent.empty()) {
            std::error_code parentError;
            if (!storage_file_ops::createDirectoriesDurably(
                    parent, parentError))
                throw std::filesystem::filesystem_error(
                    "create durable region directory", parent, parentError);
        }
        storage_file_ops::RandomAccessRegularFile file;
        bool createdFile = false;
        std::error_code fileError;
        if (!file.open(
                path_,
                storage_file_ops::RandomAccessRegularFile::Access::ReadWrite,
                true, createdFile, fileError, 0666))
            throw std::filesystem::filesystem_error(
                "cannot safely open region for update", path_, fileError);
        bool regionFileClosed = false;
        std::vector<std::uint8_t> header(8192, 0);
        std::uint64_t existingFileSize = 0;
        if (createdFile) {
            if (!file.writeAt(0, header.data(), header.size(), fileError))
                throw std::filesystem::filesystem_error(
                    "cannot initialize new region header", path_, fileError);
            existingFileSize = header.size();
        } else {
            if (!file.size(existingFileSize, fileError))
                throw std::runtime_error(
                    "cannot determine the complete region file size: " + path_);
            if (existingFileSize < header.size())
                throw std::runtime_error(
                    "cannot update a region with a truncated header: " + path_);
            if (!file.readAt(0, header.data(), header.size(), fileError))
                throw std::runtime_error(
                    "cannot read complete region header: " + path_);
        }

        const std::uint32_t oldOff = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        const std::uint8_t oldCnt = header[idx*4+3];

        // Inline records contain compression type + compressed bytes. Vanilla
        // external records instead contain a one-byte stub and put those
        // compressed bytes in c.<chunkX>.<chunkZ>.mcc.
        const std::uint32_t payloadLen = external
            ? 1U : static_cast<std::uint32_t>(comp.size() + 1U);
        std::vector<std::uint8_t> payload{
            static_cast<std::uint8_t>(payloadLen >> 24), static_cast<std::uint8_t>(payloadLen >> 16),
            static_cast<std::uint8_t>(payloadLen >> 8),  static_cast<std::uint8_t>(payloadLen),
            static_cast<std::uint8_t>(external ? 0x82U : 2U) };
        if (external) {
            payload.resize(5);
        } else {
            payload.insert(payload.end(), comp.begin(), comp.end());
        }

        const auto validRange = [existingFileSize](std::uint32_t off,
                                                   std::uint8_t count) {
            if (off < 2 || count == 0) return false;
            const auto endSector = static_cast<std::uint64_t>(off) + count;
            if (endSector > 0x1000000ULL) return false;
            // A location-table entry is not a real allocation unless its
            // entire sector range exists in the opened source file. Without
            // this check, a damaged high offset can be reused and extend a
            // short region into a multi-gigabyte sparse file on the next save.
            return endSector * 4096u <= existingFileSize;
        };
        auto overlaps = [](std::uint32_t lhsOff, std::uint8_t lhsCount,
                           std::uint32_t rhsOff, std::uint8_t rhsCount) {
            const auto lhsEnd = static_cast<std::uint64_t>(lhsOff) + lhsCount;
            const auto rhsEnd = static_cast<std::uint64_t>(rhsOff) + rhsCount;
            return static_cast<std::uint64_t>(lhsOff) < rhsEnd &&
                   static_cast<std::uint64_t>(rhsOff) < lhsEnd;
        };
        bool oldValid = validRange(oldOff, oldCnt);
        if (oldValid) {
            // A malformed header can make two chunks claim the same
            // allocation.  Reusing this slot would overwrite the other
            // chunk; append after the highest declared range instead.
            for (std::size_t i = 0; i < 1024; ++i) {
                if (i == idx) continue;
                const std::uint32_t otherOff =
                    (static_cast<std::uint32_t>(header[i * 4]) << 16) |
                    (static_cast<std::uint32_t>(header[i * 4 + 1]) << 8) |
                    static_cast<std::uint32_t>(header[i * 4 + 2]);
                const std::uint8_t otherCnt = header[i * 4 + 3];
                if (validRange(otherOff, otherCnt) &&
                    overlaps(oldOff, oldCnt, otherOff, otherCnt)) {
                    oldValid = false;
                    break;
                }
            }
        }

        std::uint32_t newOff = oldValid ? oldOff : 0;
        if (external) {
            // Keep the old chunk allocation intact until the sidecar and the
            // new stub are durable. Publishing the location table is the
            // commit point for this one-chunk update.
            std::uint64_t maxEnd = 2;
            for (std::size_t i = 0; i < 1024; ++i) {
                const std::uint32_t offset =
                    (static_cast<std::uint32_t>(header[i * 4]) << 16) |
                    (static_cast<std::uint32_t>(header[i * 4 + 1]) << 8) |
                    static_cast<std::uint32_t>(header[i * 4 + 2]);
                const std::uint8_t count = header[i * 4 + 3];
                if (validRange(offset, count))
                    maxEnd = std::max(maxEnd,
                        static_cast<std::uint64_t>(offset) + count);
            }
            if (maxEnd + needSectors > 0x1000000ULL)
                throw std::length_error("region file sector address exhausted");
            newOff = static_cast<std::uint32_t>(maxEnd);
        } else if (newOff == 0 || needSectors > oldCnt) {
            // Append after the highest valid allocation.  Ignore malformed
            // header entries instead of seeking to attacker-controlled sizes.
            std::uint64_t maxEnd = 2;
            for (std::size_t i = 0; i < 1024; ++i) {
                const std::uint32_t o = (header[i*4] << 16) | (header[i*4+1] << 8) | header[i*4+2];
                const std::uint8_t c = header[i*4+3];
                if (validRange(o, c)) maxEnd = std::max(maxEnd, static_cast<std::uint64_t>(o) + c);
            }
            if (maxEnd + needSectors > 0x1000000ULL)
                throw std::length_error("region file sector address exhausted");
            newOff = static_cast<std::uint32_t>(maxEnd);
        }

        const std::uint32_t ts = static_cast<std::uint32_t>(time(nullptr));
        const std::uint64_t wantBytes =
            (static_cast<std::uint64_t>(newOff) + needSectors) * 4096u;
        if (wantBytes > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()))
            throw std::length_error("region file is not seekable");
        std::uint64_t currentSize = 0;
        if (!file.size(currentSize, fileError))
            throw std::runtime_error("cannot determine region file size: " + path_);
        if (currentSize < wantBytes &&
            !file.resize(wantBytes, fileError))
            throw std::filesystem::filesystem_error(
                "cannot extend region file", path_, fileError);

        const std::uint64_t payloadOffset = static_cast<std::uint64_t>(newOff) * 4096u;
        if (payloadOffset > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()))
            throw std::length_error("region payload offset is not seekable");
        if (!file.writeAt(payloadOffset, payload.data(), payload.size(),
                          fileError))
            throw std::filesystem::filesystem_error(
                "cannot write region chunk payload", path_, fileError);
        const std::size_t pad = needSectors * 4096u - payload.size();
        if (pad) {
            static const char zeros[4096] = {};
            std::size_t left = pad;
            std::uint64_t padOffset = payloadOffset + payload.size();
            while (left != 0) {
                const std::size_t block = std::min(left, sizeof(zeros));
                if (!file.writeAt(padOffset, zeros, block, fileError))
                    throw std::filesystem::filesystem_error(
                        "cannot pad region chunk allocation", path_, fileError);
                padOffset += block;
                left -= block;
            }
        }

        // The new stub is not reachable through the MCA header yet. Flush it
        // before publishing the sidecar so a successful subsequent header
        // update can never point at an unwritten stub.
        if (external && !file.sync(fileError))
            throw std::filesystem::filesystem_error(
                "cannot sync external chunk stub", path_, fileError);

        const std::vector<std::uint8_t> originalHeader = header;
        header[idx*4]   = static_cast<std::uint8_t>(newOff >> 16);
        header[idx*4+1] = static_cast<std::uint8_t>(newOff >> 8);
        header[idx*4+2] = static_cast<std::uint8_t>(newOff);
        header[idx*4+3] = static_cast<std::uint8_t>(needSectors);
        for (int i = 0; i < 4; ++i)
            header[4096 + idx*4 + i] = (ts >> (24 - i*8)) & 0xFF;
        std::optional<ExternalSidecarWrite> sidecarWrite;
        bool directTransactionArmed = false;
        bool sidecarPublished = false;
        bool headerPublished = false;
        DirectExternalTransaction directTransaction;
        try {
            if (canonicalExternal) {
                directTransactionArmed = true;
                directTransaction = beginDirectExternalTransaction(
                    lx, lz, originalHeader, header, comp);
            }
            if (external) {
                sidecarWrite.emplace(
                    *sidecarPath, comp, kMaxExternalCompressedBytes,
                    canonicalExternal, canonicalExternal);
                if (canonicalExternal) {
                    // The durable transaction marker now owns rollback; do
                    // not let this stack object's destructor independently
                    // restore just one half of the MCA/.mcc pair.
                    sidecarWrite->preservePublished();
#ifdef CPPFM_REGION_EXTERNAL_TX_TESTING
                    interruptExternalTransactionForTesting(
                        ExternalTransactionTestPoint::SidecarPublished);
#endif
                }
                sidecarPublished = true;
            }

            if (!file.writeAt(0, header.data(), header.size(), fileError))
                throw std::filesystem::filesystem_error(
                    "cannot update region location table", path_, fileError);
            headerPublished = true;

            if (external) {
                if (!canonicalExternal)
                    sidecarWrite->preservePublished();
                if (!file.sync(fileError))
                    throw std::filesystem::filesystem_error(
                        "cannot sync external chunk location", path_, fileError);
                if (canonicalExternal) {
                    if (!file.close(fileError))
                        throw std::filesystem::filesystem_error(
                            "cannot close canonical region after external update",
                            path_, fileError);
                    regionFileClosed = true;
#ifdef CPPFM_REGION_EXTERNAL_TX_TESTING
                    interruptExternalTransactionForTesting(
                        ExternalTransactionTestPoint::RegionHeaderPublished);
#endif
                    directTransaction.state =
                        DirectExternalTransaction::State::Committed;
                    writeDirectExternalManifest(directTransaction, true);
#ifdef CPPFM_REGION_EXTERNAL_TX_TESTING
                    interruptExternalTransactionForTesting(
                        ExternalTransactionTestPoint::CommitMarkerPublished);
#endif
                    recoverCanonicalExternalTransactionLocked();
                    directTransactionArmed = false;
                } else {
                    sidecarWrite->commit();
                }
            } else if (createdFile && !file.sync(fileError)) {
                throw std::filesystem::filesystem_error(
                    "cannot sync new region file", path_, fileError);
            }
            if (!regionFileClosed) {
                if (!file.close(fileError))
                    throw std::filesystem::filesystem_error(
                        "cannot close region file", path_, fileError);
                regionFileClosed = true;
            }
        }
#ifdef CPPFM_REGION_EXTERNAL_TX_TESTING
        catch (const ExternalTransactionInterruptedForTesting&) {
            throw;
        }
#endif
        catch (const std::exception& exception) {
            if (directTransactionArmed) {
                std::error_code closeError;
                (void)file.close(closeError);
                try {
                    recoverCanonicalExternalTransactionLocked();
                    directTransactionArmed = false;
                } catch (const std::exception& recoveryError) {
                    throw std::runtime_error(
                        std::string("canonical external save failed and "
                                    "transaction recovery remains pending: ") +
                        recoveryError.what() + "; original failure: " +
                        exception.what());
                }
            } else if (external && sidecarWrite && sidecarPublished &&
                       !headerPublished) {
                const auto locationWriteError = fileError;
                std::error_code restoreError;
                const bool headerRestored =
                    file.writeAt(0, originalHeader.data(),
                                 originalHeader.size(), restoreError) &&
                    file.sync(restoreError);
                if (!headerRestored) {
                    sidecarWrite->preservePublished();
                    throw std::filesystem::filesystem_error(
                        "cannot publish staged external chunk location or "
                        "restore its prior region header",
                        path_, restoreError ? restoreError :
                                              locationWriteError);
                }
                if (!sidecarWrite->rollback(restoreError))
                    throw std::filesystem::filesystem_error(
                        "staged region header was restored but external "
                        "sidecar rollback failed" +
                            sidecarWrite->backupDiagnostic(),
                        *sidecarPath, restoreError);
            }
            throw;
        } catch (...) {
            if (directTransactionArmed) {
                std::error_code closeError;
                (void)file.close(closeError);
                recoverCanonicalExternalTransactionLocked();
                directTransactionArmed = false;
            } else if (external && sidecarWrite && sidecarPublished &&
                       !headerPublished) {
                std::error_code restoreError;
                if (!file.writeAt(0, originalHeader.data(),
                                  originalHeader.size(), restoreError) ||
                    !file.sync(restoreError))
                    throw std::filesystem::filesystem_error(
                        "cannot restore staged region header after external "
                        "sidecar failure", path_, restoreError);
                if (!sidecarWrite->rollback(restoreError))
                    throw std::filesystem::filesystem_error(
                        "staged region header restored but external sidecar "
                        "rollback failed" +
                            sidecarWrite->backupDiagnostic(),
                        *sidecarPath, restoreError);
            }
            throw;
        }
    }

private:
    struct RegionPathInfo final {
        std::int32_t regionX = 0;
        std::int32_t regionZ = 0;
        std::string generationSuffix;
    };

    static std::vector<std::uint8_t> decodeChunkPayload(
        std::uint8_t compression, std::vector<std::uint8_t> raw) {
        std::vector<std::uint8_t> out;
        if (compression == 2) {
            try {
                decompressUnknown(raw.data(), raw.size(), out,
                                  kMaxDecompressedBytes);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception& exception) {
                throw RegionLoadError(
                    RegionLoadFailureKind::Corruption,
                    std::string("cannot decode zlib region chunk: ") +
                        exception.what());
            }
        } else if (compression == 1) {
            try {
                decompressGzipUnknown(raw.data(), raw.size(), out,
                                      kMaxDecompressedBytes);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception& exception) {
                throw RegionLoadError(
                    RegionLoadFailureKind::Corruption,
                    std::string("cannot decode gzip region chunk: ") +
                        exception.what());
            }
        } else if (compression == 3) {
            if (raw.size() > kMaxDecompressedBytes)
                throw RegionLoadError(RegionLoadFailureKind::Corruption,
                    "uncompressed region chunk exceeds the decode limit");
            out = std::move(raw);
        } else if (compression == 4) {
            try {
                out = lz4_block::decompress(raw.data(), raw.size(),
                                            kMaxDecompressedBytes);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception& exception) {
                throw RegionLoadError(
                    RegionLoadFailureKind::Corruption,
                    std::string("cannot decode LZ4Block region chunk: ") +
                        exception.what());
            }
        } else {
            throw RegionLoadError(RegionLoadFailureKind::Unsupported,
                                  "unknown region compression type");
        }
        if (out.empty())
            throw RegionLoadError(
                RegionLoadFailureKind::Corruption,
                "occupied region chunk decoded to an empty payload");
        return out;
    }

    struct DirectExternalTransaction final {
        enum class State : std::uint8_t { Prepared = 0, Committed = 1 };
        State state = State::Prepared;
        bool hadSidecar = false;
        std::uint8_t localX = 0;
        std::uint8_t localZ = 0;
        std::array<std::uint8_t, 8> oldHeaderRecord{};
        std::array<std::uint8_t, 8> newHeaderRecord{};
        std::uint64_t sidecarSize = 0;
        std::uint64_t sidecarHash = 0;
    };

    static constexpr std::size_t kDirectExternalManifestSize = 49;

#ifdef CPPFM_REGION_EXTERNAL_TX_TESTING
    class ExternalTransactionInterruptedForTesting final
        : public std::runtime_error {
    public:
        ExternalTransactionInterruptedForTesting()
            : std::runtime_error(
                  "injected canonical external-region interruption") {}
    };

    static std::function<bool(ExternalTransactionTestPoint)>&
    externalTransactionInterruptionForTesting() {
        static thread_local
            std::function<bool(ExternalTransactionTestPoint)> hook;
        return hook;
    }

    static void interruptExternalTransactionForTesting(
        ExternalTransactionTestPoint point) {
        auto& hook = externalTransactionInterruptionForTesting();
        if (hook && hook(point))
            throw ExternalTransactionInterruptedForTesting();
    }
#endif

    static std::uint64_t directExternalPayloadHash(
        const std::uint8_t* bytes, std::size_t size) noexcept {
        std::uint64_t hash = 14695981039346656037ULL;
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    static std::uint32_t directExternalManifestChecksum(
        const std::array<std::uint8_t, kDirectExternalManifestSize>& bytes) {
        std::uint32_t hash = 2166136261U;
        for (std::size_t i = 0; i < kDirectExternalManifestSize - 4; ++i) {
            hash ^= bytes[i];
            hash *= 16777619U;
        }
        return hash;
    }

    static std::array<std::uint8_t, kDirectExternalManifestSize>
    encodeDirectExternalManifest(
        const DirectExternalTransaction& transaction) {
        std::array<std::uint8_t, kDirectExternalManifestSize> bytes{};
        constexpr std::array<std::uint8_t, 8> magic{
            'C', 'P', 'P', 'F', 'M', 'R', 'G', '1'};
        std::copy(magic.begin(), magic.end(), bytes.begin());
        bytes[8] = 1;
        bytes[9] = static_cast<std::uint8_t>(transaction.state);
        bytes[10] = transaction.hadSidecar ? 1 : 0;
        bytes[11] = transaction.localX;
        bytes[12] = transaction.localZ;
        std::copy(transaction.oldHeaderRecord.begin(),
                  transaction.oldHeaderRecord.end(), bytes.begin() + 13);
        std::copy(transaction.newHeaderRecord.begin(),
                  transaction.newHeaderRecord.end(), bytes.begin() + 21);
        for (unsigned i = 0; i < 8; ++i) {
            bytes[29 + i] = static_cast<std::uint8_t>(
                transaction.sidecarSize >> (i * 8));
            bytes[37 + i] = static_cast<std::uint8_t>(
                transaction.sidecarHash >> (i * 8));
        }
        const auto checksum = directExternalManifestChecksum(bytes);
        for (unsigned i = 0; i < 4; ++i)
            bytes[45 + i] = static_cast<std::uint8_t>(checksum >> (i * 8));
        return bytes;
    }

    static DirectExternalTransaction decodeDirectExternalManifest(
        const std::vector<std::uint8_t>& bytes) {
        constexpr std::array<std::uint8_t, 8> magic{
            'C', 'P', 'P', 'F', 'M', 'R', 'G', '1'};
        if (bytes.size() != kDirectExternalManifestSize ||
            !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
            bytes[8] != 1 || bytes[9] > 1 || (bytes[10] & ~1U) != 0 ||
            bytes[11] >= 32 || bytes[12] >= 32)
            throw std::runtime_error(
                "invalid canonical external-sidecar transaction manifest");
        std::array<std::uint8_t, kDirectExternalManifestSize> encoded{};
        std::copy(bytes.begin(), bytes.end(), encoded.begin());
        std::uint32_t expectedChecksum = 0;
        for (unsigned i = 0; i < 4; ++i)
            expectedChecksum |=
                static_cast<std::uint32_t>(bytes[45 + i]) << (i * 8);
        if (directExternalManifestChecksum(encoded) != expectedChecksum)
            throw std::runtime_error(
                "canonical external-sidecar transaction manifest checksum mismatch");

        DirectExternalTransaction transaction;
        transaction.state = static_cast<DirectExternalTransaction::State>(
            bytes[9]);
        transaction.hadSidecar = (bytes[10] & 1U) != 0;
        transaction.localX = bytes[11];
        transaction.localZ = bytes[12];
        std::copy_n(bytes.begin() + 13, 8,
                    transaction.oldHeaderRecord.begin());
        std::copy_n(bytes.begin() + 21, 8,
                    transaction.newHeaderRecord.begin());
        for (unsigned i = 0; i < 8; ++i) {
            transaction.sidecarSize |=
                static_cast<std::uint64_t>(bytes[29 + i]) << (i * 8);
            transaction.sidecarHash |=
                static_cast<std::uint64_t>(bytes[37 + i]) << (i * 8);
        }
        if (transaction.sidecarSize == 0 ||
            transaction.sidecarSize > kMaxExternalCompressedBytes)
            throw std::runtime_error(
                "canonical external-sidecar transaction size is out of range");
        return transaction;
    }

    std::filesystem::path directExternalTransactionMarker() const {
        return std::filesystem::path(path_ + ".cppfm-direct-mcc-txn");
    }

    static bool inspectDirectTxnRegularFile(
        const std::filesystem::path& path, const char* role) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory) return false;
        if (error)
            throw std::filesystem::filesystem_error(
                std::string("inspect ") + role, path, error);
        if (status.type() == std::filesystem::file_type::not_found)
            return false;
        if (status.type() != std::filesystem::file_type::regular)
            throw std::runtime_error(std::string("refusing non-regular ") +
                                     role + ": " + path.string());
        return true;
    }

    static void removeDirectTxnFileIfPresent(
        const std::filesystem::path& path, const char* role) {
        if (!inspectDirectTxnRegularFile(path, role)) return;
        std::error_code error;
        if (!std::filesystem::remove(path, error) || error)
            throw std::filesystem::filesystem_error(
                std::string("remove ") + role, path,
                error ? error : std::make_error_code(
                                    std::errc::no_such_file_or_directory));
    }

    std::filesystem::path directExternalSidecar(
        const DirectExternalTransaction& transaction) const {
        return externalSidecarPath(transaction.localX, transaction.localZ);
    }

    static std::filesystem::path directExternalSidecarBackup(
        const std::filesystem::path& sidecar) {
        return std::filesystem::path(sidecar.string() + ".cppfm-txn-backup");
    }

    static std::filesystem::path directExternalSidecarTemporary(
        const std::filesystem::path& sidecar) {
        return std::filesystem::path(sidecar.string() + ".cppfm-txn-tmp");
    }

    DirectExternalTransaction beginDirectExternalTransaction(
        std::int32_t lx, std::int32_t lz,
        const std::vector<std::uint8_t>& oldHeader,
        const std::vector<std::uint8_t>& newHeader,
        const std::vector<std::uint8_t>& compressedSidecar) const {
        DirectExternalTransaction transaction;
        transaction.localX = static_cast<std::uint8_t>(lx);
        transaction.localZ = static_cast<std::uint8_t>(lz);
        const auto slot = index(lx, lz);
        std::copy_n(oldHeader.begin() + static_cast<std::ptrdiff_t>(slot * 4U),
                    4, transaction.oldHeaderRecord.begin());
        std::copy_n(oldHeader.begin() + static_cast<std::ptrdiff_t>(4096U + slot * 4U),
                    4, transaction.oldHeaderRecord.begin() + 4);
        std::copy_n(newHeader.begin() + static_cast<std::ptrdiff_t>(slot * 4U),
                    4, transaction.newHeaderRecord.begin());
        std::copy_n(newHeader.begin() + static_cast<std::ptrdiff_t>(4096U + slot * 4U),
                    4, transaction.newHeaderRecord.begin() + 4);
        transaction.sidecarSize = compressedSidecar.size();
        transaction.sidecarHash = directExternalPayloadHash(
            compressedSidecar.data(), compressedSidecar.size());

        const auto sidecar = externalSidecarPath(lx, lz);
        const auto backup = directExternalSidecarBackup(sidecar);
        const auto temporary = directExternalSidecarTemporary(sidecar);
        if (inspectDirectTxnRegularFile(
                backup, "reserved canonical external sidecar backup") ||
            inspectDirectTxnRegularFile(
                temporary, "reserved canonical external sidecar temporary"))
            throw std::runtime_error(
                "refusing to overwrite orphan canonical external transaction data");
        transaction.hadSidecar = inspectDirectTxnRegularFile(
            sidecar, "canonical external chunk sidecar");
        writeDirectExternalManifest(transaction, false);
        return transaction;
    }

    void writeDirectExternalManifest(
        const DirectExternalTransaction& transaction, bool replace) const {
        const auto marker = directExternalTransactionMarker();
        const auto prepared = std::filesystem::path(marker.string() + ".prepare");
        const auto bytes = encodeDirectExternalManifest(transaction);
        std::error_code error;
        if (!storage_file_ops::writeFileExclusively(
                prepared, bytes.data(), bytes.size(), error, nullptr, 0600))
            throw std::filesystem::filesystem_error(
                "write canonical external-sidecar transaction manifest",
                prepared, error);
        if (replace) {
            if (!storage_file_ops::replaceFile(prepared, marker, error))
                throw std::filesystem::filesystem_error(
                    "publish canonical external-sidecar commit decision",
                    prepared, marker, error);
        } else {
            const auto moved = storage_file_ops::moveFileWithoutReplace(
                prepared, marker, error);
            if (moved != storage_file_ops::MoveWithoutReplaceStatus::Moved)
                throw std::filesystem::filesystem_error(
                    "publish canonical external-sidecar transaction intent",
                    prepared, marker,
                    error ? error : std::make_error_code(
                                        std::errc::io_error));
        }
        if (!storage_file_ops::syncDirectory(
                std::filesystem::path(path_).parent_path()))
            throw std::filesystem::filesystem_error(
                "sync canonical external-sidecar transaction manifest",
                marker.parent_path(),
                std::make_error_code(std::errc::io_error));
    }

    std::array<std::uint8_t, 8> readDirectTxnHeaderRecord(
        const DirectExternalTransaction& transaction) const {
        storage_file_ops::RandomAccessRegularFile file;
        bool created = false;
        std::error_code error;
        if (!file.open(path_,
                       storage_file_ops::RandomAccessRegularFile::Access::ReadOnly,
                       false, created, error))
            throw std::filesystem::filesystem_error(
                "open canonical region for transaction recovery", path_, error);
        const auto slot = index(transaction.localX, transaction.localZ);
        std::array<std::uint8_t, 8> record{};
        const auto timestampOffset = std::uint64_t{4096} + slot * 4U;
        if (!file.readAt(slot * 4U, record.data(), 4, error) ||
            !file.readAt(timestampOffset, record.data() + 4, 4, error))
            throw std::filesystem::filesystem_error(
                "read canonical region transaction header", path_, error);
        if (!file.close(error))
            throw std::filesystem::filesystem_error(
                "close canonical region transaction header", path_, error);
        return record;
    }

    void restoreDirectTxnHeaderRecord(
        const DirectExternalTransaction& transaction) const {
        storage_file_ops::RandomAccessRegularFile file;
        bool created = false;
        std::error_code error;
        if (!file.open(path_,
                       storage_file_ops::RandomAccessRegularFile::Access::ReadWrite,
                       false, created, error))
            throw std::filesystem::filesystem_error(
                "open canonical region to roll back external transaction",
                path_, error);
        std::uint64_t fileSize = 0;
        if (!file.size(fileSize, error) || fileSize < 8192)
            throw std::filesystem::filesystem_error(
                "inspect canonical region during external rollback", path_,
                error ? error : std::make_error_code(std::errc::io_error));
        const auto slot = index(transaction.localX, transaction.localZ);
        const auto timestampOffset = std::uint64_t{4096} + slot * 4U;
        if (!file.writeAt(slot * 4U, transaction.oldHeaderRecord.data(), 4,
                          error) ||
            !file.writeAt(timestampOffset,
                          transaction.oldHeaderRecord.data() + 4, 4, error) ||
            !file.sync(error))
            throw std::filesystem::filesystem_error(
                "restore canonical region header after external transaction",
                path_, error);
        if (!file.close(error))
            throw std::filesystem::filesystem_error(
                "close rolled-back canonical region", path_, error);
    }

    bool directTxnNewGenerationIsValid(
        const DirectExternalTransaction& transaction,
        const std::filesystem::path& sidecar) const {
        if (readDirectTxnHeaderRecord(transaction) !=
            transaction.newHeaderRecord ||
            transaction.newHeaderRecord[3] != 1)
            return false;
        const std::uint32_t sectorOffset =
            (static_cast<std::uint32_t>(transaction.newHeaderRecord[0]) << 16) |
            (static_cast<std::uint32_t>(transaction.newHeaderRecord[1]) << 8) |
            static_cast<std::uint32_t>(transaction.newHeaderRecord[2]);
        if (sectorOffset < 2) return false;
        storage_file_ops::RandomAccessRegularFile region;
        bool created = false;
        std::error_code regionError;
        if (!region.open(
                path_,
                storage_file_ops::RandomAccessRegularFile::Access::ReadOnly,
                false, created, regionError))
            return false;
        std::uint64_t regionSize = 0;
        std::array<std::uint8_t, 5> stub{};
        const bool stubRead = region.size(regionSize, regionError) &&
            (static_cast<std::uint64_t>(sectorOffset) + 1U) * 4096U <=
                regionSize &&
            region.readAt(static_cast<std::uint64_t>(sectorOffset) * 4096U,
                          stub.data(), stub.size(), regionError);
        std::error_code closeError;
        const bool regionClosed = region.close(closeError);
        if (!stubRead || !regionClosed || stub[0] != 0 || stub[1] != 0 ||
            stub[2] != 0 || stub[3] != 1 || stub[4] != 0x82)
            return false;
        std::vector<std::uint8_t> bytes;
        std::error_code error;
        const auto status = storage_file_ops::readRegularFileBounded(
            sidecar, kMaxExternalCompressedBytes, bytes, error);
        return status == storage_file_ops::BoundedReadStatus::Read &&
               bytes.size() == transaction.sidecarSize &&
               directExternalPayloadHash(bytes.data(), bytes.size()) ==
                   transaction.sidecarHash;
    }

    void rollbackDirectExternalTransaction(
        const DirectExternalTransaction& transaction,
        const std::filesystem::path& marker,
        const std::filesystem::path& sidecar,
        const std::filesystem::path& backup,
        const std::filesystem::path& temporary) const {
        const bool backupExists = inspectDirectTxnRegularFile(
            backup, "canonical external sidecar transaction backup");
        const bool sidecarExists = inspectDirectTxnRegularFile(
            sidecar, "canonical external chunk sidecar");
        if (transaction.hadSidecar) {
            if (backupExists) {
                std::error_code error;
                if (!storage_file_ops::replaceFile(backup, sidecar, error))
                    throw std::filesystem::filesystem_error(
                        "restore canonical external sidecar", backup, sidecar,
                        error);
                if (!storage_file_ops::syncFile(sidecar))
                    throw std::filesystem::filesystem_error(
                        "sync restored canonical external sidecar", sidecar,
                        std::make_error_code(std::errc::io_error));
            } else if (!sidecarExists) {
                throw std::runtime_error(
                    "canonical external transaction lost both prior sidecar and backup: " +
                    sidecar.string());
            }
        } else {
            if (backupExists)
                throw std::runtime_error(
                    "unexpected sidecar backup for a previously absent generation");
            removeDirectTxnFileIfPresent(
                sidecar, "uncommitted canonical external sidecar");
        }
        restoreDirectTxnHeaderRecord(transaction);
        const auto parent = marker.parent_path();
        if (!storage_file_ops::syncDirectory(parent))
            throw std::filesystem::filesystem_error(
                "sync canonical external transaction rollback", parent,
                std::make_error_code(std::errc::io_error));
        removeDirectTxnFileIfPresent(
            temporary, "canonical external sidecar temporary");
        removeDirectTxnFileIfPresent(
            backup, "canonical external sidecar transaction backup");
        removeDirectTxnFileIfPresent(
            std::filesystem::path(marker.string() + ".prepare"),
            "canonical external transaction manifest preparation");
        if (!storage_file_ops::syncDirectory(parent))
            throw std::filesystem::filesystem_error(
                "sync canonical external rollback cleanup", parent,
                std::make_error_code(std::errc::io_error));
        std::error_code error;
        if (!std::filesystem::remove(marker, error) || error)
            throw std::filesystem::filesystem_error(
                "remove rolled-back canonical external transaction marker",
                marker, error ? error : std::make_error_code(
                                            std::errc::no_such_file_or_directory));
        if (!storage_file_ops::syncDirectory(parent))
            throw std::filesystem::filesystem_error(
                "sync canonical external rollback completion", parent,
                std::make_error_code(std::errc::io_error));
    }

    void recoverCanonicalExternalTransactionLocked() {
        const auto info = regionPathInfo();
        if (!info || !info->generationSuffix.empty()) return;
        const auto marker = directExternalTransactionMarker();
        const auto prepared = std::filesystem::path(marker.string() + ".prepare");
        if (!inspectDirectTxnRegularFile(
                marker, "canonical external transaction marker")) {
            removeDirectTxnFileIfPresent(
                prepared,
                "stale canonical external transaction manifest preparation");
            return;
        }

        std::vector<std::uint8_t> bytes;
        std::error_code readError;
        const auto readStatus = storage_file_ops::readRegularFileBounded(
            marker, kDirectExternalManifestSize, bytes, readError);
        if (readStatus != storage_file_ops::BoundedReadStatus::Read)
            throw std::filesystem::filesystem_error(
                "read canonical external transaction marker", marker,
                readError ? readError : std::make_error_code(
                                            std::errc::io_error));
        const auto transaction = decodeDirectExternalManifest(bytes);
        const auto sidecar = directExternalSidecar(transaction);
        const auto backup = directExternalSidecarBackup(sidecar);
        const auto temporary = directExternalSidecarTemporary(sidecar);

        if (transaction.state == DirectExternalTransaction::State::Prepared) {
            rollbackDirectExternalTransaction(transaction, marker, sidecar,
                                              backup, temporary);
            return;
        }

        if (!inspectDirectTxnRegularFile(
                sidecar, "committed canonical external sidecar") ||
            !directTxnNewGenerationIsValid(transaction, sidecar)) {
            const bool backupExists = inspectDirectTxnRegularFile(
                backup, "canonical external sidecar transaction backup");
            if (transaction.hadSidecar && !backupExists)
                throw std::runtime_error(
                    "committed canonical external generation is invalid and its prior sidecar backup is unavailable");
            rollbackDirectExternalTransaction(transaction, marker, sidecar,
                                              backup, temporary);
            return;
        }

        removeDirectTxnFileIfPresent(
            temporary, "committed canonical external sidecar temporary");
        removeDirectTxnFileIfPresent(
            backup, "committed canonical external sidecar backup");
        removeDirectTxnFileIfPresent(
            prepared,
            "committed canonical external manifest preparation");
        const auto parent = marker.parent_path();
        if (!storage_file_ops::syncDirectory(parent))
            throw std::filesystem::filesystem_error(
                "sync committed canonical external cleanup", parent,
                std::make_error_code(std::errc::io_error));
        std::error_code error;
        if (!std::filesystem::remove(marker, error) || error)
            throw std::filesystem::filesystem_error(
                "remove committed canonical external transaction marker",
                marker, error ? error : std::make_error_code(
                                            std::errc::no_such_file_or_directory));
        if (!storage_file_ops::syncDirectory(parent))
            throw std::filesystem::filesystem_error(
                "sync canonical external transaction completion", parent,
                std::make_error_code(std::errc::io_error));
    }

    struct ExternalStub final {
        std::uint32_t sectorOffset = 0;
        std::uint8_t sectorCount = 0;
        std::uint8_t compression = 0;
        std::uint32_t timestamp = 0;

        bool operator==(const ExternalStub&) const = default;
    };

    std::optional<RegionPathInfo> regionPathInfo() const {
        const auto filename = std::filesystem::path(path_).filename().string();
        const auto marker = filename.find(".mca");
        if (marker == std::string::npos) return std::nullopt;
        const std::string_view base(filename.data(), marker);
        if (!base.starts_with("r.")) return std::nullopt;
        const auto separator = base.find('.', 2);
        if (separator == std::string_view::npos) return std::nullopt;
        RegionPathInfo info;
        const auto xResult = std::from_chars(
            base.data() + 2, base.data() + separator, info.regionX);
        const auto zResult = std::from_chars(
            base.data() + separator + 1, base.data() + base.size(), info.regionZ);
        if (xResult.ec != std::errc{} ||
            xResult.ptr != base.data() + separator ||
            zResult.ec != std::errc{} || zResult.ptr != base.data() + base.size())
            return std::nullopt;
        info.generationSuffix = filename.substr(marker + 4);
        return info;
    }

    bool isStagedPeerOf(const RegionFile& canonical) const {
        const auto stagedInfo = regionPathInfo();
        const auto canonicalInfo = canonical.regionPathInfo();
        if (!stagedInfo || !canonicalInfo ||
            stagedInfo->generationSuffix != ".new" ||
            !canonicalInfo->generationSuffix.empty() ||
            stagedInfo->regionX != canonicalInfo->regionX ||
            stagedInfo->regionZ != canonicalInfo->regionZ)
            return false;
        return canonicalPathKey(path_) ==
               canonicalPathKey(canonical.path_ + ".new");
    }

    std::optional<ExternalStub> externalStub(
        std::int32_t lx, std::int32_t lz) {
        std::scoped_lock lk(pathStripeMutex(path_),
                            fileIdentityStripeMutex(path_), mtx_);
        recoverCanonicalExternalTransactionLocked();
        const auto slot = index(lx, lz);
        storage_file_ops::RandomAccessRegularFile file;
        bool created = false;
        std::error_code fileError;
        if (!file.open(path_,
                       storage_file_ops::RandomAccessRegularFile::Access::ReadOnly,
                       false, created, fileError)) {
            if (fileError == std::errc::no_such_file_or_directory)
                return std::nullopt;
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot open regular region file");
        }
        std::uint64_t fileSize = 0;
        if (!file.size(fileSize, fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot determine region file size");
        if (fileSize < 8192)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "truncated region header");

        std::uint8_t header[8192];
        if (!file.readAt(0, header, sizeof(header), fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region header");
        const auto timestampOffset = std::uint64_t{4096} + slot * 4U;
        const std::uint32_t timestamp =
            (static_cast<std::uint32_t>(header[timestampOffset]) << 24) |
            (static_cast<std::uint32_t>(header[timestampOffset + 1]) << 16) |
            (static_cast<std::uint32_t>(header[timestampOffset + 2]) << 8) |
            static_cast<std::uint32_t>(header[timestampOffset + 3]);
        const std::uint32_t offset =
            (static_cast<std::uint32_t>(header[slot * 4]) << 16) |
            (static_cast<std::uint32_t>(header[slot * 4 + 1]) << 8) |
            static_cast<std::uint32_t>(header[slot * 4 + 2]);
        const std::uint8_t sectors = header[slot * 4 + 3];
        if (offset == 0 && sectors == 0) return std::nullopt;
        if (offset == 0 || sectors == 0)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "inconsistent region chunk location");
        const std::uint64_t regionEnd =
            (static_cast<std::uint64_t>(offset) + sectors) * 4096u;
        if (offset < 2 || regionEnd > fileSize)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "region chunk points outside the file");

        const std::uint64_t byteOffset =
            static_cast<std::uint64_t>(offset) * 4096u;
        std::uint8_t lengthBytes[4];
        if (!file.readAt(byteOffset, lengthBytes, sizeof(lengthBytes),
                         fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region chunk length");
        const std::uint32_t length =
            (static_cast<std::uint32_t>(lengthBytes[0]) << 24) |
            (static_cast<std::uint32_t>(lengthBytes[1]) << 16) |
            (static_cast<std::uint32_t>(lengthBytes[2]) << 8) |
            static_cast<std::uint32_t>(lengthBytes[3]);
        if (length < 1 || length > static_cast<std::uint32_t>(sectors) *
                                       4096u - 4u ||
            length > 8u * 1024u * 1024u)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "invalid region chunk length");
        std::uint8_t compressionFlag = 0;
        if (!file.readAt(byteOffset + 4, &compressionFlag,
                         sizeof(compressionFlag), fileError))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region compression type");
        if ((compressionFlag & 0x80U) == 0) return std::nullopt;
        if (sectors != 1 || length != 1)
            throw RegionLoadError(
                RegionLoadFailureKind::Corruption,
                "external Anvil chunk has a malformed stub");
        return ExternalStub{offset, sectors,
                            static_cast<std::uint8_t>(compressionFlag & 0x7fU),
                            timestamp};
    }

    std::filesystem::path externalSidecarPath(
        std::int32_t lx, std::int32_t lz,
        std::string_view generationSuffix = {}) const {
        const auto info = regionPathInfo();
        if (!info)
            throw RegionLoadError(
                RegionLoadFailureKind::Unsupported,
                "external Anvil chunk path has no canonical region identity");
        const auto chunkX = static_cast<std::int64_t>(info->regionX) * 32 + lx;
        const auto chunkZ = static_cast<std::int64_t>(info->regionZ) * 32 + lz;
        if (chunkX < std::numeric_limits<std::int32_t>::min() ||
            chunkX > std::numeric_limits<std::int32_t>::max() ||
            chunkZ < std::numeric_limits<std::int32_t>::min() ||
            chunkZ > std::numeric_limits<std::int32_t>::max())
            throw RegionLoadError(RegionLoadFailureKind::Unsupported,
                                  "external Anvil chunk coordinates overflow");
        return std::filesystem::path(path_).parent_path() /
            ("c." + std::to_string(chunkX) + "." +
             std::to_string(chunkZ) + ".mcc" + std::string(generationSuffix));
    }

    std::vector<std::uint8_t> readExternalPayload(std::int32_t lx,
                                                  std::int32_t lz,
                                                  bool stagedGeneration = false) const {
        const auto info = regionPathInfo();
        if (!info)
            throw RegionLoadError(
                RegionLoadFailureKind::Unsupported,
                "external Anvil chunk path has no canonical r.<x>.<z>.mca identity; preserving source");
        if (!info->generationSuffix.empty() &&
            !(stagedGeneration && info->generationSuffix == ".new") &&
            !isRecoveryGenerationSuffix(info->generationSuffix))
            throw RegionLoadError(
                RegionLoadFailureKind::Unsupported,
                "external chunks in staged region generations cannot be read "
                "without a coordinated MCA/sidecar transaction; preserving source");
        const auto chunkX = static_cast<std::int64_t>(info->regionX) * 32 + lx;
        const auto chunkZ = static_cast<std::int64_t>(info->regionZ) * 32 + lz;
        if (chunkX < std::numeric_limits<std::int32_t>::min() ||
            chunkX > std::numeric_limits<std::int32_t>::max() ||
            chunkZ < std::numeric_limits<std::int32_t>::min() ||
            chunkZ > std::numeric_limits<std::int32_t>::max())
            throw RegionLoadError(RegionLoadFailureKind::Unsupported,
                                  "external Anvil chunk coordinates overflow; preserving source");

        const auto sidecarGeneration = stagedGeneration &&
                info->generationSuffix == ".new"
            ? std::string_view(".new")
            : std::string_view(info->generationSuffix);
        const auto base = externalSidecarPath(lx, lz, sidecarGeneration);
        std::vector<std::uint8_t> payload;
        std::error_code error;
        const auto status = storage_file_ops::readRegularFileBounded(
            base, kMaxExternalCompressedBytes, payload, error);
        if (status == storage_file_ops::BoundedReadStatus::Read) {
            if (payload.empty())
                throw RegionLoadError(
                    RegionLoadFailureKind::Corruption,
                    "external Anvil chunk sidecar is empty");
            return payload;
        }
        if (status == storage_file_ops::BoundedReadStatus::Missing)
            throw RegionLoadError(
                RegionLoadFailureKind::Corruption,
                "external Anvil chunk sidecar is missing for chunk " +
                    std::to_string(chunkX) + "," + std::to_string(chunkZ) +
                    "; preserving source");
        const char* reason = status ==
                storage_file_ops::BoundedReadStatus::NonRegular
            ? "is not a regular file"
            : status == storage_file_ops::BoundedReadStatus::TooLarge
                ? "exceeds the bounded decoder limit"
                : "could not be read";
        throw RegionLoadError(
            RegionLoadFailureKind::Unsupported,
            "external Anvil chunk sidecar " + base.string() + " " + reason +
                "; preserving source");
    }

    static std::size_t sectorsRequiredForCompressedSize(
        std::size_t compressedBytes) {
        if (compressedBytes > std::numeric_limits<std::uint32_t>::max() - 1u)
            throw std::length_error("compressed chunk is too large");
        // 4-byte length + compression type + compressed bytes.
        constexpr std::size_t kChunkRecordHeaderBytes = 5;
        if (compressedBytes > std::numeric_limits<std::size_t>::max() -
                                  kChunkRecordHeaderBytes - 4095u)
            throw std::length_error("compressed chunk size overflows");
        const std::size_t sectors =
            (compressedBytes + kChunkRecordHeaderBytes + 4095u) / 4096u;
        if (sectors == 0)
            throw std::length_error("compressed chunk has no Anvil sectors");
        return sectors;
    }

    static constexpr std::size_t kMaxExternalCompressedBytes =
        kMaxDecompressedBytes + 1024U * 1024U;

    static bool isRecoveryGenerationSuffix(
        std::string_view suffix) noexcept {
        std::string_view remainder;
        if (suffix.starts_with(".new.recovering"))
            remainder = suffix.substr(sizeof(".new.recovering") - 1);
        else if (suffix.starts_with(".recovering"))
            remainder = suffix.substr(sizeof(".recovering") - 1);
        else
            return false;
        if (remainder.empty()) return true;
        if (remainder.front() != '.' || remainder.size() == 1) return false;
        remainder.remove_prefix(1);
        return std::all_of(remainder.begin(), remainder.end(),
                           [](char value) {
                               return value >= '0' && value <= '9';
                           });
    }

    static std::string canonicalPathKey(const std::string& path) {
        std::error_code error;
        auto normalized = std::filesystem::weakly_canonical(path, error);
        if (error) {
            error.clear();
            normalized = std::filesystem::absolute(path, error);
        }
        std::string key = error
            ? std::filesystem::path(path).lexically_normal().generic_string()
            : normalized.lexically_normal().generic_string();
#ifdef _WIN32
        for (char& c : key)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
#endif
        return key;
    }

    static std::string fileIdentityKey(const std::string& path) {
#ifdef _WIN32
        const auto nativePath = std::filesystem::path(path);
        const HANDLE file = CreateFileW(
            nativePath.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            const BOOL read = GetFileInformationByHandle(file, &info);
            CloseHandle(file);
            if (read) {
                return "win:" + std::to_string(info.dwVolumeSerialNumber) + ":" +
                    std::to_string(info.nFileIndexHigh) + ":" +
                    std::to_string(info.nFileIndexLow);
            }
        }
#else
        struct stat info {};
        if (::stat(path.c_str(), &info) == 0)
            return "posix:" + std::to_string(static_cast<std::uint64_t>(info.st_dev)) + ":" +
                   std::to_string(static_cast<std::uint64_t>(info.st_ino));
#endif
        // The path stripe still serializes a file that does not exist yet or
        // whose native identity cannot be queried.
        return canonicalPathKey(path);
    }

    static std::mutex& stripeForKey(
        const std::string& key,
        std::array<std::mutex, 64>& stripes) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char byte : key) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return stripes[hash % stripes.size()];
    }

    static std::mutex& pathStripeMutex(const std::string& path) {
        static std::array<std::mutex, 64> stripes{};
        return stripeForKey(canonicalPathKey(path), stripes);
    }

    static std::mutex& fileIdentityStripeMutex(const std::string& path) {
        static std::array<std::mutex, 64> stripes{};
        return stripeForKey(fileIdentityKey(path), stripes);
    }

    static std::size_t index(std::int32_t lx, std::int32_t lz) {
        if (lx < 0 || lx >= 32 || lz < 0 || lz >= 32)
            throw std::out_of_range("region local coordinates must be in [0, 31]");
        return static_cast<std::size_t>(lx) + static_cast<std::size_t>(lz) * 32;
    }

    std::string path_;
    std::mutex mtx_;
};

// Canonical paths serialize symlink/path aliases; native file identity also
// serializes existing hard-link aliases. Fixed stripes bound lock storage and
// may conservatively serialize unrelated paths or files on hash collision.

} // namespace cppfm
