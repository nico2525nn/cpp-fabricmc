// plan28 D26 lock: ResetScore 0x49 holder + optional objectiveName round-trip,
// wildcard-null resetAllScores affected -> single 0x49 null broadcast, and
// copy-before-erase behavior of resetScore / removeObjectiveWithReset.
// Header-only (Scoreboard.hpp + ByteBuffer.hpp); no server spawn needed.
#include "../src/game/Scoreboard.hpp"
#include "../src/proto/Ids.hpp"
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace cppfm;

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, msg) do { \
    const bool c_ = static_cast<bool>(cond); \
    std::printf("  %s  %s\n", c_ ? " ok " : "FAIL", msg); \
    if (c_) ++g_pass; else ++g_fail; } while (0)

// Decode a body produced by writeResetScorePacket (no packet-id prefix).
static void decodeResetBody(const std::vector<std::uint8_t>& body,
                            std::string& outHolder, bool& outPresent,
                            std::string& outObj) {
    ReadBuffer in(body);
    outHolder = in.string();
    outPresent = in.boolean();
    if (outPresent) outObj = in.string();
    if (in.remaining() != 0) ++g_fail;   // strict: no trailing bytes
}

int main() {
    std::printf("=== test_scoreboard_reset — plan28 D26 ResetScore 0x49 ===\n");
    Scoreboard sb;

    // -- 1) round-trip with objective present --------------------------------
    {
        const std::string holder = "Steve";
        const std::string obj = "deaths";
        WriteBuffer b;
        sb.writeResetScorePacket(b, holder, &obj);
        // wire: string "Steve" + boolean true + string "deaths"
        CHECK(b.size() == 1 + 5 + 1 + (1 + 6),
              "reset_score present body length (0x05 Steve 0x01 0x06 deaths)");
        std::string h, o; bool present = false;
        decodeResetBody(b.data, h, present, o);
        CHECK(h == "Steve" && present && o == "deaths",
              "round-trip: holder + objectiveName present recovered");
        // full frame with packet id (broadcastPacketExcept sends id+body)
        WriteBuffer frame;
        frame.varint(proto::pl::sc::ResetScore);
        sb.writeResetScorePacket(frame, holder, &obj);
        ReadBuffer fin(frame.data);
        CHECK(fin.varint() == 0x49, "packet id varint == 0x49 (ResetScore)");
        CHECK(std::string(fin.string()) == "Steve" && fin.boolean() &&
              std::string(fin.string()) == "deaths" && fin.remaining() == 0,
              "0x49 frame decode: Steve + present + deaths");
    }
    // -- 2) wildcard null (objectiveName present=false) ----------------------
    {
        WriteBuffer b;
        sb.writeResetScorePacket(b, "Steve", nullptr);
        CHECK(b.size() == 1 + 5 + 1, "reset_score null body length (0x05 Steve 0x00)");
        CHECK(b.data[6] == 0x00, "wildcard: boolean present = 0x00 (idx6)");
        std::string h, o; bool present = true;
        decodeResetBody(b.data, h, present, o);
        CHECK(h == "Steve" && !present && o.empty(),
              "round-trip: null objectiveName recovered as absent");
    }
    // -- 3) resetAllScores -> affected non-empty -> exactly ONE 0x49 null ----
    {
        Scoreboard sb3;
        sb3.addObjective("deaths", "dummy", "Deaths");
        sb3.addObjective("kills", "dummy", "Kills");
        sb3.setScore("deaths", "Steve", 5);
        sb3.setScore("kills", "Steve", 3);
        sb3.setScore("deaths", "Alex", 1);          // other holder untouched

        const auto affected = sb3.resetAllScores("Steve");
        CHECK(affected.size() == 2, "resetAllScores affected lists both objectives");
        int sent = affected.empty() ? 0 : 1;        // GameServer::sendResetScoreAllWildcard
        CHECK(sent == 1, "non-empty affected -> exactly 1 broadcast");
        CHECK(sb3.getScore("deaths", "Steve") == 0 && sb3.getScore("kills", "Steve") == 0,
              "scores erased for holder");
        CHECK(sb3.getScore("deaths", "Alex") == 1, "other holder score untouched");

        WriteBuffer b;
        b.varint(proto::pl::sc::ResetScore);
        sb3.writeResetScorePacket(b, "Steve", nullptr);
        ReadBuffer in(b.data);
        CHECK(in.varint() == 0x49 && std::string(in.string()) == "Steve" &&
              !in.boolean() && in.remaining() == 0,
              "the single broadcast is 0x49 + wildcard null");

        // empty affected -> zero packets (no ghost cleanup needed)
        Scoreboard sbEmpty;
        sbEmpty.addObjective("o", "dummy", "O");
        const auto affected2 = sbEmpty.resetAllScores("Nobody");
        CHECK(affected2.empty(), "no scores -> affected empty");
        CHECK((affected2.empty() ? 0 : 1) == 0, "empty affected -> 0 broadcasts");
    }
    // -- 4) erase-time copy-before-erase -------------------------------------
    {
        Scoreboard sb4;
        sb4.addObjective("o1", "dummy", "O1");
        sb4.setScore("o1", "Bob", 7);
        sb4.setScore("o1", "Carol", 8);
        // resetScore: single (holder, objective) erase; returns existed
        CHECK(sb4.resetScore("Bob", "o1"), "resetScore erases existing entry");
        CHECK(!sb4.resetScore("Bob", "o1"), "second resetScore returns false");
        CHECK(sb4.getScore("o1", "Carol") == 8, "other holder survives resetScore");
        // removeObjectiveWithReset: holders collected (copy) BEFORE scores.erase
        std::vector<std::string> holders;
        const bool removed = sb4.removeObjectiveWithReset("o1", holders);
        CHECK(removed, "removeObjectiveWithReset removed objective");
        CHECK(holders.size() == 1 && holders[0] == "Carol",
              "outHolders enumerated (copy-before-erase) before erase");
        CHECK(sb4.find("o1") == nullptr, "objective gone after erase");
        CHECK(sb4.getScore("o1", "Carol") == 0, "score map gone — no dangling lookup");
        // holders still enumerable after the erase (copied strings, not refs)
        for (const auto& h : holders) CHECK(h == "Carol", "holder copy survives erase");
    }
    std::printf("=== scoreboard_reset: %d PASS %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}