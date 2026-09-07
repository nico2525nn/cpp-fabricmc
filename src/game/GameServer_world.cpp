#include "GameServer.hpp"
#include "PlayerDataRecovery.hpp"
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
#include "../core/NBTValue.hpp"
#include "Anvil.hpp"
#include "RegionFile.hpp"
#include "ChunkCodec.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#include <limits>
#include <mutex>

namespace cppfm {
using namespace proto;
namespace {
constexpr std::size_t kMaxPlayerDataBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxAdminFileBytes = 8u * 1024u * 1024u;
std::mutex adminFileMutex;

bool readAdminJsonFile(const std::string& path, json::Value& value,
                       const char* label) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            std::fprintf(stderr, "[cppfm] could not read %s\n", path.c_str());
        }
        return false;
    }
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (!sizeError && size > kMaxAdminFileBytes) {
        std::fprintf(stderr, "[cppfm] refusing oversized %s (%s)\n",
                     path.c_str(), label);
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    // istreambuf_iterator reaches EOF without necessarily setting eofbit;
    // badbit is the meaningful signal for a failed file read.
    if (file.bad() || text.size() > kMaxAdminFileBytes) {
        std::fprintf(stderr, "[cppfm] could not read %s completely (%s)\n",
                     path.c_str(), label);
        return false;
    }
    try {
        value = json::Value::parse(text);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] %s is malformed: %s\n", path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[cppfm] %s is malformed\n", path.c_str());
    }
    return false;
}

bool writeAdminJsonFile(const std::string& path, const json::Value& value,
                        const char* label) {
    std::string text;
    try {
        text = value.dump();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] could not serialize %s: %s\n", label, e.what());
        return false;
    } catch (...) {
        std::fprintf(stderr, "[cppfm] could not serialize %s\n", label);
        return false;
    }
    if (text.size() > kMaxAdminFileBytes) {
        std::fprintf(stderr, "[cppfm] refusing oversized %s\n", label);
        return false;
    }

    std::lock_guard lock(adminFileMutex);
    try {
        const std::filesystem::path destination(path);
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path());
        const std::string temporary = path + ".new";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("could not open temporary file");
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.put('\n');
            file.flush();
            if (!file) throw std::runtime_error("could not flush temporary file");
        }
        std::error_code ec;
        std::filesystem::rename(temporary, destination, ec);
        if (ec) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            std::fprintf(stderr, "[cppfm] could not replace %s: %s\n",
                         path.c_str(), ec.message().c_str());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::error_code cleanupError;
        std::filesystem::remove(path + ".new", cleanupError);
        std::fprintf(stderr, "[cppfm] could not save %s: %s\n", path.c_str(), e.what());
    } catch (...) {
        std::error_code cleanupError;
        std::filesystem::remove(path + ".new", cleanupError);
        std::fprintf(stderr, "[cppfm] could not save %s\n", path.c_str());
    }
    return false;
}

template <typename Set>
void loadNameSet(const json::Value& root, Set& names, const char* field,
                 const char* label) {
    auto add = [&](const json::Value& entry) {
        if (entry.isStr()) {
            if (!entry.str.empty()) names.insert(entry.str);
            return;
        }
        if (!entry.isObj()) return;
        const auto* value = entry.find(field);
        if (value && value->isStr() && !value->str.empty())
            names.insert(value->str);
    };
    if (root.isArr()) {
        for (const auto& entry : root.arr) add(entry);
    } else if (root.isObj()) {
        // Keep accepting the old map-shaped files, but never interpret a
        // scalar/object field as a name by accident.
        for (const auto& [key, _] : root.obj)
            if (!key.empty()) names.insert(key);
    } else {
        std::fprintf(stderr, "[cppfm] ignoring non-array %s\n", label);
    }
}

