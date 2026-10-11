// Full NBT value tree: parse (named root) + serialize. Used by Anvil I/O.
#pragma once
#include "ByteBuffer.hpp"
#include "NBT.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
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
        // TAG_End is legal as the marker of an empty NBT list.  It is not a
        // legal marker once an element exists, which is enforced by the
        // serializer below because the list storage is intentionally public
        // for the existing data-building API.
        if (!isValidTag(elem))
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
            str.clear();
            byteArray.clear();
            intArray.clear();
            longArray.clear();
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

namespace detail {
inline bool sameNbtFloat(float expected, float actual) noexcept {
    if (std::isnan(expected) && std::isnan(actual)) return true;
    return std::bit_cast<std::uint32_t>(expected) ==
           std::bit_cast<std::uint32_t>(actual);
}

inline bool sameNbtDouble(double expected, double actual) noexcept {
    if (std::isnan(expected) && std::isnan(actual)) return true;
    return std::bit_cast<std::uint64_t>(expected) ==
           std::bit_cast<std::uint64_t>(actual);
}

inline bool equalNbtValue(const Value& left, const Value& right) {
    if (left.tag != right.tag) return false;
    switch (left.tag) {
    case End: return true;
    case Byte: return left.b == right.b;
    case Short: return left.s == right.s;
    case Int: return left.i == right.i;
    case Long: return left.l == right.l;
    case Float: return sameNbtFloat(left.f, right.f);
    case Double: return sameNbtDouble(left.d, right.d);
    case ByteArray: return left.byteArray == right.byteArray;
    case String: return left.str == right.str;
    case List:
        if (left.listElement != right.listElement ||
            left.list.size() != right.list.size()) return false;
        for (std::size_t i = 0; i < left.list.size(); ++i)
            if (!equalNbtValue(left.list[i], right.list[i])) return false;
        return true;
    case Compound:
        if (left.comp.size() != right.comp.size()) return false;
        for (const auto& [key, value] : left.comp) {
            const auto* other = right.get(key);
            if (!other || !equalNbtValue(value, *other)) return false;
        }
        return true;
    case IntArray: return left.intArray == right.intArray;
    case LongArray: return left.longArray == right.longArray;
    default: return false;
    }
}
} // namespace detail

// Mirrors NbtHelper.matches(standard, subject, ignoreListOrder): compounds
// in `expected` are subsets of `actual`, while primitive values and arrays
// must match exactly. Order-insensitive lists use vanilla's membership rule
// (duplicate expected values do not require duplicate actual values).
inline bool matchesSubset(const Value& expected, const Value& actual,
                          bool ignoreListOrder = true) {
    if (expected.tag != actual.tag) return false;
    switch (expected.tag) {
    case End:
        return true;
    case Byte:
        return expected.b == actual.b;
    case Short:
        return expected.s == actual.s;
    case Int:
        return expected.i == actual.i;
    case Long:
        return expected.l == actual.l;
    case Float:
        return detail::sameNbtFloat(expected.f, actual.f);
    case Double:
        return detail::sameNbtDouble(expected.d, actual.d);
    case ByteArray:
        return expected.byteArray == actual.byteArray;
    case String:
        return expected.str == actual.str;
    case List:
        if (!ignoreListOrder) {
            if (expected.listElement != actual.listElement ||
                expected.list.size() != actual.list.size()) return false;
            for (std::size_t i = 0; i < expected.list.size(); ++i)
                if (!detail::equalNbtValue(expected.list[i], actual.list[i]))
                    return false;
            return true;
        }
        if (expected.list.empty()) return actual.list.empty();
        for (const auto& expectedElement : expected.list) {
            const auto found = std::find_if(
                actual.list.begin(), actual.list.end(),
                [&](const Value& actualElement) {
                    return matchesSubset(expectedElement, actualElement, true);
                });
            if (found == actual.list.end()) return false;
        }
        return true;
    case Compound:
        for (const auto& [key, expectedValue] : expected.comp) {
            const auto* actualValue = actual.get(key);
            if (!actualValue ||
                !matchesSubset(expectedValue, *actualValue, ignoreListOrder))
                return false;
        }
        return true;
    case IntArray:
        return expected.intArray == actual.intArray;
    case LongArray:
        return expected.longArray == actual.longArray;
    default:
        return false;
    }
}

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
        if (!isValidTag(et) || (et == End && !v.list.empty()) ||
            v.list.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::runtime_error("invalid NBT list");
        out.u8(et);
        out.i32(static_cast<std::int32_t>(v.list.size()));
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
    out.u16(static_cast<std::uint16_t>(rootName.size()));
    out.raw(rootName.data(), rootName.size());
    writePayload(out, root);
}

class Parser {
public:
    static constexpr std::size_t kDefaultAllocationBudget = 64u * 1024u * 1024u;

