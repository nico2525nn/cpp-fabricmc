#include "CommandModule.hpp"

namespace cppfm {

namespace {

struct PlayerCommandSnapshot {
    std::shared_ptr<Connection> connection;
    std::array<std::uint8_t, 16> uuid{};
    std::string name;
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    float yaw = 0.0f, pitch = 0.0f;
    std::int32_t respawnX = 0, respawnY = 0, respawnZ = 0;
    float respawnAngle = 0.0f;
};

PlayerCommandSnapshot snapshotPlayer(const Player& player) {
    PlayerCommandSnapshot out;
    std::lock_guard playerLock(player.stateMtx);
    out.connection = player.conn;
    out.uuid = player.uuid;
    out.name = player.name;
    out.entityId = player.entityId;
    out.dimension = player.dimension;
    out.x = player.x;
    out.y = player.y;
    out.z = player.z;
    out.yaw = player.yaw;
    out.pitch = player.pitch;
    return out;
}

std::int8_t snapshotCommandDimension(const brigadier::CommandSource& source) {
    if (source.dimensionOverride)
        return GameServer::canonicalDimension(*source.dimensionOverride);
    const auto* player = static_cast<const Player*>(source.player);
    if (!player) return 0;
    std::lock_guard playerLock(player->stateMtx);
    return GameServer::canonicalDimension(player->dimension);
}

void sendCommandFeedback(Player* player, const std::string& message) {
    sendFeedback(player, message);
}

} // namespace


void GameServer::initPlayerCommands() {
    initPlayerCommandsPart01();
    initPlayerCommandsPart02();
    initPlayerCommandsPart03();
    initPlayerCommandsPart04();
    initPlayerCommandsPart05();
    initPlayerCommandsPart06();
    initPlayerCommandsPart07();
    initPlayerCommandsPart08();
    initPlayerCommandsPart10();
    initPlayerCommandsPart11();
    initPlayerAttributeCommands();
    initPlayerCommandsPart13();
    initPlayerCommandsPart14();
    initPlayerCommandsPart15();
    initPlayerCommandsPart16();
    initPlayerCommandsPart17();
    initPlayerCommandsPart18();
    initPlayerCommandsPart19();
    initPlayerCommandsPart20();
    initPlayerCommandsPart21();
    initPlayerCommandsPart22();
}

void GameServer::initPlayerCommandsPart01() {
    auto& d = commands_;
    {
        auto gm = CommandNode::literal("gamemode");
        auto applyMode = [](const std::string& s) -> int {
            if (s == "survival" || s == "s" || s == "0") return 0;
            if (s == "creative" || s == "c" || s == "1") return 1;
            if (s == "adventure" || s == "a" || s == "2") return 2;
            if (s == "spectator" || s == "sp" || s == "3") return 3;
            return -1;
        };
        auto modeArg = CommandNode::argument("mode", args::gamemodeArg());
        modeArg->executable = true;
        modeArg->action = [this, applyMode](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const int m = applyMode(c.arg("mode").asStr());
            if (m < 0 || !src) throw std::runtime_error("unknown gamemode");
            std::shared_ptr<Connection> connection;
            WriteBuffer ge;                          // game event 4 = gamemode
            ge.u8(4); ge.f32(static_cast<float>(m));
            std::uint8_t af = 0;
            {
                std::lock_guard playerLock(src->stateMtx);
                src->gamemode = static_cast<std::uint8_t>(m);
                if (m == 1) af |= 0x01 | 0x04 | 0x08;
                else if (m == 3) af |= 0x02 | 0x04;
                if (src->isFlying && !(af & 0x04)) src->isFlying = false;
                if (src->isFlying) af |= 0x02;
                connection = src->conn;
            }
            WriteBuffer ab;
            ab.i8(static_cast<std::int8_t>(af));
            ab.f32(0.05f); ab.f32(m == 1 ? 0.10f : 0.05f);
            if (connection) {
                connection->trySendPacket(proto::pl::sc::GameEvent, ge);
                connection->trySendPacket(proto::pl::sc::Abilities, ab);
            }
            sendCommandFeedback(src, "Set own game mode to " + c.arg("mode").asStr());
            return 1;
        };
        auto target = CommandNode::argument("target",
                                            args::entity(true, false));
        target->executable = true;
        target->action = [this, applyMode](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const int m = applyMode(c.arg("mode").asStr());
            if (m < 0) throw std::runtime_error("unknown gamemode");
            const auto sel = c.arg("target").asSelector();
            int count = 0;
            for (auto& name : sel.playerNames)
                if (Player* t = findPlayer(*this, name)) {
                    std::shared_ptr<Connection> connection;
                    std::uint8_t taf = 0;
                    {
                        std::lock_guard playerLock(t->stateMtx);
                        t->gamemode = static_cast<std::uint8_t>(m);
                        if (m == 1) taf |= 0x01 | 0x04 | 0x08;
                        else if (m == 3) taf |= 0x02 | 0x04;
                        if (t->isFlying && !(taf & 0x04)) t->isFlying = false;
                        if (t->isFlying) taf |= 0x02;
                        connection = t->conn;
                    }
                    WriteBuffer ab;
                    ab.i8(static_cast<std::int8_t>(taf));
                    ab.f32(0.05f); ab.f32(m == 1 ? 0.10f : 0.05f);
                    if (connection)
                        connection->trySendPacket(proto::pl::sc::Abilities, ab);
                    ++count;
                }
            sendCommandFeedback(src, "Updated gamemode for " + std::to_string(count));
            return count;
        };
        modeArg->then(target);
        gm->then(modeArg);
        d.root->then(gm);
    }
}

void GameServer::initPlayerCommandsPart02() {
    auto& d = commands_;
    {
        auto give = CommandNode::literal("give");
        auto who = CommandNode::argument("target", args::entity(true, false));
        auto item = CommandNode::argument("item", args::itemStackArg());
        item->executable = true;
        item->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("target").asSelector();
            const std::string raw = c.arg("item").asStr();
            // extract base item name before '['
            std::string base = raw;
            std::string compPart;
            auto br = raw.find('[');
            if (br!=std::string::npos) { base = raw.substr(0, br); compPart = raw.substr(br); }
            auto it = gen::itemIdByName().find(base);
            if (it == gen::itemIdByName().end())
                throw std::runtime_error("Unknown item: " + base);
            ItemStack stack = ItemStack::of(it->second, 1);
            if (!compPart.empty() && compPart.find("trim")!=std::string::npos) {
                // naive extract pattern and material strings
                auto extract = [&](const std::string& key)->std::string{
                    auto pos = compPart.find(key);
                    if (pos==std::string::npos) return "";
                    auto q1 = compPart.find('"', pos);
                    if (q1==std::string::npos) return "";
                    auto q2 = compPart.find('"', q1+1);
                    if (q2==std::string::npos) return "";
                    return compPart.substr(q1+1, q2-q1-1);
                };
                std::string pat = extract("pattern");
                std::string mat = extract("material");
                if (!pat.empty()) {
                    ItemStack::ArmorTrim tr; tr.has=true; tr.pattern=pat; tr.material= mat.empty()?"minecraft:iron":mat;
                    stack.setTrim(tr);
                }
            }
            int given = 0;
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    ItemStack toGive = stack;
                    const bool isMap = base=="minecraft:filled_map" || base=="minecraft:map";
                    int mapId = -1;
                    {
                        std::lock_guard playerLock(t->stateMtx);
                        if (isMap) {
                            mapId = nextMapId_.fetch_add(1);
                            WriteBuffer tmp; tmp.varint(mapId);
                            toGive.components.erase(std::remove_if(toGive.components.begin(), toGive.components.end(), [](auto &pr){return pr.first==36;}), toGive.components.end());
                            toGive.components.emplace_back(36, std::vector<uint8_t>(tmp.data.begin(), tmp.data.end()));
                            toGive.count = 1;
                        }
                        bool placed=false;
                        for(int i: kMainInventoryOrder){
                            auto &s = t->inv[i];
                            if (s.empty()) { s = toGive; placed=true; break; }
                        }
                        if(!placed) {
                            addToInventory(*t, toGive.itemId, 1);
                            for(int i: kMainInventoryOrder)
                                if(!t->inv[i].empty() && t->inv[i].itemId==toGive.itemId) {
                                    t->inv[i]=toGive;
                                    break;
                                }
                        }
                    }
                    resendInventory(*t);
                    if (mapId >= 0) sendMapData(*t, mapId);
                    if (base.find("_helmet")!=std::string::npos||base.find("_chestplate")!=std::string::npos||base.find("_leggings")!=std::string::npos||base.find("_boots")!=std::string::npos)
                        syncEquipmentOnChange(*t);
                    ++given;
                }
            sendCommandFeedback(src, "Given 1 x " + base);
            return given;
        };
        auto cnt = CommandNode::argument("count", args::integer(1, 576));
        cnt->executable = true;
        cnt->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("target").asSelector();
            const std::string raw = c.arg("item").asStr();
            std::string base = raw;
            auto br = raw.find('[');
            if (br!=std::string::npos) base = raw.substr(0, br);
            auto it = gen::itemIdByName().find(base);
            if (it == gen::itemIdByName().end())
                throw std::runtime_error("Unknown item: " + base);
            const int n2 = c.arg("count").asInt();
            int given = 0;
            for (auto& nm : sel.playerNames)
                if (Player* t = findPlayer(*this, nm)) {
                    std::string compPart = br!=std::string::npos ? raw.substr(br) : "";
                    ItemStack stack = ItemStack::of(it->second, 1);
                    if (!compPart.empty() && compPart.find("trim")!=std::string::npos) {
                        auto extract = [&](const std::string& key)->std::string{
                            auto pos = compPart.find(key);
                            if (pos==std::string::npos) return "";
                            auto q1 = compPart.find('"', pos);
                            if (q1==std::string::npos) return "";
                            auto q2 = compPart.find('"', q1+1);
                            if (q2==std::string::npos) return "";
                            return compPart.substr(q1+1, q2-q1-1);
                        };
                        std::string pat = extract("pattern");
                        std::string mat = extract("material");
                        if (!pat.empty()) { ItemStack::ArmorTrim tr; tr.has=true; tr.pattern=pat; tr.material= mat.empty()?"minecraft:iron":mat; stack.setTrim(tr); }
                    }
                    const bool isMap = (base=="minecraft:filled_map" || base=="minecraft:map");
                    for(int k=0;k<n2;k++){
                        ItemStack toGive = stack;
                        int curMapId = -1;
                        {
                            std::lock_guard playerLock(t->stateMtx);
                            if (isMap) {
                                curMapId = nextMapId_.fetch_add(1);
                                WriteBuffer tmp; tmp.varint(curMapId);
                                toGive.components.erase(std::remove_if(toGive.components.begin(), toGive.components.end(), [](auto &pr){return pr.first==36;}), toGive.components.end());
                                toGive.components.emplace_back(36, std::vector<uint8_t>(tmp.data.begin(), tmp.data.end()));
                            }
                            bool placed=false;
                            for(int i: kMainInventoryOrder){
                                auto &s = t->inv[i];
                                if (s.empty()) { s = toGive; placed=true; break; }
                            }
                            if(!placed) addToInventory(*t, toGive.itemId, 1);
                        }
                        if (isMap && curMapId>=0) sendMapData(*t, curMapId);
                    }
                    resendInventory(*t);
                    ++given;
                }
            sendCommandFeedback(src, "Given " + std::to_string(n2) + " x " + base);
            return given;
        };
        item->then(cnt);
        who->then(item);
        give->then(who);
        d.root->then(give);
    }
}

