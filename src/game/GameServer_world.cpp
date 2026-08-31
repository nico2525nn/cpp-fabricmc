#include "GameServer.hpp"
#include "BlockEvent.hpp"
#include "MetadataTypes.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../worldgen/PortalHandler.hpp"
#include "../core/Json.hpp"
#include "GameServerHelpers.hpp"
#include "StairsHelper.hpp"
#include "Constants.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/EntityIds.hpp"
#include "MenuInteraction.hpp"
#include "BehaviorTree.hpp"
#include "BehaviorTreeParser.hpp"
#include "EquipmentComponent.hpp"
#include "DamageComponent.hpp"
#include "EnchantmentHelper.hpp"
#include "MobSpawner.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "CostCalculator.hpp"
#include "PotionBrewing.hpp"
#include "Particles.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppfm {
using namespace proto;
static void savePlayerNBT(const std::string& path, Player& p) {
    WriteBuffer out;
    out.u8(10); out.u16(0);                            // root compound
    out.u8(5); out.u16(6); out.raw("Health", 6); out.f32(p.health);
    out.u8(3); out.u16(9); out.raw("foodLevel", 9); out.i32(p.food);
    out.u8(5); out.u16(10); out.raw("foodSaturation", 10); out.f32(p.saturation);
    out.u8(3); out.u16(9); out.raw("XpLevel", 9); out.i32(p.xp.level);
    out.u8(3); out.u16(9); out.raw("XpTotal", 9); out.i32(p.xp.totalXp);
    out.u8(5); out.u16(13); out.raw("XpP", 13); out.f32(p.xp.progress);
    // playerDim / pos
    out.u8(3); out.u16(3); out.raw("Dim", 3); out.i32(static_cast<std::int32_t>(p.dimension));
    out.u8(9); out.u16(3); out.raw("Pos", 3);
    out.u8(6); out.i32(3);
    out.f64(p.x); out.f64(p.y); out.f64(p.z);
    out.u8(9); out.u16(9); out.raw("Inventory", 9);
    int count = 0;
    for (int i = 0; i < 46; ++i)
        if (!p.inv[i].empty()) ++count;
    out.i32(count);
    for (int i = 0; i < 46; ++i) {
        const auto& sl = p.inv[i];
        if (sl.empty()) continue;
        out.u8(10);
        const std::string nm = sl.name();
        out.u16((uint16_t)nm.size()); out.raw(nm.data(), nm.size());
        out.u8(1); out.u16(5); out.raw("Count", 5); out.i8((int8_t)sl.count);
        out.u8(1); out.u16(4); out.raw("Slot", 4); out.i8((int8_t)i);
        out.u8(0);
    }
    out.u8(0);
    out.u8(0);
    std::filesystem::create_directories(
        path.substr(0, path.find_last_of('/')));
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data.data()), out.data.size());
}

static bool loadPlayerNBT(const std::string& path, Player& p) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 10 || bytes[0] != 10) return false;
    try {
        ReadBuffer r(bytes);
        nbt::Parser parser(r);
        nbt::Value root = parser.readFileRoot();
        if (const auto* v = root.get("Health")) p.health = v->f;
        if (const auto* v = root.get("foodLevel")) p.food = v->i;
        if (const auto* v = root.get("foodSaturation")) p.saturation = v->f;
        if (const auto* v = root.get("XpLevel")) p.xp.level = v->i;
        if (const auto* v = root.get("XpTotal")) p.xp.totalXp = v->i;
        if (const auto* v = root.get("XpP")) p.xp.progress = v->f;
        if (const auto* v = root.get("Dim"))
            p.dimension = static_cast<std::int8_t>(v->i);
        if (const auto* v = root.get("Pos")) {
            if (v->list.size() == 3) {
                p.x = v->list[0].d; p.y = v->list[1].d; p.z = v->list[2].d;
                p.prevFeetY = p.y;
            }
        }
        if (const auto* invv = root.get("Inventory")) {
            for (const auto& item : invv->list) {
                const auto* idv = item.get("id");
                const auto* cv = item.get("Count");
                const auto* sv = item.get("Slot");
                if (!idv || !sv) continue;
                auto it = gen::itemIdByName().find(idv->str);
                if (it == gen::itemIdByName().end()) continue;
                const int slot = sv->b;
                if (slot < 0 || slot >= 46) continue;
                p.inv[slot] = ItemStack::of(it->second,
                                            cv ? static_cast<std::int16_t>(cv->b) : 1);
            }
        }
        return true;
    } catch (...) { return false; }
}

