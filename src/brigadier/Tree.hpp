// Tree: Brigadier's CommandNode/CommandDispatcher port for 1.21.4.
//
// * Builds the declare_commands packet exactly like vanilla: nodes flattened
//   depth-first, root referenced by index; argument nodes carry parser id +
//   property blob.
// * Parses user input against the tree and executes the deepest executable
//   node whose parse consumed everything.
// * Produces tab-completion suggestions (command_suggestions response) via an
//   iterative token walk mirroring vanilla's parseNodes behaviour.
#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/ByteBuffer.hpp"
#include "Arguments.hpp"
#include "StringReader.hpp"

namespace cppfm::brigadier {

class CommandNode;
using NodePtr = std::shared_ptr<CommandNode>;

struct ExecutionResult {
    bool ok = false;
    int value = 0;
    std::string errorText;
};

struct CommandSource {
    void* player = nullptr;                          // cppfm::Player* when set
    std::string name = "Server";                     // display name
    bool console = true;
    bool hasOp = true;                               // permission level >=2
    double srcX = 0, srcY = 0, srcZ = 0;
    float srcYaw = 0, srcPitch = 0;
    std::function<void(const std::string&, SelectorResult&)> resolveSelector;
};

class CommandContext : public ParseCtx {
public:
    CommandSource source;
    std::string input;
    std::unordered_map<std::string, ArgValue> args;

    const ArgValue& arg(const std::string& n) const {
        static ArgValue none;
        auto it = args.find(n);
        return it != args.end() ? it->second : none;
    }
};

class CommandNode {
public:
    enum class Kind { Root, Literal, Argument };

    Kind kind = Kind::Root;
    std::string name;                                // literal text / arg name
    brigadier::ArgumentType argType{};               // valid when kind==Argument
    bool executable = false;
    std::vector<NodePtr> children;
    NodePtr redirect;                                // optional redirect target
    std::function<int(CommandContext&)> action;      // command body
    SuggestFn suggestions;                           // overrides argType.suggest

    static NodePtr root() {
        auto n = std::make_shared<CommandNode>();
        n->kind = Kind::Root;
        return n;
    }
    static NodePtr literal(std::string text) {
        auto n = std::make_shared<CommandNode>();
        n->kind = Kind::Literal;
        n->name = std::move(text);
        return n;
    }
    static NodePtr argument(std::string argName, ArgumentType type) {
        auto n = std::make_shared<CommandNode>();
        n->kind = Kind::Argument;
        n->name = std::move(argName);
        n->argType = std::move(type);
        return n;
    }

    NodePtr& then(NodePtr child) {
        children.push_back(std::move(child));
        return children.back();
    }

    // ---------------------------------------------------------- execution --
    // Depth-first match; executes the deepest executable node that consumes
    // the whole line. `isFirstToken` permits a leading token without space.
    bool executeAt(StringReader& reader, CommandContext& ctx,
                   ExecutionResult& res, bool isFirstToken = true) const {
        const std::string rest = reader.remainingText();
        const bool onlySpacesLeft =
            !rest.empty() && rest.find_first_not_of(' ') == std::string::npos;

        if (executable && (!reader.canRead() || onlySpacesLeft)) {
            try {
                res.value = action ? action(ctx) : 1;
                res.ok = true;
            } catch (const std::runtime_error& e) {
                res.ok = false;
                res.errorText = e.what();
            }
            return true;
        }
        if (!reader.canRead()) return false;

        const std::size_t wsMark = reader.cursor();
        if (reader.peek() == ' ') reader.skip();
        else if (!isFirstToken) return false;
        if (!reader.canRead()) { reader.setCursor(wsMark); return false; }

        for (const auto& child : children) {
            const std::size_t mark = reader.cursor();
            try {
                if (child->kind == Kind::Literal) {
                    const std::string word = reader.readUnquotedString();
                    if (word.empty() || word != child->name) {
                        reader.setCursor(mark);
                        continue;
                    }
                } else {
                    ctx.args[child->name] = child->argType.parse(reader, ctx);
                    if (reader.cursor() == mark) {
                        reader.setCursor(mark);
                        continue;
                    }
                }
                if (child->executeAt(reader, ctx, res, false)) return true;
                reader.setCursor(mark);
            } catch (const std::exception&) {
                reader.setCursor(mark);
            }
        }
        reader.setCursor(wsMark);
        return false;
    }

    // -------------------------------------------------------- wire output --
    void flatten(std::vector<const CommandNode*>& order,
                 std::unordered_map<const CommandNode*, int>& index) const {
        std::function<void(const CommandNode*)> walk = [&](const CommandNode* n) {
            index[n] = static_cast<int>(order.size());
            order.push_back(n);
            for (auto& c : n->children) walk(c.get());
        };
        walk(this);
    }

    void writeNode(WriteBuffer& b,
                   const std::unordered_map<const CommandNode*, int>& index) const {
        std::uint8_t flags = 0;
        switch (kind) {
        case Kind::Root: flags = 0x00; break;
        case Kind::Literal: flags = 0x01; break;
        case Kind::Argument: flags = 0x02; break;
        }
        if (executable) flags |= 0x04;
        if (redirect) flags |= 0x08;
        b.u8(flags);
        b.varint(static_cast<std::int32_t>(children.size()));
        for (auto& c : children) {
            auto it = index.find(c.get());
            b.varint(it != index.end() ? it->second : 0);
        }
        if (redirect) {
            auto it = index.find(redirect.get());
            b.varint(it != index.end() ? it->second : 0);
        }
        if (kind == Kind::Literal) b.string(name);
        else if (kind == Kind::Argument) {
            b.string(name);
            b.varint(static_cast<std::int32_t>(argType.id));
            if (argType.writeProps) argType.writeProps(b);
        }
    }
};

class CommandDispatcher {
public:
    NodePtr root = CommandNode::root();