void writeItemList(nbt::Value& root, const char* key, const ItemStack* slots,
                   int slotCount) {
    using namespace nbt;
    Value items = Value::makeList(Compound);
    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
        const auto& slot = slots[slotIndex];
        if (slot.empty()) continue;
        Value item = Value::makeCompound();
        item.set("id", Value::makeString(slot.name()));
        item.set("Count", Value::makeByte(static_cast<std::int8_t>(slot.count)));
        item.set("Slot", Value::makeByte(static_cast<std::int8_t>(slotIndex)));
        if (!slot.components.empty()) {
            Value components = Value::makeList(Compound);
            for (const auto& [type, payload] : slot.components) {
                Value component = Value::makeCompound();
                component.set("type", Value::makeInt(static_cast<std::int32_t>(type)));
                Value data;
                data.tag = ByteArray;
                data.byteArray = payload;
                component.set("data", std::move(data));
                components.list.push_back(std::move(component));
            }
            item.set("components", std::move(components));
        }
        if (!slot.removedComponents.empty()) {
            Value removed = Value::makeList(Int);
            for (const auto type : slot.removedComponents)
                removed.list.push_back(Value::makeInt(static_cast<std::int32_t>(type)));
            item.set("removed", std::move(removed));
        }
        items.list.push_back(std::move(item));
    }
    root.set(key, std::move(items));
}
}

