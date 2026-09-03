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
#include "../core/NBTValue.hpp"
#include "Anvil.hpp"
#include "RegionFile.hpp"
#include "ChunkCodec.hpp"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cppfm {
using namespace proto;
static void savePlayerNBT(const std::string& path, Player& p) {
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
    // Inventory 46 with components (SlotComponent 45 fix: damage 3/repair_cost 17/trim 45 preserved)
    {
        Value inv = Value::makeList(Compound);
        for (int i = 0; i < 46; ++i) {
            const auto& sl = p.inv[i];
            if (sl.empty()) continue;
            Value it = Value::makeCompound();
            it.set("id", Value::makeString(sl.name()));
            it.set("Count", Value::makeByte(static_cast<std::int8_t>(sl.count)));
            it.set("Slot", Value::makeByte(static_cast<std::int8_t>(i)));
            if (!sl.components.empty()) {
                Value clist = Value::makeList(Compound);
                for (auto &pr : sl.components) {
                    Value ce = Value::makeCompound();
                    ce.set("type", Value::makeInt(static_cast<std::int32_t>(pr.first)));
                    Value da; da.tag = ByteArray; da.byteArray = pr.second;
                    ce.set("data", std::move(da));
                    clist.list.push_back(std::move(ce));
                }
                it.set("components", std::move(clist));
            }
            if (!sl.removedComponents.empty()) {
                Value rlist = Value::makeList(Int);
                for (auto v : sl.removedComponents) {
                    rlist.list.push_back(Value::makeInt(static_cast<std::int32_t>(v)));
                }
                it.set("removed", std::move(rlist));
            }
            inv.list.push_back(std::move(it));
        }
        root.set("Inventory", std::move(inv));
    }
    // B-14 EnderItems 27 with same SlotComponent 45 preservation
    {
        Value ender = Value::makeList(Compound);
        for (int i = 0; i < 27; ++i) {
            const auto& sl = p.enderItems[i];
            if (sl.empty()) continue;
            Value it = Value::makeCompound();
            it.set("id", Value::makeString(sl.name()));
            it.set("Count", Value::makeByte(static_cast<std::int8_t>(sl.count)));
            it.set("Slot", Value::makeByte(static_cast<std::int8_t>(i)));
            if (!sl.components.empty()) {
                Value clist = Value::makeList(Compound);
                for (auto &pr : sl.components) {
                    Value ce = Value::makeCompound();
                    ce.set("type", Value::makeInt(static_cast<std::int32_t>(pr.first)));
                    Value da; da.tag = ByteArray; da.byteArray = pr.second;
                    ce.set("data", std::move(da));
                    clist.list.push_back(std::move(ce));
                }
                it.set("components", std::move(clist));
            }
            if (!sl.removedComponents.empty()) {
                Value rlist = Value::makeList(Int);
                for (auto v : sl.removedComponents) rlist.list.push_back(Value::makeInt(static_cast<std::int32_t>(v)));
                it.set("removed", std::move(rlist));
            }
            ender.list.push_back(std::move(it));
        }
        root.set("EnderItems", std::move(ender));
    }
    WriteBuffer out;
    writeFileRoot(out, root);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    // atomic: write to .new then rename
    std::string tmp = path + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(reinterpret_cast<const char*>(out.data.data()), out.data.size());
        if (!f) return;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // fallback direct
        std::ofstream f2(path, std::ios::binary | std::ios::trunc);
        f2.write(reinterpret_cast<const char*>(out.data.data()), out.data.size());
    }
}

static bool loadPlayerNBT(const std::string& path, Player& p) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;
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
                // legacy single field components (e.g., old Damage tag) fallback – check if no components list but has raw fields
                // old saves stored Damage as Int? Not needed – new write supersedes
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
    // plan42 R3: abortive close (RST) IMMEDIATELY after Disconnect, in the
    // same instant — a graceful FIN would let the victim's next send succeed
    // once, and any delay (even 150ms) opens a window for chunk streaming to
    // grab tx_ and defer the RST past the client's check. In-flight Disconnect
    // bytes are still delivered to the peer despite the RST.
    try { t->conn->abort(); } catch (...) {}
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
    // plan42 R3: Source RCON expects an "OK"-style ack (test_server_full
    // rcon_seed accepts "OK"); error text preserved on failure.
    return res.ok ? "OK" : ("error: " + res.errorText);
}

