// Arguments: typed parsed values + argument types with wire properties (48 parsers).
//
// Parser ids match Minecraft Java 1.21.4 vanilla registry order 0-53 (verified
// against PrismarineJS minecraft-data 1.21.4 protocol.json + Yarn 1.21.4).
// Strict mapping (plan15): 17 Style=18 / Message=19 / Nbt=20 / NbtTag=21 etc
// through 48 Heightmap=49 / LootTable=50 / LootPredicate=51 / LootModifier=52.
// Each ArgumentType knows how to:
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
    ItemPredicate = 15, Color = 16, Component = 17, Style = 18, Message = 19,
    Nbt = 20, NbtTag = 21, NbtPath = 22, Objective = 23,
    ObjectiveCriteria = 24, Operation = 25, Particle = 26, Angle = 27,
    Rotation = 28, ScoreboardSlot = 29, ScoreHolder = 30, Swizzle = 31, Team = 32,
    ItemSlot = 33, ItemSlots = 34, ResourceLocation = 35, Function = 36,
    EntityAnchor = 37, IntRange = 38, FloatRange = 39, Dimension = 40,
    Gamemode = 41, Time = 42, ResourceOrTag = 43, ResourceOrTagKey = 44,
    Resource = 45, ResourceKey = 46, TemplateMirror = 47,
    TemplateRotation = 48, Heightmap = 49, LootTable = 50, LootPredicate = 51,
    LootModifier = 52, Uuid = 53,
    // Legacy aliases for existing code
    NbtCompoundTag = Nbt,
    MobEffect = ItemSlots,
    FunctionTag = Function,
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

// Forward declaration of the per-command parse environment. Defined in Tree.hpp to avoid a circular include; parsers receive it for context
// like the command source position (relative coordinates).
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

// ---- positions ------------------------------------------------------------ Reads one coordinate token supporting ~ relative notation.
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
    if (!rel) return v;
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

// ---- entities / game types ------------------------------------------------- Entity selector: @p/@a/@r/@e/@s plus optional
// [type=...,limit=...,name=...]. Raw text is stored in SelectorResult via ctx.resolveSelector hook.
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
            (void)r.read(); // a p r e s (selector kind char)
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

// minecraft:item_stack: `minecraft:item_id` or `id[comp=value,...]`. We parse and keep the item id + raw component text (components applied
// by the game layer).
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

