#pragma once

// Shared include/alias boundary for the split command registration units.
// Keeping this list in one place prevents each command branch from drifting
// into a different transitive-header environment.
#include "GameServer.hpp"
#include "Messages.hpp"
#include "Particles.hpp"
#include "CommandsHelpers.hpp"
#include "../generated/EntityIds.hpp"
#include "../generated/BlockStates.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>

namespace cppfm {

using brigadier::CommandNode;
using brigadier::CommandContext;
namespace args = brigadier::args;
using NodePtr = brigadier::NodePtr;

} // namespace cppfm