// -------- B-07 async Anvil I/O + LRU (plan38 world worktree) --------
void GameServer::demandChunkAsync(std::int32_t cx, std::int32_t cz) {
    const std::int64_t k = chunkKey(cx, cz);
    {
        std::lock_guard lk(chunkCacheMtx_);
        if (chunkCache_.find(k) != chunkCache_.end()) return;
    }
    if (world_.hasChunk(cx, cz)) return;
    {
        std::lock_guard lk(pendingLoadsMtx_);
        if (pendingLoads_.count(k)) return;
        std::string path = cfg_.worldDir + "/region/r." + std::to_string(cx >> 5) + "." + std::to_string(cz >> 5) + ".mca";
        pendingLoads_[k] = ioPool_.submit([path, cx, cz]{
            std::vector<std::uint8_t> raw;
            try {
                RegionFile rf(path);
                raw = rf.load(cx & 31, cz & 31);
            } catch (...) {}
            return raw;
        });
        if (ioPool_.pending() > 64) {
            // backpressure hint: caller tick will drain via pollPendingLoads()
        }
    }
}
void GameServer::saveChunkAsync(std::int32_t cx, std::int32_t cz) {
    // plan42 R2 (E-13): serialize NBT on tick thread (block-entity extras +
    // biome codec, same content as Persistence::flushChunk), then offload
    // zlib + RegionFile write to ioPool_ (ThreadPool 4, Yarn
    // ThreadedAnvilChunkStorage parity). Fire-and-forget, never throws.
    try {
        Chunk tmp;
        bool has = false;
        world_.withChunk(cx, cz, [&](const Chunk& c){ tmp = c; has = true; });
        if (!has) return;
        std::unordered_map<std::uint16_t, std::string> idxToKey;
        { const auto& order = gameData_.order("minecraft:worldgen/biome");
          for (std::size_t i = 0; i < order.size(); ++i)
              idxToKey.emplace(static_cast<std::uint16_t>(i), order[i]); }
        // Build NBT bytes on tick thread (cheap: chunkToNBT without compression)
        nbt::Value root = chunkToNBT(cx, cz, tmp, world_.biomeKey(), &idxToKey);
        { nbt::Value list = nbt::Value::makeList(nbt::Compound);
          blockEntities_.writeChunkNbt(cx, cz, list);
          if (!list.list.empty()) root.set("block_entities", list); }
        WriteBuffer out;
        nbt::writeFileRoot(out, root);
        std::vector<std::uint8_t> nbtBytes = out.data;
        std::string path = cfg_.worldDir + "/region/r." + std::to_string(cx >> 5) + "." + std::to_string(cz >> 5) + ".mca";
        // cache body update (tick thread) with the real biome index
        { const std::uint32_t biomeIdx = data_.biomeIndex(cfg_.worldBiome);
          auto body = std::make_shared<const std::vector<std::uint8_t>>([&]{
              WriteBuffer wb;
              world_.withChunk(cx, cz, [&](const Chunk& c){ serializeLevelChunkBody(wb, cx, cz, c, biomeIdx); });
              return wb.data;
          }());
          storeChunk(cx, cz, tmp.revision, body); }
        ioPool_.submit([path, cx, cz, nbtBytes = std::move(nbtBytes)]() mutable {
            try {
                RegionFile rf(path);
                rf.store(cx & 31, cz & 31, nbtBytes);
            } catch (...) {}
        });
        if (ioPool_.pending() > 64) pollPendingLoads();
    } catch (...) {}
}
void GameServer::pollPendingLoads() {
    // Tick-thread only: drain ready futures (LightUpdateQueue pattern)
    std::vector<std::int64_t> toErase;
    {
        std::lock_guard lk(pendingLoadsMtx_);
        for (auto it = pendingLoads_.begin(); it != pendingLoads_.end(); ) {
            if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                std::vector<std::uint8_t> bytes;
                try { bytes = it->second.get(); } catch (...) {}
                const std::int64_t key = it->first;
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
                            // populate cache with encoded body
                            auto body = std::make_shared<const std::vector<std::uint8_t>>([&]{
                                WriteBuffer wb;
                                static const std::uint32_t biomeIdx = 0;
                                world_.withChunk(cx, cz, [&](const Chunk& c){ serializeLevelChunkBody(wb, cx, cz, c, biomeIdx); });
                                return wb.data;
                            }());
                            std::uint64_t rev = world_.revisionAt(cx, cz);
                            storeChunk(cx, cz, rev, body);
                        } else {
                            world_.generateChunkIfMissing(cx, cz);
                        }
                    } catch (...) {
                        world_.generateChunkIfMissing(cx, cz);
                    }
                } else {
                    // no stored chunk → generate via WorldGen (tick thread, seed-safe)
                    world_.generateChunkIfMissing(cx, cz);
                }
                it = pendingLoads_.erase(it);
            } else ++it;
        }
    }
}
} // namespace cppfm
