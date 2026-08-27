#include "TestClient.hpp"
#include "../src/core/NBT.hpp"
#include <cstdlib>

static std::size_t rest_size(cppfm::ReadBuffer& r){ return r.len - r.off; }
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <algorithm>

namespace cpptest {

// Minimal MD5 for OfflinePlayer:<name> UUIDv3 (public algorithm).
static void md5(const std::string& msg, std::uint8_t out[16]) {
    static const std::uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int S[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
                              5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                              4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
                              6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    std::uint8_t msg2[512]; std::size_t len = msg.size();
    std::memcpy(msg2, msg.data(), len);
    msg2[len++] = 0x80;
    while (len % 64 != 56) msg2[len++] = 0;   // pad to ≡56 (mod 64)
    std::uint64_t bits = static_cast<std::uint64_t>(msg.size()) * 8;
    std::memcpy(msg2 + len, &bits, 8); len += 8;   // little-endian on x86
    std::uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    for (std::size_t off = 0; off < len; off += 64) {
        std::uint32_t m[16];
        for (int i = 0; i < 16; ++i)
            m[i] = msg2[off+i*4] | (msg2[off+i*4+1]<<8) | (msg2[off+i*4+2]<<16) | (msg2[off+i*4+3]<<24);
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f; int g;
            if (i<16){ f=(b&c)|(~b&d); g=i; }
            else if(i<32){ f=(d&b)|(~d&c); g=(5*i+1)%16; }
            else if(i<48){ f=b^c^d; g=(3*i+5)%16; }
            else { f=c^(b|~d); g=(7*i)%16; }
            std::uint32_t tmp=d; d=c; c=b;
            b=b+((a+f+K[i]+m[g]) << S[i] | (a+f+K[i]+m[g]) >> (32-S[i]));
            a=tmp;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
    }
    for (int i=0;i<4;++i){ out[i*4]=h[i]&0xff; out[i*4+1]=(h[i]>>8)&0xff; out[i*4+2]=(h[i]>>16)&0xff; out[i*4+3]=(h[i]>>24)&0xff; }
}

bool TestClient::connect(const std::string& host, std::uint16_t port, int timeoutSec) {
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
        return false;
    int fd = ::socket(res->ai_family, SOCK_STREAM, 0);
    timeval tv{timeoutSec, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    bool ok = ::connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok) { ::close(fd); return false; }
    conn_ = std::make_unique<Connection>(fd);
    conn_->setNoDelay();
    sockaddr_in la{}; socklen_t ll = sizeof(la);
    getsockname(fd, reinterpret_cast<sockaddr*>(&la), &ll);
    localPort_ = ntohs(la.sin_port);
    return true;
}

void TestClient::close() {
    running_ = false;
    if (reader_.joinable()) reader_.join();
    if (conn_) conn_->close();
    conn_.reset();
}

std::string TestClient::queryStatusJson(std::uint16_t) {
    WriteBuffer hb;
    hb.varint(proto::kProtocolVersion);
    hb.string("127.0.0.1");
    hb.u16(25565);
    hb.varint(1);
    conn_->sendPacket(proto::hb::cs::Intention, hb);
    conn_->sendPacket(proto::st::cs::Request, {});
    // no reader thread yet: read frames directly
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < deadline) {
        Packet p;
        try { p.body = conn_->readFrame(); }
        catch (const std::exception& e) { lastError = e.what(); return {}; }
        ReadBuffer in(p.body);
        const std::uint8_t pid = in.u8();
        if (pid == proto::st::sc::Response) {
            const std::string js = in.string();
            // ping/pong leg (vanilla closes afterwards)
            WriteBuffer ping;
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            ping.i64(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
            conn_->sendPacket(proto::st::cs::Ping, ping);
            try { auto f = conn_->readFrame(); (void)f; } catch (...) {}
            return js;
        }
    }
    lastError = "no status response";
    return {};
}

static cppfm::crypto::Bytes rsaEncryptPub(const cppfm::crypto::Bytes& pubDer, const cppfm::crypto::Bytes& pt) {
    const unsigned char* pp = pubDer.data();
    EVP_PKEY* pk = d2i_PUBKEY(nullptr, &pp, (long)pubDer.size());
    if (!pk) throw std::runtime_error("bad pubkey der");
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pk, nullptr);
    EVP_PKEY_encrypt_init(ctx);
    size_t outl = 0;
    EVP_PKEY_encrypt(ctx, nullptr, &outl, pt.data(), pt.size());
    cppfm::crypto::Bytes ct(outl);
    EVP_PKEY_encrypt(ctx, ct.data(), &outl, pt.data(), pt.size());
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pk);
    ct.resize(outl);
    return ct;
}