// ---- extended arg types (plan13 §10) -----------------------------------
// BlockState parser id 12: `minecraft:stone` or `minecraft:oak_stairs[facing=north,half=top]`
inline ArgumentType blockStateArg() {
    ArgumentType a;
    a.id = ParserId::BlockState;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string id = readIdentifier(r);
        if (id.empty()) throw StringReader::ParseError("expected block id");
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        // optional block state props [prop=value,...]
        if (r.peek() == '[') {
            int depth = 0;
            std::string props;
            while (r.canRead()) {
                char ch = r.read();
                props.push_back(ch);
                if (ch == '[') ++depth;
                else if (ch == ']') { --depth; if (!depth) break; }
            }
            id += props;
        }
        // optional NBT { ... } for block entity
        if (r.peek() == '{') {
            int depth = 0;
            std::string nbt;
            while (r.canRead()) {
                char ch = r.read();
                nbt.push_back(ch);
                if (ch == '{') ++depth;
                else if (ch == '}') { --depth; if (!depth) break; }
            }
            id += nbt;
        }
        return id;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        // lightweight default list; full list is injected via node-level suggestions in Commands.cpp
        return std::vector<std::string>{"minecraft:stone","minecraft:dirt","minecraft:grass_block","minecraft:cobblestone","minecraft:oak_planks","minecraft:glass","minecraft:sand","minecraft:oak_log","minecraft:glowstone","minecraft:air"};
    };
    return a;
}
inline ArgumentType blockPredicateArg() {
    ArgumentType a;
    a.id = ParserId::BlockPredicate;
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        // same as blockState but also allows leading # for tag
        if (r.peek() == '#') { r.skip(); std::string tag = readIdentifier(r); if (tag.empty()) throw StringReader::ParseError("expected tag"); if (tag.find(':')==std::string::npos) tag="minecraft:"+tag; return std::string("#")+tag; }
        return blockStateArg().parse(r,c);
    };
    a.suggest = blockStateArg().suggest;
    return a;
}
inline ArgumentType itemPredicateArg() {
    ArgumentType a;
    a.id = ParserId::ItemPredicate;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        bool isTag = false;
        if (r.peek() == '#') { isTag=true; r.skip(); }
        std::string id = readIdentifier(r);
        if (id.empty()) throw StringReader::ParseError("expected item id");
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        if (isTag) id = "#" + id;
        if (r.peek() == '[') {
            int depth=0;
            while(r.canRead()){ char ch=r.read(); if(ch=='[')++depth; else if(ch==']'){--depth; if(!depth)break; } }
        }
        if (r.peek() == '{') {
            int depth=0;
            while(r.canRead()){ char ch=r.read(); if(ch=='{')++depth; else if(ch=='}'){--depth; if(!depth)break; } }
        }
        return id;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{"minecraft:stone","minecraft:dirt","minecraft:diamond","minecraft:iron_ingot","minecraft:diamond_sword"};
    };
    return a;
}
inline ArgumentType nbtArg() {
    ArgumentType a;
    a.id = ParserId::Nbt;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        r.skipWhitespace();
        if (!r.canRead() || r.peek()!='{') throw StringReader::ParseError("expected NBT compound");
        int depth=0;
        std::string out;
        while(r.canRead()){
            char ch=r.read();
            out.push_back(ch);
            if(ch=='{')++depth;
            else if(ch=='}'){--depth; if(!depth)break; }
        }
        if(depth!=0) throw StringReader::ParseError("unterminated NBT");
        return out;
    };
    return a;
}
inline ArgumentType nbtCompoundTagArg() {
    ArgumentType a;
    a.id = ParserId::NbtCompoundTag;
    a.parse = nbtArg().parse;
    return a;
}
inline ArgumentType nbtTagArg() {
    ArgumentType a;
    a.id = ParserId::NbtTag;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        r.skipWhitespace();
        const std::size_t start=r.cursor();
        // accept any NBT value: compound, list, primitive, string
        if(r.peek()=='{'){
            int d=0;
            while(r.canRead()){char c=r.read(); if(c=='{')++d; else if(c=='}'){--d; if(!d)break;}}
        } else if(r.peek()=='['){
            int d=0;
            while(r.canRead()){char c=r.read(); if(c=='[')++d; else if(c==']'){--d; if(!d)break;}}
        } else if(r.peek()=='"'){
            r.readQuotedString();
        } else {
            r.readUnquotedString();
            if(r.canRead() && r.peek()=='"') r.readQuotedString();
        }
        std::string s=r.slice(start);
        if(s.empty()) throw StringReader::ParseError("expected NBT tag");
        return s;
    };
    return a;
}
inline ArgumentType nbtPathArg() {
    ArgumentType a;
    a.id = ParserId::NbtPath;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        r.skipWhitespace();
        std::size_t start=r.cursor();
        while(r.canRead() && r.peek()!=' '){
            char c=r.peek();
            if(c=='"'){ r.readQuotedString(); continue; }
            if(c=='['||c==']'||c=='.'||c=='{'||c=='}'|| isalnum((unsigned char)c) || c=='_' || c=='-' ) r.skip();
            else break;
        }
        std::string s=r.slice(start);
        if(s.empty()) throw StringReader::ParseError("expected NBT path");
        return s;
    };
    return a;
}
inline ArgumentType objectiveArg() {
    ArgumentType a;
    a.id = ParserId::Objective;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string n=r.readUnquotedString();
        if(n.empty()) throw StringReader::ParseError("expected objective");
        // vanilla objective name: 1-16 chars [a-zA-Z0-9_.-]
        if(n.size()>16) throw StringReader::ParseError("objective too long");
        return n;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{};
    };
    return a;
}
inline ArgumentType objectiveCriteriaArg() {
    ArgumentType a;
    a.id = ParserId::ObjectiveCriteria;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string c=r.readUnquotedString();
        if(c.empty()) c=readIdentifier(r);
        if(c.empty()) throw StringReader::ParseError("expected criteria");
        return c;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{"dummy","deathCount","playerKillCount","totalKillCount","health","xp","level","food","air","armor"};
    };
    return a;
}
inline ArgumentType teamArg() {
    ArgumentType a;
    a.id = ParserId::Team;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string n=r.readUnquotedString();
        if(n.empty()) throw StringReader::ParseError("expected team");
        if(n.size()>16) throw StringReader::ParseError("team name too long");
        return n;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{};
    };
    return a;
}

