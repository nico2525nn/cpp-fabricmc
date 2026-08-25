// StringReader: cursor over a command string (Brigadier's StringReader port).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cppfm::brigadier {

class StringReader {
public:
    static constexpr char kSymbolQuote = '"';

    explicit StringReader(std::string_view s) : s_(s) {}

    std::size_t cursor() const { return pos_; }
    void setCursor(std::size_t p) {
        if (p > s_.size()) p = s_.size();
        pos_ = p;
    }
    std::size_t remainingLength() const { return s_.size() - pos_; }
    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    char peek(std::size_t ahead) const {
        return pos_ + ahead < s_.size() ? s_[pos_ + ahead] : '\0';
    }
    char read() { return pos_ < s_.size() ? s_[pos_++] : '\0'; }
    void skip() { if (pos_ < s_.size()) ++pos_; }
    bool canRead(std::size_t n = 1) const { return remainingLength() >= n; }

    void skipWhitespace() {
        while (canRead() && peek() == ' ') skip();
    }
    bool isAllowedInUnquotedString(char c) const {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.' || c == '+';
    }
    static bool isQuotedStringStart(char c) { return c == kSymbolQuote || c == '\''; }

    std::string readUnquotedString() {
        const std::size_t start = pos_;
        while (canRead() && isAllowedInUnquotedString(peek())) skip();
        return std::string(s_.substr(start, pos_ - start));
    }
    std::string readQuotedString() {
        if (!canRead()) return "";
        const char quote = read();
        if (quote != kSymbolQuote && quote != '\'')
            throw ParseError("expected quote");
        std::string out;
        while (canRead()) {
            const char c = read();
            if (c == '\\') {
                if (!canRead()) throw ParseError("bad escape");
                out.push_back(read());
            } else if (c == quote) {
                return out;
            } else out.push_back(c);
        }
        throw ParseError("unterminated quoted string");
    }
    std::string readStringUntil(char end) {
        const std::size_t start = pos_;
        while (canRead() && peek() != end) skip();
        return std::string(s_.substr(start, pos_ - start));
    }
    std::string readString() {                       // brigadier string mode
        if (!canRead()) return "";
        if (isQuotedStringStart(peek())) return readQuotedString();
        return readUnquotedString();
    }
    std::string readGreedyString() {
        const std::size_t start = pos_;
        while (canRead()) skip();
        return std::string(s_.substr(start));
    }

    std::int64_t readLong() {
        const std::size_t start = pos_;
        while (canRead() && isNumericChar(peek())) skip();
        const std::string tok(s_.substr(start, pos_ - start));
        if (tok.empty()) throw ParseError("expected long");
        try {
            std::size_t used = 0;
            const std::int64_t v = std::stoll(tok, &used);
            if (used != tok.size()) throw ParseError("invalid long");
            return v;
        } catch (const std::out_of_range&) {
            throw ParseError("long out of range");
        }
    }
    std::int32_t readInt() {
        const std::int64_t v = readLong();
        if (v < INT32_MIN || v > INT32_MAX) throw ParseError("integer out of range");
        return static_cast<std::int32_t>(v);
    }
    double readDouble() {
        const std::size_t start = pos_;
        while (canRead() && (isNumericChar(peek()) || peek() == 'e' || peek() == 'E')) skip();
        const std::string tok(s_.substr(start, pos_ - start));
        if (tok.empty()) throw ParseError("expected double");
        try {
            std::size_t used = 0;
            const double v = std::stod(tok, &used);
            if (used != tok.size()) throw ParseError("invalid double");
            return v;
        } catch (const std::out_of_range&) {
            throw ParseError("double out of range");
        }
    }
    float readFloat() { return static_cast<float>(readDouble()); }

    struct ParseError : std::runtime_error {
        explicit ParseError(const std::string& w) : std::runtime_error(w) {}
    };

private:
    static bool isNumericChar(char c) {
        return (c >= '0' && c <= '9') || c == '.' || c == '-';
    }
    std::string_view s_;
    std::size_t pos_ = 0;

public:
    // Slice of the underlying input between two cursors (for token capture).
    std::string slice(std::size_t from) const {
        const std::size_t to = pos_;
        if (from > to || to > s_.size()) return {};
        return std::string(s_.substr(from, to - from));
    }
    std::string remainingText() const {
        return pos_ < s_.size() ? std::string(s_.substr(pos_)) : std::string();
    }
};

} // namespace cppfm::brigadier
