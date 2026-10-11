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
#include <limits>
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
public:
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

    // Check the same compression and 255-sector admission rule used by
    // store(), without touching a region file. Gameplay can use this before
    // committing block-entity inventory edits that would otherwise become
    // impossible to persist.
    static bool canStoreBytes(const std::vector<std::uint8_t>& nbt) noexcept {
        try {
            if (nbt.empty() || nbt.size() > kMaxDecompressedBytes) return false;
            std::vector<std::uint8_t> compressed;
            compressRaw(nbt.data(), nbt.size(), compressed);
            (void)sectorsForCompressedSize(compressed.size());
            return true;
        } catch (...) {
            return false;
        }
    }

    // Returns decompressed chunk bytes or empty if absent.
    std::vector<std::uint8_t> load(std::int32_t lx, std::int32_t lz) {
        std::scoped_lock lk(pathStripeMutex(path_), fileIdentityStripeMutex(path_), mtx_);
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
        }
        else if (compression == 1) {
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
        }
        else if (compression == 3) {
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
        // An absent slot returned above before decompression. An occupied
        // slot whose stream expands to zero bytes is malformed, not missing:
        // treating it as missing can regenerate and overwrite the save.
        if (out.empty())
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "occupied region chunk decoded to an empty payload");
        return out;
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
            return readExternalPayload(lx, lz, true);

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
        const std::size_t idx = index(lx, lz);
        if (nbt.empty()) throw std::invalid_argument("cannot store an empty chunk");
        std::vector<std::uint8_t> comp;
        compressRaw(nbt.data(), nbt.size(), comp);
        const std::size_t needSectors = sectorsForCompressedSize(comp.size());
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

        // payload: length(u32)=compressed bytes + compression byte, then data
        const std::uint32_t payloadLen = static_cast<std::uint32_t>(comp.size() + 1u);
        std::vector<std::uint8_t> payload{
            static_cast<std::uint8_t>(payloadLen >> 24), static_cast<std::uint8_t>(payloadLen >> 16),
            static_cast<std::uint8_t>(payloadLen >> 8),  static_cast<std::uint8_t>(payloadLen),
            2 };
        payload.insert(payload.end(), comp.begin(), comp.end());

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
        if (newOff == 0 || needSectors > oldCnt) {
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
        header[idx*4]   = static_cast<std::uint8_t>(newOff >> 16);
        header[idx*4+1] = static_cast<std::uint8_t>(newOff >> 8);
        header[idx*4+2] = static_cast<std::uint8_t>(newOff);
        header[idx*4+3] = static_cast<std::uint8_t>(needSectors);
        for (int i = 0; i < 4; ++i)
            header[4096 + idx*4 + i] = (ts >> (24 - i*8)) & 0xFF;
        if (!file.writeAt(0, header.data(), header.size(), fileError))
            throw std::filesystem::filesystem_error(
                "cannot update region location table", path_, fileError);
        if (createdFile && !file.sync(fileError))
            throw std::filesystem::filesystem_error(
                "cannot sync new region file", path_, fileError);
        if (!file.close(fileError))
            throw std::filesystem::filesystem_error(
                "cannot close region file", path_, fileError);
    }

private:
    struct RegionPathInfo final {
        std::int32_t regionX = 0;
        std::int32_t regionZ = 0;
        std::string generationSuffix;
    };

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
            !(stagedGeneration && info->generationSuffix == ".new"))
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

        const auto base = externalSidecarPath(
            lx, lz, stagedGeneration ? std::string_view(".new")
                                    : std::string_view{});
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

    static std::size_t sectorsForCompressedSize(std::size_t compressedBytes) {
        if (compressedBytes > std::numeric_limits<std::uint32_t>::max() - 1u)
            throw std::length_error("compressed chunk is too large");
        // 4-byte length + compression type + compressed bytes.
        constexpr std::size_t kChunkRecordHeaderBytes = 5;
        if (compressedBytes > std::numeric_limits<std::size_t>::max() -
                                  kChunkRecordHeaderBytes - 4095u)
            throw std::length_error("compressed chunk size overflows");
        const std::size_t sectors =
            (compressedBytes + kChunkRecordHeaderBytes + 4095u) / 4096u;
        if (sectors == 0 ||
            sectors > std::numeric_limits<std::uint8_t>::max())
            throw std::length_error(
                "compressed chunk does not fit in an Anvil entry");
        return sectors;
    }

    static constexpr std::size_t kMaxExternalCompressedBytes =
        kMaxDecompressedBytes + 1024U * 1024U;

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