bool TestClient::joinOnline(const std::string& name) {
    WriteBuffer hb;
    hb.varint(proto::kProtocolVersion);
    hb.string("127.0.0.1");
    hb.u16(25565);
    hb.varint(2);
    conn_->sendPacket(proto::hb::cs::Intention, hb);

    unsigned char uuid[16];
    md5("OfflinePlayer:" + name, uuid);
    WriteBuffer hello;
    hello.string(name);
    hello.uuid(uuid);
    conn_->sendPacket(proto::lo::cs::Hello, hello);

    cppfm::crypto::Bytes sharedSecret(16, 0);
    RAND_bytes(sharedSecret.data(), 16);

    bool sawSuccess = false;
    for (int guard = 0; guard < 60 && !sawSuccess; ++guard) {
        auto frame = conn_->readFrame();
        ReadBuffer in(frame);
        const std::uint8_t pid = in.u8();
        switch (pid) {
        case proto::lo::sc::EncryptionRequest: {
            (void)in.string();
            auto pkLen = in.varint();
            cppfm::crypto::Bytes pubDer = in.bytes(pkLen);
            auto tkLen = in.varint();
            cppfm::crypto::Bytes token = in.bytes(tkLen);
            cppfm::crypto::Bytes secretCt = rsaEncryptPub(pubDer, sharedSecret);
            cppfm::crypto::Bytes tokCt = rsaEncryptPub(pubDer, token);
            WriteBuffer resp;
            resp.varint((std::int32_t)secretCt.size()); resp.raw(secretCt.data(), secretCt.size());
            resp.varint((std::int32_t)tokCt.size()); resp.raw(tokCt.data(), tokCt.size());
            conn_->sendPacket(proto::lo::cs::Key, resp);
            conn_->enableEncryption(sharedSecret);
            break;
        }
        case proto::lo::sc::SetCompression: {
            ReadBuffer cin(frame);
            cin.u8();
            auto th = cin.varint();
            conn_->setCompression(th);
            break;
        }
        case proto::lo::sc::GameProfile:
            sawSuccess = true;
            break;
        default:
            lastError = "unexpected login packet (online)";
            return false;
        }
    }
    if (!sawSuccess) { lastError = "no success (online)"; return false; }

    conn_->sendPacket(proto::lo::cs::LoginAcknowledged, {});
    bool finishSeen = false;
    for (int guard = 0; guard < 400 && !finishSeen; ++guard) {
        Packet p;
        try { p.body = conn_->readFrame(); }
        catch (const std::exception& e) { lastError = e.what(); return false; }
        ReadBuffer in(p.body);
        switch (in.u8()) {
        case proto::cf::sc::CustomPayload: in.string(); break;
        case proto::cf::sc::SelectKnownPacks: {
            const std::int32_t n = in.varint();
            for (std::int32_t i = 0; i < n; ++i) { (void)in.string(); (void)in.string(); (void)in.string(); }
            conn_->sendPacket(proto::cf::cs::SelectKnownPacks, WriteBuffer{});
            break;
        }
        case proto::cf::sc::KeepAlive: {
            WriteBuffer echo; echo.raw(in.p + in.off, in.remaining());
            conn_->sendPacket(proto::cf::cs::KeepAlive, echo);
            break;
        }
        case proto::cf::sc::FinishConfiguration:
            finishSeen = true;
            conn_->sendPacket(proto::cf::cs::FinishAcknowledgement, {});
            break;
        default: break;
        }
    }
    if (!finishSeen) { lastError = "no finish (online)"; return false; }

    running_ = true;
    reader_ = std::thread([this]{ readerLoop(); });
    return true;
}