void GameServer::initPlayerCommandsPart03() {
    auto& d = commands_;
    {
        auto tp = CommandNode::literal("tp");
        auto pos = CommandNode::argument("pos", args::vec3());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (!src) return 0;
            const auto v = c.arg("pos").asVec3();
            PlayerCommandSnapshot state;
            {
                std::lock_guard playerLock(src->stateMtx);
                src->fallDist = 0;
                src->x = v.x; src->y = v.y; src->z = v.z;
                state.connection = src->conn;
                state.yaw = src->yaw;
                state.pitch = src->pitch;
            }
            WriteBuffer tb;
            tb.varint(++teleportCounterForTest_);
            tb.f64(v.x); tb.f64(v.y); tb.f64(v.z);
            tb.f64(0); tb.f64(0); tb.f64(0);
            tb.f32(state.yaw); tb.f32(state.pitch);
            tb.u32(0);
            if (state.connection)
                state.connection->trySendPacket(proto::pl::sc::PlayerPosition, tb);
            sendCommandFeedback(src, "Teleported to " + std::to_string(v.x) + ", " +
                         std::to_string(v.y) + ", " + std::to_string(v.z));
            return 1;
        };
        tp->then(pos);
        d.root->then(tp);
    }
}

void GameServer::initPlayerCommandsPart04() {
    auto& d = commands_;
    {
        auto kill = CommandNode::literal("kill");
        kill->executable = true;
        kill->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (src) { applyDamage(*src, 1000.f, "/kill"); return 1; }
            return 0;
        };
        auto targets = CommandNode::argument("targets",
                                             args::entity(false, false));
        targets->executable = true;
        targets->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            const auto dimension = snapshotCommandDimension(c.source);
            int killed = 0;
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    applyDamage(*t, 1000.f, "killed");
                    ++killed;
                }
            std::vector<std::shared_ptr<MobEntity>> mobs;
            {
                std::lock_guard lk(entsMtx_);
                mobs = mobs_;
            }
            std::vector<std::pair<std::int8_t, std::int32_t>> ids;
            for (auto id : sel.entityIds)
                for (auto& m : mobs) {
                    if (!m) continue;
                    std::lock_guard entityLock(*m->stateMtx);
                    if (m->entityId == id && !m->dead &&
                        canonicalDimension(m->dimension) == dimension) {
                        m->health = 0; m->dead = true;
                        ids.emplace_back(dimension, id);
                        ++killed;
                        break;
                    }
                }
            for (const auto& [mobDimension, id] : ids) {
                WriteBuffer rm; rm.varint(1); rm.varint(id);
                broadcastPacketExceptInDimension(
                    mobDimension, nullptr, proto::pl::sc::RemoveEntities, rm);
            }
            sendCommandFeedback(src, "Killed " + std::to_string(killed) + " entities");
            return killed;
        };
        kill->then(targets);
        d.root->then(kill);
    }
}

