// Anvil region file (.mca) reader/writer.
// Layout: 1024 x 4B sector offsets, 1024 x 4B epoch timestamps,
// then chunks: [length u32 incl. compression byte][compression byte=2 zlib][data].
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
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
        std::lock_guard lk(mtx_);
        std::ifstream f(path_, std::ios::binary);
        if (!f) return {};
        std::uint8_t header[8192];
        if (!f.read(reinterpret_cast<char*>(header), sizeof(header))) return {};
        const std::size_t idx = (static_cast<std::size_t>(lx & 31)) + (lz & 31) * 32;
        const std::uint32_t off = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        const std::uint8_t count = header[idx*4+3];
        if (off == 0 || count == 0) return {};
        f.seekg(off * 4096);
        std::uint8_t lenb[4];
        if (!f.read(reinterpret_cast<char*>(lenb), 4)) return {};
        const std::uint32_t total = (lenb[0]<<24)|(lenb[1]<<16)|(lenb[2]<<8)|lenb[3];
        if (total < 2 || total > 8u*1024*1024) return {};
        std::uint8_t comp = 0;
        if (!f.read(reinterpret_cast<char*>(&comp), 1)) return {};
        std::vector<std::uint8_t> raw(total - 1);
        if (!f.read(reinterpret_cast<char*>(raw.data()), raw.size())) return {};
        std::vector<std::uint8_t> out;
        if (comp == 2) decompressUnknown(raw.data(), raw.size(), out);
        else if (comp == 1) throw std::runtime_error("gzip regions unsupported");
        else out = std::move(raw);                                      // uncompressed
        return out;
    }

    void store(std::int32_t lx, std::int32_t lz, const std::vector<std::uint8_t>& nbt) {
        std::lock_guard lk(mtx_);
        std::vector<std::uint8_t> comp;
        compressRaw(nbt.data(), nbt.size(), comp);
        std::ifstream in(path_, std::ios::binary);
        std::vector<std::uint8_t> header(8192, 0);
        bool exists = static_cast<bool>(in);
        if (exists) {
            in.read(reinterpret_cast<char*>(header.data()), header.size());
            if (!in) std::fill(header.begin(), header.end(), 0);
        }
        in.close();

        const std::size_t idx = (static_cast<std::size_t>(lx & 31)) + (lz & 31) * 32;
        const std::uint32_t oldOff = (header[idx*4] << 16) | (header[idx*4+1] << 8) | header[idx*4+2];
        const std::uint8_t oldCnt = header[idx*4+3];

        // payload: length(u32)=comp.size()+1, compression byte, data
        const std::uint32_t payloadLen = static_cast<std::uint32_t>(comp.size() + 1);
        std::vector<std::uint8_t> payload{
            static_cast<std::uint8_t>(payloadLen >> 24), static_cast<std::uint8_t>(payloadLen >> 16),
            static_cast<std::uint8_t>(payloadLen >> 8),  static_cast<std::uint8_t>(payloadLen),
            2 };
        payload.insert(payload.end(), comp.begin(), comp.end());

        const std::size_t needSectors = (payload.size() + 4095) / 4096;

        // find space: reuse old run if large enough; else append
        std::uint32_t newOff = oldOff;
        if (oldOff == 0 || needSectors > oldCnt) {
            // append at end of used region
            std::uint32_t maxEnd = 2;
            for (std::size_t i = 0; i < 1024; ++i) {
                const std::uint32_t o = (header[i*4] << 16) | (header[i*4+1] << 8) | header[i*4+2];
                const std::uint8_t c = header[i*4+3];
                if (o && c && o + c > maxEnd) maxEnd = o + c;
            }
            newOff = maxEnd;
        }
        const std::uint32_t ts = static_cast<std::uint32_t>(time(nullptr));

        std::fstream out(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!out) out = std::fstream(path_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (newOff != oldOff || !exists) {
            // ensure file size covers append target
            out.seekp(0, std::ios::end);
            const auto sz = out.tellp();
            const auto want = static_cast<std::streamoff>(newOff * 4096 + needSectors * 4096);
            if (sz < want) {
                std::vector<char> zeros(static_cast<std::size_t>(want - sz), 0);
                out.write(zeros.data(), zeros.size());
            }
        }
        out.seekp(newOff * 4096);
        out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        const std::size_t pad = needSectors * 4096 - payload.size();
        if (pad) { static const char z[4096] = {}; out.write(z, pad); }
        header[idx*4]   = static_cast<std::uint8_t>(newOff >> 16);
        header[idx*4+1] = static_cast<std::uint8_t>(newOff >> 8);
        header[idx*4+2] = static_cast<std::uint8_t>(newOff);
        header[idx*4+3] = static_cast<std::uint8_t>(needSectors);
        for (int i = 0; i < 4; ++i)
            header[4096 + idx*4 + i] = (ts >> (24 - i*8)) & 0xFF;
        out.seekp(0);
        out.write(reinterpret_cast<const char*>(header.data()), header.size());
        out.flush();
    }

private:
    std::string path_;
    std::mutex mtx_;
};

} // namespace cppfm
