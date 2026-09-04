// ByteBuffer: read/write primitives for Minecraft Java 1.21.4 protocol 769. Clean-room implementation based on publicly documented wire
// 26-12-26 pack per wiki.vg/NBT. Strict overflow checks: varint >5 bytes / varlong >10 bytes throws.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <optional>

namespace cppfm {

class WriteBuffer {
public:
    std::vector<std::uint8_t> data;

    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        data.insert(data.end(), b, b + n);
    }
    void u8(std::uint8_t v) { data.push_back(v); }
    void i8(std::int8_t v) { u8(static_cast<std::uint8_t>(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }

    void u16(std::uint16_t v) {
        data.push_back(static_cast<std::uint8_t>(v >> 8));
        data.push_back(static_cast<std::uint8_t>(v));
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }

    void u32(std::uint32_t v) {
        data.push_back(static_cast<std::uint8_t>(v >> 24));
        data.push_back(static_cast<std::uint8_t>(v >> 16));
        data.push_back(static_cast<std::uint8_t>(v >> 8));
        data.push_back(static_cast<std::uint8_t>(v));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f32(float v) { std::uint32_t u; std::memcpy(&u, &v, 4); u32(u); }

    void u64(std::uint64_t v) {
        for (int s = 56; s >= 0; s -= 8)
            data.push_back(static_cast<std::uint8_t>(v >> s));
    }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void f64(double v) { std::uint64_t u; std::memcpy(&u, &v, 8); u64(u); }

    static void writeVarintTo(std::vector<std::uint8_t>& out, std::int32_t value) {
        std::uint32_t v = static_cast<std::uint32_t>(value);
        while (true) {
            if ((v & ~0x7Fu) == 0) { out.push_back(static_cast<std::uint8_t>(v)); return; }
            out.push_back(static_cast<std::uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
    }
    static void writeVarlongTo(std::vector<std::uint8_t>& out, std::int64_t value) {
        std::uint64_t v = static_cast<std::uint64_t>(value);
        while (true) {
            if ((v & ~0x7FULL) == 0) { out.push_back(static_cast<std::uint8_t>(v)); return; }
            out.push_back(static_cast<std::uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
    }
    void varint(std::int32_t v) { writeVarintTo(data, v); }
    void varlong(std::int64_t v) { writeVarlongTo(data, v); }

    void bytes(std::initializer_list<std::uint8_t> v) { data.insert(data.end(), v); }

    void string(std::string_view s) {
        varint(static_cast<std::int32_t>(s.size()));
        raw(s.data(), s.size());
    }
    // Minecraft "position": x:26 | z:26 | y:12 signed two's complement
    void position(std::int32_t x, std::int32_t y, std::int32_t z) {
        std::uint64_t v = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) & 0x3FFFFFFULL) << 38
                        | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) & 0x3FFFFFFULL) << 12
                        | (static_cast<std::uint64_t>(y) & 0xFFFULL);
        u64(v);
    }
    void uuid(const std::uint8_t bytes[16]) { raw(bytes, 16); }
    void uuid(std::string_view hexNoDashes);

    std::size_t size() const { return data.size(); }
};

class ReadBuffer {
public:
    const std::uint8_t* p;
    std::size_t len;
    std::size_t off = 0;

    ReadBuffer(const std::uint8_t* d, std::size_t n) : p(d), len(n) {}
    explicit ReadBuffer(const std::vector<std::uint8_t>& v) : p(v.data()), len(v.size()) {}

    std::size_t remaining() const { return off <= len ? len - off : 0; }
    void need(std::size_t n) const {
        if (remaining() < n) throw std::runtime_error("buffer underrun (need " + std::to_string(n) + ", have " + std::to_string(remaining()) + ")");
    }
    std::uint8_t u8() { need(1); return p[off++]; }
    bool boolean() { return u8() != 0; }
    std::int8_t i8() { return static_cast<std::int8_t>(u8()); }
    std::uint16_t u16() {
        need(2);
        const std::uint16_t v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[off]) << 8) | p[off + 1]);
        off += 2; return v;
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = (static_cast<std::uint32_t>(p[off]) << 24) | (static_cast<std::uint32_t>(p[off+1]) << 16)
                        | (static_cast<std::uint32_t>(p[off+2]) << 8) | p[off+3];
        off += 4; return v;
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32() { std::uint32_t u = u32(); float f; std::memcpy(&f, &u, 4); return f; }
    std::uint64_t u64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[off + i];
        off += 8; return v;
    }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    double f64() { std::uint64_t u = u64(); double f; std::memcpy(&f, &u, 8); return f; }

    std::int32_t varint() {
        std::uint32_t result = 0;
        int shift = 0;
        while (true) {
            std::uint8_t b = u8();
            result |= static_cast<std::uint32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return static_cast<std::int32_t>(result);
            shift += 7;
            if (shift >= 35) throw std::runtime_error("varint too large");
        }
    }
    std::int64_t varlong() {
        std::uint64_t result = 0;
        int shift = 0;
        while (true) {
            std::uint8_t b = u8();
            result |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return static_cast<std::int64_t>(result);
            shift += 7;
            if (shift >= 70) throw std::runtime_error("varlong too large");
        }
    }
    std::string string(std::size_t maxLen = 262144) {
        std::int32_t n = varint();
        if (n < 0 || static_cast<std::size_t>(n) > maxLen) throw std::runtime_error("string length out of range");
        need(static_cast<std::size_t>(n));
        std::string s(reinterpret_cast<const char*>(p + off), static_cast<std::size_t>(n));
        off += static_cast<std::size_t>(n);
        return s;
    }
    void position(std::int32_t& x, std::int32_t& y, std::int32_t& z) {
        const std::int64_t v = static_cast<std::int64_t>(u64());   // arithmetic shifts
        x = static_cast<std::int32_t>(v >> 38);
        y = static_cast<std::int32_t>((v << 52) >> 52);
        z = static_cast<std::int32_t>((v << 26) >> 38);
    }
    std::vector<std::uint8_t> bytes(std::size_t n) {
        need(n);
        std::vector<std::uint8_t> v(p + off, p + off + n);
        off += n;
        return v;
    }
    std::size_t skipRest() { std::size_t r = remaining(); off = len; return r; }
};

} // namespace cppfm