void GameServer::initPlayerCommandsPart05() {
    auto& d = commands_;
    {
        auto effect = CommandNode::literal("effect");
        // amplifier byte on the wire (site 1 sends e.amplifier, others the raw arg).
        auto storeEffect = [](Player& t, EffectInstance e, int ampWire) {
            std::shared_ptr<Connection> connection;
            std::int32_t entityId = 0;
            {
                std::lock_guard playerLock(t.stateMtx);
                t.effects.erase(
                    std::remove_if(t.effects.begin(), t.effects.end(),
                                   [&](const EffectInstance& x)
                                       { return x.type == e.type; }),
                    t.effects.end());
                t.effects.push_back(e);
                entityId = t.entityId;
                connection = t.conn;
            }
            if (!connection) return;
            WriteBuffer b;
            b.varint(entityId);
            b.varint(e.type);
            b.varint(ampWire);
            b.varint(e.durationTicks);
            b.u8(effectFlags(e));
            connection->trySendPacket(proto::pl::sc::EntityEffect, b);
        };
        auto give = CommandNode::literal("give");
        auto targets = CommandNode::argument("targets",
                                             args::entity(false, false));
        auto eff = CommandNode::argument("effect", args::resourceLocation());
        eff->executable = true;
        eff->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            std::vector<std::string> v;
            for (int i = effects::Speed; i <= effects::Darkness; ++i)
                v.emplace_back(effects::nameOf(static_cast<std::uint8_t>(i)));
            return v;
        };
        eff->action = [this, storeEffect](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            int applied = 0;
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = 30 * 20;
                    storeEffect(*t, e, e.amplifier);
                    ++applied;
                }
            sendCommandFeedback(src, "Applied " + en + " to " +
                         std::to_string(applied));
            return applied;
        };
        auto secs = CommandNode::argument("seconds", args::integer(1, 1000000));
        secs->executable = true;
        secs->action = [this, storeEffect](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            const int dur = c.arg("seconds").asInt();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = dur * 20;
                    storeEffect(*t, e, e.amplifier);
                }
            sendCommandFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s)");
            return 1;
        };
        auto amp = CommandNode::argument("amplifier", args::integer(0, 255));
        amp->executable = true;
        amp->action = [this, storeEffect](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            const int dur = c.arg("seconds").asInt();
            const int ampv = c.arg("amplifier").asInt();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = dur * 20;
                    e.amplifier = static_cast<std::int8_t>(ampv); // level-1 model
                    storeEffect(*t, e, ampv); // raw 0..255 (int8_t wraps >127)
                }
            sendCommandFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s, amplifier " +
                         std::to_string(ampv) + ")");
            return 1;
        };
        // vanilla optional <hideParticles> boolean (low priority completion)
        auto hide = CommandNode::argument("hideParticles", args::boolean());
        hide->executable = true;
        hide->action = [this, storeEffect](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string en = c.arg("effect").asStr();
            auto it = effects::byName().find(en);
            if (it == effects::byName().end())
                throw std::runtime_error("unknown effect: " + en);
            const auto sel = c.arg("targets").asSelector();
            const int dur = c.arg("seconds").asInt();
            const int ampv = c.arg("amplifier").asInt();
            const bool hidep = c.arg("hideParticles").asBool();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    EffectInstance e;
                    e.type = it->second;
                    e.durationTicks = dur * 20;
                    e.amplifier = static_cast<std::int8_t>(ampv);
                    e.showParticles = !hidep;
                    storeEffect(*t, e, ampv);
                }
            sendCommandFeedback(src, "Applied " + en + " (" +
                         std::to_string(dur) + "s, amplifier " +
                         std::to_string(ampv) + ", hideParticles " +
                         (hidep ? "true" : "false") + ")");
            return 1;
        };
        eff->then(secs);
        amp->then(hide);
        secs->then(amp);
        targets->then(eff);
        give->then(targets);
        effect->then(give);
        d.root->then(effect);
    }
}

void GameServer::initPlayerCommandsPart06() {
    auto& d = commands_;
    {
        auto xpCmd = CommandNode::literal("xp");
        auto add = CommandNode::literal("add");
        auto targets = CommandNode::argument("targets",
                                             args::entity(true, false));
        auto amount = CommandNode::argument("amount", args::integer(-1000, 1000));
        amount->executable = true;
        amount->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("targets").asSelector();
            const int amt = c.arg("amount").asInt();
            for (auto& n : sel.playerNames)
                if (Player* t = findPlayer(*this, n)) {
                    {
                        std::lock_guard playerLock(t->stateMtx);
                        t->xp.addPoints(amt);
                    }
                    sendSetExperience(*t);
                }
            sendCommandFeedback(src, "Gave " + std::to_string(amt) + " xp");
            return 1;
        };
        targets->then(amount);
        add->then(targets);
        xpCmd->then(add);
        d.root->then(xpCmd);
    }
}

void GameServer::initPlayerCommandsPart07() {
    auto& d = commands_;
    {
        auto summon = CommandNode::literal("summon");
        auto ent = CommandNode::argument("entity", args::resourceLocation());
        ent->executable = true;
        ent->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string en = c.arg("entity").asStr();
            if (en.find(':') == std::string::npos) en = "minecraft:" + en;
            auto it = gen::entityTypeIdByName().find(en);
            if (it == gen::entityTypeIdByName().end())
                throw std::runtime_error("unknown entity: " + en);
            const auto source = src ? snapshotPlayer(*src) : PlayerCommandSnapshot{};
            const auto dimension = c.source.dimensionOverride
                ? canonicalDimension(*c.source.dimensionOverride) : source.dimension;
            spawnMobByTypeNameFor(dimension, en,
                src ? source.x + 2.0 : 0.5, src ? source.y + 1.0 : -60.0,
                src ? source.z + 2.0 : 0.5);
            sendCommandFeedback(src, "Summoned " + en);
            return 1;
        };
        summon->then(ent);
        d.root->then(summon);
    }
}

void GameServer::initPlayerCommandsPart08() {
    auto& d = commands_;
    {
        auto clear = CommandNode::literal("clear");
        clear->executable = true;
        clear->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            int removed = 0;
            {
                std::lock_guard playerLock(src->stateMtx);
                for (auto& s : src->inv)
                    if (!s.empty()) { ++removed; s = ItemStack::air(); }
            }
            resendInventory(*src);
            sendCommandFeedback(src, "Removed " + std::to_string(removed) +
                         " items");
            return removed;
        };
        d.root->then(clear);
    }
}

void GameServer::initPlayerCommandsPart10() {
    auto& d = commands_;
    {
        auto sp2 = CommandNode::literal("spectate");
        sp2->executable = true;
        sp2->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (!src) return 0;
            const auto state = snapshotPlayer(*src);
            if (state.connection) {
                WriteBuffer cam;
                cam.varint(state.entityId);
                state.connection->trySendPacket(proto::pl::sc::Camera, cam);
            }
            sendCommandFeedback(src, "Camera reset");
            return 1;
        };
        auto who = CommandNode::argument("target", args::entity(true, false));
        who->executable = true;
        who->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto source = src ? snapshotPlayer(*src) : PlayerCommandSnapshot{};
            const auto sel = c.arg("target").asSelector();
            if (!sel.playerNames.empty()) {
                if (Player* t = findPlayer(*this, sel.playerNames[0])) {
                    const auto target = snapshotPlayer(*t);
                    if (source.connection) {
                        WriteBuffer cam;
                        cam.varint(target.entityId);
                        source.connection->trySendPacket(proto::pl::sc::Camera, cam);
                        sendCommandFeedback(src, "Spectating " + target.name);
                    }
                }
            }
            return 1;
        };
        sp2->then(who);
        d.root->then(sp2);
    }
}

void GameServer::initPlayerCommandsPart11() {
    auto& d = commands_;
    {
        // extend /clear to support predicate filtering: /clear <targets> <item> [maxCount]
        auto clear2 = CommandNode::literal("clear");
        auto who = CommandNode::argument("targets", args::entity(false,false));
        auto itemPred = CommandNode::argument("item", args::itemPredicateArg());
        itemPred->executable = true;
        itemPred->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            const auto sel=c.arg("targets").asSelector();
            std::string pred=c.arg("item").asStr();
            // handle tag predicate like #minecraft:planks
            bool isTag = !pred.empty() && pred[0]=='#';
            std::string base = isTag ? pred.substr(1) : pred;
            if(base.find(':')==std::string::npos) base="minecraft:"+base;
            int removed=0;
            for(auto& n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                {
                    std::lock_guard playerLock(p->stateMtx);
                    for(auto& s: p->inv) if(!s.empty()){
                        bool match=false;
                        if(isTag){
                            auto* tagSet = datapackManager_.tagManager.getItemTag(base);
                            if(tagSet && tagSet->count(s.itemId)) match=true;
                        } else {
                            auto it=gen::itemIdByName().find(base);
                            if(it!=gen::itemIdByName().end() && it->second==s.itemId) match=true;
                        }
                        if(match){ removed+=s.count; s=ItemStack::air(); }
                    }
                }
                resendInventory(*p);
            }
            sendCommandFeedback(src,"Cleared "+std::to_string(removed)+" matching "+pred);
            return removed;
        };
        auto maxCount = CommandNode::argument("maxCount", args::integer(1,64));
        maxCount->executable = true;
        maxCount->action = [this](CommandContext& c){
            Player* src=static_cast<Player*>(c.source.player);
            const auto sel=c.arg("targets").asSelector();
            std::string pred=c.arg("item").asStr();
            int limit=c.arg("maxCount").asInt();
            bool isTag = !pred.empty() && pred[0]=='#';
            std::string base = isTag ? pred.substr(1) : pred;
            if(base.find(':')==std::string::npos) base="minecraft:"+base;
            int removed=0;
            for(auto& n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                {
                    std::lock_guard playerLock(p->stateMtx);
                    for(auto& s: p->inv) if(!s.empty() && removed<limit){
                        bool match=false;
                        if(isTag){
                            auto* tagSet = datapackManager_.tagManager.getItemTag(base);
                            if(tagSet && tagSet->count(s.itemId)) match=true;
                        } else {
                            auto it=gen::itemIdByName().find(base);
                            if(it!=gen::itemIdByName().end() && it->second==s.itemId) match=true;
                        }
                        if(match){
                            int take = std::min<int>(s.count, limit-removed);
                            removed+=take;
                            s.count-=take;
                            if(s.count<=0) s=ItemStack::air();
                        }
                    }
                }
                resendInventory(*p);
            }
            sendCommandFeedback(src,"Cleared "+std::to_string(removed)+" matching "+pred+" (limit)");
            return removed;
        };
        itemPred->then(maxCount);
        who->then(itemPred);
        clear2->then(who);
        d.root->then(clear2);
    }
}