    NodePtr registerCommand(NodePtr built) {
        for (auto& c : built->children) root->then(c);
        return root;
    }

    ExecutionResult execute(const std::string& line, CommandSource src) {
        ExecutionResult res;
        CommandContext ctx;
        ctx.source = std::move(src);
        ctx.input = line;
        ctx.srcX = ctx.source.srcX;
        ctx.srcY = ctx.source.srcY;
        ctx.srcZ = ctx.source.srcZ;
        ctx.srcYaw = ctx.source.srcYaw;
        ctx.srcPitch = ctx.source.srcPitch;
        ctx.resolveSelector = ctx.source.resolveSelector;
        StringReader r(line);
        if (!root->executeAt(r, ctx, res)) {
            res.ok = false;
            if (res.errorText.empty())
                res.errorText = "Unknown or incomplete command, see below for error";
        }
        return res;
    }

    struct Suggestion { std::string match, tooltip; };

    std::vector<Suggestion> suggest(const std::string& line, CommandSource src) {
        CommandContext ctx;
        ctx.source = std::move(src);
        ctx.input = line;
        ctx.srcX = ctx.source.srcX;
        ctx.srcY = ctx.source.srcY;
        ctx.srcZ = ctx.source.srcZ;
        ctx.resolveSelector = ctx.source.resolveSelector;
        std::vector<Suggestion> out;
        StringReader r(line);

        const CommandNode* cur = root.get();
        bool firstToken = true;
        int guard = 0;
        while (++guard < 64) {
            // consume separators, then detect a bare boundary
            bool skippedSpace = false;
            while (r.canRead() && r.peek() == ' ') { r.skip(); skippedSpace = true; }
            if (!r.canRead()) {                          // trailing boundary
                if (!firstToken || skippedSpace || line.empty())
                    for (const auto& c : cur->children)
                        addSuggestionsFor(*c, r, ctx, "", out);
                break;
            }
            // capture current token
            const std::size_t tokStart = r.cursor();
            while (r.canRead() && r.peek() != ' ') r.skip();
            const std::string tok = r.slice(tokStart);
            const bool atEnd = !r.canRead();
            const CommandNode* fullLiteral = nullptr;
            for (const auto& c : cur->children) {
                if (c->kind != CommandNode::Kind::Literal) continue;
                if (c->name.rfind(tok, 0) == 0) {
                    if (c->name == tok) fullLiteral = c.get();
                    else out.push_back({c->name, ""});
                }
            }
            if (fullLiteral && atEnd) break;             // complete as typed
            if (fullLiteral) { cur = fullLiteral; firstToken = false; continue; }

            // argument suggestions filtered by current partial token
            for (const auto& c : cur->children) {
                if (c->kind != CommandNode::Kind::Argument) continue;
                const SuggestFn& fn =
                    c->suggestions ? c->suggestions : c->argType.suggest;
                if (!fn) continue;
                for (auto& m : fn(r, ctx))
                    if (m.size() >= tok.size() &&
                        m.compare(0, tok.size(), tok) == 0)
                        out.push_back({m, ""});
            }
            // try to descend through a fully-parsed argument
            bool descended = false;
            for (const auto& c : cur->children) {
                if (c->kind != CommandNode::Kind::Argument) continue;
                r.setCursor(tokStart);
                try {
                    ctx.args[c->name] = c->argType.parse(r, ctx);
                    if (r.cursor() > tokStart) {
                        cur = c.get();
                        descended = true;
                        break;
                    }
                } catch (const std::exception&) {}
                r.setCursor(tokStart);
            }
            if (descended) { firstToken = false; continue; }
            break;
        }
        (void)firstToken;
        std::sort(out.begin(), out.end(),
                  [](auto& a, auto& b) { return a.match < b.match; });
        out.erase(std::unique(out.begin(), out.end(),
                              [](auto& a, auto& b) { return a.match == b.match; }),
                  out.end());
        return out;
    }

    // Serializes the full declare_commands payload body (packet id excluded).
    void writeDeclareCommands(WriteBuffer& b) const {
        std::vector<const CommandNode*> order;
        std::unordered_map<const CommandNode*, int> index;
        root->flatten(order, index);
        b.varint(static_cast<std::int32_t>(order.size()));
        for (const CommandNode* n : order) n->writeNode(b, index);
        b.varint(0);                                 // root index
    }

private:
    static void addSuggestionsFor(const CommandNode& c, StringReader& r,
                                  CommandContext& ctx,
                                  std::string_view prefix,
                                  std::vector<Suggestion>& out) {
        if (c.kind == CommandNode::Kind::Literal) {
            if (prefix.empty() || c.name.rfind(prefix, 0) == 0)
                out.push_back({c.name, ""});
            return;
        }
        const SuggestFn& fn = c.suggestions ? c.suggestions : c.argType.suggest;
        if (!fn) return;
        for (auto& m : fn(r, ctx))
            if (prefix.empty() ||
                (m.size() >= prefix.size() && m.compare(0, prefix.size(), prefix) == 0))
                out.push_back({m, ""});
    }
};

} // namespace cppfm::brigadier
