// test_fuzz.cpp — ByteBuffer/PacketDecoder Fuzz cases (plan34 §5)
// 20 cases covering malformed varint, compressed bomb, Position wrap, fragment, string.
// Each case verifies throw -> connection would close (SocketClosedError upstream).
// Unit-form, <1s, no server.

#include "../src/core/ByteBuffer.hpp"
#include "../src/core/Zlib.hpp"
#include "../src/net/PacketDecoder.hpp"
#include <cstdio>
#include <vector>
#include <functional>
#include <stdexcept>
#include <string>

using namespace cppfm;

static int g_pass = 0;
static int g_fail = 0;

static void expectThrow(const char* name, std::function<void()> fn){
    try { fn(); std::printf("  FAIL %s — expected throw but none\n", name); ++g_fail; }
    catch (const std::exception& e){ std::printf("  ok   %s — threw: %s\n", name, e.what()); ++g_pass; }
    catch (...){ std::printf("  ok   %s — threw unknown\n", name); ++g_pass; }
}
static void expectNoThrow(const char* name, std::function<void()> fn){
    try { fn(); std::printf("  ok   %s — no throw as expected\n", name); ++g_pass; }
    catch (const std::exception& e){ std::printf("  FAIL %s — unexpected throw: %s\n", name, e.what()); ++g_fail; }
}
static void check(bool cond, const char* name){
    if(cond){ std::printf("  ok   %s\n", name); ++g_pass; }
    else { std::printf("  FAIL %s\n", name); ++g_fail; }
}