void GameServer::initPlayerAttributeCommands() {
    auto attribute = CommandNode::literal("attribute");
    auto target = CommandNode::argument("target", args::entity(true,false));
    auto attrArg = CommandNode::argument("attribute", args::resourceLocation());
        attrArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(auto a: {Attribute::MAX_HEALTH, Attribute::MOVEMENT_SPEED, Attribute::ATTACK_DAMAGE, Attribute::ARMOR, Attribute::ARMOR_TOUGHNESS, Attribute::KNOCKBACK_RESISTANCE, Attribute::ATTACK_SPEED, Attribute::ATTACK_KNOCKBACK, Attribute::BLOCK_BREAK_SPEED, Attribute::BLOCK_INTERACTION_RANGE, Attribute::ENTITY_INTERACTION_RANGE, Attribute::FALL_DAMAGE_MULTIPLIER, Attribute::FLYING_SPEED, Attribute::FOLLOW_RANGE, Attribute::GRAVITY, Attribute::JUMP_STRENGTH, Attribute::LUCK, Attribute::MAX_ABSORPTION, Attribute::SAFE_FALL_DISTANCE, Attribute::SCALE, Attribute::STEP_HEIGHT, Attribute::SPAWN_REINFORCEMENTS, Attribute::TEMPT_RANGE, Attribute::WATER_MOVEMENT_EFFICIENCY}){
                v.emplace_back(attributeKey(a));
            }
            // also add short names for convenience
            v.push_back("minecraft:generic.max_health"); v.push_back("minecraft:generic.movement_speed");
            return v;
        };
    initAttributeGetCommands(attrArg);
    initAttributeBaseCommands(attrArg);
    initAttributeModifierCommands(attrArg);
    target->then(attrArg);
    attribute->then(target);
    commands_.root->then(attribute);
}

std::optional<Attribute> GameServer::resolveAttributeCommand(const std::string& raw) const {
    std::string id = raw;
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    for (auto a : {Attribute::MOVEMENT_SPEED, Attribute::MAX_HEALTH, Attribute::KNOCKBACK_RESISTANCE, Attribute::ARMOR, Attribute::ARMOR_TOUGHNESS, Attribute::ATTACK_DAMAGE, Attribute::ATTACK_SPEED, Attribute::FLYING_SPEED, Attribute::FOLLOW_RANGE, Attribute::MAX_ABSORPTION, Attribute::STEP_HEIGHT, Attribute::ATTACK_KNOCKBACK, Attribute::BLOCK_BREAK_SPEED, Attribute::BLOCK_INTERACTION_RANGE, Attribute::BURNING_TIME, Attribute::ENTITY_INTERACTION_RANGE, Attribute::EXPLOSION_KNOCKBACK_RESISTANCE, Attribute::FALL_DAMAGE_MULTIPLIER, Attribute::GRAVITY, Attribute::JUMP_STRENGTH, Attribute::LUCK, Attribute::MINING_EFFICIENCY, Attribute::MOVEMENT_EFFICIENCY, Attribute::OXYGEN_BONUS, Attribute::SAFE_FALL_DISTANCE, Attribute::SCALE, Attribute::SNEAKING_SPEED, Attribute::SPAWN_REINFORCEMENTS, Attribute::SUBMERGED_MINING_SPEED, Attribute::SWEEPING_DAMAGE_RATIO, Attribute::TEMPT_RANGE, Attribute::WATER_MOVEMENT_EFFICIENCY}) {
        if (std::string(attributeKey(a)) == id) return a;
    }
    std::string low = id;
    for (char& c : low) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (low == "minecraft:generic.max_health" || low == "generic.max_health" || low == "max_health") return Attribute::MAX_HEALTH;
    if (low == "minecraft:generic.movement_speed" || low == "generic.movement_speed" || low == "movement_speed") return Attribute::MOVEMENT_SPEED;
    return std::nullopt;
}

std::pair<Player*, Attribute> GameServer::attributeCommandHead(brigadier::CommandContext& c) {
    Player* src = static_cast<Player*>(c.source.player);
    const std::string attrRaw = c.arg("attribute").asStr();
    auto aopt = resolveAttributeCommand(attrRaw);
    if (!aopt) throw std::runtime_error("Unknown attribute: " + attrRaw);
    return {src, *aopt};
}

void GameServer::sendAttributeCommandUpdate(Player& p) {
    std::shared_ptr<Connection> connection;
    WriteBuffer ab;
    {
        std::lock_guard playerLock(p.stateMtx);
        p.attributes.writeUpdate(ab, p.entityId);
        connection = p.conn;
    }
    if (connection)
        connection->trySendPacket(proto::pl::sc::UpdateAttributes, ab);
}

void GameServer::initAttributeGetCommands(const brigadier::NodePtr& attrArg) {
            auto getLit = CommandNode::literal("get");
            getLit->executable = true;
            getLit->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target for attribute get");
                double v;
                {
                    std::lock_guard playerLock(targets.front()->stateMtx);
                    v = targets.front()->attributes.getValue(at);
                }
                sendCommandFeedback(src, std::string(attributeKey(at))+" has value "+std::to_string(v));
                return (int)std::llround(v);
            };
            auto scaleArg = CommandNode::argument("scale", args::floatArg(-1e9f, 1e9f));
            scaleArg->executable = true;
            scaleArg->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                double scale = c.arg("scale").asDouble();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target for attribute get");
                double v;
                {
                    std::lock_guard playerLock(targets.front()->stateMtx);
                    v = targets.front()->attributes.getValue(at) * scale;
                }
                sendCommandFeedback(src, std::string(attributeKey(at))+" scaled value "+std::to_string(v));
                return (int)std::llround(v);
            };
            getLit->then(scaleArg);
            attrArg->then(getLit);
         }

void GameServer::initAttributeBaseCommands(const brigadier::NodePtr& attrArg) {
            auto baseLit = CommandNode::literal("base");
            auto baseSet = CommandNode::literal("set");
            auto baseVal = CommandNode::argument("value", args::floatArg(-1e9f, 1e9f));
            baseVal->executable = true;
            baseVal->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                double v = c.arg("value").asDouble();
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    {
                        std::lock_guard playerLock(p->stateMtx);
                        p->attributes.setBase(at, v);
                    }
                    sendAttributeCommandUpdate(*p);
                    ++cnt;
                }
                if(cnt==0 && src){
                    {
                        std::lock_guard playerLock(src->stateMtx);
                        src->attributes.setBase(at, v);
                    }
                    sendAttributeCommandUpdate(*src);
                    cnt=1;
                }
                sendCommandFeedback(src, std::string(attributeKey(at))+" base set to "+std::to_string(v));
                return cnt;
            };
            baseSet->then(baseVal);
            baseLit->then(baseSet);
            auto baseGet = CommandNode::literal("get");
            baseGet->executable = true;
            baseGet->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target");
                double v;
                {
                    std::lock_guard playerLock(targets.front()->stateMtx);
                    v = targets.front()->attributes.getBase(at);
                }
                sendCommandFeedback(src, std::string(attributeKey(at))+" base is "+std::to_string(v));
                return (int)std::llround(v);
            };
            auto baseGetScale = CommandNode::argument("scale", args::floatArg(-1e9f, 1e9f));
            baseGetScale->executable = true;
            baseGetScale->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                double scale = c.arg("scale").asDouble();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                double v;
                {
                    std::lock_guard playerLock(targets.front()->stateMtx);
                    v = targets.front()->attributes.getBase(at) * scale;
                }
                sendCommandFeedback(src, std::string(attributeKey(at))+" base scaled "+std::to_string(v));
                return (int)std::llround(v);
            };
            baseGet->then(baseGetScale);
            baseLit->then(baseGet);
            auto baseReset = CommandNode::literal("reset");
            baseReset->executable = true;
            baseReset->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                // reset to default base per AttributeManager defaults
                AttributeManager defaults;
                double def = defaults.getBase(at);
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    {
                        std::lock_guard playerLock(p->stateMtx);
                        p->attributes.setBase(at, def);
                    }
                    sendAttributeCommandUpdate(*p);
                    ++cnt;
                }
                sendCommandFeedback(src, std::string(attributeKey(at))+" base reset");
                return cnt;
            };
            baseLit->then(baseReset);
            attrArg->then(baseLit);
         }

