// FunctionEvaluator: executes datapack functions with store/score/return/schedule
// Plan13 §10: handles execute store/score, return value propagation, and scheduled execution.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <functional>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include "../brigadier/Tree.hpp"

namespace cppfm {

class GameServer;
struct Player;
struct Scoreboard;

class FunctionEvaluator {
public:
    struct ScheduledEntry {
        std::string functionId;
        std::int64_t dueTick = 0;
        std::int64_t interval = 0; // 0 = once, >0 = repeating (not used)
        bool replace = true;
        std::string source; // who scheduled
    };

    explicit FunctionEvaluator(GameServer* srv = nullptr) : server_(srv) {}

    void setServer(GameServer* srv) { server_ = srv; }

    // Execute a function by id, with given source. Returns result value (from return command) or last command result.
    // Handles recursion limit (max 10).
    int executeFunction(const std::string& id, brigadier::CommandSource src);
    // plan37 macro minimal: $var replacement with args map (for `function <id> {arg: value}`)
    int executeFunction(const std::string& id, brigadier::CommandSource src, const std::map<std::string,std::string>& args);
    // helper for macro expansion
    static std::string expandMacro(const std::string& line, const std::map<std::string,std::string>& args);

    // Execute a single line as command via server's dispatcher, capturing return value.
    int executeLine(const std::string& line, brigadier::CommandSource src);

    // Schedule a function to run after delayTicks (timeArg). Mode: "replace" (default) or "append".
    void scheduleFunction(const std::string& id, std::int64_t delayTicks, const std::string& mode, std::int64_t nowTick);

    // Called each tick to run due scheduled functions
    void tick(std::int64_t nowTick);

    // Return handling for current function execution
    void setReturnValue(int v) { hasReturn_ = true; returnValue_ = v; }
    bool hasReturn() const { return hasReturn_; }
    int getReturnValue() const { return returnValue_; }
    void clearReturn() { hasReturn_ = false; returnValue_ = 0; }

    // Execute with store: wraps a command execution and stores result/success into scoreboard
    // storeType: "result" or "success", target selector string, objective name
    int executeWithStore(const std::string& storeType,
                         const std::string& targetSelector,
                         const std::string& objective,
                         const std::string& innerCommand,
                         brigadier::CommandSource src);

    // For testing: get scheduled count
    size_t scheduledCount() const { return scheduled_.size(); }
    const std::vector<ScheduledEntry>& scheduled() const { return scheduled_; }

    void clearScheduled() { scheduled_.clear(); }

private:
    GameServer* server_ = nullptr;
    std::vector<ScheduledEntry> scheduled_;
    int recursionDepth_ = 0;
    static constexpr int kMaxRecursion = 10;
    bool hasReturn_ = false;
    int returnValue_ = 0;
    std::unordered_map<std::string, std::int64_t> scheduledMap_; // id -> dueTick for replace logic

    std::vector<std::string> getFunctionLines(const std::string& id);
};

} // namespace cppfm
