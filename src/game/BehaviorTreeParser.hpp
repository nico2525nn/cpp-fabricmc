// via type strings and children recursion.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "BehaviorTree.hpp"
#include "EntityData.hpp"

namespace cppfm {

// Parser that builds a BehaviorTree from a list of behaviors (data-driven). Each behavior type string is resolved via createNodeForType()
// factory. Composite types "selector"/"sequence" become composite nodes with children.
class BehaviorTreeParser {
public:
    // Build a BehaviorTree from an EntityDataDef's behaviors array. Returns nullptr if definition has no behaviors.
    static std::unique_ptr<BehaviorTree> parse(const EntityDataDef& def) {
        return EntityDataLoader::buildUniqueTreeFor(def);
    }

    static std::unique_ptr<BehaviorTree> parseFlat(const std::vector<std::pair<std::string,int>>& entries) {
        return buildBehaviorTreeFromTypes(entries);
    }

    // Build from a single Behavior node recursively (handles selector/sequence nesting).
    static std::unique_ptr<BehaviorNode> parseNode(const EntityDataDef::Behavior& beh) {
        // Directly use the same logic as EntityData.cpp's nodeFromBehavior
        // We delegate to a helper that mirrors that function to avoid duplication.
        return nodeFromBehaviorImpl(beh);
    }

private:
    static std::string toLowerCopy(std::string s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }
    static std::string stripPrefix(const std::string& t) {
        auto pos = t.find(':'); if (pos!=std::string::npos) return t.substr(pos+1);
        return t;
    }
    static bool isSelector(const std::string& raw) {
        std::string t = toLowerCopy(stripPrefix(raw));
        return t=="selector" || t=="select";
    }
    static bool isSequence(const std::string& raw) {
        std::string t = toLowerCopy(stripPrefix(raw));
        return t=="sequence" || t=="seq";
    }
    static std::unique_ptr<BehaviorNode> nodeFromBehaviorImpl(const EntityDataDef::Behavior& beh) {
        if (isSelector(beh.type)) {
            auto sel = std::make_unique<SelectorNode>();
            for (auto& ch : beh.children) {
                auto node = nodeFromBehaviorImpl(ch);
                if (node) sel->addChild(std::move(node));
            }
            return sel;
        }
        if (isSequence(beh.type)) {
            auto seq = std::make_unique<SequenceNode>();
            for (auto& ch : beh.children) {
                auto node = nodeFromBehaviorImpl(ch);
                if (node) seq->addChild(std::move(node));
            }
            return seq;
        }
        if (!beh.children.empty()) {
            auto seq = std::make_unique<SequenceNode>();
            for (auto& ch : beh.children) {
                auto node = nodeFromBehaviorImpl(ch);
                if (node) seq->addChild(std::move(node));
            }
            auto leaf = createNodeForType(beh.type);
            if (leaf) seq->addChild(std::move(leaf));
            return seq;
        }
        return createNodeForType(beh.type);
    }
};

} // namespace cppfm
