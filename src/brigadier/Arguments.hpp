// Arguments: typed parsed values + argument types with wire properties.
//
// Parser ids match the 1.21.4 vanilla registry order (verified against
// community protocol documentation). Each ArgumentType knows how to:
//   1. write its declare_commands property blob (writeProps), and
//   2. parse itself out of a StringReader into an ArgValue (parse).
#pragma once
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "StringReader.hpp"

namespace cppfm::brigadier {

enum class ParserId : std::uint8_t {
    Bool = 0, Float = 1, Double = 2, Integer = 3, Long = 4, String = 5,
    Entity = 6, GameProfile = 7, BlockPos = 8, ColumnPos = 9, Vec3 = 10,
    Vec2 = 11, BlockState = 12, BlockPredicate = 13, ItemStack = 14,
    ItemPredicate = 15, Color = 16, Component = 17, Message = 18,
    Nbt = 19, NbtCompoundTag = 20, NbtTag = 21, NbtPath = 22, Objective = 23,
    ObjectiveCriteria = 24, Operation = 25, Particle = 26, Angle = 27,
    Rotation = 28, ScoreboardSlot = 29, Swizzle = 30, Team = 31,
    ItemSlot = 32, ResourceLocation = 33, MobEffect = 34, FunctionTag = 35,
    EntityAnchor = 36, IntRange = 37, FloatRange = 38, Dimension = 39,
    Gamemode = 40, Time = 41, ResourceOrTag = 42, ResourceOrTagKey = 43,
    Resource = 44, ResourceKey = 45, TemplateMirror = 46,
    TemplateRotation = 47, Uuid = 48,
};

enum class StringMode : std::uint8_t { SingleWord = 0, Quotable = 1, Greedy = 2 };

// A resolved selector: which entities matched.
struct SelectorResult {
    bool playersOnly = false;
    std::vector<std::string> playerNames;
    std::vector<std::int32_t> entityIds;             // non-player entity ids
};

struct Vec3d { double x = 0, y = 0, z = 0; };
struct Vec2f { float x = 0, y = 0; };
struct BlockPosI { std::int32_t x = 0, y = 0, z = 0; };

struct ArgValue {
    using Variant = std::variant<std::monostate, bool, std::int64_t, double,
                                 std::string, Vec3d, Vec2f, BlockPosI,
                                 SelectorResult>;
    Variant v;

    ArgValue() = default;
    template <typename T> ArgValue(T t) : v(std::move(t)) {}
    bool isBool() const { return std::holds_alternative<bool>(v); }
    bool asBool() const { return std::get<bool>(v); }
    std::int64_t asI64() const {
        if (auto* p = std::get_if<std::int64_t>(&v)) return *p;
        if (auto* d = std::get_if<double>(&v))
            return static_cast<std::int64_t>(std::llround(*d));
        return 0;
    }
    std::int32_t asInt() const { return static_cast<std::int32_t>(asI64()); }
    double asDouble() const {
        if (auto* d = std::get_if<double>(&v)) return *d;
        if (auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
        return 0;
    }
    const std::string& asStr() const {
        static const std::string e;
        const auto* s = std::get_if<std::string>(&v);
        return s ? *s : e;
    }
    Vec3d asVec3() const {
        if (const auto* q = std::get_if<Vec3d>(&v)) return *q;
        return {};
    }
    BlockPosI asBlockPos() const {
        if (const auto* q = std::get_if<BlockPosI>(&v)) return *q;
        return {};
    }
    SelectorResult asSelector() const {
        if (const auto* q = std::get_if<SelectorResult>(&v)) return *q;
        return {};
    }
};

// ------------------------------------------------------------------ parsers

struct ParseCtx;                                     // fwd (Tree.hpp provides)

using ParseFn = std::function<ArgValue(StringReader&, ParseCtx&)>;
using SuggestFn = std::function<std::vector<std::string>(StringReader&, ParseCtx&)>;

// Forward declaration of the per-command parse environment. Defined in
// Tree.hpp to avoid a circular include; parsers receive it for context like
// the command source position (relative coordinates).
struct ParseCtx {
    // source position for relative coordinates (~ / ~3)
    double srcX = 0, srcY = 0, srcZ = 0;
    float srcYaw = 0, srcPitch = 0;
    // names of online players (for selectors / player-arg suggestions)
    std::vector<std::string> playerNames;
    // opaque hook so game code can extend parsing (e.g., resolve selectors)
    std::function<void(const std::string& raw, SelectorResult& out)> resolveSelector;