int main(){
    std::printf("=== fuzz 20 — ByteBuffer/PacketDecoder ===\n");

    // 1) varint 5-byte overflow (FF FF FF FF FF) -> shift >=35
    expectThrow("F1 varint 5-byte overflow FF*5", []{
        std::vector<uint8_t> bomb{0xFF,0xFF,0xFF,0xFF,0xFF};
        ReadBuffer r(bomb);
        (void)r.varint();
    });

    // 2) varint valid -1 (FF FF FF FF 0F) -> should NOT throw, value -1
    expectNoThrow("F2 varint -1 valid (FF*4 0F)", []{
        std::vector<uint8_t> v{0xFF,0xFF,0xFF,0xFF,0x0F};
        ReadBuffer r(v);
        int32_t x = r.varint();
        if(x != -1) throw std::runtime_error("expected -1");
    });

    // 3) varint truncated (80 80 80) — buffer underrun
    expectThrow("F3 varint truncated 80 80 80", []{
        std::vector<uint8_t> v{0x80,0x80,0x80};
        ReadBuffer r(v);
        (void)r.varint();
    });

    // 4) varint 0 valid
    expectNoThrow("F4 varint 0", []{
        std::vector<uint8_t> v{0x00};
        ReadBuffer r(v);
        if(r.varint()!=0) throw std::runtime_error("0");
    });

    // 5) varlong 10-byte overflow (FF*10 + 01) -> shift >=70
    expectThrow("F5 varlong 10-byte overflow", []{
        std::vector<uint8_t> v{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01};
        ReadBuffer r(v);
        (void)r.varlong();
    });

    // 6) varlong valid -1 (FF*9 01) -> should not throw (actually 10 bytes for -1 is FF FF FF FF FF FF FF FF FF 01)
    expectNoThrow("F6 varlong -1 valid 10 bytes", []{
        std::vector<uint8_t> v{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01};
        ReadBuffer r(v);
        int64_t x = r.varlong();
        if(x != -1) throw std::runtime_error("varlong -1 failed");
    });

    // 7) compressed bomb: dataLen > kMaxFrame (8M+1)
    expectThrow("F7 compressed bomb declared size >8M", []{
        WriteBuffer frame;
        frame.varint(8*1024*1024 + 1); // 8M+1
        std::vector<uint8_t> dummy{0x00};
        frame.raw(dummy.data(), dummy.size());
        (void)PacketDecoder::decodeFrame(frame.data, 256);
    });

    // 8) compressed bomb: dataLen 8M but small payload -> decompress fails (dst mismatch)
    expectThrow("F8 compressed bomb 8M claim with 10-zero payload", []{
        std::vector<uint8_t> small(10,0);
        std::vector<uint8_t> comp;
        compressRaw(small.data(), small.size(), comp);
        WriteBuffer frame;
        frame.varint(8*1024*1024);
        frame.raw(comp.data(), comp.size());
        (void)PacketDecoder::decodeFrame(frame.data, 256);
    });

    // 9) empty frame -> throw
    expectThrow("F9 empty frame decodeFrame", []{
        std::vector<uint8_t> empty;
        (void)PacketDecoder::decodeFrame(empty, 256);
    });

    // 10) valid uncompressed frame (dataLen 0 + body)
    expectNoThrow("F10 valid uncompressed frame dataLen 0", []{
        WriteBuffer frame;
        frame.varint(0); // not compressed
        frame.u8(0x05); // fake id
        frame.u8(0x01);
        auto out = PacketDecoder::decodeFrame(frame.data, 256);
        if(out.empty() || out[0]!=0x05) throw std::runtime_error("decode failed");
    });

    // 11) position wrap: y=5000 wraps to 904 (12-bit signed)
    {
        WriteBuffer w; w.position(30000000, 5000, 30000000);
        ReadBuffer r(w.data); int32_t x,y,z; r.position(x,y,z);
        check(y != 5000, "F11 position y=5000 wraps not 5000");
        // also verify x,z wrap for huge values (26-bit)
        WriteBuffer w2; w2.position(100000000, 0, 100000000);
        ReadBuffer r2(w2.data); int32_t x2,y2,z2; r2.position(x2,y2,z2);
        check(x2 != 100000000, "F11b position x huge wraps");
        (void)z2; (void)y2;
    }

    // 12) position roundtrip normal
    {
        WriteBuffer w; w.position(10,64,-5);
        ReadBuffer r(w.data); int32_t x,y,z; r.position(x,y,z);
        check(x==10 && y==64 && z==-5, "F12 position roundtrip 10,64,-5");
    }

    // 13) fragment outer length mismatch (varint 10 + 5 bytes)
    expectThrow("F13 outer length mismatch 0x0A + 5 bytes", []{
        std::vector<uint8_t> outer{0x0A, 0x01,0x02,0x03,0x04,0x05};
        (void)PacketDecoder::decodeOuter(outer, -1, nullptr);
    });

    // 14) outer varint truncated (0x80 alone)
    expectThrow("F14 outer varint truncated 80", []{
        std::vector<uint8_t> outer{0x80};
        (void)PacketDecoder::decodeOuter(outer, -1, nullptr);
    });

    // 15) outer empty
    expectThrow("F15 outer empty", []{
        std::vector<uint8_t> outer;
        (void)PacketDecoder::decodeOuter(outer, -1, nullptr);
    });

    // 16) string length out of range (>262144)
    expectThrow("F16 string length 262145 > max", []{
        WriteBuffer w; w.varint(262145);
        w.raw(std::vector<uint8_t>(262145,'a').data(), 262145);
        ReadBuffer r(w.data);
        (void)r.string();
    });

    // 17) string negative length (-1)
    expectThrow("F17 string negative length -1", []{
        WriteBuffer w; w.varint(-1);
        ReadBuffer r(w.data);
        (void)r.string();
    });

    // 18) string underrun (declare 10 but have 5)
    expectThrow("F18 string underrun declare 10 have 5", []{
        WriteBuffer w; w.varint(10);
        w.raw((const uint8_t*)"hello",5);
        ReadBuffer r(w.data);
        (void)r.string();
    });

    // 19) string empty valid (varint 0)
    expectNoThrow("F19 string empty varint 0", []{
        WriteBuffer w; w.varint(0);
        ReadBuffer r(w.data);
        std::string s = r.string();
        if(!s.empty()) throw std::runtime_error("expected empty");
    });

    // 20) buffer underrun for u32/u64
    expectThrow("F20 u32 underrun 2 bytes", []{
        std::vector<uint8_t> v{0x00,0x01};
        ReadBuffer r(v);
        (void)r.u32();
    });
    expectThrow("F20b u64 underrun 4 bytes", []{
        std::vector<uint8_t> v{0x00,0x01,0x02,0x03};
        ReadBuffer r(v);
        (void)r.u64();
    });
    // Extra: NBT-like string maxLen custom
    expectThrow("F21 custom maxLen 5 exceeded", []{
        WriteBuffer w; w.string("hello world");
        ReadBuffer r(w.data);
        (void)r.string(5);
    });

    std::printf("=== fuzz: %d PASS %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