static bool savePlayerNBT(const std::string& path, const Player& p) {
    using namespace nbt;
    Value root = Value::makeCompound();
    root.set("Health", Value::makeFloat(p.health));
    root.set("foodLevel", Value::makeInt(p.food));
    root.set("foodSaturation", Value::makeFloat(p.saturation));
    root.set("XpLevel", Value::makeInt(p.xp.level));
    root.set("XpTotal", Value::makeInt(p.xp.totalXp));
    root.set("XpP", Value::makeFloat(p.xp.progress));
    root.set("Dim", Value::makeInt(static_cast<std::int32_t>(p.dimension)));
    // Pos as List<Double> 3
    {
        Value pos = Value::makeList(Double);
        Value vx; vx.tag = Double; vx.d = p.x; pos.list.push_back(vx);
        Value vy; vy.tag = Double; vy.d = p.y; pos.list.push_back(vy);
        Value vz; vz.tag = Double; vz.d = p.z; pos.list.push_back(vz);
        root.set("Pos", std::move(pos));
    }
    // Keep the two vanilla inventories on one serializer so component and
    // removed-component handling cannot drift between them.
    writeItemList(root, "Inventory", p.inv.data(), static_cast<int>(p.inv.size()));
    writeItemList(root, "EnderItems", p.enderItems.data(),
                  static_cast<int>(p.enderItems.size()));
    WriteBuffer out;
    writeFileRoot(out, root);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    if (out.data.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::length_error("player data is too large");
    // Atomic: write to a sibling temporary file and replace the destination.
    const std::string tmp = path + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            std::error_code cleanupError;
            std::filesystem::remove(tmp, cleanupError);
            return false;
        }
        f.write(reinterpret_cast<const char*>(out.data.data()),
                static_cast<std::streamsize>(out.data.size()));
        f.flush();
        if (!f) {
            std::error_code cleanupError;
            std::filesystem::remove(tmp, cleanupError);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code cleanupError;
        std::filesystem::remove(tmp, cleanupError);
        std::fprintf(stderr, "[cppfm] player data save failed for %s: %s\n",
                     path.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

static bool loadPlayerNBT(const std::string& path, Player& p) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    if (!sizeError && fileSize > kMaxPlayerDataBytes) {
        std::fprintf(stderr, "[cppfm] player data too large, ignoring %s\n", path.c_str());
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    // istreambuf_iterator reaches EOF without necessarily setting eofbit.
    if (bytes.empty() || bytes.size() > kMaxPlayerDataBytes || f.bad()) return false;
    try {
        ReadBuffer r(bytes);
        nbt::Parser parser(r);
        nbt::Value root = parser.readFileRoot();
        if (const auto* v = root.get("Health")) {
            if (v->tag == nbt::Float) p.health = v->f;
            else if (v->tag == nbt::Double) p.health = static_cast<float>(v->d);
            else if (v->tag == nbt::Int) p.health = static_cast<float>(v->i);
        }
        if (const auto* v = root.get("foodLevel")) {
            if (v->tag == nbt::Int) p.food = v->i;
            else if (v->tag == nbt::Byte) p.food = v->b;
            else if (v->tag == nbt::Short) p.food = v->s;
        }
        if (const auto* v = root.get("foodSaturation")) {
            if (v->tag == nbt::Float) p.saturation = v->f;
            else if (v->tag == nbt::Double) p.saturation = static_cast<float>(v->d);
        }
        if (const auto* v = root.get("XpLevel")) p.xp.level = (v->tag==nbt::Int? v->i : (v->tag==nbt::Byte? (int)v->b : v->i));
        if (const auto* v = root.get("XpTotal")) p.xp.totalXp = (v->tag==nbt::Int? v->i : (v->tag==nbt::Byte? (int)v->b : v->i));
        if (const auto* v = root.get("XpP")) {
            if (v->tag == nbt::Float) p.xp.progress = v->f;
            else if (v->tag == nbt::Double) p.xp.progress = static_cast<float>(v->d);
        }
        if (const auto* v = root.get("Dim")) {
            if (v->tag == nbt::Int) p.dimension = static_cast<std::int8_t>(v->i);
            else if (v->tag == nbt::Byte) p.dimension = static_cast<std::int8_t>(v->b);
        }
        if (const auto* v = root.get("Pos")) {
            if (v->tag == nbt::List && v->list.size() == 3) {
                p.x = v->list[0].d; p.y = v->list[1].d; p.z = v->list[2].d;
                if (v->list[0].tag == nbt::Float) p.x = v->list[0].f;
                if (v->list[1].tag == nbt::Float) p.y = v->list[1].f;
                if (v->list[2].tag == nbt::Float) p.z = v->list[2].f;
                // handle Double vs Float
                p.prevFeetY = p.y;
            }
        }
        // helper to load Inventory or EnderItems
        auto loadItems = [&](const char* key, ItemStack* dst, int dstSize){
            const auto* lst = root.get(key);
            if (!lst || lst->tag != nbt::List) return;
            for (const auto& item : lst->list) {
                const auto* idv = item.get("id");
                const auto* sv = item.get("Slot");
                if (!idv || !sv) continue;
                auto it = gen::itemIdByName().find(idv->str);
                if (it == gen::itemIdByName().end()) continue;
                int slot = 0;
                if (sv->tag == nbt::Byte) slot = sv->b;
                else if (sv->tag == nbt::Int) slot = sv->i;
                else if (sv->tag == nbt::Short) slot = sv->s;
                else continue;
                if (slot < 0 || slot >= dstSize) continue;
                const auto* cv = item.get("Count");
                int cnt = 1;
                if (cv) {
                    if (cv->tag == nbt::Byte) cnt = cv->b;
                    else if (cv->tag == nbt::Int) cnt = cv->i;
                    else if (cv->tag == nbt::Short) cnt = cv->s;
                }
                ItemStack st = ItemStack::of(it->second, static_cast<std::int16_t>(cnt));
                // components
                if (const auto* cl = item.get("components")) {
                    if (cl->tag == nbt::List) {
                        for (const auto& ce : cl->list) {
                            const auto* tv = ce.get("type");
                            const auto* dv = ce.get("data");
                            if (!tv || !dv) continue;
                            int typeId = (tv->tag==nbt::Int? tv->i : (int)tv->b);
                            std::vector<std::uint8_t> payload;
                            if (dv->tag == nbt::ByteArray) payload = dv->byteArray;
                            else if (dv->tag == nbt::String) payload.assign(dv->str.begin(), dv->str.end());
                            st.components.emplace_back((std::uint32_t)typeId, std::move(payload));
                        }
                    }
                }
                if (const auto* rl = item.get("removed")) {
                    if (rl->tag == nbt::List) {
                        for (const auto& re : rl->list) {
                            if (re.tag == nbt::Int) st.removedComponents.push_back((std::uint32_t)re.i);
                            else if (re.tag == nbt::Byte) st.removedComponents.push_back((std::uint32_t)(std::uint8_t)re.b);
                        }
                    }
                }
                dst[slot] = std::move(st);
            }
        };
        // clear before load (important for rejoin)
        // Do not clear player position/health already set; inventory is overwritten per slot, but clear empty slots remain air – we keep existing air for slots not in file
        // To ensure round-trip, we should clear inventory before loading? We'll clear only if list exists
        if (root.get("Inventory")) {
            for (auto &s : p.inv) s = ItemStack::air();
        }
        if (root.get("EnderItems")) {
            for (auto &s : p.enderItems) s = ItemStack::air();
        }
        loadItems("Inventory", p.inv.data(), 46);
        loadItems("EnderItems", p.enderItems.data(), 27);
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
    const std::string path = cfg_.worldDir + "/playerdata/" + uuidHex + ".dat";
    try {
        std::filesystem::create_directories(cfg_.worldDir + "/playerdata");
        if (!savePlayerNBT(path, p))
            std::fprintf(stderr, "[cppfm] player data save failed for %s\n", path.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] player data save failed for %s: %s\n",
                     path.c_str(), e.what());
    } catch (...) {
        std::fprintf(stderr, "[cppfm] player data save failed for %s\n", path.c_str());
    }
}
bool GameServer::loadPlayerData(const std::string& uuidHex, Player& p) {
    // the player starts fresh; neighbours and startup are unaffected.
    return loadPlayerDataIsolated(cfg_.worldDir + "/playerdata/" + uuidHex + ".dat",
                                  [&](const std::string& path) {
                                      return loadPlayerNBT(path, p);
                                  });
}
void GameServer::loadOps() {
    ops_.clear();
    json::Value root;
    if (readAdminJsonFile("ops.json", root, "operators"))
        loadNameSet(root, ops_, "name", "ops.json");
    // also allow ops.txt one name per line fallback
    try {
        std::ifstream f2("ops.txt");
        std::string line;
        while (std::getline(f2, line)) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            if (!line.empty()) ops_.insert(line);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] could not read ops.txt: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "[cppfm] could not read ops.txt\n");
    }
}
void GameServer::saveOps() const {
    json::Value root = json::Value::array();
    for (const auto& name : ops_) {
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::ofString(name));
        entry.set("level", json::Value::ofNumber(4));
        entry.set("bypassesPlayerLimit", json::Value::ofBool(false));
        root.push(std::move(entry));
    }
    (void)writeAdminJsonFile("ops.json", root, "operators");
}
void GameServer::loadBans() {
    bannedPlayers_.clear();
    json::Value root;
    if (readAdminJsonFile("banned-players.json", root, "banned players"))
        loadNameSet(root, bannedPlayers_, "name", "banned-players.json");
}
void GameServer::saveBans() const {
    json::Value root = json::Value::array();
    for (const auto& name : bannedPlayers_) {
        json::Value entry = json::Value::object();
        entry.set("name", json::Value::ofString(name));
        entry.set("reason", json::Value::ofString("Banned by an operator."));
        root.push(std::move(entry));
    }
    (void)writeAdminJsonFile("banned-players.json", root, "banned players");
}
void GameServer::loadBannedIps() {
    bannedIps_.clear();
    json::Value root;
    if (readAdminJsonFile("banned-ips.json", root, "banned IPs")) {
        if (root.isArr()) {
            for (const auto& entry : root.arr) {
                if (entry.isStr()) {
                    if (!entry.str.empty()) bannedIps_.insert(entry.str);
                    continue;
                }
                if (!entry.isObj()) continue;
                const auto* ip = entry.find("ip");
                if (ip && ip->isStr() && !ip->str.empty()) {
                    bannedIps_.insert(ip->str);
                    continue;
                }
                const auto* name = entry.find("name");
                if (name && name->isStr() && !name->str.empty())
                    bannedIps_.insert(name->str);
            }
        } else if (root.isObj()) {
            for (const auto& [key, _] : root.obj)
                if (!key.empty()) bannedIps_.insert(key);
        } else {
            std::fprintf(stderr, "[cppfm] ignoring non-array banned-ips.json\n");
        }
    }
}
void GameServer::saveBannedIps() const {
    json::Value root = json::Value::array();
    for (const auto& ip : bannedIps_) {
        json::Value entry = json::Value::object();
        entry.set("ip", json::Value::ofString(ip));
        entry.set("reason", json::Value::ofString("Banned by an operator."));
        root.push(std::move(entry));
    }
    (void)writeAdminJsonFile("banned-ips.json", root, "banned IPs");
}
void GameServer::saveWhitelist() const {
    if (!whitelist_.save("whitelist.json"))
        std::fprintf(stderr, "[cppfm] whitelist save failed\n");
}
void GameServer::kickPlayer(const std::string& name, const std::string& reason) {
    Player* t = nullptr;
    for (auto& p : playersSnapshot()) if (p->name == name) { t = p.get(); break; }
    if (!t || !t->conn) return;
    std::string txt = reason.empty() ? "Kicked by an operator." : reason;
    WriteBuffer b;
    nbt::writeTextComponent(b, txt);
    t->conn->trySendPacket(proto::pl::sc::Disconnect, b);
    t->conn->abort();
    t->conn->close();
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
    }
    i.f64(oldSize); i.f64(newSize);
    i.varlong(lerpMs);
    i.varint((int)constants::kWorldBorderDiameter); // max world border (portalTeleportBoundary)
    i.varint(5);  // warning blocks
    i.varint(15); // warning time
    p.conn->trySendPacket(proto::pl::sc::InitializeWorldBorder, i);
    // also send Center for spec compliance (separate packet)
    WriteBuffer c;
    c.f64(worldBorderCenterX_); c.f64(worldBorderCenterZ_);
    p.conn->trySendPacket(proto::pl::sc::WorldBorderCenter, c);
    // Lerp-specific separate packets if active
    if (worldBorderLerpRemainingTicks_ > 0) {
        WriteBuffer l;
        l.f64(oldSize); l.f64(newSize); l.varlong(lerpMs);
        p.conn->trySendPacket(proto::pl::sc::WorldBorderLerpSize, l);
    } else {
        WriteBuffer s;
        s.f64(newSize);
        p.conn->trySendPacket(proto::pl::sc::WorldBorderSize, s);
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
    std::string command = line;
    std::string javaResponse;
    // RCON/console is another command ingress.  A Java-registered command is
    // consumed by the embedded bridge; rewritten/unknown commands continue
    // through the authoritative native dispatcher below.
    if (jvmRuntime_ && !jvmRuntime_->onCommand(nullptr, command, &javaResponse))
        return javaResponse.empty() ? "OK" : javaResponse;
    brigadier::CommandSource src;
    src.console = true;
    src.srcX = 0; src.srcY = -60; src.srcZ = 0;
    src.resolveSelector = [this](const std::string& raw,
                                 brigadier::SelectorResult& out) {
        out = resolveSelector(raw, nullptr);
    };
    std::string captured;
    consoleCapture_ = &captured;
    try {
        const auto res = commands_.execute(command, std::move(src));
        consoleCapture_ = nullptr;
        if (!captured.empty()) return captured;
        return res.ok ? "OK" : ("error: " + res.errorText);
    } catch (const std::exception& e) {
        consoleCapture_ = nullptr;
        return std::string("error: ") + e.what();
    } catch (...) {
        consoleCapture_ = nullptr;
        return "error: command failed";
    }
}

