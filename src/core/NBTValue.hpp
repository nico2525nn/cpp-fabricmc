// Full NBT value tree: parse (named root) + serialize. Used by Anvil I/O.
#pragma once
#include "ByteBuffer.hpp"
#include "NBT.hpp"
#include <map>
#include <memory>
#include <stdexcept>

namespace cppfm::nbt {

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct Value {
    Tag tag = End;
    std::int8_t b = 0;
    std::int16_t s = 0;
    std::int32_t i = 0;
    std::int64_t l = 0;
    float f = 0;
    double d = 0;
    std::string str;
    std::vector<std::uint8_t> byteArray;
    std::vector<std::int32_t> intArray;
    std::vector<std::int64_t> longArray;
    std::vector<Value> list;
    std::vector<std::pair<std::string, Value>> comp;

    static Value makeByte(std::int8_t v) { Value x; x.tag = Byte; x.b = v; return x; }
    static Value makeShort(std::int16_t v) { Value x; x.tag = Short; x.s = v; return x; }
    static Value makeInt(std::int32_t v) { Value x; x.tag = Int; x.i = v; return x; }
    static Value makeLong(std::int64_t v) { Value x; x.tag = Long; x.l = v; return x; }
    static Value makeString(std::string v) { Value x; x.tag = String; x.str = std::move(v); return x; }
    static Value makeCompound() { Value x; x.tag = Compound; return x; }
    static Value makeList(Tag elem, std::size_t reserve = 0) {
        Value x; x.tag = List; x.list.reserve(reserve);
        x.comp.push_back({"__elem__", [&]{ Value e; e.tag = elem; return e; }()});
        return x;
    }
    Tag elemType() const {
        for (auto& [k, v] : comp) if (k == "__elem__") return v.tag;
        return End;
    }
    const Value* get(std::string_view key) const {
        if (tag != Compound) return nullptr;
        for (auto& [k, v] : comp) if (k == key) return &v;
        return nullptr;
    }
    Value& set(std::string key, Value v) {
        comp.emplace_back(std::move(key), std::move(v));
        return comp.back().second;
    }
};

inline void writePayload(WriteBuffer& out, const Value& v) {
    switch (v.tag) {
    case Byte: out.i8(v.b); break;
    case Short: out.i16(v.s); break;
    case Int: out.i32(v.i); break;
    case Long: out.i64(v.l); break;
    case Float: out.f32(v.f); break;
    case Double: out.f64(v.d); break;
    case ByteArray: out.i32((std::int32_t)v.byteArray.size()); out.raw(v.byteArray.data(), v.byteArray.size()); break;
    case String: out.u16((std::uint16_t)v.str.size()); out.raw(v.str.data(), v.str.size()); break;
    case List: {
        const Tag et = v.elemType();
        out.u8(et);
        out.i32((std::int32_t)v.list.size());
        for (auto& e : v.list) writePayload(out, e);
        break;
    }
    case Compound:
        for (auto& [k, c] : v.comp) {
            out.u8(c.tag);
            out.u16((std::uint16_t)k.size());
            out.raw(k.data(), k.size());
            writePayload(out, c);
        }
        out.u8(End);
        break;
    case IntArray: out.i32((std::int32_t)v.intArray.size()); for (auto x : v.intArray) out.i32(x); break;
    case LongArray: out.i32((std::int32_t)v.longArray.size()); for (auto x : v.longArray) out.i64(x); break;
    default: throw std::runtime_error("cannot serialize tag");
    }
}

// Writes a named root compound: [10][name][payload]
inline void writeFileRoot(WriteBuffer& out, const Value& root, std::string_view rootName = "") {
    out.u8(Compound);
    out.u16((std::uint16_t)rootName.size());
    out.raw(rootName.data(), rootName.size());
    writePayload(out, root);
}

class Parser {
public:
    explicit Parser(ReadBuffer& in) : in_(in) {}
    // Reads a NAMED root (file style): type + name + payload
    Value readFileRoot(std::string* nameOut = nullptr) {
        const Tag t = static_cast<Tag>(in_.u8());
        if (t != Compound) throw std::runtime_error("file root not compound");
        const std::uint16_t nl = in_.u16();
        std::string name(reinterpret_cast<const char*>(in_.p + in_.off), nl);
        in_.off += nl;
        if (nameOut) *nameOut = name;
        return payload(Compound);
    }
private:
    Value payload(Tag t) {
        Value v; v.tag = t;
        switch (t) {
        case Byte: v.b = in_.i8(); break;
        case Short: v.s = in_.i16(); break;
        case Int: v.i = in_.i32(); break;
        case Long: v.l = in_.i64(); break;
        case Float: v.f = in_.f32(); break;
        case Double: v.d = in_.f64(); break;
        case ByteArray: { auto n = in_.i32(); v.byteArray = in_.bytes((std::size_t)n); break; }
        case String: { auto n = in_.u16(); v.str.assign(reinterpret_cast<const char*>(in_.p + in_.off), n); in_.off += n; break; }
        case List: {
            const Tag et = static_cast<Tag>(in_.u8());
            auto n = in_.i32();
            for (std::int32_t i = 0; i < n; ++i) v.list.push_back(payload(et));
            break;
        }
        case Compound: {
            for (;;) {
                const Tag et = static_cast<Tag>(in_.u8());
                if (et == End) break;
                const std::uint16_t nl = in_.u16();
                std::string name(reinterpret_cast<const char*>(in_.p + in_.off), nl);
                in_.off += nl;
                v.comp.emplace_back(std::move(name), payload(et));
            }
            break;
        }
        case IntArray: { auto n = in_.i32(); for (int32_t i=0;i<n;++i) v.intArray.push_back(in_.i32()); break; }
        case LongArray: { auto n = in_.i32(); for (int32_t i=0;i<n;++i) v.longArray.push_back(in_.i64()); break; }
        default: throw std::runtime_error("bad nbt tag in file");
        }
        return v;
    }
    ReadBuffer& in_;
};

} // namespace cppfm::nbt
