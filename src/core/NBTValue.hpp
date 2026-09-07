// Full NBT value tree: parse (named root) + serialize. Used by Anvil I/O.
#pragma once
#include "ByteBuffer.hpp"
#include "NBT.hpp"
#include <map>
#include <memory>
#include <limits>
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
    // Lists have an element type even when they are empty.  Keeping it as
    // metadata avoids the old magic "__elem__" compound entry, which could
    // leak into callers and be serialized as a real field.
    Tag listElement = End;

    static Value makeByte(std::int8_t v) { Value x; x.tag = Byte; x.b = v; return x; }
    static Value makeShort(std::int16_t v) { Value x; x.tag = Short; x.s = v; return x; }
    static Value makeInt(std::int32_t v) { Value x; x.tag = Int; x.i = v; return x; }
    static Value makeLong(std::int64_t v) { Value x; x.tag = Long; x.l = v; return x; }
    static Value makeFloat(float v) { Value x; x.tag = Float; x.f = v; return x; }
    static Value makeDouble(double v) { Value x; x.tag = Double; x.d = v; return x; }
    static Value makeString(std::string v) { Value x; x.tag = String; x.str = std::move(v); return x; }
    static Value makeCompound() { Value x; x.tag = Compound; return x; }
    static Value makeList(Tag elem, std::size_t reserve = 0) {
        if (!isValidTag(elem) || elem == End)
            throw std::invalid_argument("NBT lists cannot use this element type");
        Value x; x.tag = List; x.listElement = elem; x.list.reserve(reserve);
        return x;
    }
    Tag elemType() const {
        return tag == List ? listElement : End;
    }
    const Value* get(std::string_view key) const {
        if (tag != Compound) return nullptr;
        for (auto& [k, v] : comp) if (k == key) return &v;
        return nullptr;
    }
    Value* get(std::string_view key) {
        if (tag != Compound) return nullptr;
        for (auto& [k, v] : comp) if (k == key) return &v;
        return nullptr;
    }
    Value& set(std::string key, Value v) {
        if (tag != Compound) {
            tag = Compound;
            listElement = End;
            list.clear();
        }
        for (auto& [existing, value] : comp) {
            if (existing == key) {
                value = std::move(v);
                return value;
            }
        }
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
    case ByteArray:
        if (v.byteArray.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::length_error("NBT byte array is too large");
        out.i32(static_cast<std::int32_t>(v.byteArray.size()));
        out.raw(v.byteArray.data(), v.byteArray.size());
        break;
    case String:
        if (v.str.size() > std::numeric_limits<std::uint16_t>::max())
            throw std::length_error("NBT string is too long");
        out.u16(static_cast<std::uint16_t>(v.str.size()));
        out.raw(v.str.data(), v.str.size());
        break;
    case List: {
        const Tag et = v.elemType();
        if (!isValidTag(et) || et == End ||
            v.list.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::runtime_error("invalid NBT list");
        out.u8(et);
        out.i32((std::int32_t)v.list.size());
        for (auto& e : v.list) {
            if (e.tag != et) throw std::runtime_error("NBT list element type mismatch");
            writePayload(out, e);
        }
        break;
    }
    case Compound:
        for (auto& [k, c] : v.comp) {
            if (k.size() > std::numeric_limits<std::uint16_t>::max())
                throw std::length_error("NBT compound key is too long");
            if (!isValidTag(c.tag) || c.tag == End)
                throw std::runtime_error("invalid compound child tag");
            out.u8(c.tag);
            out.u16(static_cast<std::uint16_t>(k.size()));
            out.raw(k.data(), k.size());
            writePayload(out, c);
        }
        out.u8(End);
        break;
    case IntArray:
        if (v.intArray.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::length_error("NBT int array is too large");
        out.i32(static_cast<std::int32_t>(v.intArray.size()));
        for (auto x : v.intArray) out.i32(x);
        break;
    case LongArray:
        if (v.longArray.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::length_error("NBT long array is too large");
        out.i32(static_cast<std::int32_t>(v.longArray.size()));
        for (auto x : v.longArray) out.i64(x);
        break;
    default: throw std::runtime_error("cannot serialize tag");
    }
}

// Writes a named root compound: [10][name][payload]
inline void writeFileRoot(WriteBuffer& out, const Value& root, std::string_view rootName = "") {
    if (root.tag != Compound) throw std::runtime_error("NBT file root must be a compound");
    if (rootName.size() > std::numeric_limits<std::uint16_t>::max())
        throw std::length_error("NBT root name is too long");
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
        const auto nameBytes = in_.bytes(nl);
        std::string name;
        if (!nameBytes.empty())
            name.assign(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
        if (nameOut) *nameOut = name;
        Value root = payload(Compound);
        if (in_.remaining() != 0)
            throw std::runtime_error("trailing bytes after NBT root");
        return root;
    }
private:
    Value payload(Tag t, std::size_t depth = 0) {
        if (depth > kMaxNbtDepth) throw std::runtime_error("nbt nesting depth exceeded");
        Value v; v.tag = t;
        switch (t) {
        case Byte: v.b = in_.i8(); break;
        case Short: v.s = in_.i16(); break;
        case Int: v.i = in_.i32(); break;
        case Long: v.l = in_.i64(); break;
        case Float: v.f = in_.f32(); break;
        case Double: v.d = in_.f64(); break;
        case ByteArray: {
            const auto n = in_.i32();
            if (n < 0 || static_cast<std::size_t>(n) > kMaxNbtElements)
                throw std::runtime_error("bad NBT byte array length");
            v.byteArray = in_.bytes(static_cast<std::size_t>(n));
            break;
        }
        case String: {
            const auto n = in_.u16();
            const auto bytes = in_.bytes(n);
            if (!bytes.empty())
                v.str.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            break;
        }
        case List: {
            const Tag et = static_cast<Tag>(in_.u8());
            const auto n = in_.i32();
            if (!isValidTag(et) || n < 0 || static_cast<std::size_t>(n) > kMaxNbtElements ||
                (n != 0 && et == End))
                throw std::runtime_error("bad NBT list");
            v.listElement = et;
            v.list.reserve(static_cast<std::size_t>(n));
            for (std::int32_t i = 0; i < n; ++i) v.list.push_back(payload(et, depth + 1));
            break;
        }
        case Compound: {
            std::size_t fields = 0;
            for (;;) {
                const Tag et = static_cast<Tag>(in_.u8());
                if (et == End) break;
                if (!isValidTag(et)) throw std::runtime_error("bad NBT tag");
                if (++fields > kMaxNbtElements) throw std::runtime_error("too many NBT compound fields");
                const std::uint16_t nl = in_.u16();
                const auto nameBytes = in_.bytes(nl);
                std::string name;
                if (!nameBytes.empty())
                    name.assign(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
                v.comp.emplace_back(std::move(name), payload(et, depth + 1));
            }
            break;
        }
        case IntArray: {
            const auto n = in_.i32();
            if (n < 0 || static_cast<std::size_t>(n) > kMaxNbtElements)
                throw std::runtime_error("bad NBT int array length");
            v.intArray.reserve(static_cast<std::size_t>(n));
            for (std::int32_t i = 0; i < n; ++i) v.intArray.push_back(in_.i32());
            break;
        }
        case LongArray: {
            const auto n = in_.i32();
            if (n < 0 || static_cast<std::size_t>(n) > kMaxNbtElements)
                throw std::runtime_error("bad NBT long array length");
            v.longArray.reserve(static_cast<std::size_t>(n));
            for (std::int32_t i = 0; i < n; ++i) v.longArray.push_back(in_.i64());
            break;
        }
        default: throw std::runtime_error("bad nbt tag in file");
        }
        return v;
    }
    ReadBuffer& in_;
};

} // namespace cppfm::nbt