    explicit Parser(ReadBuffer& in,
                    std::size_t allocationBudget = kDefaultAllocationBudget)
        : in_(in), allocationBudget_(allocationBudget) {}
    // Reads one unnamed root value as used by the network protocol. Unlike
    // file-style NBT, an anonymous root may be any non-End tag, not only a
    // compound. The caller owns framing and may continue reading afterward.
    Value readAnonymousRoot() {
        const Tag tag = static_cast<Tag>(in_.u8());
        if (!isValidTag(tag) || tag == End)
            throw std::runtime_error("invalid anonymous NBT root tag");
        return payload(tag);
    }

    // Reads a NAMED root (file style): type + name + payload
    Value readFileRoot(std::string* nameOut = nullptr) {
        const Tag t = static_cast<Tag>(in_.u8());
        if (t != Compound) throw std::runtime_error("file root not compound");
        const std::uint16_t nl = in_.u16();
        std::string name;
        if (nameOut && nl != 0) {
            chargeAllocation(static_cast<std::size_t>(nl) + 1);
            in_.need(nl);
            name.assign(reinterpret_cast<const char*>(in_.p + in_.off), nl);
            in_.skip(nl);
            *nameOut = std::move(name);
        } else {
            in_.skip(nl);
            if (nameOut) nameOut->clear();
        }
        Value root = payload(Compound);
        if (in_.remaining() != 0)
            throw std::runtime_error("trailing bytes after NBT root");
        return root;
    }

    // Validate a serialized file-style NBT root using the same structural,
    // depth, element-count, and cumulative allocation rules as readFileRoot,
    // but without building the Value tree. Persistence uses this before
    // committing bytes so it cannot write a record that its own loader must
    // reject at the allocation budget.
    void validateFileRoot() {
        const Tag tag = static_cast<Tag>(in_.u8());
        if (tag != Compound) throw std::runtime_error("file root not compound");
        const auto rootNameLength = in_.u16();
        in_.skip(rootNameLength);
        validatePayload(Compound);
        if (in_.remaining() != 0)
            throw std::runtime_error("trailing bytes after NBT root");
    }
private:
    void chargeAllocation(std::size_t amount) {
        if (usedAllocation_ > allocationBudget_ ||
            amount > allocationBudget_ - usedAllocation_)
            throw std::length_error("NBT allocation budget exceeded");
        usedAllocation_ += amount;
    }