    virtual ~ParseCtx() = default;
};

struct ArgumentType {
    ParserId id;
    std::function<void(WriteBuffer&)> writeProps;    // may be empty
    ParseFn parse;
    SuggestFn suggest;                               // optional tab-completion
};

namespace args {

inline void requireNumber(double v, double lo, double hi) {
    if (v < lo || v > hi)
        throw StringReader::ParseError("value out of range");
}

// ---- primitives -----------------------------------------------------------
inline ArgumentType integer(std::int32_t lo = INT32_MIN, std::int32_t hi = INT32_MAX) {
    ArgumentType a;
    a.id = ParserId::Integer;
    a.writeProps = [lo, hi](WriteBuffer& b) {
        b.u8(0x01 | 0x02);                           // min | max present
        b.varint(lo);
        b.varint(hi);
    };
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue { return r.readInt(); };
    a.suggest = nullptr;
    return a;
}
inline ArgumentType floatArg(float lo = -FLT_MAX, float hi = FLT_MAX) {
    ArgumentType a;
    a.id = ParserId::Float;
    a.writeProps = [lo, hi](WriteBuffer& b) {
        b.u8(0x01 | 0x02);
        b.f32(lo);
        b.f32(hi);
    };
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue { return r.readFloat(); };
    return a;
}
inline ArgumentType boolean() {
    ArgumentType a;
    a.id = ParserId::Bool;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        const std::size_t start = r.cursor();
        const std::string w = r.readUnquotedString();
        if (w == "true") return true;
        if (w == "false") return false;
        r.setCursor(start);
        throw StringReader::ParseError("expected bool");
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{"true", "false"};
    };
    return a;
}
inline ArgumentType stringWord() {
    ArgumentType a;
    a.id = ParserId::String;
    a.writeProps = [](WriteBuffer& b) { b.varint(static_cast<int>(StringMode::SingleWord)); };
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue { return r.readUnquotedString(); };
    return a;
}
inline ArgumentType stringQuotable() {
    ArgumentType a;
    a.id = ParserId::String;
    a.writeProps = [](WriteBuffer& b) { b.varint(static_cast<int>(StringMode::Quotable)); };
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue { return r.readString(); };
    return a;
}
inline ArgumentType stringGreedy() {
    ArgumentType a;
    a.id = ParserId::String;
    a.writeProps = [](WriteBuffer& b) { b.varint(static_cast<int>(StringMode::Greedy)); };
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue { return r.readGreedyString(); };
    return a;
}

// ---- positions ------------------------------------------------------------
// Reads one coordinate token supporting ~ relative notation.
inline double readCoord(StringReader& r, ParseCtx& ctx, char axis, bool& relativeOut) {
    bool rel = false;
    double v = 0;
    if (r.peek() == '~') {
        rel = true;
        r.skip();
        if (r.canRead() && (isdigit((unsigned char)r.peek()) || r.peek() == '-' ||
                            r.peek() == '.')) {
            v = r.readDouble();
        }
    } else {
        v = r.readDouble();
    }
    relativeOut = rel;
    const double base = axis == 'x' ? ctx.srcX : axis == 'y' ? ctx.srcY : ctx.srcZ;
    return base + v;
}

inline ArgumentType blockPos() {
    ArgumentType a;
    a.id = ParserId::BlockPos;
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        bool rx = false, ry = false, rz = false;
        const double x = readCoord(r, c, 'x', rx);
        r.skipWhitespace();
        const double y = readCoord(r, c, 'y', ry);
        r.skipWhitespace();
        const double z = readCoord(r, c, 'z', rz);
        (void)rx; (void)ry; (void)rz;
        return BlockPosI{static_cast<std::int32_t>(std::floor(x)),
                         static_cast<std::int32_t>(std::floor(y)),
                         static_cast<std::int32_t>(std::floor(z))};
    };
    return a;
}
inline ArgumentType vec3() {
    ArgumentType a;
    a.id = ParserId::Vec3;
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        bool rel = false;
        const double x = readCoord(r, c, 'x', rel);
        r.skipWhitespace();
        const double y = readCoord(r, c, 'y', rel);
        r.skipWhitespace();
        const double z = readCoord(r, c, 'z', rel);
        return Vec3d{x, y, z};
    };
    return a;
}
inline ArgumentType vec2() {
    ArgumentType a;
    a.id = ParserId::Vec2;
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        bool rel = false;
        const double x = readCoord(r, c, 'x', rel);
        r.skipWhitespace();
        const double y = readCoord(r, c, 'y', rel);
        return Vec2f{static_cast<float>(x), static_cast<float>(y)};
    };
    return a;
}

