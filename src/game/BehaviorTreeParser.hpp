// Adapter for data-driven entity behavior definitions.
#pragma once
#include <memory>
#include "BehaviorTree.hpp"
#include "EntityData.hpp"

namespace cppfm {

// EntityDataLoader owns the canonical behavior-node factory and recursive
// builder. Keep this named adapter for callers that use parser terminology.
class BehaviorTreeParser {
public:
    // Build a BehaviorTree from an EntityDataDef's behaviors array. Returns nullptr if definition has no behaviors.
    static std::unique_ptr<BehaviorTree> parse(const EntityDataDef& def) {
        return EntityDataLoader::buildUniqueTreeFor(def);
    }

};

} // namespace cppfm