void GameServer::initAttributeModifierCommands(const brigadier::NodePtr& attrArg) {
            auto modLit = CommandNode::literal("modifier");
            // add <uuid> <name> <value> <operation>
            auto addLit = CommandNode::literal("add");
            auto uuidArg = CommandNode::argument("uuid", args::stringWord());
            auto nameArg = CommandNode::argument("name", args::stringWord());
            auto valArg = CommandNode::argument("value", args::floatArg(-1e9f, 1e9f));
            auto opArg = CommandNode::argument("operation", args::stringWord());
            opArg->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&){ return std::vector<std::string>{"add_value","add_multiplied_base","add_multiplied_total","0","1","2"}; };
            opArg->executable = true;
            opArg->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                std::string uuid = c.arg("uuid").asStr();
                std::string opStr = c.arg("operation").asStr();
                double amount = c.arg("value").asDouble();
                int op = 0;
                if(opStr=="add_value" || opStr=="0") op=0;
                else if(opStr=="add_multiplied_base" || opStr=="1") op=1;
                else if(opStr=="add_multiplied_total" || opStr=="2") op=2;
                else throw std::runtime_error("Unknown operation: "+opStr);
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    {
                        std::lock_guard playerLock(p->stateMtx);
                        p->attributes.addModifier(at, {uuid, amount, op});
                    }
                    sendAttributeCommandUpdate(*p);
                    ++cnt;
                }
                if(cnt==0 && src){
                    {
                        std::lock_guard playerLock(src->stateMtx);
                        src->attributes.addModifier(at, {uuid, amount, op});
                    }
                    sendAttributeCommandUpdate(*src);
                    cnt=1;
                }
                sendCommandFeedback(src, "Added modifier "+uuid+" to "+std::string(attributeKey(at)));
                return cnt;
            };
            valArg->then(opArg);
            nameArg->then(valArg);
            uuidArg->then(nameArg);
            addLit->then(uuidArg);
            modLit->then(addLit);
            // remove <uuid>
            auto remLit = CommandNode::literal("remove");
            auto remUuid = CommandNode::argument("uuid", args::stringWord());
            remUuid->executable = true;
            remUuid->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                std::string uuid = c.arg("uuid").asStr();
                int cnt=0;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)){
                    {
                        std::lock_guard playerLock(p->stateMtx);
                        p->attributes.removeModifier(at, uuid);
                    }
                    sendAttributeCommandUpdate(*p);
                    ++cnt;
                }
                if(cnt==0 && src){
                    {
                        std::lock_guard playerLock(src->stateMtx);
                        src->attributes.removeModifier(at, uuid);
                    }
                    sendAttributeCommandUpdate(*src);
                    cnt=1;
                }
                sendCommandFeedback(src, "Removed modifier "+uuid);
                return cnt;
            };
            remLit->then(remUuid);
            modLit->then(remLit);
            // value get <uuid> [<scale>]
            auto valGetLit = CommandNode::literal("value");
            auto valGetKw = CommandNode::literal("get");
            auto vgUuid = CommandNode::argument("uuid", args::stringWord());
            vgUuid->executable = true;
            vgUuid->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                std::string uuid = c.arg("uuid").asStr();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target");
                std::optional<double> opt;
                {
                    std::lock_guard playerLock(targets.front()->stateMtx);
                    opt = targets.front()->attributes.getModifierValue(at, uuid);
                }
                double amt = opt ? *opt : 0;
                if(!opt) throw std::runtime_error("Modifier not found: "+uuid);
                sendCommandFeedback(src, "Modifier "+uuid+" has value "+std::to_string(amt));
                return (int)std::llround(amt);
            };
            auto vgScale = CommandNode::argument("scale", args::floatArg(-1e9f, 1e9f));
            vgScale->executable = true;
            vgScale->action = [this](CommandContext& c){
                auto [src, at] = attributeCommandHead(c);
                const auto sel = c.arg("target").asSelector();
                double scale = c.arg("scale").asDouble();
                std::string uuid = c.arg("uuid").asStr();
                std::vector<Player*> targets;
                for(auto &n: sel.playerNames) if(Player* p=findPlayer(*this,n)) targets.push_back(p);
                if(targets.empty() && src) targets.push_back(src);
                if(targets.empty()) throw std::runtime_error("No target");
                std::optional<double> opt;
                {
                    std::lock_guard playerLock(targets.front()->stateMtx);
                    opt = targets.front()->attributes.getModifierValue(at, uuid);
                }
                double amt = opt ? *opt * scale : 0;
                if(!opt) throw std::runtime_error("Modifier not found: "+uuid);
                sendCommandFeedback(src, "Modifier "+uuid+" scaled value "+std::to_string(amt));
                return (int)std::llround(amt);
            };
            vgUuid->then(vgScale);
            valGetKw->then(vgUuid);
            valGetLit->then(valGetKw);
            modLit->then(valGetLit);
            attrArg->then(modLit);
         }

void GameServer::initPlayerCommandsPart13() {
    auto& d = commands_;
    {
        auto trigger = CommandNode::literal("trigger");
        auto objective = CommandNode::argument("objective", args::objectiveArg());
        objective->suggestions = [this](brigadier::StringReader&, brigadier::ParseCtx&){
            std::vector<std::string> v;
            for(auto &o: scoreboard.objectives) v.push_back(o.name);
            return v;
        };
        objective->executable = true;
        objective->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            if(!src) throw std::runtime_error("trigger can only be run by a player");
            const auto source = snapshotPlayer(*src);
            std::string obj = c.arg("objective").asStr();
            auto* o = scoreboard.find(obj);
            if(!o) {
                if(!scoreboard.addObjective(obj, "trigger", obj))
                    throw std::runtime_error("Unknown objective: "+obj);
                o = scoreboard.find(obj);
                if(o) sendObjectiveAll(*o, 0);
            }
            if(o->criteria!="trigger") throw std::runtime_error("Objective "+obj+" is not trigger criteria");
            // bare trigger enables? In vanilla, bare trigger does nothing but feedback. We implement as add 1
            // Check if score exists and enabled? Simplified: add 1
            scoreboard.addScore(obj, source.name, 1);
            int v = scoreboard.getScore(obj, source.name);
            sendScoreAll(obj, source.name, v);
            sendCommandFeedback(src, "Triggered "+obj+" add 1 (now "+std::to_string(v)+")");
            return v;
        };
        auto addLit = CommandNode::literal("add");
        auto addVal = CommandNode::argument("value", args::integer(INT32_MIN, INT32_MAX));
        addVal->executable = true;
        addVal->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            if(!src) throw std::runtime_error("trigger can only be run by a player");
            const auto source = snapshotPlayer(*src);
            std::string obj = c.arg("objective").asStr();
            auto* o = scoreboard.find(obj);
            if(!o) throw std::runtime_error("Unknown objective: "+obj);
            if(o->criteria!="trigger") throw std::runtime_error("Objective "+obj+" is not trigger criteria");
            int delta = c.arg("value").asInt();
            scoreboard.addScore(obj, source.name, delta);
            int v = scoreboard.getScore(obj, source.name);
            sendScoreAll(obj, source.name, v);
            sendCommandFeedback(src, "Triggered "+obj+" add "+std::to_string(delta)+" (now "+std::to_string(v)+")");
            return v;
        };
        addLit->then(addVal);
        objective->then(addLit);
        auto setLit = CommandNode::literal("set");
        auto setVal = CommandNode::argument("value", args::integer(INT32_MIN, INT32_MAX));
        setVal->executable = true;
        setVal->action = [this](CommandContext& c){
            Player* src = static_cast<Player*>(c.source.player);
            if(!src) throw std::runtime_error("trigger can only be run by a player");
            const auto source = snapshotPlayer(*src);
            std::string obj = c.arg("objective").asStr();
            auto* o = scoreboard.find(obj);
            if(!o) throw std::runtime_error("Unknown objective: "+obj);
            if(o->criteria!="trigger") throw std::runtime_error("Objective "+obj+" is not trigger criteria");
            int v = c.arg("value").asInt();
            scoreboard.setScore(obj, source.name, v);
            sendScoreAll(obj, source.name, v);
            sendCommandFeedback(src, "Triggered "+obj+" set "+std::to_string(v));
            return v;
        };
        setLit->then(setVal);
        objective->then(setLit);
        trigger->then(objective);
        d.root->then(trigger);
    }
}

