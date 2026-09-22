// Mojang session-server auth helpers (libcurl).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

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

namespace mojang_detail {
inline std::vector<std::uint8_t> base64Decode(const std::string& text) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> out;
    int value = 0;
    int bits = -8;
    bool padding = false;
    std::size_t useful = 0;
    for (unsigned char ch : text) {
        if (ch == '=') {
            padding = true;
            continue;
        }
        const char* pos = std::find(alphabet, alphabet + 64, static_cast<char>(ch));
        if (pos == alphabet + 64 || padding) return {};
        ++useful;
        value = (value << 6) | static_cast<int>(pos - alphabet);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    const auto remainder = useful % 4;
    if (text.empty() || remainder == 1 ||
        (remainder == 2 && (value & 0x0F) != 0) ||
        (remainder == 3 && (value & 0x03) != 0) ||
        (text.find('=') != std::string::npos &&
         (text.size() % 4 != 0 || text.find('=') < text.size() - 2))) return {};
    return out;
}
}

inline std::vector<std::vector<std::uint8_t>> fetchMojangProfilePublicKeys() {
    const std::string json = httpGet("https://api.minecraftservices.com/publickeys", 10);
    const auto keyNamespace = json.find("\"playerCertificateKeys\"");
    if (keyNamespace == std::string::npos) return {};
    const auto arrayBegin = json.find('[', keyNamespace);
    if (arrayBegin == std::string::npos) return {};
    const auto arrayEnd = json.find(']', arrayBegin + 1);
    if (arrayEnd == std::string::npos) return {};

    std::vector<std::vector<std::uint8_t>> keys;
    std::size_t cursor = arrayBegin + 1;
    while (cursor < arrayEnd) {
        const auto keyTag = json.find("\"publicKey\"", cursor);
        if (keyTag == std::string::npos || keyTag >= arrayEnd) break;
        const auto begin = json.find('\"', keyTag + 11);
        if (begin == std::string::npos || begin >= arrayEnd) break;
        const auto end = json.find('\"', begin + 1);
        if (end == std::string::npos || end > arrayEnd) break;
        auto decoded = mojang_detail::base64Decode(json.substr(begin + 1, end - begin - 1));
        if (!decoded.empty()) keys.push_back(std::move(decoded));
        cursor = end + 1;
    }
    return keys;
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
