// Mojang session-server auth helpers (libcurl).
#pragma once
#include <string>

namespace cppfm {

// Uses the system curl binary (present on virtually all servers).
inline std::string httpGet(const std::string& url, long timeoutSec = 10) {
    std::string cmd = "curl -sS --max-time " + std::to_string(timeoutSec) +
                      " --fail '" + url + "' 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) throw std::runtime_error("popen curl failed");
    std::string out;
    char buf[4096];
    std::size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const int rc = pclose(f);
    if (rc != 0 && out.empty()) throw std::runtime_error("curl exited " + std::to_string(rc));
    return out;
}

struct HasJoinedResult {
    std::string uuidNoDashes;
    struct Prop { std::string name, value, signature; };
    std::vector<Prop> props;
};

// Minimal JSON scan for hasJoined response: {"id":"..","name":"..","properties":[{"name":"textures","value":"..","signature":".."}]}
inline bool parseHasJoined(const std::string& json, HasJoinedResult& out) {
    auto findStr = [&](const std::string& key) -> std::string {
        const std::string k = "\"" + key + "\":\"";
        const auto p = json.find(k);
        if (p == std::string::npos) return {};
        const auto s = p + k.size();
        const auto e = json.find('"', s);
        if (e == std::string::npos) return {};
        return json.substr(s, e - s);
    };
    out.uuidNoDashes = findStr("id");
    if (out.uuidNoDashes.size() != 32) return false;
    // properties array entries
    const auto parr = json.find("\"properties\"");
    if (parr != std::string::npos) {
        std::size_t pos = parr;
        while (true) {
            pos = json.find("\"name\":\"", pos + 1);
            if (pos == std::string::npos) break;
            const auto ns = pos + 8;
            const auto ne = json.find('"', ns);
            if (ne == std::string::npos) break;
            HasJoinedResult::Prop prop;
            prop.name = json.substr(ns, ne - ns);
            const std::string vkey = "\"value\":\"";
            const auto vs = json.find(vkey, ne);
            if (vs == std::string::npos) break;
            const auto vsStart = vs + vkey.size();
            const auto ve = json.find('"', vsStart);
            prop.value = json.substr(vsStart, ve - vsStart);
            const std::string gkey = "\"signature\":\"";
            const auto gs = json.find(gkey, ve);
            if (gs != std::string::npos && gs < ve + 64) {
                const auto gsStart = gs + gkey.size();
                const auto ge = json.find('"', gsStart);
                prop.signature = json.substr(gsStart, ge - gsStart);
            }
            out.props.push_back(prop);
        }
    }
    return true;
}

} // namespace cppfm