void GameServer::initPlayerCommandsPart14() {
    auto& d = commands_;
    {
        // /clear <targets> (bare, no item) — vanilla clears whole inventory.
        // (Item-filtered /clear <targets> <item> [maxCount] already exists.)
        auto clearT = CommandNode::literal("clear");
        auto ctWho = CommandNode::argument("clearTargets", args::entity(false, false));
        ctWho->executable = true;
        ctWho->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("clearTargets").asSelector();
            int removed = 0;
            std::string names;
            for (auto& nm : sel.playerNames)
                if (Player* p = findPlayer(*this, nm)) {
                    std::string targetName;
                    {
                        std::lock_guard playerLock(p->stateMtx);
                        for (auto& s : p->inv)
                            if (!s.empty()) { removed += s.count; s = ItemStack::air(); }
                        targetName = p->name;
                    }
                    resendInventory(*p);
                    if (!names.empty()) names += ", ";
                    names += targetName;
                }
            sendCommandFeedback(src, "Removed " + std::to_string(removed) +
                         " items from " + (names.empty() ? "no players" : names));
            return removed;
        };
        clearT->then(ctWho);
        d.root->then(clearT);
    }
}

void GameServer::initPlayerCommandsPart15() {
    auto& d = commands_;
    {
        // /experience + /xp alias, add <targets> <amount> [points|levels].
        auto buildXp = [this](const std::string& litName) {
            auto xp = CommandNode::literal(litName);
            auto add = CommandNode::literal("add");
            auto targets = CommandNode::argument("xpTargets", args::entity(false, false));
            auto amount = CommandNode::argument("xpAmount", args::integer(-100000, 100000));
            amount->executable = true;   // no suffix -> points (vanilla default)
            amount->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("xpTargets").asSelector();
                const int amt = c.arg("xpAmount").asInt();
                for (auto& nm : sel.playerNames)
                    if (Player* t = findPlayer(*this, nm)) {
                        {
                            std::lock_guard playerLock(t->stateMtx);
                            t->xp.addPoints(amt);
                        }
                        sendSetExperience(*t);
                    }
                sendCommandFeedback(src, "Gave " + std::to_string(amt) + " xp");
                return 1;
            };
            auto suffix = CommandNode::argument("xpUnit", args::stringWord());
            suffix->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
                return std::vector<std::string>{"points", "levels"};
            };
            suffix->executable = true;
            suffix->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("xpTargets").asSelector();
                const int amt = c.arg("xpAmount").asInt();
                const std::string u = c.arg("xpUnit").asStr();
                if (u != "points" && u != "levels")
                    throw std::runtime_error("Unknown xp unit '" + u + "' (expected points or levels)");
                for (auto& nm : sel.playerNames)
                    if (Player* t = findPlayer(*this, nm)) {
                        {
                            std::lock_guard playerLock(t->stateMtx);
                            if (u == "levels") {
                                t->xp.level = std::max(0, t->xp.level + amt);
                                t->xp.totalXp = std::max(0, t->xp.totalXp + amt * xpToNextLevel(t->xp.level));
                            } else {
                                t->xp.addPoints(amt);
                            }
                        }
                        sendSetExperience(*t);
                    }
                sendCommandFeedback(src, "Gave " + std::to_string(amt) + " xp (" + u + ")");
                return 1;
            };
            amount->then(suffix);
            targets->then(amount);
            add->then(targets);
            xp->then(add);
            return xp;
        };
        d.root->then(buildXp("experience"));
        d.root->then(buildXp("xp"));
    }
}

void GameServer::initPlayerCommandsPart16() {
    auto& d = commands_;
    {
        // /summon <entity> [<pos>] — pos form (bare form already exists).
        auto summon = CommandNode::literal("summon");
        auto ent = CommandNode::argument("summonEntity", args::resourceLocation());
        auto pos = CommandNode::argument("summonPos", args::vec3());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string en = c.arg("summonEntity").asStr();
            if (en.find(':') == std::string::npos) en = "minecraft:" + en;
            auto it = gen::entityTypeIdByName().find(en);
            if (it == gen::entityTypeIdByName().end())
                throw std::runtime_error("Unknown entity: " + en);
            const auto v = c.arg("summonPos").asVec3();
            spawnMobByTypeNameFor(snapshotCommandDimension(c.source), en, v.x, v.y, v.z);
            sendCommandFeedback(src, "Summoned " + en);
            return 1;
        };
        ent->then(pos);
        summon->then(ent);
        d.root->then(summon);
    }
}

void GameServer::initPlayerCommandsPart17() {
    auto& d = commands_;
    {
        // /tp <targets> <pos> + /teleport alias (self /tp <pos> already exists).
        auto buildTp = [this](const std::string& litName) {
            auto tp = CommandNode::literal(litName);
            auto targets = CommandNode::argument("tpTargets", args::entity(false, false));
            auto pos = CommandNode::argument("tpPos", args::vec3());
            pos->executable = true;
            pos->action = [this](CommandContext& c) {
                Player* src = static_cast<Player*>(c.source.player);
                const auto sel = c.arg("tpTargets").asSelector();
                const auto v = c.arg("tpPos").asVec3();
                int n = 0;
                for (auto& nm : sel.playerNames)
                    if (Player* t = findPlayer(*this, nm)) {
                        PlayerCommandSnapshot target;
                        {
                            std::lock_guard playerLock(t->stateMtx);
                            t->fallDist = 0;
                            t->x = v.x; t->y = v.y; t->z = v.z;
                            target.connection = t->conn;
                            target.yaw = t->yaw;
                            target.pitch = t->pitch;
                        }
                        WriteBuffer tb;
                        tb.varint(++teleportCounterForTest_);
                        tb.f64(v.x); tb.f64(v.y); tb.f64(v.z);
                        tb.f64(0); tb.f64(0); tb.f64(0);
                        tb.f32(target.yaw); tb.f32(target.pitch);
                        tb.u32(0);
                        if (target.connection)
                            target.connection->trySendPacket(proto::pl::sc::PlayerPosition, tb);
                        ++n;
                    }
                if (n == 0) throw std::runtime_error("Unknown player for teleport");
                sendCommandFeedback(src, "Teleported " + std::to_string(n) + " entit" +
                             (n == 1 ? "y" : "ies") + " to " +
                             std::to_string(v.x) + ", " + std::to_string(v.y) +
                             ", " + std::to_string(v.z));
                return n;
            };
            targets->then(pos);
            tp->then(targets);
            return tp;
        };
        d.root->then(buildTp("tp"));
        d.root->then(buildTp("teleport"));
    }
}

