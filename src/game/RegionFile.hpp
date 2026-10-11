// Anvil region file (.mca) reader/writer. Layout: 1024 x 4B sector offsets, 1024 x 4B epoch timestamps, then chunks: [length u32 incl.
// compression byte][compression byte=2 zlib][data].
#pragma once
#include "platform/StorageFileOps.hpp"
#include <array>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
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
        std::error_code statusError;
        const auto status =
            std::filesystem::symlink_status(path_, statusError);
        if (statusError == std::errc::no_such_file_or_directory)
            return {};
        if (statusError)
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot inspect region file");
        if (status.type() == std::filesystem::file_type::not_found)
            return {};
        if (status.type() != std::filesystem::file_type::regular)
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "region path is not a regular file");
        std::ifstream f(path_, std::ios::binary);
        if (!f) {
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot open existing region file");
        }
        f.seekg(0, std::ios::end);
        if (!f)
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot seek region file");
        const auto end = f.tellg();
        if (end < 0)
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot determine region file size");
        if (end < static_cast<std::streamoff>(8192))
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "truncated region header");
        const std::uint64_t fileSize = static_cast<std::uint64_t>(end);
        f.seekg(0, std::ios::beg);
        std::uint8_t header[8192];
        if (!f.read(reinterpret_cast<char*>(header), sizeof(header)))
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
                std::numeric_limits<std::streamoff>::max()))
            throw RegionLoadError(RegionLoadFailureKind::Unsupported,
                                  "region chunk offset is not seekable");
        f.seekg(static_cast<std::streamoff>(byteOffset));
        if (!f)
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot seek to region chunk");
        std::uint8_t lenb[4];
        if (!f.read(reinterpret_cast<char*>(lenb), 4))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region chunk length");
        const std::uint32_t total = (static_cast<std::uint32_t>(lenb[0]) << 24) |
                                    (static_cast<std::uint32_t>(lenb[1]) << 16) |
                                    (static_cast<std::uint32_t>(lenb[2]) << 8) |
                                    static_cast<std::uint32_t>(lenb[3]);
        if (total < 2 || total > static_cast<std::uint32_t>(count) * 4096u - 4u ||
            total > 8u*1024u*1024u)
            throw RegionLoadError(RegionLoadFailureKind::Corruption,
                                  "invalid region chunk length");
        std::uint8_t comp = 0;
        if (!f.read(reinterpret_cast<char*>(&comp), 1))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region compression type");
        std::vector<std::uint8_t> raw(total - 1);
        if (!f.read(reinterpret_cast<char*>(raw.data()), raw.size()))
            throw RegionLoadError(RegionLoadFailureKind::Io,
                                  "cannot read region chunk payload");
        std::vector<std::uint8_t> out;
        if (comp == 2) {
            try {
                decompressUnknown(raw.data(), raw.size(), out,
                                  kMaxDecompressedBytes);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception& exception) {
                throw RegionLoadError(
                    RegionLoadFailureKind::Corruption,
                    std::string("corrupt zlib region chunk: ") +
                        exception.what());
            }
        }
        else if (comp == 1)
            throw RegionLoadError(RegionLoadFailureKind::Unsupported,
                                  "gzip regions unsupported");
        else if (comp == 3) out = std::move(raw);                       // uncompressed
        else throw RegionLoadError(RegionLoadFailureKind::Unsupported,
                                   "unknown region compression type");
        return out;
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
        std::ifstream in(path_, std::ios::binary);
        std::vector<std::uint8_t> header(8192, 0);
        std::error_code pathError;
        const auto pathStatus =
            std::filesystem::symlink_status(path_, pathError);
        if (pathError == std::errc::no_such_file_or_directory) {
            pathError.clear();
        } else if (pathError) {
            throw std::filesystem::filesystem_error(
                "cannot inspect region before update", path_, pathError);
        } else if (pathStatus.type() == std::filesystem::file_type::symlink) {
            throw std::runtime_error(
                "refusing to update symlink region file: " + path_);
        } else if (pathStatus.type() != std::filesystem::file_type::not_found &&
                   pathStatus.type() != std::filesystem::file_type::regular) {
            throw std::runtime_error(
                "refusing to update non-regular region file: " + path_);
        }
        if (in) {
            if (!in.read(reinterpret_cast<char*>(header.data()),
                         static_cast<std::streamsize>(header.size())))
                throw std::runtime_error(
                    "cannot update a region with a truncated or unreadable header: " +
                    path_);
        } else {
            if (pathStatus.type() != std::filesystem::file_type::not_found)
                throw std::runtime_error(
                    "cannot open existing region file for update: " + path_);
            std::error_code createError;
            if (!storage_file_ops::writeFileExclusively(
                    path_, header.data(), header.size(), createError,
                    nullptr, 0666))
                throw std::filesystem::filesystem_error(
                    "cannot exclusively create region file", path_,
                    createError);
        }
        in.close();

        const std::uint32_t oldOff = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        const std::uint8_t oldCnt = header[idx*4+3];

        // payload: length(u32)=compressed bytes + compression byte, then data
        const std::uint32_t payloadLen = static_cast<std::uint32_t>(comp.size() + 1u);
        std::vector<std::uint8_t> payload{
            static_cast<std::uint8_t>(payloadLen >> 24), static_cast<std::uint8_t>(payloadLen >> 16),
            static_cast<std::uint8_t>(payloadLen >> 8),  static_cast<std::uint8_t>(payloadLen),
            2 };
        payload.insert(payload.end(), comp.begin(), comp.end());

        const auto validRange = [](std::uint32_t off, std::uint8_t count) {
            return off >= 2 && count != 0 &&
                   static_cast<std::uint64_t>(off) + count <= 0x1000000ULL;
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
        std::fstream out(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!out) {
            throw std::runtime_error(
                "cannot reopen existing region for update: " + path_);
        }
        if (!out) throw std::runtime_error("cannot open region file for writing: " + path_);

        // Ensure the payload target exists even when an old header entry is
        // reused in a truncated file.  Seeking to the last byte lets the OS
        // create a sparse file without allocating a giant temporary buffer.
        out.seekp(0, std::ios::end);
        const auto currentSize = out.tellp();
        const std::uint64_t wantBytes =
            (static_cast<std::uint64_t>(newOff) + needSectors) * 4096u;
        if (wantBytes > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max()))
            throw std::length_error("region file is not seekable");
        const auto want = static_cast<std::streamoff>(wantBytes);
        if (currentSize < 0)
            throw std::runtime_error("cannot determine region file size: " + path_);
        if (currentSize < want) {
            out.seekp(want - 1);
            out.put('\0');
        }
        if (!out) throw std::runtime_error("cannot extend region file: " + path_);

        const std::uint64_t payloadOffset = static_cast<std::uint64_t>(newOff) * 4096u;
        if (payloadOffset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max()))
            throw std::length_error("region payload offset is not seekable");
        out.seekp(static_cast<std::streamoff>(payloadOffset));
        out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        const std::size_t pad = needSectors * 4096u - payload.size();
        if (pad) {
            static const char zeros[4096] = {};
            std::size_t left = pad;
            while (left != 0) {
                const std::size_t block = std::min(left, sizeof(zeros));
                out.write(zeros, static_cast<std::streamsize>(block));
                left -= block;
            }
        }
        header[idx*4]   = static_cast<std::uint8_t>(newOff >> 16);
        header[idx*4+1] = static_cast<std::uint8_t>(newOff >> 8);
        header[idx*4+2] = static_cast<std::uint8_t>(newOff);
        header[idx*4+3] = static_cast<std::uint8_t>(needSectors);
        for (int i = 0; i < 4; ++i)
            header[4096 + idx*4 + i] = (ts >> (24 - i*8)) & 0xFF;
        out.seekp(0);
        out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        out.flush();
        if (!out) throw std::runtime_error("cannot write region file: " + path_);
    }

private:
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