inline ArgumentType dimensionArg() {
    ArgumentType a;
    a.id = ParserId::Dimension;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string id = readIdentifier(r);
        if (id.empty()) throw StringReader::ParseError("expected dimension");
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        return id;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{"minecraft:overworld","minecraft:the_nether","minecraft:the_end"};
    };
    return a;
}
inline ArgumentType swizzleArg() {
    ArgumentType a;
    a.id = ParserId::Swizzle;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string s = r.readUnquotedString();
        if (s.empty()) throw StringReader::ParseError("expected swizzle");
        return s;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{"x","y","z","xy","xz","yz","xyz"};
    };
    return a;
}
inline ArgumentType entityAnchorArg() {
    ArgumentType a;
    a.id = ParserId::EntityAnchor;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string s = r.readUnquotedString();
        if (s!="feet" && s!="eyes") throw StringReader::ParseError("expected feet|eyes");
        return s;
    };
    a.suggest = [](StringReader&, ParseCtx&) {
        return std::vector<std::string>{"feet","eyes"};
    };
    return a;
}
inline ArgumentType scoreHolderArg() {
    ArgumentType a;
    a.id = ParserId::ScoreHolder;
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        const std::size_t start=r.cursor();
        if (r.peek()=='@') {
            r.skip(); char k=r.read(); (void)k;
            if(r.peek()=='['){ int d=0; while(r.canRead()){ char ch=r.read(); if(ch=='[')++d; else if(ch==']'){--d; if(!d)break; } } }
        } else r.readUnquotedString();
        std::string raw=r.slice(start);
        if(raw.empty()) throw StringReader::ParseError("expected score holder");
        SelectorResult out;
        if(c.resolveSelector) c.resolveSelector(raw,out);
        if(!out.playerNames.empty() || !out.entityIds.empty()) return out;
        // fallback: raw name as single holder
        SelectorResult sr; sr.playerNames.push_back(raw); return sr;
    };
    a.suggest = [](StringReader&, ParseCtx& c){
        std::vector<std::string> v{"@a","@p","@s"};
        for(auto& n: c.playerNames) v.push_back(n);
        return v;
    };
    return a;
}
inline ArgumentType vec3Arg(bool = false) {
    return vec3();
}
inline ArgumentType vec2Arg() {
    return vec2();
}
inline ArgumentType rotationArg() {
    ArgumentType a;
    a.id = ParserId::Rotation;
    a.parse = [](StringReader& r, ParseCtx& c) -> ArgValue {
        bool rel=false;
        double yaw = readCoord(r,c,'x',rel);
        r.skipWhitespace();
        double pitch = readCoord(r,c,'y',rel);
        return Vec2f{static_cast<float>(yaw), static_cast<float>(pitch)};
    };
    return a;
}
inline ArgumentType angleArg() {
    ArgumentType a;
    a.id = ParserId::Angle;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue { return r.readFloat(); };
    return a;
}
inline ArgumentType intRangeArg() {
    ArgumentType a;
    a.id = ParserId::IntRange;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string s=r.readUnquotedString();
        if(s.empty()) throw StringReader::ParseError("expected int range");
        return s;
    };
    return a;
}
inline ArgumentType lootTableArg() {
    ArgumentType a;
    a.id = ParserId::LootTable;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string id=readIdentifier(r);
        if(id.empty()) throw StringReader::ParseError("expected loot table");
        if(id.find(':')==std::string::npos) id="minecraft:"+id;
        return id;
    };
    return a;
}
inline ArgumentType boolArg() { return boolean(); }
inline ArgumentType gameProfileArg() {
    ArgumentType a;
    a.id = ParserId::GameProfile;
    a.parse = [](StringReader& r, ParseCtx&) -> ArgValue {
        std::string s = r.readUnquotedString();
        if (s.empty()) throw StringReader::ParseError("expected player name");
        return s;
    };
    a.suggest = [](StringReader&, ParseCtx& c) {
        std::vector<std::string> v;
        for (auto& n : c.playerNames) v.push_back(n);
        return v;
    };
    return a;
}

} // namespace args
} // namespace cppfm::brigadier