void GameServer::initPlayerCommandsPart18() {
    auto& d = commands_;
    {
        // /spawnpoint [<targets>] [<pos>] [<angle>].  A player respawn point
        // is player data, not the world's default spawn; keeping these
        // separate matters for death, reconnect, and multi-dimensional play.
        auto sp = CommandNode::literal("spawnpoint");
        sp->executable = true;
        sp->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            if (!src) throw std::runtime_error("spawnpoint requires a player");
            PlayerCommandSnapshot state;
            {
                std::lock_guard playerLock(src->stateMtx);
                src->hasRespawnPoint = true;
                src->respawnX = static_cast<std::int32_t>(std::floor(src->x));
                src->respawnY = static_cast<std::int32_t>(std::floor(src->y));
                src->respawnZ = static_cast<std::int32_t>(std::floor(src->z));
                src->respawnDimension = canonicalDimension(src->dimension);
                src->respawnAngle = src->yaw;
                state.respawnX = src->respawnX;
                state.respawnY = src->respawnY;
                state.respawnZ = src->respawnZ;
                state.respawnAngle = src->respawnAngle;
                state.connection = src->conn;
                savePlayerData(uuidToHex(src->uuid), *src);
            }
            WriteBuffer point;
            point.position(state.respawnX, state.respawnY, state.respawnZ);
            point.f32(state.respawnAngle);
            if (state.connection)
                state.connection->trySendPacket(proto::pl::sc::SetDefaultSpawn, point);
            sendCommandFeedback(src, "Set spawn point to current position");
            return 1;
        };
        auto targets = CommandNode::argument("spTargets", args::entity(false, false));
        auto pos = CommandNode::argument("spPos", args::blockPos());
        pos->executable = true;
        pos->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("spTargets").asSelector();
            const auto p = c.arg("spPos").asBlockPos();
            const auto commandDim = snapshotCommandDimension(c.source);
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (Player* target = findPlayer(*this, nm)) {
                    PlayerCommandSnapshot state;
                    {
                        std::lock_guard playerLock(target->stateMtx);
                        target->hasRespawnPoint = true;
                        target->respawnX = p.x;
                        target->respawnY = p.y;
                        target->respawnZ = p.z;
                        target->respawnDimension = c.source.dimensionOverride
                                                       ? commandDim
                                                       : canonicalDimension(target->dimension);
                        target->respawnAngle = target->yaw;
                        state.respawnAngle = target->respawnAngle;
                        state.connection = target->conn;
                        savePlayerData(uuidToHex(target->uuid), *target);
                    }
                    WriteBuffer point;
                    point.position(p.x, p.y, p.z);
                    point.f32(state.respawnAngle);
                    if (state.connection)
                        state.connection->trySendPacket(proto::pl::sc::SetDefaultSpawn, point);
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for spawnpoint");
            sendCommandFeedback(src, "Set " + std::to_string(n) + " players' spawn point to " +
                         std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return n;
        };
        auto angle = CommandNode::argument("spAngle", args::angleArg());
        angle->executable = true;
        angle->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("spTargets").asSelector();
            const auto p = c.arg("spPos").asBlockPos();
            const auto commandDim = snapshotCommandDimension(c.source);
            const float angle = static_cast<float>(c.arg("spAngle").asDouble());
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (Player* target = findPlayer(*this, nm)) {
                    PlayerCommandSnapshot state;
                    {
                        std::lock_guard playerLock(target->stateMtx);
                        target->hasRespawnPoint = true;
                        target->respawnX = p.x;
                        target->respawnY = p.y;
                        target->respawnZ = p.z;
                        target->respawnDimension = c.source.dimensionOverride
                                                       ? commandDim
                                                       : canonicalDimension(target->dimension);
                        target->respawnAngle = angle;
                        state.connection = target->conn;
                        savePlayerData(uuidToHex(target->uuid), *target);
                    }
                    WriteBuffer point;
                    point.position(p.x, p.y, p.z);
                    point.f32(angle);
                    if (state.connection)
                        state.connection->trySendPacket(proto::pl::sc::SetDefaultSpawn, point);
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for spawnpoint");
            sendCommandFeedback(src, "Set " + std::to_string(n) + " players' spawn point to " +
                         std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z));
            return n;
        };
        pos->then(angle);
        targets->then(pos);
        sp->then(targets);
        d.root->then(sp);
    }
}

void GameServer::initPlayerCommandsPart19() {
    auto& d = commands_;
    {
        // /damage <targets> <amount> [<damageType>] (Yarn DamageCommand).
        auto dmg = CommandNode::literal("damage");
        auto targets = CommandNode::argument("dmgTargets", args::entity(false, false));
        auto amount = CommandNode::argument("dmgAmount", args::floatArg(0.f, 1000000.f));
        amount->executable = true;
        amount->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("dmgTargets").asSelector();
            const float amt = static_cast<float>(c.arg("dmgAmount").asDouble());
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (Player* t = findPlayer(*this, nm)) {
                    applyDamage(*t, amt, "generic");
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for damage");
            sendCommandFeedback(src, "Dealt " + std::to_string(amt) + " generic damage to " +
                         std::to_string(n) + " entit" + (n == 1 ? "y" : "ies"));
            return n;
        };
        auto dtype = CommandNode::argument("damageType", args::resourceLocation());
        dtype->executable = true;
        dtype->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("dmgTargets").asSelector();
            const float amt = static_cast<float>(c.arg("dmgAmount").asDouble());
            std::string dt = c.arg("damageType").asStr();
            if (dt.rfind("minecraft:", 0) == 0) dt = dt.substr(10);
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (Player* t = findPlayer(*this, nm)) {
                    applyDamage(*t, amt, dt.c_str());
                    ++n;
                }
            if (n == 0) throw std::runtime_error("Unknown player for damage");
            sendCommandFeedback(src, "Dealt " + std::to_string(amt) + " " + dt + " damage to " +
                         std::to_string(n) + " entit" + (n == 1 ? "y" : "ies"));
            return n;
        };
        amount->then(dtype);
        targets->then(amount);
        dmg->then(targets);
        d.root->then(dmg);
    }
}