bool TestClient::join(const std::string& name) {
    std::fprintf(stderr, "[tc] join(\"%s\") local=%u caller=%p\n",
                 name.c_str(), localPort_, __builtin_return_address(0));
    WriteBuffer hb;
    hb.varint(proto::kProtocolVersion);
    hb.string("127.0.0.1");
    hb.u16(25565);
    hb.varint(2);
    conn_->sendPacket(proto::hb::cs::Intention, hb);

    std::uint8_t uuid[16];
    md5("OfflinePlayer:" + name, uuid);
    WriteBuffer hello;
    hello.string(name);
    hello.uuid(uuid);
    conn_->sendPacket(proto::lo::cs::Hello, hello);

    bool sawSuccess = false, sawCompression = false;
    for (int guard = 0; guard < 50 && !sawSuccess; ++guard) {
        Packet p;
        try { p.id = 0xFF; auto f = conn_->readFrame(); p.body = std::move(f); }
        catch (const std::exception& e) { lastError = e.what(); return false; }
        ReadBuffer in(p.body);
        const std::uint8_t pid = in.u8();
        switch (pid) {
        case proto::lo::sc::SetCompression: {
            sawCompression = true;
            const std::int32_t th = in.varint();
            conn_->setCompression(th);
            break;
        }
        case proto::lo::sc::GameProfile:
            sawSuccess = true;
            break;
        case proto::lo::sc::Disconnect:
            lastError = "kicked at login";
            return false;
        default:
            lastError = "unexpected login packet";
            return false;
        }
    }
    (void)sawCompression;

    // configuration phase
    conn_->sendPacket(proto::lo::cs::LoginAcknowledged, {});
    bool finishSeen = false;
    std::vector<std::uint8_t> knownPacksReply;
    for (int guard = 0; guard < 400 && !finishSeen; ++guard) {
        Packet p;
        try { p.id = 0xFF; p.body = conn_->readFrame(); }
        catch (const std::exception& e) { lastError = e.what(); return false; }
        ReadBuffer in(p.body);
        const std::uint8_t cfgPid = in.u8();
        if (getenv("TC_VERBOSE")) {
            std::fprintf(stderr, "[tc pid=%d] cfg read %02x total=%zu head=",(int)getpid(), cfgPid, p.body.size());
            for (std::size_t k = 0; k < p.body.size() && k < 6; ++k) std::fprintf(stderr, "%02x", p.body[k]);
            std::fprintf(stderr, "\n");
        }
        switch (cfgPid) {
        case proto::cf::sc::CustomPayload: in.string(); break;
        case proto::cf::sc::SelectKnownPacks: {
            const std::int32_t n = in.varint();
            for (std::int32_t i = 0; i < n; ++i) { (void)in.string(); (void)in.string(); (void)in.string(); }
            WriteBuffer reply; reply.varint(0);
            knownPacksReply = reply.data;
            conn_->sendPacket(proto::cf::cs::SelectKnownPacks, reply);
            break;
        }
        case proto::cf::sc::KeepAlive: {
            WriteBuffer echo; echo.raw(in.p + in.off, in.remaining());
            conn_->sendPacket(proto::cf::cs::KeepAlive, echo);
            break;
        }
        case proto::cf::sc::FinishConfiguration:
            finishSeen = true;
            conn_->sendPacket(proto::cf::cs::FinishAcknowledgement, {});
            break;
        case proto::cf::sc::Disconnect:
            lastError = "kicked at config";
            return false;
        default: break;
        }
    }
    if (!finishSeen) { lastError = "no finish_configuration"; return false; }

    // play phase: synchronously process frames until the join game packet has
    // been captured (eliminates any startup race), then hand over to the reader.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
    bool gotLogin = false;
    while (!gotLogin && std::chrono::steady_clock::now() < deadline) {
        Packet p;
        try { p.body = conn_->readFrame(); }
        catch (const std::exception& e) { lastError = e.what(); return false; }
        ReadBuffer in(p.body);
        p.id = in.u8();
        if (p.id == proto::pl::sc::KeepAlive) respondKeepAlive(in.i64());
        handleIncoming(p.id, std::vector<std::uint8_t>(in.p + in.off, in.p + in.len));
        gotLogin = count(proto::pl::sc::Login) > 0;
    }
    if (!gotLogin) { lastError = "no join game during startup burst"; return false; }

    running_ = true;
    reader_ = std::thread([this]{ readerLoop(); });
    return true;
}