// ---- identifiers ----------------------------------------------------------
inline std::string readIdentifier(StringReader& r) {
    const std::size_t start = r.cursor();
    while (r.canRead()) {
        const char c = r.peek();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
            c == ':' || c == '/')
            r.skip();
        else break;
    }
    return r.slice(start);
}

inline ArgumentType resourceLocation() {
    ArgumentType a;
    a.id = ParserId::ResourceLocation;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string id = readIdentifier(r);
        if (id.empty()) throw StringReader::ParseError("expected identifier");
        // normalize: missing namespace implies minecraft:
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        return id;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{};             // filled by game layer
    };
    return a;
}

// ---- entities / game types -------------------------------------------------
// Entity selector: @p/@a/@r/@e/@s plus optional [type=...,limit=...,name=...].
// Raw text is stored in SelectorResult via ctx.resolveSelector hook.
inline ArgumentType entity(bool playersOnly, bool single) {
    ArgumentType a;
    a.id = ParserId::Entity;
    a.writeProps = [playersOnly, single](WriteBuffer& b) {
        b.u8((single ? 0x01 : 0x00) | (playersOnly ? 0x02 : 0x00));
    };
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        const std::size_t start = r.cursor();
        if (r.peek() == '@') {
            r.skip();
            const char k = r.read();                  // a p r e s
            (void)k;
            if (r.peek() == '[') {                    // filter args
                int depth = 0;
                while (r.canRead()) {
                    const char ch = r.read();
                    if (ch == '[') ++depth;
                    else if (ch == ']') { --depth; if (!depth) break; }
                }
            }
        } else {
            r.readUnquotedString();                   // plain player name
        }
        SelectorResult out;
        const std::string raw = r.slice(start);
        if (c.resolveSelector) c.resolveSelector(raw, out);
        return out;
    };
    a.suggest = [](StringReader&, ParseCtx& c) {
        std::vector<std::string> v{"@a", "@e", "@p", "@r", "@s"};
        for (auto& n : c.playerNames) v.push_back(n);
        return v;
    };
    return a;
}

// minecraft:gamemode — enum of the four modes.
inline ArgumentType gamemodeArg() {
    ArgumentType a;
    a.id = ParserId::Gamemode;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        return r.readUnquotedString();
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{"survival", "creative", "adventure",
                                        "spectator"};
    };
    return a;
}

// minecraft:time — integer with optional unit suffix (d/s/t).
inline ArgumentType timeArg() {
    ArgumentType a;
    a.id = ParserId::Time;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::int64_t v = r.readLong();
        if (r.canRead()) {
            const char u = r.peek();
            if (u == 'd') { r.skip(); v *= 24000; }
            else if (u == 's') { r.skip(); v *= 20; }
            else if (u == 't') r.skip();
        }
        return v;
    };
    return a;
}

// minecraft:message — greedy string that may contain selectors.
inline ArgumentType messageArg() {
    ArgumentType a;
    a.id = ParserId::Message;
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        const std::size_t start = r.cursor();
        r.readGreedyString();
        const std::string raw = r.slice(start);
        // resolve inline selectors for display purposes
        SelectorResult sel;
        if (c.resolveSelector) c.resolveSelector(raw, sel);
        return raw;
    };
    return a;
}

// minecraft:item_stack: `minecraft:item_id` or `id[comp=value,...]`.
// We parse and keep the item id + raw component text (components applied by
// the game layer).
inline ArgumentType itemStackArg() {
    ArgumentType a;
    a.id = ParserId::ItemStack;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string id = readIdentifier(r);
        if (id.empty()) throw StringReader::ParseError("expected item id");
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        if (r.peek() == '[') {
            int depth = 0;
            while (r.canRead()) {
                const char ch = r.read();
                if (ch == '[') ++depth;
                else if (ch == ']') { --depth; if (!depth) break; }
            }
        }
        return id;
    };
    return a;
}

} // namespace args
} // namespace cppfm::brigadier
