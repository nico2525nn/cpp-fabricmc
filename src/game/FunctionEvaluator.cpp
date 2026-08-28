#include "FunctionEvaluator.hpp"
#include "GameServer.hpp"
#include "DatapackManager.hpp"
#include <fstream>
#include <filesystem>

namespace cppfm {

std::vector<std::string> FunctionEvaluator::getFunctionLines(const std::string& id) {
    std::string norm = id;
    if (norm.find(':') == std::string::npos) norm = "minecraft:" + norm;
    // plan14 §6: first try DatapackManager (in-memory functions from datapack loadAll)
    if (server_) {
        if (auto* fn = server_->datapackManager().getFunction(norm)) {
            // trim comments and leading slash like filesystem path
            std::vector<std::string> out;
            out.reserve(fn->size());
            for (auto &line : *fn) {
                size_t start = line.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) continue;
                size_t end = line.find_last_not_of(" \t\r\n");
                std::string trimmed = line.substr(start, end - start + 1);
                if (trimmed.empty() || trimmed[0] == '#') continue;
                if (!trimmed.empty() && trimmed.front() == '/') trimmed = trimmed.substr(1);
                out.push_back(trimmed);
            }
            if (!out.empty()) return out;
        }
    }
    // Try filesystem: assets/data/<ns>/functions/<path>.mcfunction
    auto colon = norm.find(':');
    std::string ns = colon != std::string::npos ? norm.substr(0, colon) : "minecraft";
    std::string path = colon != std::string::npos ? norm.substr(colon+1) : norm;
    std::string file = "assets/data/" + ns + "/functions/" + path + ".mcfunction";
    std::ifstream f(file);
    if (!f) {
        // try world/datapacks
        namespace fs = std::filesystem;
        std::error_code ec;
        if (fs::exists("world/datapacks", ec)) {
            for (auto& entry : fs::directory_iterator("world/datapacks", ec)) {
                if (!entry.is_directory(ec)) continue;
                std::string alt = entry.path().string() + "/data/" + ns + "/functions/" + path + ".mcfunction";
                std::ifstream f2(alt);
                if (f2) { f = std::move(f2); break; }
            }
        }
    }
    if (!f) return {};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(start, end - start + 1);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        if (!trimmed.empty() && trimmed.front() == '/') trimmed = trimmed.substr(1);
        lines.push_back(trimmed);
    }
    return lines;
}

int FunctionEvaluator::executeLine(const std::string& line, brigadier::CommandSource src) {
    if (!server_) return 0;
    // Handle return command directly
    if (line.rfind("return ", 0) == 0) {
        std::string valStr = line.substr(7);
        // trim
        size_t s = valStr.find_first_not_of(" \t");
        if (s != std::string::npos) valStr = valStr.substr(s);
        try {
            int v = std::stoi(valStr);
            setReturnValue(v);
            return v;
        } catch (...) {
            setReturnValue(0);
            return 0;
        }
    }
    // Handle schedule inside function? schedule is top-level but can be executed as line
    // Dispatch via server's dispatcher
    auto res = server_->commands().execute(line, std::move(src));
    if (!res.ok) {
        // if command failed, return 0? but still propagate
        return 0;
    }
    // If this command was a function call that set return, propagate
    if (hasReturn()) return getReturnValue();
    return res.value;
}

int FunctionEvaluator::executeFunction(const std::string& id, brigadier::CommandSource src) {
    if (recursionDepth_ >= kMaxRecursion) {
        std::fprintf(stderr, "[cppfm] function recursion limit reached for %s\n", id.c_str());
        return 0;
    }
    auto lines = getFunctionLines(id);
    if (lines.empty()) {
        // plan14 §6: function not found in datapack or filesystem -> error
        return 0;
    }
    recursionDepth_++;
    clearReturn();
    int lastResult = 0;
    for (auto& line : lines) {
        if (line.empty()) continue;
        // Prepare source for each line (copy)
        brigadier::CommandSource lineSrc = src;
        // If hasReturn, break (return command stops function)
        if (hasReturn()) {
            lastResult = getReturnValue();
            break;
        }
        int res = executeLine(line, lineSrc);
        lastResult = res;
        if (hasReturn()) {
            lastResult = getReturnValue();
            break;
        }
    }
    int final = hasReturn() ? getReturnValue() : lastResult;
    recursionDepth_--;
    if (recursionDepth_ == 0) clearReturn();
    return final;
}

void FunctionEvaluator::scheduleFunction(const std::string& id, std::int64_t delayTicks, const std::string& mode, std::int64_t nowTick) {
    std::string norm = id;
    if (norm.find(':') == std::string::npos) norm = "minecraft:" + norm;
    std::int64_t due = nowTick + delayTicks;
    if (mode == "append") {
        scheduled_.push_back({norm, due, 0, false, ""});
    } else { // replace (default)
        // remove existing with same id
        scheduled_.erase(std::remove_if(scheduled_.begin(), scheduled_.end(),
            [&](const ScheduledEntry& e){ return e.functionId == norm; }), scheduled_.end());
        scheduled_.push_back({norm, due, 0, true, ""});
    }
    // keep sorted by dueTick
    std::sort(scheduled_.begin(), scheduled_.end(),
        [](const ScheduledEntry& a, const ScheduledEntry& b){ return a.dueTick < b.dueTick; });
}

void FunctionEvaluator::tick(std::int64_t nowTick) {
    if (scheduled_.empty() || !server_) return;
    std::vector<ScheduledEntry> due;
    auto it = scheduled_.begin();
    while (it != scheduled_.end()) {
        if (it->dueTick <= nowTick) {
            due.push_back(*it);
            it = scheduled_.erase(it);
        } else ++it;
    }
    for (auto& e : due) {
        brigadier::CommandSource src;
        src.console = true;
        src.name = "Server";
        src.resolveSelector = [this](const std::string& raw, brigadier::SelectorResult& out){
            if (server_) out = server_->resolveSelector(raw, nullptr);
        };
        executeFunction(e.functionId, src);
    }
}

int FunctionEvaluator::executeWithStore(const std::string& storeType,
                         const std::string& targetSelector,
                         const std::string& objective,
                         const std::string& innerCommand,
                         brigadier::CommandSource src) {
    if (!server_) return 0;
    // Execute inner command and capture result
    brigadier::CommandSource innerSrc = src;
    auto res = server_->commands().execute(innerCommand, std::move(innerSrc));
    int value = 0;
    if (storeType == "result") {
        value = res.ok ? res.value : 0;
    } else if (storeType == "success") {
        value = res.ok ? 1 : 0;
    }
    // Store into scoreboard for each target
    auto sel = server_->resolveSelector(targetSelector, static_cast<Player*>(src.player));
    for (auto& name : sel.playerNames) {
        server_->scoreboard.setScore(objective, name, value);
        // broadcast
        server_->sendScoreAll(objective, name, value);
    }
    // also handle entityIds? For now only player scores
    return value;
}

} // namespace cppfm