void GameServer::demandChunkAsync(std::int32_t cx, std::int32_t cz) {
    const std::int64_t k = chunkKey(cx, cz);
    {
        std::lock_guard lk(chunkCacheMtx_);
        if (chunkCache_.find(k) != chunkCache_.end()) return;
    }
    if (world_.hasChunk(cx, cz)) return;
    const std::string path = cfg_.worldDir + "/region/r." + std::to_string(cx >> 5) + "." + std::to_string(cz >> 5) + ".mca";
    {
        std::lock_guard lk(pendingLoadsMtx_);
        if (pendingLoads_.count(k)) return;
        try {
            auto future = ioPool_.submit([path, cx, cz]{
                std::vector<std::uint8_t> raw;
                try {
                    RegionFile rf(path);
                    raw = rf.load(cx & 31, cz & 31);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "[GameServer] async chunk load %d,%d failed: %s\n",
                                 cx, cz, e.what());
                } catch (...) {
                    std::fprintf(stderr, "[GameServer] async chunk load %d,%d failed\n", cx, cz);
                }
                return raw;
            });
            pendingLoads_.emplace(k, std::move(future));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[GameServer] could not queue chunk load %d,%d: %s\n",
                         cx, cz, e.what());
        } catch (...) {
            std::fprintf(stderr, "[GameServer] could not queue chunk load %d,%d\n", cx, cz);
        }
    }
}
void GameServer::saveChunkAsync(std::int32_t cx, std::int32_t cz) {
    try {
        std::unordered_map<std::uint16_t, std::string> idxToKey;
        { const auto& order = gameData_.order("minecraft:worldgen/biome");
          for (std::size_t i = 0; i < order.size(); ++i)
              idxToKey.emplace(static_cast<std::uint16_t>(i), order[i]); }
        std::vector<std::uint8_t> nbtBytes;
        std::vector<std::uint8_t> bodyBytes;
        std::uint64_t revision = 0;
        const std::uint32_t biomeIdx = data_.biomeIndex(cfg_.worldBiome);
        const bool has = world_.withChunk(cx, cz, [&](const Chunk& c) {
            revision = c.revision;

            // Serialize directly from the read-locked chunk.  Copying a full
            // Chunk here used to allocate roughly 0.8 MiB for every dirty
            // unload, then allocate the NBT and cache payloads as well.
            nbt::Value root = chunkToNBT(cx, cz, c, world_.biomeKey(), &idxToKey);
            nbt::Value list = nbt::Value::makeList(nbt::Compound);
            blockEntities_.writeChunkNbt(cx, cz, list);
            if (!list.list.empty()) root.set("block_entities", std::move(list));
            WriteBuffer out;
            nbt::writeFileRoot(out, root);
            nbtBytes = std::move(out.data);

            WriteBuffer body;
            serializeLevelChunkBody(body, cx, cz, c, biomeIdx);
            bodyBytes = std::move(body.data);
        });
        if (!has) return;
        std::string path = cfg_.worldDir + "/region/r." + std::to_string(cx >> 5) + "." + std::to_string(cz >> 5) + ".mca";
        // Cache body update (tick thread) with the same revision/snapshot as
        // the durable NBT bytes; the I/O worker receives bytes only.
        auto body = std::make_shared<const std::vector<std::uint8_t>>(std::move(bodyBytes));
        storeChunk(cx, cz, revision, body);
        const std::int64_t key = chunkKey(cx, cz);
        const auto coordinator = chunkSaveCoordinator_;
        std::shared_ptr<ChunkSaveCoordinator::Stamp> stamp;
        {
            std::lock_guard lock(coordinator->mutex);
            auto& current = coordinator->latest[key];
            if (!current) current = std::make_shared<ChunkSaveCoordinator::Stamp>();
            current->latestRevision.store(revision, std::memory_order_release);
            stamp = current;
        }
        ioPool_.submit([path, cx, cz, key, revision, stamp, coordinator,
                        nbtBytes = std::move(nbtBytes)]() mutable {
            try {
                // Serialize saves for one chunk, but do not hold a global
                // coordinator lock during compression and disk I/O.  A newer
                // revision supersedes an older queued task before it writes.
                std::lock_guard writeLock(stamp->writeMutex);
                if (stamp->latestRevision.load(std::memory_order_acquire) != revision)
                    return;
                RegionFile rf(path);
                rf.store(cx & 31, cz & 31, nbtBytes);
                std::lock_guard coordinatorLock(coordinator->mutex);
                auto it = coordinator->latest.find(key);
                if (it != coordinator->latest.end() && it->second == stamp &&
                    stamp->latestRevision.load(std::memory_order_acquire) == revision)
                    coordinator->latest.erase(it);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[GameServer] async chunk save %d,%d failed: %s\n",
                             cx, cz, e.what());
            } catch (...) {
                std::fprintf(stderr, "[GameServer] async chunk save %d,%d failed\n", cx, cz);
            }
        });
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[GameServer] could not queue chunk save %d,%d: %s\n",
                     cx, cz, e.what());
    } catch (...) {
        std::fprintf(stderr, "[GameServer] could not queue chunk save %d,%d\n", cx, cz);
    }
}
void GameServer::pollPendingLoads() {
    // Drain ready futures without holding the registry lock while parsing NBT
    // or mutating the world.  Network threads can continue to queue requests.
    using ChunkFuture = std::future<std::vector<std::uint8_t>>;
    std::vector<std::pair<std::int64_t, ChunkFuture>> ready;
    {
        std::lock_guard lk(pendingLoadsMtx_);
        for (auto it = pendingLoads_.begin(); it != pendingLoads_.end(); ) {
            if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                ready.emplace_back(it->first, std::move(it->second));
                it = pendingLoads_.erase(it);
                continue;
            }
            ++it;
        }
    }
    for (auto& [key, future] : ready) {
        std::vector<std::uint8_t> bytes;
        try { bytes = future.get(); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "[GameServer] async chunk future %lld failed: %s\n",
                         static_cast<long long>(key), e.what());
        } catch (...) {
            std::fprintf(stderr, "[GameServer] async chunk future %lld failed\n",
                         static_cast<long long>(key));
        }
        const std::int32_t cx = static_cast<std::int32_t>(key >> 32);
        const std::int32_t cz = static_cast<std::int32_t>(key & 0xFFFFFFFFLL);
        if (!bytes.empty()) {
            try {
                ReadBuffer rb(bytes);
                nbt::Parser parser(rb);
                nbt::Value root = parser.readFileRoot();
                Chunk chunk;
                std::string bio;
                if (chunkFromNBT(root, chunk, {}, bio, nullptr)) {
                    world_.setChunk(cx, cz, std::move(chunk));
                    // Populate the cache with the encoded body.
                    auto body = std::make_shared<const std::vector<std::uint8_t>>([&]{
                        WriteBuffer wb;
                        static const std::uint32_t biomeIdx = 0;
                        world_.withChunk(cx, cz, [&](const Chunk& c) {
                            serializeLevelChunkBody(wb, cx, cz, c, biomeIdx);
                        });
                        return wb.data;
                    }());
                    const std::uint64_t rev = world_.revisionAt(cx, cz);
                    storeChunk(cx, cz, rev, body);
                } else {
                    world_.generateChunkIfMissing(cx, cz);
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[GameServer] stored chunk %d,%d rejected: %s; regenerating\n",
                             cx, cz, e.what());
                world_.generateChunkIfMissing(cx, cz);
            } catch (...) {
                std::fprintf(stderr, "[GameServer] stored chunk %d,%d rejected; regenerating\n",
                             cx, cz);
                world_.generateChunkIfMissing(cx, cz);
            }
        } else {
            // No stored chunk -> generate via WorldGen (tick thread, seed-safe).
            world_.generateChunkIfMissing(cx, cz);
        }
    }
}
} // namespace cppfm