void GameServer::initPlayerCommandsPart20() {
    auto& d = commands_;
    {
        // /particle <name> [<pos>] — full form with delta/speed/count. Ids: Prismarine minecraft-data 1.21.4 particles.json (112 entries).
        auto part = CommandNode::literal("particle");
        auto name = CommandNode::argument("particleName", args::resourceLocation());
        auto pos = CommandNode::argument("particlePos", args::vec3());
        auto dx = CommandNode::argument("pdx", args::floatArg(0.f, 1000000.f));
        auto dy = CommandNode::argument("pdy", args::floatArg(0.f, 1000000.f));
        auto dz = CommandNode::argument("pdz", args::floatArg(0.f, 1000000.f));
        auto speed = CommandNode::argument("pSpeed", args::floatArg(0.f, 1000000.f));
        auto count = CommandNode::argument("pCount", args::integer(1, 1000000));
        count->executable = true;
        static const std::unordered_map<std::string,int> kParticleIds = {
            {"minecraft:angry_villager",0},{"minecraft:block",1},{"minecraft:block_marker",2},
            {"minecraft:bubble",3},{"minecraft:cloud",4},{"minecraft:crit",5},
            {"minecraft:damage_indicator",6},{"minecraft:dragon_breath",7},
            {"minecraft:dripping_lava",8},{"minecraft:falling_lava",9},{"minecraft:landing_lava",10},
            {"minecraft:dripping_water",11},{"minecraft:falling_water",12},{"minecraft:dust",13},
            {"minecraft:dust_color_transition",14},{"minecraft:effect",15},{"minecraft:elder_guardian",16},
            {"minecraft:enchanted_hit",17},{"minecraft:enchant",18},{"minecraft:end_rod",19},
            {"minecraft:entity_effect",20},{"minecraft:explosion_emitter",21},{"minecraft:explosion",22},
            {"minecraft:gust",23},{"minecraft:small_gust",24},{"minecraft:gust_emitter_large",25},
            {"minecraft:gust_emitter_small",26},{"minecraft:sonic_boom",27},{"minecraft:falling_dust",28},
            {"minecraft:firework",29},{"minecraft:fishing",30},{"minecraft:flame",31},
            {"minecraft:infested",32},{"minecraft:cherry_leaves",33},{"minecraft:pale_oak_leaves",34},
            {"minecraft:sculk_soul",35},{"minecraft:sculk_charge",36},{"minecraft:sculk_charge_pop",37},
            {"minecraft:soul_fire_flame",38},{"minecraft:soul",39},{"minecraft:flash",40},
            {"minecraft:happy_villager",41},{"minecraft:composter",42},{"minecraft:heart",43},
            {"minecraft:instant_effect",44},{"minecraft:item",45},{"minecraft:vibration",46},
            {"minecraft:trail",47},{"minecraft:item_slime",48},{"minecraft:item_cobweb",49},
            {"minecraft:item_snowball",50},{"minecraft:large_smoke",51},{"minecraft:lava",52},
            {"minecraft:mycelium",53},{"minecraft:note",54},{"minecraft:poof",55},
            {"minecraft:portal",56},{"minecraft:rain",57},{"minecraft:smoke",58},
            {"minecraft:white_smoke",59},{"minecraft:sneeze",60},{"minecraft:spit",61},
            {"minecraft:squid_ink",62},{"minecraft:sweep_attack",63},{"minecraft:totem_of_undying",64},
            {"minecraft:underwater",65},{"minecraft:splash",66},{"minecraft:witch",67},
            {"minecraft:bubble_pop",68},{"minecraft:current_down",69},{"minecraft:bubble_column_up",70},
            {"minecraft:nautilus",71},{"minecraft:dolphin",72},{"minecraft:campfire_cosy_smoke",73},
            {"minecraft:campfire_signal_smoke",74},{"minecraft:dripping_honey",75},{"minecraft:falling_honey",76},
            {"minecraft:landing_honey",77},{"minecraft:falling_nectar",78},{"minecraft:falling_spore_blossom",79},
            {"minecraft:ash",80},{"minecraft:crimson_spore",81},{"minecraft:warped_spore",82},
            {"minecraft:spore_blossom_air",83},{"minecraft:dripping_obsidian_tear",84},
            {"minecraft:falling_obsidian_tear",85},{"minecraft:landing_obsidian_tear",86},
            {"minecraft:reverse_portal",87},{"minecraft:white_ash",88},{"minecraft:small_flame",89},
            {"minecraft:snowflake",90},{"minecraft:dripping_dripstone_lava",91},
            {"minecraft:falling_dripstone_lava",92},{"minecraft:dripping_dripstone_water",93},
            {"minecraft:falling_dripstone_water",94},{"minecraft:glow_squid_ink",95},{"minecraft:glow",96},
            {"minecraft:wax_on",97},{"minecraft:wax_off",98},{"minecraft:electric_spark",99},
            {"minecraft:scrape",100},{"minecraft:shriek",101},{"minecraft:egg_crack",102},
            {"minecraft:dust_plume",103},{"minecraft:trial_spawner_detection",104},
            {"minecraft:trial_spawner_detection_ominous",105},{"minecraft:vault_connection",106},
            {"minecraft:dust_pillar",107},{"minecraft:ominous_spawning",108},{"minecraft:raid_omen",109},
            {"minecraft:trial_omen",110},{"minecraft:block_crumble",111},
        };
        count->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            std::string nm = c.arg("particleName").asStr();
            if (nm.find(':') == std::string::npos) nm = "minecraft:" + nm;
            auto itp = kParticleIds.find(nm);
            if (itp == kParticleIds.end())
                throw std::runtime_error("Unknown particle: " + nm);
            const auto v = c.arg("particlePos").asVec3();
            WriteBuffer body = makeWorldParticlesBody(
                v.x, v.y, v.z,
                static_cast<float>(c.arg("pdx").asDouble()),
                static_cast<float>(c.arg("pdy").asDouble()),
                static_cast<float>(c.arg("pdz").asDouble()),
                static_cast<float>(c.arg("pSpeed").asDouble()),
                c.arg("pCount").asInt(), itp->second, ParticleData{}, false, false);
            broadcastPacketExceptInDimension(snapshotCommandDimension(c.source), nullptr,
                                             proto::pl::sc::WorldParticles, body);
            sendCommandFeedback(src, "Displayed particle " + nm);
            return 1;
        };
        speed->then(count);
        dz->then(speed); dy->then(dz); dx->then(dy);
        pos->then(dx);
        name->then(pos);
        part->then(name);
        d.root->then(part);
    }
}

void GameServer::initPlayerCommandsPart21() {
    auto& d = commands_;
    {
        // /playsound <sound> <source> <targets> [<pos> [<volume> [<pitch>]]]
        auto ps = CommandNode::literal("playsound");
        auto sound = CommandNode::argument("sound", args::resourceLocation());
        auto source = CommandNode::argument("psSource", args::stringWord());
        source->suggestions = [](brigadier::StringReader&, brigadier::ParseCtx&) {
            return std::vector<std::string>{"master","music","record","weather","block",
                                            "hostile","neutral","player","ambient","voice"};
        };
        auto targets = CommandNode::argument("psTargets", args::entity(false, false));
        targets->executable = true;
        auto doPlaysound = [this](CommandContext& c) -> int {
            Player* src = static_cast<Player*>(c.source.player);
            const std::string snd = c.arg("sound").asStr();
            std::string cat = c.arg("psSource").asStr();
            for (auto& ch : cat) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
            static const std::unordered_set<std::string> kCats = {
                "master","music","record","weather","block","hostile",
                "neutral","player","ambient","voice"};
            if (!kCats.count(cat))
                throw std::runtime_error("Unknown sound source '" + cat + "'");
            const auto sel = c.arg("psTargets").asSelector();
            const auto sourceState = src ? snapshotPlayer(*src) : PlayerCommandSnapshot{};
            double x = src ? sourceState.x : 0;
            double y = src ? sourceState.y : -60;
            double z = src ? sourceState.z : 0;
            float vol = 1.f, pitch = 1.f;
            auto itPos = c.args.find("psPos");
            if (itPos != c.args.end()) {
                const auto v = itPos->second.asVec3();
                x = v.x; y = v.y; z = v.z;
            }
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for playsound");
            broadcastSoundFor(snapshotCommandDimension(c.source), snd.c_str(), x, y, z,
                              vol, pitch, cat.c_str());
            sendCommandFeedback(src, "Played sound " + snd + " (playsound) to " +
                         std::to_string(n) + " player(s)");
            return 1;
        };
        targets->action = doPlaysound;
        auto ppos = CommandNode::argument("psPos", args::vec3());
        ppos->executable = true;
        ppos->action = doPlaysound;
        auto pvol = CommandNode::argument("psVolume", args::floatArg(0.f, 1000000.f));
        pvol->executable = true;
        pvol->action = doPlaysound;
        auto ppitch = CommandNode::argument("psPitch", args::floatArg(0.f, 2.f));
        ppitch->executable = true;
        ppitch->action = doPlaysound;
        pvol->then(ppitch);
        ppos->then(pvol);
        targets->then(ppos);
        source->then(targets);
        sound->then(source);
        ps->then(sound);
        d.root->then(ps);
    }
}

void GameServer::initPlayerCommandsPart22() {
    auto& d = commands_;
    {
        // /stopsound [<targets>] [<source>] [<sound>]
        auto ss = CommandNode::literal("stopsound");
        ss->executable = true;
        ss->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            broadcastStopSound(std::nullopt, std::nullopt);
            sendCommandFeedback(src, "Stopped all sounds (stopsound)");
            return 1;
        };
        auto targets = CommandNode::argument("ssTargets", args::entity(false, false));
        targets->executable = true;
        targets->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("ssTargets").asSelector();
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for stopsound");
            broadcastStopSound(std::nullopt, std::nullopt);
            sendCommandFeedback(src, "Stopped sounds for " + std::to_string(n) + " player(s) (stopsound)");
            return n;
        };
        auto source = CommandNode::argument("ssSource", args::stringWord());
        source->executable = true;
        source->action = targets->action;
        auto sound = CommandNode::argument("ssSound", args::resourceLocation());
        sound->executable = true;
        sound->action = [this](CommandContext& c) {
            Player* src = static_cast<Player*>(c.source.player);
            const auto sel = c.arg("ssTargets").asSelector();
            int n = 0;
            for (auto& nm : sel.playerNames)
                if (findPlayer(*this, nm)) ++n;
            if (n == 0) throw std::runtime_error("Unknown player for stopsound");
            std::string snd = c.arg("ssSound").asStr();
            broadcastStopSound(GameServer::SoundSource::Master, &snd);
            sendCommandFeedback(src, "Stopped sound " + snd + " (stopsound)");
            return n;
        };
        source->then(sound);
        targets->then(source);
        ss->then(targets);
        d.root->then(ss);
    }
}


} // namespace cppfm