void TestClient::sendPlayerLoadedOnce() {
    if (!playerLoadedSent_) { playerLoadedSent_ = true; conn_->sendPacket(proto::pl::cs::PlayerLoaded, {}); }
}

void TestClient::readerLoop() {
    static std::atomic<int> seq{0};
    const int myId = ++seq;
    int transientErrors = 0;
    while (running_) {
        Packet p;
        try { p.body = conn_->readFrame(); transientErrors = 0; }
        catch (const SocketClosedError& e) {
            if (e.timedOut) continue;              // idle window: keep waiting
            std::fprintf(stderr, "[tc] reader#%d exit: %s\n", myId, e.what());
            running_ = false; break;
        }
        catch (const std::exception& e) {
            if (++transientErrors > 8) {           // do not spin on garbage
                std::fprintf(stderr, "[tc] reader#%d exit: %s\n", myId, e.what());
                running_ = false; break;
            }
            continue;
        }
        ReadBuffer in(p.body);
        p.id = in.u8();
        {   // permanent lightweight trace of the first packets of each reader
            if (myFirstPackets < 10) {
                std::fprintf(stderr, "[tc pid=%d] r#%d filed %02x raw=",(int)getpid(), myId, p.id);
                const std::size_t n = std::min<std::size_t>(in.len - in.off, 8);
                for (std::size_t k = 0; k < n; ++k)
                    std::fprintf(stderr, "%02x", p.body[k]);
                std::fprintf(stderr, "\n");
            }
            ++myFirstPackets;
        }
        // automatic server-required responses
        if (p.id == proto::pl::sc::KeepAlive) {
            const std::int64_t id = in.i64();
            respondKeepAlive(id);
        }
        handleIncoming(p.id, std::vector<std::uint8_t>(in.p + in.off, in.p + in.len));
    }
}

void TestClient::handleIncoming(std::uint8_t pid, std::vector<std::uint8_t> body) {
    Packet p{pid, std::move(body)};
    try {
        if (p.id == proto::pl::sc::PlayerPosition) {   // parse & confirm teleport
            ReadBuffer pin(p.body);
            const std::int32_t tid = pin.varint();
            x = pin.f64(); y = pin.f64(); z = pin.f64();
            confirmTeleport(tid);
            sendPlayerLoadedOnce();
        } else if (p.id == proto::pl::sc::KeepAlive) {
            // already responded by caller for sync path; safe to re-respond here
        }
        filePacket(std::move(p));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[tc] filePacket error (id=%02x): %s\n", p.id, e.what());
    }
}

