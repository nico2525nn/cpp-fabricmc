// Independent vectors for the Java/Minecraft RNG primitives.
//
// These tests intentionally do not call the world generator or nextRandom()
// to manufacture their expected values. The LocalRandom values below are
// cross-checked against java.util.Random's 48-bit contract; the Xoroshiro
// values are the public-domain xoroshiro128++ reference transition with
// Minecraft's Stafford-13 seed expansion.

#include "../src/core/Random.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

static int gPass = 0;
static int gFail = 0;

static void check(bool condition, const char* name) {
    if (condition) {
        ++gPass;
        std::printf("PASS %s\n", name);
    } else {
        ++gFail;
        std::printf("FAIL %s\n", name);
    }
}

static void testLocalRandom() {
    cppfm::VanillaLocalRandom random(123456789LL);
    check(random.nextInt() == -1442945365, "LocalRandom nextInt #1");
    check(random.nextInt() == -1016548095, "LocalRandom nextInt #2");
    check(random.nextInt(1000) == 483, "LocalRandom bounded nextInt");
    check(random.nextLong() == 4701514676984888228LL, "LocalRandom nextLong");
    check(std::abs(random.nextFloat() - 0.21659654F) < 1.0e-7F,
          "LocalRandom nextFloat");
    check(std::abs(random.nextDouble() - 0.8933411602003871) < 1.0e-15,
          "LocalRandom nextDouble");

    cppfm::VanillaLocalRandom reset(1234LL);
    check(reset.nextBits(31) == 1388524628, "LocalRandom next(31) #1");
    check(reset.nextBits(31) == 557894633, "LocalRandom next(31) #2");

    bool threw = false;
    try {
        (void)reset.nextInt(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "LocalRandom rejects non-positive bound");
}

static void testXoroshiro() {
    const auto unmixed = cppfm::VanillaXoroshiro128PlusPlus::createUnmixedSeed(0);
    check(unmixed.seedLo == 7640891576956012809ULL,
          "Xoroshiro unmixed low seed");
    check(unmixed.seedHi == 594862322569659678ULL,
          "Xoroshiro unmixed high seed");

    const auto seed = cppfm::VanillaXoroshiro128PlusPlus::createSeed(0);
    check(seed.seedLo == 3847398142028685078ULL,
          "Xoroshiro Stafford-13 low seed");
    check(seed.seedHi == 7192185014346937746ULL,
          "Xoroshiro Stafford-13 high seed");

    cppfm::VanillaXoroshiro128PlusPlus random(seed);
    constexpr std::uint64_t expected[] = {
        3038984756725240190ULL,
        14752704786953913202ULL,
        4633751808701151732ULL,
        2160572957309072155ULL,
        1839370574944072389ULL,
    };
    for (std::uint64_t value : expected)
        check(static_cast<std::uint64_t>(random.nextLong()) == value,
              "Xoroshiro128++ nextLong vector");

    cppfm::VanillaXoroshiro128PlusPlus fromSeed(0);
    check(static_cast<std::uint64_t>(fromSeed.nextLong()) == expected[0],
          "Xoroshiro seed constructor expands identically");

    cppfm::VanillaXoroshiro128PlusPlus floatRandom(0);
    check(std::abs(floatRandom.nextFloat() - 0.16474366F) < 1.0e-7F,
          "Xoroshiro nextFloat uses one upper-bit sample");
    cppfm::VanillaXoroshiro128PlusPlus doubleRandom(0);
    check(std::abs(doubleRandom.nextDouble() - 0.16474369376959186) < 1.0e-15,
          "Xoroshiro nextDouble uses one upper-bit sample");
    cppfm::VanillaXoroshiro128PlusPlus boundedRandom(0);
    check(boundedRandom.nextInt(1000) == 962,
          "Xoroshiro unsigned-multiply bounded nextInt");

    cppfm::VanillaXoroshiro128PlusPlus splitSource(0);
    const auto splitter = splitSource.nextSplitter();
    check(static_cast<std::uint64_t>(splitter.split(42).nextLong()) ==
              13325287730323210021ULL,
          "Xoroshiro splitter long seed");
    check(static_cast<std::uint64_t>(splitter.split(1, 2, 3).nextLong()) ==
              12047218113837772196ULL,
          "Xoroshiro splitter coordinate seed");
    check(static_cast<std::uint64_t>(splitter.split("abc").nextLong()) ==
              15970774147879585662ULL,
          "Xoroshiro splitter MD5 string seed");
}

int main() {
    std::puts("=== test_rng_parity ===");
    testLocalRandom();
    testXoroshiro();
    std::printf("RNG PARITY: %d PASS %d FAIL\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