    void validatePayload(Tag tag, std::size_t depth = 0) {
        if (depth > kMaxNbtDepth)
            throw std::runtime_error("nbt nesting depth exceeded");
        switch (tag) {
        case Byte: (void)in_.i8(); break;
        case Short: (void)in_.i16(); break;
        case Int: (void)in_.i32(); break;
        case Long: (void)in_.i64(); break;
        case Float: (void)in_.f32(); break;
        case Double: (void)in_.f64(); break;
        case ByteArray: {
            const auto length = in_.i32();
            if (length < 0 ||
                static_cast<std::size_t>(length) > kMaxNbtElements)
                throw std::runtime_error("bad NBT byte array length");
            const auto count = static_cast<std::size_t>(length);
            chargeAllocation(count);
            in_.skip(count);
            break;
        }
        case String: {
            const auto length = in_.u16();
            if (length != 0) chargeAllocation(
                static_cast<std::size_t>(length) + 1);
            in_.skip(length);
            break;
        }
        case List: {
            const Tag element = static_cast<Tag>(in_.u8());
            const auto length = in_.i32();
            if (!isValidTag(element) || length < 0 ||
                static_cast<std::size_t>(length) > kMaxNbtElements ||
                (length != 0 && element == End))
                throw std::runtime_error("bad NBT list");
            const auto count = static_cast<std::size_t>(length);
            if (count > std::numeric_limits<std::size_t>::max() /
                            sizeof(Value))
                throw std::length_error("NBT list allocation size overflow");
            chargeAllocation(count * sizeof(Value));
            for (std::int32_t index = 0; index < length; ++index)
                validatePayload(element, depth + 1);
            break;
        }
        case Compound: {
            using CompoundEntry = std::pair<std::string, Value>;
            std::size_t fields = 0;
            std::size_t capacity = 0;
            for (;;) {
                const Tag element = static_cast<Tag>(in_.u8());
                if (element == End) break;
                if (!isValidTag(element))
                    throw std::runtime_error("bad NBT tag");
                if (++fields > kMaxNbtElements)
                    throw std::runtime_error("too many NBT compound fields");
                const auto nameLength = in_.u16();
                if (fields - 1 == capacity) {
                    const std::vector<CompoundEntry> emptyCompound;
                    const auto maximumCapacity = emptyCompound.max_size();
                    const auto nextCapacity = capacity == 0
                        ? std::size_t{1}
                        : (capacity > maximumCapacity / 2
                               ? maximumCapacity : capacity * 2);
                    if (nextCapacity <= capacity)
                        throw std::length_error(
                            "NBT compound allocation size overflow");
                    chargeAllocation(nextCapacity * sizeof(CompoundEntry));
                    capacity = nextCapacity;
                }
                if (nameLength != 0)
                    chargeAllocation(static_cast<std::size_t>(nameLength) + 1);
                in_.skip(nameLength);
                validatePayload(element, depth + 1);
            }
            break;
        }
        case IntArray:
        case LongArray: {
            const auto length = in_.i32();
            if (length < 0 ||
                static_cast<std::size_t>(length) > kMaxNbtElements)
                throw std::runtime_error(tag == IntArray
                    ? "bad NBT int array length"
                    : "bad NBT long array length");
            const auto count = static_cast<std::size_t>(length);
            const std::size_t elementSize =
                tag == IntArray ? sizeof(std::int32_t) : sizeof(std::int64_t);
            if (count > std::numeric_limits<std::size_t>::max() / elementSize)
                throw std::length_error(tag == IntArray
                    ? "NBT int-array allocation size overflow"
                    : "NBT long-array allocation size overflow");
            const auto byteCount = count * elementSize;
            chargeAllocation(byteCount);
            in_.skip(byteCount);
            break;
        }
        case End:
        default:
            throw std::runtime_error("bad nbt tag in file");
        }
    }

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
            chargeAllocation(static_cast<std::size_t>(n));
            v.byteArray = in_.bytes(static_cast<std::size_t>(n));
            break;
        }
        case String: {
            const auto n = in_.u16();
            if (n != 0) {
                chargeAllocation(static_cast<std::size_t>(n) + 1);
                in_.need(n);
                v.str.assign(reinterpret_cast<const char*>(in_.p + in_.off), n);
                in_.skip(n);
            }
            break;
        }
        case List: {
            const Tag et = static_cast<Tag>(in_.u8());
            const auto n = in_.i32();
            if (!isValidTag(et) || n < 0 || static_cast<std::size_t>(n) > kMaxNbtElements ||
                (n != 0 && et == End))
                throw std::runtime_error("bad NBT list");
            const auto count = static_cast<std::size_t>(n);
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value))
                throw std::length_error("NBT list allocation size overflow");
            chargeAllocation(count * sizeof(Value));
            v.listElement = et;
            v.list.reserve(count);
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
                using CompoundEntry = std::pair<std::string, Value>;
                if (v.comp.size() == v.comp.capacity()) {
                    const auto currentCapacity = v.comp.capacity();
                    const auto maximumCapacity = v.comp.max_size();
                    const auto nextCapacity = currentCapacity == 0
                        ? std::size_t{1}
                        : (currentCapacity > maximumCapacity / 2
                               ? maximumCapacity
                               : currentCapacity * 2);
                    if (nextCapacity <= currentCapacity ||
                        nextCapacity >
                            std::numeric_limits<std::size_t>::max() /
                                sizeof(CompoundEntry))
                        throw std::length_error(
                            "NBT compound allocation size overflow");
                    // Account each actual vector buffer before reserve(). The
                    // old buffers are included because reallocation allocates
                    // the replacement before releasing the prior storage.
                    chargeAllocation(nextCapacity * sizeof(CompoundEntry));
                    v.comp.reserve(nextCapacity);
                }
                if (static_cast<std::size_t>(nl) >
                    std::numeric_limits<std::size_t>::max() / 2)
                    throw std::length_error("NBT compound name is too large");
                std::string name;
                if (nl != 0) {
                    chargeAllocation(static_cast<std::size_t>(nl) + 1);
                    in_.need(nl);
                    name.assign(reinterpret_cast<const char*>(in_.p + in_.off),
                                nl);
                    in_.skip(nl);
                }
                v.comp.emplace_back(std::move(name), payload(et, depth + 1));
            }
            break;
        }
        case IntArray: {
            const auto n = in_.i32();
            if (n < 0 || static_cast<std::size_t>(n) > kMaxNbtElements)
                throw std::runtime_error("bad NBT int array length");
            const auto count = static_cast<std::size_t>(n);
            if (count > std::numeric_limits<std::size_t>::max() /
                            sizeof(std::int32_t))
                throw std::length_error("NBT int-array allocation size overflow");
            chargeAllocation(count * sizeof(std::int32_t));
            v.intArray.reserve(count);
            for (std::int32_t i = 0; i < n; ++i) v.intArray.push_back(in_.i32());
            break;
        }
        case LongArray: {
            const auto n = in_.i32();
            if (n < 0 || static_cast<std::size_t>(n) > kMaxNbtElements)
                throw std::runtime_error("bad NBT long array length");
            const auto count = static_cast<std::size_t>(n);
            if (count > std::numeric_limits<std::size_t>::max() /
                            sizeof(std::int64_t))
                throw std::length_error("NBT long-array allocation size overflow");
            chargeAllocation(count * sizeof(std::int64_t));
            v.longArray.reserve(count);
            for (std::int32_t i = 0; i < n; ++i) v.longArray.push_back(in_.i64());
            break;
        }
        default: throw std::runtime_error("bad nbt tag in file");
        }
        return v;
    }
    ReadBuffer& in_;
    const std::size_t allocationBudget_;
    std::size_t usedAllocation_ = 0;
};

} // namespace cppfm::nbt
