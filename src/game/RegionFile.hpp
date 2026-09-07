// Anvil region file (.mca) reader/writer. Layout: 1024 x 4B sector offsets, 1024 x 4B epoch timestamps, then chunks: [length u32 incl.
// compression byte][compression byte=2 zlib][data].
#pragma once
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <vector>
#include "../core/Zlib.hpp"

namespace cppfm {

class RegionFile {
public:
    explicit RegionFile(std::string path) : path_(std::move(path)) {}

    // Returns decompressed chunk bytes or empty if absent.
    std::vector<std::uint8_t> load(std::int32_t lx, std::int32_t lz) {
        std::scoped_lock lk(globalMutex(), mtx_);
        const std::size_t idx = index(lx, lz);
        std::ifstream f(path_, std::ios::binary);
        if (!f) return {};
        f.seekg(0, std::ios::end);
        const auto end = f.tellg();
        if (end < static_cast<std::streamoff>(8192)) return {};
        const std::uint64_t fileSize = static_cast<std::uint64_t>(end);
        f.seekg(0, std::ios::beg);
        std::uint8_t header[8192];
        if (!f.read(reinterpret_cast<char*>(header), sizeof(header))) return {};
        const std::uint32_t off = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        const std::uint8_t count = header[idx*4+3];
        if (off == 0 || count == 0) return {};
        const std::uint64_t regionEnd = (static_cast<std::uint64_t>(off) + count) * 4096u;
        if (off < 2 || regionEnd > fileSize)
            throw std::runtime_error("region chunk points outside the file");
        f.seekg(off * 4096);
        std::uint8_t lenb[4];
        if (!f.read(reinterpret_cast<char*>(lenb), 4)) return {};
        const std::uint32_t total = (lenb[0]<<24)|(lenb[1]<<16)|(lenb[2]<<8)|lenb[3];
        if (total < 2 || total > static_cast<std::uint32_t>(count) * 4096u - 4u ||
            total > 8u*1024u*1024u)
            throw std::runtime_error("invalid region chunk length");
        std::uint8_t comp = 0;
        if (!f.read(reinterpret_cast<char*>(&comp), 1)) return {};
        std::vector<std::uint8_t> raw(total - 1);
        if (!f.read(reinterpret_cast<char*>(raw.data()), raw.size())) return {};
        std::vector<std::uint8_t> out;
        if (comp == 2) decompressUnknown(raw.data(), raw.size(), out, kMaxDecompressedBytes);
        else if (comp == 1) throw std::runtime_error("gzip regions unsupported");
        else if (comp == 3) out = std::move(raw);                       // uncompressed
        else throw std::runtime_error("unknown region compression type");
        return out;
    }

    void store(std::int32_t lx, std::int32_t lz, const std::vector<std::uint8_t>& nbt) {
        std::scoped_lock lk(globalMutex(), mtx_);
        const std::size_t idx = index(lx, lz);
        if (nbt.empty()) throw std::invalid_argument("cannot store an empty chunk");
        std::vector<std::uint8_t> comp;
        compressRaw(nbt.data(), nbt.size(), comp);
        if (comp.size() > std::numeric_limits<std::uint32_t>::max() - 1u)
            throw std::length_error("compressed chunk is too large");
        std::ifstream in(path_, std::ios::binary);
        std::vector<std::uint8_t> header(8192, 0);
        const bool exists = static_cast<bool>(in);
        if (exists && in.read(reinterpret_cast<char*>(header.data()), header.size())) {
            // header loaded
        } else if (exists) {
            // A truncated region is recoverable: rebuild its empty header.
            std::fill(header.begin(), header.end(), 0);
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

        const std::size_t needSectors = (payload.size() + 4095u) / 4096u;
        if (needSectors == 0 || needSectors > std::numeric_limits<std::uint8_t>::max())
            throw std::length_error("compressed chunk does not fit in an Anvil entry");

        const auto validRange = [](std::uint32_t off, std::uint8_t count) {
            return off >= 2 && count != 0 &&
                   static_cast<std::uint64_t>(off) + count <= 0x1000000ULL;
        };
        const bool oldValid = validRange(oldOff, oldCnt);

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
        const auto parent = std::filesystem::path(path_).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);

        std::fstream out(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!out) out.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open region file for writing: " + path_);

        // Ensure the payload target exists even when an old header entry is
        // reused in a truncated file.  Seeking to the last byte lets the OS
        // create a sparse file without allocating a giant temporary buffer.
        out.seekp(0, std::ios::end);
        const auto currentSize = out.tellp();
        const auto want = static_cast<std::streamoff>(
            (static_cast<std::uint64_t>(newOff) + needSectors) * 4096u);
        if (currentSize < want) {
            out.seekp(want - 1);
            out.put('\0');
        }
        if (!out) throw std::runtime_error("cannot extend region file: " + path_);

        out.seekp(static_cast<std::streamoff>(newOff) * 4096);
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
    static std::mutex& globalMutex() {
        static std::mutex mutex;
        return mutex;
    }
    static std::size_t index(std::int32_t lx, std::int32_t lz) {
        if (lx < 0 || lx >= 32 || lz < 0 || lz >= 32)
            throw std::out_of_range("region local coordinates must be in [0, 31]");
        return static_cast<std::size_t>(lx) + static_cast<std::size_t>(lz) * 32;
    }

    std::string path_;
    std::mutex mtx_;
};

/*
 * The implementation above deliberately owns the complete read/write
 * transaction.  Keeping the old per-instance mutex in addition to the
 * process-wide guard protects callers that construct more than one RegionFile
 * for the same path (the persistence and async I/O paths do exactly that).
 */

} // namespace cppfm
