#include "FunctionEvaluator.hpp"
#include "GameServer.hpp"
#include "DatapackManager.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>

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
    // D26: handle scoreboard players reset directly for function support (wildcard vs specific, Prismarine 0x49)
    if (line.rfind("scoreboard players reset", 0) == 0) {
        std::istringstream iss(line);
        std::vector<std::string> tokens; std::string tok;
        while (iss >> tok) tokens.push_back(tok);
        if (tokens.size() >= 4) {
            std::string holder = tokens[3];
            std::string obj; std::string* objPtr = nullptr;
            if (tokens.size() >= 5) { obj = tokens[4]; objPtr = &obj; }
            std::vector<std::string> holders;
            if (!holder.empty() && holder[0] == '@') {
                auto sel = server_->resolveSelector(holder, static_cast<Player*>(src.player));
                holders = sel.playerNames;
                if (holders.empty()) holders.push_back(holder);
            } else {
                holders.push_back(holder);
            }
            int cnt = 0;
            for (auto& h : holders) {
                if (objPtr) { if (server_->scoreboard.resetScore(h, *objPtr)) { server_->sendResetScoreAll(h, objPtr); ++cnt; } }
                else { auto aff = server_->scoreboard.resetAllScores(h); if (!aff.empty()) { server_->sendResetScoreAllWildcard(h); ++cnt; } }
            }
            return cnt;
        }
    }
    // Handle return command directly — plan38 B-13: return run <command> propagation
    if (line.rfind("return ", 0) == 0) {
        std::string rest = line.substr(7);
        size_t s = rest.find_first_not_of(" \t");
        if (s != std::string::npos) rest = rest.substr(s);
        else rest.clear();
        if (rest.rfind("run ", 0) == 0) {
            std::string inner = rest.substr(4);
            size_t t = inner.find_first_not_of(" \t");
            if (t != std::string::npos) inner = inner.substr(t);
            else inner.clear();
            if (inner.empty()) { setReturnValue(0); return 0; }
            auto res = server_->commands().execute(inner, std::move(src));
            int v = res.ok ? res.value : 0;
            setReturnValue(v);
            return v;
        }
        try {
            int v = std::stoi(rest);
            setReturnValue(v);
            return v;
        } catch (...) {
            setReturnValue(0);
            return 0;
        }
    }
    // Handle schedule inside function? schedule is top-level but can be executed as line Dispatch via server's dispatcher
    auto res = server_->commands().execute(line, std::move(src));
    if (!res.ok) {
        // if command failed, return 0? but still propagate
        return 0;
    }
    // If this command was a function call that set return, propagate
    if (hasReturn()) return getReturnValue();
    return res.value;
}

std::string FunctionEvaluator::expandMacro(const std::string& line, const std::map<std::string,std::string>& args) {
    if (args.empty()) return line;
    if (line.empty() || line[0] != '$') return line;
    std::string body = line.substr(1);
    size_t s = body.find_first_not_of(" \t");
    if (s != std::string::npos) body = body.substr(s);
    else body.clear();
    // 1) $(var) first (must before $var to avoid prefix overlap)
    for (auto& [k, v] : args) {
        std::string pat2 = "$(" + k + ")";
        size_t pos = 0;
        while ((pos = body.find(pat2, pos)) != std::string::npos) {
            body.replace(pos, pat2.size(), v);
            pos += v.size();
        }
    }
    // 2) $var legacy
    for (auto& [k, v] : args) {
        std::string pat1 = "$" + k;
        size_t pos = 0;
        while ((pos = body.find(pat1, pos)) != std::string::npos) {
            body.replace(pos, pat1.size(), v);
            pos += v.size();
        }
    }
    if (body.find("$(") != std::string::npos) return "";
    return body;
}

int FunctionEvaluator::executeFunction(const std::string& id, brigadier::CommandSource src) {
    return executeFunction(id, std::move(src), {});
}

int FunctionEvaluator::executeFunction(const std::string& id, brigadier::CommandSource src, const std::map<std::string,std::string>& args) {
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
    for (auto origLine : lines) {
        if (origLine.empty()) continue;
        bool isMacro = !origLine.empty() && origLine[0] == '$';
        std::string line = expandMacro(origLine, args);
        if (isMacro && line.empty()) { lastResult = 0; break; } // missing var -> fail
        std::string toExec = isMacro ? line : origLine;
        // Prepare source for each line (copy)
        brigadier::CommandSource lineSrc = src;
        // If hasReturn, break (return command stops function)
        if (hasReturn()) {
            lastResult = getReturnValue();
            break;
        }
        int res = executeLine(toExec, lineSrc);
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
