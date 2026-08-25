// Json: minimal self-contained JSON value + parser + serializer.
// Used by: recipe loading, worldgen definitions, stats/advancement persistence,
// server-list ping responses, plugin payloads. Clean-room implementation.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm::json {

class Value {
public:
    enum class Type { Null, Bool, Num, Str, Arr, Obj };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;   // insertion order preserved

    // ------------------------------------------------------------ factories
    static Value null() { return Value{}; }
    static Value ofBool(bool v) { Value x; x.type = Type::Bool; x.boolean = v; return x; }
    static Value ofNumber(double v) { Value x; x.type = Type::Num; x.number = v; return x; }
    static Value ofString(std::string v) { Value x; x.type = Type::Str; x.str = std::move(v); return x; }
    static Value array() { Value x; x.type = Type::Arr; return x; }
    static Value object() { Value x; x.type = Type::Obj; return x; }

    // -------------------------------------------------------------- queries
    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNum() const { return type == Type::Num; }
    bool isStr() const { return type == Type::Str; }
    bool isArr() const { return type == Type::Arr; }
    bool isObj() const { return type == Type::Obj; }

    int asInt(int def = 0) const { return isNum() ? static_cast<int>(std::lround(number)) : def; }
    std::int64_t asI64(std::int64_t def = 0) const {
        return isNum() ? static_cast<std::int64_t>(std::llround(number)) : def; }
    float asFloat(float def = 0.f) const { return isNum() ? static_cast<float>(number) : def; }
    bool asBool(bool def = false) const { return isBool() ? boolean : def; }
    const std::string& asStr() const { static const std::string e; return isStr() ? str : e; }

    const Value* find(std::string_view key) const {
        if (type != Type::Obj) return nullptr;
        for (auto& [k, v] : obj) if (k == key) return &v;
        return nullptr;
    }
    Value* find(std::string_view key) {
        if (type != Type::Obj) return nullptr;
        for (auto& [k, v] : obj) if (k == key) return &v;
        return nullptr;
    }
    // Convenience accessors returning fallback when missing / wrong type.
    const Value& at(std::string_view key) const {
        static const Value nul;
        const Value* v = find(key);
        return v ? *v : nul;
    }
    void set(std::string key, Value v) {
        if (type != Type::Obj) { *this = object(); }
        for (auto& [k, ex] : obj) if (k == key) { ex = std::move(v); return; }
        obj.emplace_back(std::move(key), std::move(v));
    }
    void push(Value v) {
        if (type != Type::Arr) *this = array();
        arr.push_back(std::move(v));
    }

    // ---------------------------------------------------------------- parse
    static Value parse(std::string_view text) {
        std::size_t i = 0;
        Value v = parseValue(text, i);
        skipWs(text, i);
        if (i != text.size()) throw std::runtime_error("json: trailing data");
        return v;
    }

    // ----------------------------------------------------------------- dump
    std::string dump() const {
        std::string out;
        write(out);
        return out;
    }

private:
    static void skipWs(std::string_view s, std::size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    static char peek(std::string_view s, std::size_t i) {
        if (i >= s.size()) throw std::runtime_error("json: unexpected end");
        return s[i];
    }
    static Value parseValue(std::string_view s, std::size_t& i) {
        skipWs(s, i);
        const char c = peek(s, i);
        switch (c) {
        case '{': return parseObject(s, i);
        case '[': return parseArray(s, i);
        case '"': return Value::ofString(parseString(s, i));
        case 't':
            expect(s, i, "true");  return Value::ofBool(true);
        case 'f':
            expect(s, i, "false"); return Value::ofBool(false);
        case 'n':
            expect(s, i, "null");  return Value::null();
        default:
            return Value::ofNumber(parseNumber(s, i));
        }
    }
    static void expect(std::string_view s, std::size_t& i, std::string_view word) {
        if (s.compare(i, word.size(), word) != 0)
            throw std::runtime_error("json: bad literal");
        i += word.size();
    }
    static Value parseObject(std::string_view s, std::size_t& i) {
        Value out = Value::object();
        ++i;                                        // '{'
        skipWs(s, i);
        if (peek(s, i) == '}') { ++i; return out; }
        for (;;) {
            skipWs(s, i);
            std::string key = parseString(s, i);
            skipWs(s, i);
            if (peek(s, i) != ':') throw std::runtime_error("json: expected ':'");
            ++i;
            out.obj.emplace_back(std::move(key), parseValue(s, i));
            skipWs(s, i);
            const char c = peek(s, i);
            if (c == ',') { ++i; continue; }
            if (c == '}') { ++i; return out; }
            throw std::runtime_error("json: expected ',' or '}'");
        }
    }
    static Value parseArray(std::string_view s, std::size_t& i) {
        Value out = Value::array();
        ++i;                                        // '['
        skipWs(s, i);
        if (peek(s, i) == ']') { ++i; return out; }
        for (;;) {
            out.arr.push_back(parseValue(s, i));
            skipWs(s, i);
            const char c = peek(s, i);
            if (c == ',') { ++i; continue; }
            if (c == ']') { ++i; return out; }
            throw std::runtime_error("json: expected ',' or ']'");
        }
    }
    static std::string parseString(std::string_view s, std::size_t& i) {
        if (peek(s, i) != '"') throw std::runtime_error("json: expected string");
        ++i;
        std::string out;
        while (true) {
            if (i >= s.size()) throw std::runtime_error("json: unterminated string");
            const char c = s[i++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }
            if (i >= s.size()) throw std::runtime_error("json: bad escape");
            const char e = s[i++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (i + 4 > s.size()) throw std::runtime_error("json: bad \\u");
                unsigned cp = hex4(s, i); i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size()
                    && s[i] == '\\' && s[i + 1] == 'u') {
                    const unsigned lo = hex4(s, i + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                appendUtf8(out, cp);
                break;
            }
            default: throw std::runtime_error("json: unknown escape");
            }
        }
    }
    static unsigned hex4(std::string_view s, std::size_t i) {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i + static_cast<std::size_t>(k)];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else throw std::runtime_error("json: bad hex");
        }
        return v;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    static double parseNumber(std::string_view s, std::size_t& i) {
        const std::size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.'
                                || s[i] == 'e' || s[i] == 'E'
                                || s[i] == '-' || s[i] == '+')) ++i;
        if (i == start) throw std::runtime_error("json: bad number");
        try {
            return std::stod(std::string(s.substr(start, i - start)));
        } catch (...) {
            throw std::runtime_error("json: unparseable number");
        }
    }

    void write(std::string& out) const {
        switch (type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += boolean ? "true" : "false"; break;
        case Type::Num: {
            char buf[40];
            if (number == std::floor(number) && std::abs(number) < 1e15)
                snprintf(buf, sizeof buf, "%lld", static_cast<long long>(number));
            else snprintf(buf, sizeof buf, "%.10g", number);
            out += buf;
            break;
        }
        case Type::Str: writeString(out, str); break;
        case Type::Arr: {
            out += '[';
            bool first = true;
            for (auto& v : arr) {
                if (!first) out += ',';
                first = false;
                v.write(out);
            }
            out += ']';
            break;
        }
        case Type::Obj: {
            out += '{';
            bool first = true;
            for (auto& [k, v] : obj) {
                if (!first) out += ',';
                first = false;
                writeString(out, k);
                out += ':';
                v.write(out);
            }
            out += '}';
            break;
        }
        }
    }
    static void writeString(std::string& out, std::string_view s) {
        out += '"';
        for (const char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else out.push_back(c);
            }
        }
        out += '"';
    }
};

} // namespace cppfm::json
