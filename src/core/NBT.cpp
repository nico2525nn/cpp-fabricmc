#include "NBT.hpp"

namespace cppfm::nbt {

void Reader::skipPayload(Tag t) {
    switch (t) {
    case Byte: in_.u8(); break;
    case Short: in_.u16(); break;
    case Int: case Float: in_.u32(); break;
    case Long: case Double: in_.u64(); break;
    case ByteArray: { std::int32_t n = in_.i32(); if (n < 0) throw std::runtime_error("bad byte array len"); in_.bytes(static_cast<std::size_t>(n)); break; }
    case String: { std::uint16_t n = in_.u16(); in_.bytes(n); break; }
    case List: {
        Tag et = static_cast<Tag>(in_.u8());
        std::int32_t n = in_.i32();
        if (n < 0) throw std::runtime_error("bad list len");
        for (std::int32_t i = 0; i < n; ++i) skipPayload(et);
        break;
    }
    case Compound: {
        while (true) {
            Tag et = static_cast<Tag>(in_.u8());
            if (et == End) break;
            std::uint16_t n = in_.u16();
            in_.bytes(n);
            skipPayload(et);
        }
        break;
    }
    case IntArray: { std::int32_t n = in_.i32(); if (n < 0) throw std::runtime_error("bad int array"); in_.bytes(4u * static_cast<std::size_t>(n)); break; }
    case LongArray: { std::int32_t n = in_.i32(); if (n < 0) throw std::runtime_error("bad long array"); in_.bytes(8u * static_cast<std::size_t>(n)); break; }
    default: throw std::runtime_error("bad nbt tag");
    }
}

Reader::Value Reader::readValue(ReadBuffer& in, Tag t) {
    Value v;
    v.tag = t;
    switch (t) {
    case Byte: v.b = static_cast<std::int8_t>(in.u8()); break;
    case Short: v.s = in.i16(); break;
    case Int: v.i = in.i32(); break;
    case Long: v.l = in.i64(); break;
    case Float: v.f = in.f32(); break;
    case Double: v.d = in.f64(); break;
    case String: { std::uint16_t n = in.u16(); v.str.assign(reinterpret_cast<const char*>(in.p + in.off), n); in.bytes(n); break; }
    case List: {
        Tag et = static_cast<Tag>(in.u8());
        std::int32_t n = in.i32();
        for (std::int32_t i = 0; i < n && i < 65536; ++i) v.list.push_back(readValue(in, et));
        break;
    }
    case Compound: {
        while (true) {
            Tag et = static_cast<Tag>(in.u8());
            if (et == End) break;
            std::uint16_t n = in.u16();
            std::string name(reinterpret_cast<const char*>(in.p + in.off), n);
            in.bytes(n);
            v.comp.emplace_back(std::move(name), readValue(in, et));
        }
        break;
    }
    case IntArray: case LongArray: { in.skipRest(); throw std::runtime_error("array read unsupported in tree mode"); }
    default: throw std::runtime_error("bad tag");
    }
    return v;
}

Reader::Value Reader::readNamedValue() {
    Tag t = static_cast<Tag>(in_.u8());
    // named form not used on network root; treat as unnamed payload
    return readValue(in_, t);
}

} // namespace cppfm::nbt