void TestClient::filePacket(Packet p) {
    static const auto start = std::chrono::steady_clock::now();
    p.t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    {
        std::lock_guard lk(mtx_);
        switch (p.id) {
        case proto::pl::sc::LevelChunkWithLight: {
            ReadBuffer in(p.body);
            const std::int32_t cx = in.i32(), cz = in.i32();
            chunkCoords.emplace_back(cx, cz);
            if (cx == 0 && cz == 0) hasChunk00 = true;
            rawChunks.push_back(p.body);          // FULL body (helpers parse header)
            break;
        }
        case proto::pl::sc::BlockUpdate: {
            ReadBuffer in(p.body);
            std::int32_t x,y,z; in.position(x,y,z);
            blockUpdates.push_back({x,y,z, static_cast<std::uint32_t>(in.varint())});
            break;
        }
        case proto::pl::sc::SystemChat: {
            std::string text;
            if (extractChatText(p.body, text)) chatLines.push_back(text);
            break;
        }
        case proto::pl::sc::PlayerChat: {
            // PlayerChat 0x3B: sender(16) + index varint + hasSig bool + [sig] + message string + timestamp + salt ...
            try {
                ReadBuffer in(p.body);
                if (in.remaining() < 16) throw std::runtime_error("short playerchat");
                in.bytes(16);
                (void)in.varint();
                bool hasSig = in.boolean();
                if (hasSig) {
                    int32_t slen = in.varint();
                    if (slen > 0 && slen < 2048) in.bytes((size_t)slen);
                }
                std::string msg = in.string(8192);
                if (!msg.empty()) chatLines.push_back(msg);
                // Also try to capture formatted component later (sender name) if msg empty
                if (msg.empty()) {
                    // heuristic fallback: search raw payload for printable message
                    std::string raw(reinterpret_cast<const char*>(p.body.data()), p.body.size());
                    // already tried; ignore
                }
            } catch (...) {
                // fallback: raw search for known substrings – push whole raw as chat line for waitChat heuristic
                std::string raw(reinterpret_cast<const char*>(p.body.data()), p.body.size());
                // Keep only printable part
                std::string filtered;
                for (unsigned char c : raw) if (c >= 32 && c <= 126) filtered.push_back(c); else filtered.push_back(' ');
                chatLines.push_back(filtered);
            }
            break;
        }
        case proto::pl::sc::DisguisedChat: {
            // DisguisedChat 0x1C: message string + chat type? Try to extract via string parsing
            try {
                ReadBuffer in(p.body);
                // first field may be chat component? Try to extract via extractChatText fallback
                std::string text;
                if (extractChatText(p.body, text)) chatLines.push_back(text);
                else {
                    // try to read as string
                    try { std::string s = in.string(8192); if (!s.empty()) chatLines.push_back(s); } catch(...) {}
                }
            } catch(...) {}
            break;
        }
        case proto::pl::sc::MultiBlockChange: {
            try {
                ReadBuffer in(p.body);
                uint64_t packed = in.u64();
                int32_t baseCx = (int32_t)((packed >> 42) & 0x3FFFFF);
                // sign extend 22 bits
                if (baseCx & 0x200000) baseCx |= ~0x3FFFFF;
                int32_t baseCz = (int32_t)((packed >> 20) & 0x3FFFFF);
                if (baseCz & 0x200000) baseCz |= ~0x3FFFFF;
                int32_t baseSy = (int32_t)(packed & 0xFFFFF);
                if (baseSy & 0x80000) baseSy |= ~0xFFFFF;
                int32_t cnt = in.varint();
                for (int i=0;i<cnt;i++) {
                    int32_t enc = in.varint();
                    uint32_t state = (uint32_t)(enc >> 12);
                    int32_t lx = (enc >> 8) & 0xF;
                    int32_t lz = (enc >> 4) & 0xF;
                    int32_t ly = enc & 0xF;
                    int32_t x = (baseCx << 4) | lx;
                    int32_t y = (baseSy << 4) | ly;
                    int32_t z = (baseCz << 4) | lz;
                    blockUpdates.push_back({x,y,z, state});
                }
            } catch(...) {}
            break;
        }
        case proto::pl::sc::BundleDelimiter: {
            // no payload, just marker for smoke bundle check – count via recent, ignore here
            break;
        }
        case proto::pl::sc::BossBar:
        case proto::pl::sc::Teams:
            // keep in recent for count, no extra state needed
            break;
        case proto::pl::sc::AckBlockChange: acks++; break;
        case proto::pl::sc::SpawnEntity: spawnsReceived++; break;
        case proto::pl::sc::MoveEntityPosRot:
        case proto::pl::sc::MoveEntityPos:
        case proto::pl::sc::EntityTeleport: entityMoves++; break;
        case proto::pl::sc::UpdateTime: timeUpdates++; break;
        case proto::pl::sc::DeclareCommands: declares++; break;
        case proto::pl::sc::Respawn: gotRespawn = true; break;
        default: break;
        }
        recent_.push_back(std::move(p));
        if (recent_.size() > 4096) recent_.pop_front();
    }
    cv_.notify_all();
}

