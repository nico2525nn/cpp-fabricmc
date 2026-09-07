// Minimal NBT support: network-style (unnamed root) writer + a read/skip walker.
// Needed for: text components, heightmaps, registry entry walking, chunk block entities.
#pragma once
#include "ByteBuffer.hpp"
#include <map>
#include <functional>

namespace cppfm::nbt {

enum Tag : std::uint8_t {
    End = 0, Byte = 1, Short = 2, Int = 3, Long = 4,
    Float = 5, Double = 6, ByteArray = 7, String = 8,
    List = 9, Compound = 10, IntArray = 11, LongArray = 12
};

inline constexpr bool isValidTag(Tag tag) {
    return static_cast<std::uint8_t>(tag) <= LongArray;
}

// NBT is frequently read from untrusted network or region-file input.  These
// bounds are deliberately centralized so the tree parser and the lightweight
// walker cannot accidentally grow different denial-of-service limits.
inline constexpr std::size_t kMaxNbtDepth = 512;
inline constexpr std::size_t kMaxNbtElements = 1'000'000;

// ------------------------------------------------------------------ writer
class Writer {
public:
    explicit Writer(WriteBuffer& out) : out_(out) {}

    void rootCompound() { out_.u8(Compound); } // network NBT: no root name

    void namedByte(std::string_view name, std::int8_t v) { key(name, Byte); out_.i8(v); }
    void namedShort(std::string_view name, std::int16_t v) { key(name, Short); out_.i16(v); }
    void namedInt(std::string_view name, std::int32_t v) { key(name, Int); out_.i32(v); }
    void namedLong(std::string_view name, std::int64_t v) { key(name, Long); out_.i64(v); }
    void namedFloat(std::string_view name, float v) { key(name, Float); out_.f32(v); }
    void namedDouble(std::string_view name, double v) { key(name, Double); out_.f64(v); }
    void namedString(std::string_view name, std::string_view v) { key(name, String); str(v); }
    void namedLongArray(std::string_view name, const std::vector<std::int64_t>& v) {
        key(name, LongArray);
        if (v.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::length_error("NBT long array is too large");
        out_.i32(static_cast<std::int32_t>(v.size()));
        for (auto x : v) out_.i64(x);
    }
    void beginList(std::string_view name, Tag elemType, std::int32_t count) {
        if (!isValidTag(elemType) || elemType == End || count < 0 ||
            static_cast<std::size_t>(count) > kMaxNbtElements)
            throw std::invalid_argument("invalid NBT list");
        key(name, List);
        out_.u8(elemType);
        out_.i32(count);
    }
    void beginCompound(std::string_view name) { key(name, Compound); }
    void endCompound() { out_.u8(End); }

    // bare (unnamed) helpers used inside lists
    void bareString(std::string_view v) { str(v); }
    void bareCompound() {} // compound payload has no marker itself
    void bareEnd() { out_.u8(End); }

private:
    void key(std::string_view name, Tag t) {
        out_.u8(t);
        str(name);
    }
    void str(std::string_view s) {
        if (s.size() > std::numeric_limits<std::uint16_t>::max())
            throw std::length_error("NBT string is too long");
        out_.u16(static_cast<std::uint16_t>(s.size()));
        out_.raw(s.data(), s.size());
    }
    WriteBuffer& out_;
};

// Convenience builders for chat components ---------------------------------
inline void writeTextComponent(WriteBuffer& out, std::string_view text) {
    nbt::Writer w(out);
    w.rootCompound();
    w.namedString("text", text);
    w.endCompound();
}

// ------------------------------------------------------------- reader/walker
class Reader {
public:
    Reader(ReadBuffer& in) : in_(in) {}

    // Skips an anonymous-root NBT payload; returns offset after it.
    void skipRoot() {
        Tag t = static_cast<Tag>(in_.u8());
        if (t == End) return;
        skipPayload(t); // root is unnamed on the network
    }
    // Reads entries of the anonymous root compound: calls cb(name) per child.
    void walkRoot(const std::function<void(const std::string&)>& cb) {
        Tag t = static_cast<Tag>(in_.u8());
        if (t != Compound) throw std::runtime_error("root nbt not compound");
        while (true) {
            Tag et = static_cast<Tag>(in_.u8());
            if (et == End) return;
            if (!isValidTag(et)) throw std::runtime_error("bad nbt tag");
            std::uint16_t n = in_.u16();
            const auto nameBytes = in_.bytes(n);
            std::string name(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
            cb(name);
            skipPayload(et);
        }
    }
    // Reads a full value into a generic tree (bounded).
    struct Value;
    static Value readValue(ReadBuffer& in, Tag t, std::size_t depth = 0);

    struct Value {
        Tag tag{};
        std::int8_t b{}; std::int16_t s{}; std::int32_t i{}; std::int64_t l{};
        float f{}; double d{};
        std::string str;
        std::vector<Value> list;
        std::vector<std::pair<std::string, Value>> comp;
        const Value* get(std::string_view k) const {
            for (auto& [n, v] : comp) if (n == k) return &v;
            return nullptr;
        }
    };
    Value readNamedValue();

private:
    void skipPayload(Tag t, std::size_t depth = 0);

    ReadBuffer& in_;
};

} // namespace cppfm::nbt