void GameServer::saveLevelData() {
    persist_->saveLevelData(tickNo_, dayTime());
}
void GameServer::loadLevelData() {
    persist_->loadLevelData();
}
void GameServer::savePlayerData(const std::string& uuidHex, Player& p) {
    std::filesystem::create_directories(cfg_.worldDir + "/playerdata");
    savePlayerNBT(cfg_.worldDir + "/playerdata/" + uuidHex + ".dat", p);
}
bool GameServer::loadPlayerData(const std::string& uuidHex, Player& p) {
    return loadPlayerNBT(cfg_.worldDir + "/playerdata/" + uuidHex + ".dat", p);
}
void GameServer::loadOps() {
    ops_.clear();
    try {
        std::ifstream f("ops.json");
        if (f) {
            std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto v = json::Value::parse(txt);
            if (v.isArr()) {
                for (auto& e : v.arr) {
                    if (e.isObj()) {
                        if (auto* n = e.find("name")) ops_.insert(n->asStr());
                    } else if (e.isStr()) ops_.insert(e.asStr());
                }
            } else if (v.isObj()) {
                for (auto& [k,_] : v.obj) ops_.insert(k);
            }
        }
    } catch (...) {}
    // also allow ops.txt one name per line fallback
    try {
        std::ifstream f2("ops.txt");
        std::string line;
        while (std::getline(f2, line)) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            if (!line.empty()) ops_.insert(line);
        }
    } catch (...) {}
}
void GameServer::saveOps() const {
    try {
        std::ofstream f("ops.json", std::ios::trunc);
        if (!f) return;
        f << "[\n";
        bool first = true;
        for (auto& n : ops_) {
            if (!first) f << ",\n";
            first = false;
            f << "  {\"name\":\"" << n << "\",\"level\":4,\"bypassesPlayerLimit\":false}";
        }
        f << "\n]\n";
    } catch (...) {}
}
void GameServer::loadBans() {
    bannedPlayers_.clear();
    try {
        std::ifstream f("banned-players.json");
        if (!f) return;
        std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto v = json::Value::parse(txt);
        if (v.isArr()) {
            for (auto& e : v.arr) {
                if (e.isObj()) {
                    if (auto* n = e.find("name")) bannedPlayers_.insert(n->asStr());
                } else if (e.isStr()) bannedPlayers_.insert(e.asStr());
            }
        } else if (v.isObj()) {
            for (auto& [k,_] : v.obj) bannedPlayers_.insert(k);
        }
    } catch (...) {}
}
void GameServer::saveBans() const {
    try {
        std::ofstream f("banned-players.json", std::ios::trunc);
        if (!f) return;
        f << "[\n";
        bool first = true;
        for (auto& n : bannedPlayers_) {
            if (!first) f << ",\n";
            first = false;
            f << "  {\"name\":\"" << n << "\",\"reason\":\"Banned by an operator.\"}";
        }
        f << "\n]\n";
    } catch (...) {}
}
void GameServer::loadBannedIps() {
    bannedIps_.clear();
    try {
        std::ifstream f("banned-ips.json");
        if (!f) return;
        std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto v = json::Value::parse(txt);
        if (v.isArr()) {
            for (auto& e : v.arr) {
                if (e.isObj()) {
                    if (auto* ip = e.find("ip")) bannedIps_.insert(ip->asStr());
                    else if (auto* n = e.find("name")) bannedIps_.insert(n->asStr());
                } else if (e.isStr()) bannedIps_.insert(e.asStr());
            }
        } else if (v.isObj()) {
            for (auto& [k,_] : v.obj) bannedIps_.insert(k);
        }
    } catch (...) {}
}
void GameServer::saveBannedIps() const {
    try {
        std::ofstream f("banned-ips.json", std::ios::trunc);
        if (!f) return;
        f << "[\n";
        bool first = true;
        for (auto& ip : bannedIps_) {
            if (!first) f << ",\n";
            first = false;
            f << "  {\"ip\":\"" << ip << "\",\"reason\":\"Banned by an operator.\"}";
        }
        f << "\n]\n";
    } catch (...) {}
}
void GameServer::saveWhitelist() const {
    try { whitelist_.save("whitelist.json"); } catch (...) {}
}
void GameServer::kickPlayer(const std::string& name, const std::string& reason) {
    Player* t = nullptr;
    for (auto& p : playersSnapshot()) if (p->name == name) { t = p.get(); break; }
    if (!t || !t->conn) return;
    std::string txt = reason.empty() ? "Kicked by an operator." : reason;
    WriteBuffer b;
    nbt::writeTextComponent(b, txt);
    try { t->conn->sendPacket(proto::pl::sc::Disconnect, b); } catch (...) {}
    try { t->conn->close(); } catch (...) {}
}
void GameServer::sendWorldBorderTo(Player& p) const {
    if (!p.conn) return;
    // InitializeWorldBorder full packet — Yarn WorldBorder 59999968, lerp interpolation
    WriteBuffer i;
    i.f64(worldBorderCenterX_); i.f64(worldBorderCenterZ_);
    double oldSize = worldBorderDiameter_;
    double newSize = worldBorderDiameter_;
    std::int64_t lerpMs = 0;
    if (worldBorderLerpRemainingTicks_ > 0) {
        oldSize = worldBorderDiameter_;
        newSize = worldBorderLerpTo_;
        lerpMs = worldBorderLerpMs_;
        // if at start, oldSize should be lerpFrom (diameter is from)
        // current diameter already interpolates, so oldSize is current
        // but for packet spec, we send current->target with remaining time
        // maintain vanilla: old = current, new = target
    }
    i.f64(oldSize); i.f64(newSize);
    i.varlong(lerpMs);
    i.varint((int)constants::kWorldBorderDiameter); // max world border (portalTeleportBoundary)
    i.varint(5);  // warning blocks
    i.varint(15); // warning time
    try { p.conn->sendPacket(proto::pl::sc::InitializeWorldBorder, i); } catch (...) {}
    // also send Center for spec compliance (separate packet)
    WriteBuffer c;
    c.f64(worldBorderCenterX_); c.f64(worldBorderCenterZ_);
    try { p.conn->sendPacket(proto::pl::sc::WorldBorderCenter, c); } catch (...) {}
    // Lerp-specific separate packets if active
    if (worldBorderLerpRemainingTicks_ > 0) {
        WriteBuffer l;
        l.f64(oldSize); l.f64(newSize); l.varlong(lerpMs);
        try { p.conn->sendPacket(proto::pl::sc::WorldBorderLerpSize, l); } catch (...) {}
    } else {
        WriteBuffer s;
        s.f64(newSize);
        try { p.conn->sendPacket(proto::pl::sc::WorldBorderSize, s); } catch (...) {}
    }
}
void GameServer::broadcastWorldBorder() {
    for (auto& p : playersSnapshot()) {
        if (!p->inPlay || !p->conn) continue;
        sendWorldBorderTo(*p);
    }
    if (persist_) {
        if (worldBorderLerpRemainingTicks_ > 0) {
            double cur = worldBorderDiameter_;
            persist_->setWorldBorder(cur, worldBorderCenterX_, worldBorderCenterZ_);
            persist_->setWorldBorderLerp(cur, worldBorderLerpTo_, worldBorderLerpRemainingTicks_);
        } else {
            persist_->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
        }
        persist_->saveLevelData(tickNo_, dayTime());
    }
}
std::string GameServer::dispatchConsole(const std::string& line) {
    brigadier::CommandSource src;
    src.console = true;
    src.srcX = 0; src.srcY = -60; src.srcZ = 0;
    src.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = resolveSelector(raw, nullptr);
    };
    const auto res = commands_.execute(line, std::move(src));
    return res.ok ? "ok" : ("error: " + res.errorText);
}
} // namespace cppfm
