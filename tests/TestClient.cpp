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

void TestClient::close() noexcept {
    try {
        running_ = false;
        if (conn_) {
            try { conn_->close(); } catch (...) {}
        }
        if (reader_.joinable()) {
            try { reader_.join(); } catch (...) {}
        }
        conn_.reset();
    } catch (...) {}
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
    if (!playerLoadedSent_) { playerLoadedSent_ = true; try { if (conn_) conn_->sendPacket(proto::pl::cs::PlayerLoaded, {}); } catch (...) {} }
}

void TestClient::readerLoop() {
    static std::atomic<int> seq{0};
    const int myId = ++seq;
    int transientErrors = 0;
    while (running_) {
        if (!conn_) { running_ = false; break; }
        Packet p;
        try { p.body = conn_->readFrame(); transientErrors = 0; }
        catch (const SocketClosedError& e) {
            if (e.timedOut && running_) continue;              // idle window: keep waiting
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
        {   // permanent lightweight trace of the first packets of each reader,
            // or ALL packets when CPPFM_PKTRACE is set (debug aid)
            const bool full = std::getenv("CPPFM_PKTRACE") != nullptr;
            if (full || myFirstPackets < 10) {
                std::fprintf(stderr, "[tc pid=%d] r#%d filed %02x raw=",(int)getpid(), myId, p.id);
                const std::size_t n = std::min<std::size_t>(in.len - in.off, full ? 32 : 8);
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
        case proto::pl::sc::Login: joinGameBody = p.body; break;
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
    try { if (conn_) conn_->sendPacket(proto::pl::cs::AcceptTeleportation, b); } catch (...) {}
}
void TestClient::sendPlayerLoaded() { try { if (conn_) conn_->sendPacket(proto::pl::cs::PlayerLoaded, {}); } catch (...) {} }

void TestClient::sendPosition(double px, double py, double pz, bool onGround) {
    x = px; y = py; z = pz;
    if (!conn_) return;
    WriteBuffer b;
    b.f64(px); b.f64(py); b.f64(pz);
    b.u8(onGround ? 1 : 0);
    try { conn_->sendPacket(proto::pl::cs::MovePlayerPos, b); } catch (...) {}
}

void TestClient::sendChatMessage(const std::string& message) {
    if (!conn_) return;
    WriteBuffer b;
    b.string(message);
    b.i64(0); b.i64(0);
    b.boolean(false);
    b.varint(0);
    b.u8(0); b.u8(0); b.u8(0);   // acknowledged bitset
    try { conn_->sendPacket(proto::pl::cs::ChatMessage, b); } catch (...) {}
}

void TestClient::sendChatCommand(const std::string& command) {
    if (!conn_) return;
    WriteBuffer b; b.string(command);
    try { conn_->sendPacket(proto::pl::cs::ChatCommand, b); } catch (...) {}
}

static void packPos(WriteBuffer& b, std::int32_t x, std::int32_t y, std::int32_t z) {
    b.position(x, y, z);
}
void TestClient::sendDig(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t seq) {
    if (!conn_) return;
    WriteBuffer b;
    b.varint(0);                 // started digging (creative: instant)
    packPos(b, x, y, z);
    b.i8(1);                     // face
    b.varint(seq);
    try { conn_->sendPacket(proto::pl::cs::PlayerAction, b); } catch (...) {}
}
void TestClient::sendUseItemOn(std::int32_t x, std::int32_t y, std::int32_t z, int face, std::int32_t seq) {
    if (!conn_) return;
    WriteBuffer b;
    b.varint(0); // hand main
    b.position(x,y,z);
    b.varint(face);
    b.f32(0.5f); b.f32(0.5f); b.f32(0.5f);
    b.boolean(false); // insideBlock
    b.boolean(false); // worldBorderHit
    b.varint(seq);
    try { conn_->sendPacket(proto::pl::cs::UseItemOn, b); } catch (...) {}
}
void TestClient::sendUseEntity(std::int32_t entityId, int action, bool sneaking) {
    if (!conn_) return;
    WriteBuffer b;
    b.varint(entityId);
    b.varint(action);
    if (action == 2) { b.f32(0.5f); b.f32(0.5f); b.f32(0.5f); }
    // plan43 W-02: spec layout — hand varint (mainhand default) for INTERACT/
    // INTERACT_AT only, then trailing sneaking bool for ALL mouse kinds.
    // (The old shape wrote sneaking as varint and no trailing bool, which the
    // fixed server reads as underrun -> disconnect.)
    if (action != 1) b.varint(0);                            // hand = mainhand
    b.boolean(sneaking);
    try { conn_->sendPacket(proto::pl::cs::UseEntity, b); } catch (...) {}
}
// plan43 B1+B2 spec-exact sends (Prismarine protocol.json 1.21.4 hand-built,
// NOT copied from server WriteBuffer output — tautology guard).
void TestClient::sendSignedCommand(const std::string& command, int nSignatures, std::uint8_t sigByte) {
    if (!conn_) return;
    WriteBuffer b;
    b.string(command);
    b.i64(0); b.i64(0);                                  // timestamp, salt
    b.varint(nSignatures);                               // argumentSignatures count (no boolean)
    for (int i = 0; i < nSignatures; ++i) {
        b.string("arg" + std::to_string(i));             // argumentName
        std::vector<std::uint8_t> sig(256, sigByte);     // fixed 256B buffer
        b.raw(sig.data(), sig.size());
    }
    b.varint(0);                                         // messageCount
    const std::uint8_t ack[3] = {0, 0, 0};               // acknowledged[3]
    b.raw(ack, 3);
    try { conn_->sendPacket(proto::pl::cs::ChatCommandSigned, b); } catch (...) {}
}
void TestClient::sendTabComplete(std::int32_t transactionId, const std::string& text) {
    if (!conn_) return;
    WriteBuffer b;
    b.varint(transactionId);
    b.string(text);                                      // spec: 2 fields only, no trailing bool
    try { conn_->sendPacket(proto::pl::cs::TabComplete, b); } catch (...) {}
}
void TestClient::sendMovePlayerFlags(double px, double py, double pz, std::uint8_t flags) {
    x = px; y = py; z = pz;
    if (!conn_) return;
    WriteBuffer b;
    b.f64(px); b.f64(py); b.f64(pz);
    b.u8(flags);                                         // MovementFlags bitfield (bit0 onGround)
    try { conn_->sendPacket(proto::pl::cs::MovePlayerPos, b); } catch (...) {}
}
void TestClient::sendMovePlayerPosRotFlags(double px, double py, double pz, float yaw, float pitch, std::uint8_t flags) {
    x = px; y = py; z = pz;
    if (!conn_) return;
    WriteBuffer b;
    b.f64(px); b.f64(py); b.f64(pz); b.f32(yaw); b.f32(pitch);
    b.u8(flags);
    try { conn_->sendPacket(proto::pl::cs::MovePlayerPosRot, b); } catch (...) {}
}
void TestClient::sendMovePlayerRotFlags(float yaw, float pitch, std::uint8_t flags) {
    if (!conn_) return;
    WriteBuffer b;
    b.f32(yaw); b.f32(pitch);
    b.u8(flags);
    try { conn_->sendPacket(proto::pl::cs::MovePlayerRot, b); } catch (...) {}
}
void TestClient::sendFlyingFlags(std::uint8_t flags) {
    if (!conn_) return;
    WriteBuffer b;
    b.u8(flags);
    try { conn_->sendPacket(proto::pl::cs::MovePlayerStatusOnly, b); } catch (...) {}
}
void TestClient::sendUseEntityFull(std::int32_t target, int mouse, int hand, bool sneaking) {
    if (!conn_) return;
    WriteBuffer b;
    b.varint(target);
    b.varint(mouse);
    if (mouse == 2) { b.f32(0.5f); b.f32(0.5f); b.f32(0.5f); }
    if (mouse == 0 || mouse == 2) b.varint(hand);         // hand (0 main/1 off)
    b.boolean(sneaking);                                 // trailing bool for ALL mouse kinds
    try { conn_->sendPacket(proto::pl::cs::UseEntity, b); } catch (...) {}
}
void TestClient::sendAbilitiesFlags(std::int8_t flags) {
    if (!conn_) return;
    WriteBuffer b;
    b.i8(flags);
    try { conn_->sendPacket(proto::pl::cs::Abilities, b); } catch (...) {}    // cs packet_abilities 0x26 (W-06)
}
void TestClient::sendSignUpdate(std::int32_t sx, std::int32_t sy, std::int32_t sz, bool front,
                                const std::string lines[4]) {
    if (!conn_) return;
    WriteBuffer b;
    b.position(sx, sy, sz);
    b.boolean(front);
    for (int i = 0; i < 4; ++i) b.string(lines[i]);
    try { conn_->sendPacket(proto::pl::cs::UpdateSign, b); } catch (...) {}
}
void TestClient::sendRawPlay(std::uint8_t pid, const WriteBuffer& body) {
    if (!conn_) return;
    try { conn_->sendPacket(pid, body); } catch (...) {}
}
bool TestClient::joinWithFinishContamination(const std::string& name) {
    // plan43 W-12: vanilla-style join that re-sends ClientInformation, Pong,
    // ResourcePackResponse and SelectKnownPacks right BEFORE FinishAcknowledgement.
    // Pre-fix server throws ("unexpected packet during finish-ack wait") -> join fails.
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
    bool sawSuccess = false;
    for (int guard = 0; guard < 50 && !sawSuccess; ++guard) {
        Packet p;
        try { p.body = conn_->readFrame(); }
        catch (const std::exception& e) { lastError = e.what(); return false; }
        ReadBuffer in(p.body);
        switch (in.u8()) {
        case proto::lo::sc::SetCompression: conn_->setCompression(in.varint()); break;
        case proto::lo::sc::GameProfile: sawSuccess = true; break;
        default: lastError = "unexpected login packet"; return false;
        }
    }
    if (!sawSuccess) { lastError = "no success"; return false; }
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
            WriteBuffer reply; reply.varint(0);
            conn_->sendPacket(proto::cf::cs::SelectKnownPacks, reply);
            break;
        }
        case proto::cf::sc::KeepAlive: {
            WriteBuffer echo; echo.raw(in.p + in.off, in.remaining());
            conn_->sendPacket(proto::cf::cs::KeepAlive, echo);
            break;
        }
        case proto::cf::sc::FinishConfiguration: finishSeen = true; break;
        case proto::cf::sc::Disconnect: lastError = "kicked at config"; return false;
        default: break;
        }
    }
    if (!finishSeen) { lastError = "no finish_configuration"; return false; }
    // --- contaminate the finish-ack wait, then ack ---
    {   // ClientInformation resend (config 0x00 layout: locale/i8/varint/bool/u8/varint/bool/bool)
        WriteBuffer s;
        s.string("en_us"); s.i8(8); s.varint(0); s.boolean(true);
        s.u8(0x7f); s.varint(0); s.boolean(false); s.boolean(true);
        conn_->sendPacket(proto::cf::cs::ClientInformation, s);
    }
    { WriteBuffer p; p.i32(1234); conn_->sendPacket(proto::cf::cs::Pong, p); }
    {   // ResourcePackResponse (uuid + result varint)
        WriteBuffer r;
        std::uint8_t z[16] = {};
        r.uuid(z); r.varint(0);
        conn_->sendPacket(proto::cf::cs::ResourcePackResponse, r);
    }
    { WriteBuffer k; k.varint(0); conn_->sendPacket(proto::cf::cs::SelectKnownPacks, k); }
    conn_->sendPacket(proto::cf::cs::FinishAcknowledgement, {});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
    bool gotLogin = false;
    while (!gotLogin && std::chrono::steady_clock::now() < deadline) {
        Packet p;
        try { p.body = conn_->readFrame(); }
        catch (const std::exception& e) { lastError = std::string("post-finish: ") + e.what(); return false; }
        ReadBuffer in(p.body);
        p.id = in.u8();
        if (p.id == proto::pl::sc::KeepAlive) respondKeepAlive(in.i64());
        if (p.id == proto::pl::sc::Disconnect) { lastError = "kicked at play-enter"; return false; }
        handleIncoming(p.id, std::vector<std::uint8_t>(in.p + in.off, in.p + in.len));
        gotLogin = count(proto::pl::sc::Login) > 0;
    }
    if (!gotLogin) { lastError = "no join game after contaminated finish"; return false; }
    running_ = true;
    reader_ = std::thread([this]{ readerLoop(); });
    return true;
}
bool TestClient::waitSuggestions(std::int32_t transactionId, SuggestionsResp& out, int timeoutMs) {
    // match by transactionId INSIDE the wait (stale 0x10 responses from earlier
    // cases must not satisfy a later wait — same-tick double-tab guard).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        Packet p;
        if (!waitFor([&](const Packet& q){ return q.id == proto::pl::sc::CommandSuggestions; }, 200, &p))
            continue;
        try {
            ReadBuffer in(p.body);
            SuggestionsResp r;
            r.transactionId = in.varint();
            if (r.transactionId != transactionId) continue;   // stale response, keep waiting
            r.start = in.varint();
            r.length = in.varint();
            const auto n = in.varint();
            for (std::int32_t i = 0; i < n; ++i) {
                Suggestion s; s.match = in.string(32767);
                (void)in.boolean();                          // tooltip present=false
                r.matches.push_back(std::move(s));
            }
            out = std::move(r);
            return true;
        } catch (...) {}
    }
    return false;
}
std::vector<TestClient::Spawned> TestClient::spawns() const {
    std::lock_guard lk(mtx_);
    std::vector<Spawned> out;
    for (auto& p : recent_) {
        if (p.id != proto::pl::sc::SpawnEntity) continue;
        try {
            ReadBuffer in(p.body);
            Spawned s;
            s.eid = in.varint();
            (void)in.bytes(16);
            s.type = in.varint();
            (void)in.varint();                           // type id
            s.x = in.f64(); s.y = in.f64(); s.z = in.f64();
            out.push_back(s);
        } catch (...) {}
    }
    return out;
}
void TestClient::sendMoveVehicle(double x, double y, double z, float yaw, float pitch) {    if (!conn_) return;
    WriteBuffer b;
    b.f64(x); b.f64(y); b.f64(z);
    b.f32(yaw); b.f32(pitch);
    try { conn_->sendPacket(proto::pl::cs::MoveVehicle, b); } catch (...) {}
}
void TestClient::sendRespawnRequest() {
    if (!conn_) return;
    WriteBuffer b; b.varint(0);
    try { conn_->sendPacket(proto::pl::cs::ClientCommand, b); } catch (...) {}
}
void TestClient::respondKeepAlive(std::int64_t id) {
    if (!conn_) return;
    WriteBuffer b; b.i64(id);
    try { conn_->sendPacket(proto::pl::cs::KeepAlive, b); } catch (...) {}
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