bool TestClient::extractChatText(const std::vector<std::uint8_t>& body, std::string& out) {
    // body: anonymous NBT component + overlay bool; supports {"text": "..."} and plain string
    if (body.size() < 2) return false;
    if (body[0] == nbt::String) {                     // plain string tag
        const std::uint16_t n = (body[1] << 8) | body[2];
        out.assign(reinterpret_cast<const char*>(body.data()) + 3, n);
        return true;
    }
    if (body[0] != nbt::Compound) return false;
    // walk first entry: expect String "text"
    std::size_t i = 1;
    while (i + 3 <= body.size() && body[i] == nbt::String) {
        const std::uint16_t nl = (body[i+1] << 8) | body[i+2];
        i += 3 + nl;
        if (i + 2 > body.size()) return false;
        const std::uint16_t vl = (body[i] << 8) | body[i+1];
        if (nl == 4) {
            std::string key(reinterpret_cast<const char*>(body.data()) + i - nl, nl);
            if (key == "text") { out.assign(reinterpret_cast<const char*>(body.data()) + i + 2, vl); return true; }
        }
        i += 2 + vl;
    }
    return false;
}

void TestClient::confirmTeleport(std::int32_t teleportId) {
    WriteBuffer b; b.varint(teleportId);
    conn_->sendPacket(proto::pl::cs::AcceptTeleportation, b);
}
void TestClient::sendPlayerLoaded() { conn_->sendPacket(proto::pl::cs::PlayerLoaded, {}); }

void TestClient::sendPosition(double px, double py, double pz, bool onGround) {
    x = px; y = py; z = pz;
    WriteBuffer b;
    b.f64(px); b.f64(py); b.f64(pz);
    b.u8(onGround ? 1 : 0);
    conn_->sendPacket(proto::pl::cs::MovePlayerPos, b);
}

void TestClient::sendChatMessage(const std::string& message) {
    WriteBuffer b;
    b.string(message);
    b.i64(0); b.i64(0);
    b.boolean(false);
    b.varint(0);
    b.u8(0); b.u8(0); b.u8(0);   // acknowledged bitset
    conn_->sendPacket(proto::pl::cs::ChatMessage, b);
}

void TestClient::sendChatCommand(const std::string& command) {
    WriteBuffer b; b.string(command);
    conn_->sendPacket(proto::pl::cs::ChatCommand, b);
}

static void packPos(WriteBuffer& b, std::int32_t x, std::int32_t y, std::int32_t z) {
    b.position(x, y, z);
}
void TestClient::sendDig(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t seq) {
    WriteBuffer b;
    b.varint(0);                 // started digging (creative: instant)
    packPos(b, x, y, z);
    b.i8(1);                     // face
    b.varint(seq);
    conn_->sendPacket(proto::pl::cs::PlayerAction, b);
}
void TestClient::sendRespawnRequest() {
    WriteBuffer b; b.varint(0);
    conn_->sendPacket(proto::pl::cs::ClientCommand, b);
}
void TestClient::respondKeepAlive(std::int64_t id) {
    WriteBuffer b; b.i64(id);
    conn_->sendPacket(proto::pl::cs::KeepAlive, b);
}

void TestClient::pump(int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool TestClient::waitFor(std::function<bool(const Packet&)> pred, int timeoutMs, Packet* out) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock lk(mtx_);
    while (true) {
        for (auto it = recent_.rbegin(); it != recent_.rend(); ++it)
            if (pred(*it)) { if (out) *out = *it; return true; }
        if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) return false;
    }
}

size_t TestClient::count(std::uint8_t id) const {
    std::lock_guard lk(mtx_);
    size_t n = 0;
    for (auto& p : recent_) if (p.id == id) ++n;
    return n;
}

} // namespace cpptest
