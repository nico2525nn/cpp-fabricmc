// Vanilla-compatible random primitives and the process-wide gameplay source.
//
// Minecraft 1.21.4 exposes two materially different random families:
// LocalRandom/CheckedRandom uses the Java 48-bit LCG, while the worldgen
// family uses Xoroshiro128++. Keeping both explicit is important: a random
// sequence is only reproducible when the algorithm, seed transformation, and
// call width are all part of the contract. The process-wide helper at the
// bottom remains an atomic, non-negative 31-bit stream for legacy gameplay
// call sites; it now uses the same LCG transition instead of the old unrelated
// Numerical Recipes generator.
#pragma once

#include <atomic>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cppfm {

namespace rng_detail {

inline std::int32_t signedBits32(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

inline std::int64_t signedBits64(std::uint64_t value) noexcept {
    return std::bit_cast<std::int64_t>(value);
}

inline std::uint32_t md5RotateLeft(std::uint32_t value, unsigned bits) noexcept {
    return (value << bits) | (value >> (32U - bits));
}

// Minecraft's RandomSeed.createXoroshiroSeed(String) uses Guava MD5 and reads
// the two 64-bit words in big-endian order. Keeping this small dependency-free
// implementation here avoids making the core RNG depend on OpenSSL or Guava.
inline std::array<std::uint8_t, 16> md5Digest(std::string_view input) {
    static constexpr std::array<std::uint32_t, 64> kShift = {
        7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
        7U, 12U, 17U, 22U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U,
        5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 4U, 11U, 16U, 23U,
        4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
        6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U,
        6U, 10U, 15U, 21U};
    static constexpr std::array<std::uint32_t, 64> kConstant = {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
        0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
        0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
        0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
        0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
        0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
        0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
        0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
        0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
        0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
        0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
        0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
        0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
        0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
        0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U};

    std::vector<std::uint8_t> data(input.begin(), input.end());
    data.push_back(0x80U);
    while ((data.size() % 64U) != 56U) data.push_back(0U);
    const auto bitCount = static_cast<std::uint64_t>(input.size()) * 8ULL;
    for (unsigned i = 0; i < 8U; ++i)
        data.push_back(static_cast<std::uint8_t>(bitCount >> (8U * i)));

    std::uint32_t a0 = 0x67452301U;
    std::uint32_t b0 = 0xefcdab89U;
    std::uint32_t c0 = 0x98badcfeU;
    std::uint32_t d0 = 0x10325476U;
    for (std::size_t offset = 0; offset < data.size(); offset += 64U) {
        std::array<std::uint32_t, 16> word{};
        for (unsigned i = 0; i < 16U; ++i) {
            const auto base = offset + static_cast<std::size_t>(i) * 4U;
            word[i] = static_cast<std::uint32_t>(data[base]) |
                      (static_cast<std::uint32_t>(data[base + 1U]) << 8U) |
                      (static_cast<std::uint32_t>(data[base + 2U]) << 16U) |
                      (static_cast<std::uint32_t>(data[base + 3U]) << 24U);
        }

        std::uint32_t a = a0;
        std::uint32_t b = b0;
        std::uint32_t c = c0;
        std::uint32_t d = d0;
        for (unsigned i = 0; i < 64U; ++i) {
            std::uint32_t f = 0;
            unsigned index = 0;
            if (i < 16U) {
                f = (b & c) | ((~b) & d);
                index = i;
            } else if (i < 32U) {
                f = (d & b) | ((~d) & c);
                index = (5U * i + 1U) % 16U;
            } else if (i < 48U) {
                f = b ^ c ^ d;
                index = (3U * i + 5U) % 16U;
            } else {
                f = c ^ (b | (~d));
                index = (7U * i) % 16U;
            }
            const auto next = d;
            d = c;
            c = b;
            b += md5RotateLeft(a + f + kConstant[i] + word[index], kShift[i]);
            a = next;
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }

    std::array<std::uint8_t, 16> digest{};
    const std::array<std::uint32_t, 4> state = {a0, b0, c0, d0};
    for (unsigned i = 0; i < 4U; ++i) {
        for (unsigned byte = 0; byte < 4U; ++byte)
            digest[i * 4U + byte] = static_cast<std::uint8_t>(
                state[i] >> (8U * byte));
    }
    return digest;
}

inline std::uint64_t bigEndianWord(const std::array<std::uint8_t, 16>& bytes,
                                   std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8U; ++i)
        value = (value << 8U) | bytes[offset + i];
    return value;
}

inline std::uint64_t coordinateSeed(int x, int y, int z) noexcept {
    // This is MathHelper.getSeed(x, y, z) in 1.21.4. The first multiply is
    // intentionally an int multiply before Java widens it to long.
    const auto xProduct = static_cast<std::uint32_t>(x) * 3129871U;
    const auto xProductBits = std::bit_cast<std::int32_t>(xProduct);
    const auto first = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(xProductBits));
    const auto second = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(z)) * 116129781ULL;
    const auto third = static_cast<std::uint64_t>(static_cast<std::int64_t>(y));
    const auto value = first ^ second ^ third;
    const auto mixed = value * value * 42317861ULL + value * 11ULL;
    const auto shifted = mixed >> 16U;
    return (mixed & (1ULL << 63U)) != 0 ? shifted | (~0ULL << 48U) : shifted;
}

inline std::int32_t javaStringHash(std::string_view value) noexcept {
    // Java's String.hashCode works on UTF-16 code units. Decode UTF-8 so the
    // positional LocalRandom splitter remains correct for non-ASCII names.
    std::uint32_t hash = 0;
    const auto append = [&hash](std::uint32_t codeUnit) noexcept {
        hash = hash * 31U + codeUnit;
    };
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<std::uint8_t>(value[i]);
        std::uint32_t codePoint = first;
        std::size_t width = 1;
        if ((first & 0xe0U) == 0xc0U && i + 1U < value.size()) {
            codePoint = (static_cast<std::uint32_t>(first & 0x1fU) << 6U) |
                        (static_cast<std::uint8_t>(value[i + 1U]) & 0x3fU);
            width = 2;
        } else if ((first & 0xf0U) == 0xe0U && i + 2U < value.size()) {
            codePoint = (static_cast<std::uint32_t>(first & 0x0fU) << 12U) |
                        ((static_cast<std::uint8_t>(value[i + 1U]) & 0x3fU) << 6U) |
                        (static_cast<std::uint8_t>(value[i + 2U]) & 0x3fU);
            width = 3;
        } else if ((first & 0xf8U) == 0xf0U && i + 3U < value.size()) {
            codePoint = (static_cast<std::uint32_t>(first & 0x07U) << 18U) |
                        ((static_cast<std::uint8_t>(value[i + 1U]) & 0x3fU) << 12U) |
                        ((static_cast<std::uint8_t>(value[i + 2U]) & 0x3fU) << 6U) |
                        (static_cast<std::uint8_t>(value[i + 3U]) & 0x3fU);
            width = 4;
        }
        if (codePoint <= 0xffffU) {
            append(codePoint);
        } else if (codePoint <= 0x10ffffU) {
            append(0xd800U + ((codePoint - 0x10000U) >> 10U));
            append(0xdc00U + ((codePoint - 0x10000U) & 0x3ffU));
        } else {
            append(first);
            width = 1;
        }
        i += width;
    }
    return std::bit_cast<std::int32_t>(hash);
}

} // namespace rng_detail

class VanillaLocalRandomSplitter;

class VanillaLocalRandom final {
public:
    static constexpr std::uint64_t kMultiplier = 25214903917ULL;
    static constexpr std::uint64_t kIncrement = 11ULL;
    static constexpr std::uint64_t kSeedMask = (1ULL << 48U) - 1ULL;

    explicit VanillaLocalRandom(std::int64_t seed = 0) noexcept { setSeed(seed); }

    void setSeed(std::int64_t seed) noexcept {
        seed_ = (static_cast<std::uint64_t>(seed) ^ kMultiplier) & kSeedMask;
        gaussianReady_ = false;
    }

    // This is the protected Java/Minecraft next(bits) primitive. Keeping it
    // public makes byte-level vector tests and deterministic adapters possible
    // without exposing mutable state.
    std::int32_t nextBits(int bits) noexcept {
        if (bits <= 0 || bits > 32) return 0;
        seed_ = (seed_ * kMultiplier + kIncrement) & kSeedMask;
        return rng_detail::signedBits32(
            static_cast<std::uint32_t>(seed_ >>
                                        (48U - static_cast<unsigned>(bits))));
    }

    std::int32_t nextInt() noexcept {
        return nextBits(32);
    }

    std::int32_t nextInt(std::int32_t bound) {
        if (bound <= 0) throw std::invalid_argument("random bound must be positive");
        const std::int32_t mask = bound - 1;
        std::int32_t value = static_cast<std::int32_t>(nextBits(31));
        if ((bound & mask) == 0)
            return static_cast<std::int32_t>(
                (static_cast<std::int64_t>(bound) * value) >> 31);

        // java.util.Random and Minecraft's LocalRandom deliberately perform
        // this rejection check with a wrapping signed int. The sum is
        // non-negative before wrapping, so the equivalent wide comparison is
        // defined C++ and avoids signed-overflow UB.
        for (;;) {
            const std::int32_t remainder = value % bound;
            const std::int64_t rejection = static_cast<std::int64_t>(value) -
                                           remainder + mask;
            if (rejection <= std::numeric_limits<std::int32_t>::max())
                return remainder;
            value = static_cast<std::int32_t>(nextBits(31));
        }
    }

    std::int64_t nextLong() noexcept {
        const auto high = static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(nextBits(32)));
        const auto low = static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(nextBits(32)));
        return rng_detail::signedBits64((high << 32U) | low);
    }

    bool nextBoolean() noexcept { return nextBits(1) != 0; }

    float nextFloat() noexcept {
        return static_cast<float>(nextBits(24)) * 0x1.0p-24F;
    }

    double nextDouble() noexcept {
        const auto high = static_cast<std::uint64_t>(nextBits(26));
        const auto low = static_cast<std::uint64_t>(nextBits(27));
        return static_cast<double>((high << 27U) | low) * 0x1.0p-53;
    }

    double nextGaussian() noexcept {
        if (gaussianReady_) {
            gaussianReady_ = false;
            return gaussianCache_;
        }
        double u;
        double v;
        double s;
        do {
            u = 2.0 * nextDouble() - 1.0;
            v = 2.0 * nextDouble() - 1.0;
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        const double multiplier = std::sqrt(-2.0 * std::log(s) / s);
        gaussianCache_ = v * multiplier;
        gaussianReady_ = true;
        return u * multiplier;
    }

    VanillaLocalRandom split() noexcept { return VanillaLocalRandom(nextLong()); }

    VanillaLocalRandomSplitter nextSplitter() noexcept;

private:
    std::uint64_t seed_ = 0;
    bool gaussianReady_ = false;
    double gaussianCache_ = 0.0;
};

class VanillaLocalRandomSplitter final {
public:
    explicit VanillaLocalRandomSplitter(std::int64_t seed) noexcept : seed_(seed) {}

    VanillaLocalRandom split(std::int64_t seed) const noexcept {
        return VanillaLocalRandom(seed);
    }

    VanillaLocalRandom split(int x, int y, int z) const noexcept {
        return VanillaLocalRandom(rng_detail::signedBits64(
            rng_detail::coordinateSeed(x, y, z) ^
            static_cast<std::uint64_t>(seed_)));
    }

    VanillaLocalRandom split(std::string_view seed) const noexcept {
        const auto hash = static_cast<std::uint32_t>(rng_detail::javaStringHash(seed));
        return VanillaLocalRandom(rng_detail::signedBits64(
            static_cast<std::uint64_t>(hash) ^ static_cast<std::uint64_t>(seed_)));
    }

private:
    std::int64_t seed_ = 0;
};

inline VanillaLocalRandomSplitter VanillaLocalRandom::nextSplitter() noexcept {
    return VanillaLocalRandomSplitter(nextLong());
}

struct XoroshiroSeed128 {
    std::uint64_t seedLo = 0;
    std::uint64_t seedHi = 0;
};

class VanillaXoroshiroSplitter;

class VanillaXoroshiro128PlusPlus final {
public:
    static constexpr std::uint64_t kGoldenRatio64 = 0x9E3779B97F4A7C15ULL;
    static constexpr std::uint64_t kSilverRatio64 = 0x6A09E667F3BCC909ULL;

    VanillaXoroshiro128PlusPlus(std::uint64_t seedLo, std::uint64_t seedHi) noexcept
        : seedLo_(seedLo), seedHi_(seedHi) {}

    explicit VanillaXoroshiro128PlusPlus(std::int64_t seed) noexcept
        : VanillaXoroshiro128PlusPlus(createSeed(seed)) {}

    explicit VanillaXoroshiro128PlusPlus(XoroshiroSeed128 seed) noexcept
        : seedLo_(seed.seedLo), seedHi_(seed.seedHi) {}

    static std::uint64_t mixStafford13(std::uint64_t seed) noexcept {
        seed = (seed ^ (seed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        seed = (seed ^ (seed >> 27U)) * 0x94D049BB133111EBULL;
        return seed ^ (seed >> 31U);
    }

    static XoroshiroSeed128 createUnmixedSeed(std::int64_t seed) noexcept {
        const auto raw = static_cast<std::uint64_t>(seed) ^ kSilverRatio64;
        return {raw, raw + kGoldenRatio64};
    }

    static XoroshiroSeed128 createSeed(std::int64_t seed) noexcept {
        const auto unmixed = createUnmixedSeed(seed);
        return {mixStafford13(unmixed.seedLo), mixStafford13(unmixed.seedHi)};
    }

    std::uint64_t nextLongBits() noexcept {
        const std::uint64_t s0 = seedLo_;
        std::uint64_t s1 = seedHi_;
        const std::uint64_t result = rotl(s0 + s1, 17U) + s0;
        s1 ^= s0;
        seedLo_ = rotl(s0, 49U) ^ s1 ^ (s1 << 21U);
        seedHi_ = rotl(s1, 28U);
        return result;
    }

    std::int64_t nextLong() noexcept {
        return rng_detail::signedBits64(nextLongBits());
    }

    std::int32_t nextInt() noexcept {
        return rng_detail::signedBits32(
            static_cast<std::uint32_t>(nextLongBits()));
    }

    std::int32_t nextInt(std::int32_t bound) {
        if (bound <= 0) throw std::invalid_argument("random bound must be positive");
        const auto unsignedBound = static_cast<std::uint32_t>(bound);
        auto value = static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(nextInt()));
        auto product = value * unsignedBound;
        auto low = product & 0xffffffffULL;
        if (low < unsignedBound) {
            const auto threshold = (0U - unsignedBound) % unsignedBound;
            while (low < threshold) {
                value = static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(nextInt()));
                product = value * unsignedBound;
                low = product & 0xffffffffULL;
            }
        }
        return static_cast<std::int32_t>(product >> 32U);
    }

    bool nextBoolean() noexcept { return (nextLongBits() & 1ULL) != 0; }

    float nextFloat() noexcept {
        return static_cast<float>(nextBitsUnsigned(24)) * 0x1.0p-24F;
    }

    double nextDouble() noexcept {
        return static_cast<double>(nextBitsUnsigned(53)) * 0x1.0p-53;
    }

    double nextGaussian() noexcept {
        if (gaussianReady_) {
            gaussianReady_ = false;
            return gaussianCache_;
        }
        double u;
        double v;
        double s;
        do {
            u = 2.0 * nextDouble() - 1.0;
            v = 2.0 * nextDouble() - 1.0;
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        const double multiplier = std::sqrt(-2.0 * std::log(s) / s);
        gaussianCache_ = v * multiplier;
        gaussianReady_ = true;
        return u * multiplier;
    }

    std::int64_t nextBits(int bits) noexcept {
        if (bits <= 0 || bits > 64) return 0;
        return rng_detail::signedBits64(nextBitsUnsigned(bits));
    }

    void skip(int count) noexcept {
        for (int i = 0; i < count; ++i) (void)nextLongBits();
    }

    VanillaXoroshiro128PlusPlus split() noexcept {
        // C++ does not sequence function arguments. Capture both draws before
        // constructing the child so the Java left-to-right draw order is
        // explicit and no unsequenced mutation can occur.
        const auto seedLo = nextLongBits();
        const auto seedHi = nextLongBits();
        return VanillaXoroshiro128PlusPlus(seedLo, seedHi);
    }

    VanillaXoroshiroSplitter nextSplitter() noexcept;

    void setSeed(std::int64_t seed) noexcept {
        const auto expanded = createSeed(seed);
        seedLo_ = expanded.seedLo;
        seedHi_ = expanded.seedHi;
        gaussianReady_ = false;
    }

private:
    std::uint64_t nextBitsUnsigned(int bits) noexcept {
        if (bits <= 0 || bits > 64) return 0;
        return nextLongBits() >> (64U - static_cast<unsigned>(bits));
    }

    static std::uint64_t rotl(std::uint64_t value, unsigned bits) noexcept {
        return (value << bits) | (value >> (64U - bits));
    }

    std::uint64_t seedLo_ = 0;
    std::uint64_t seedHi_ = 0;
    bool gaussianReady_ = false;
    double gaussianCache_ = 0.0;
};

class VanillaXoroshiroSplitter final {
public:
    VanillaXoroshiroSplitter(std::uint64_t seedLo, std::uint64_t seedHi) noexcept
        : seedLo_(seedLo), seedHi_(seedHi) {}

    VanillaXoroshiro128PlusPlus split(std::int64_t seed) const noexcept {
        const auto bits = static_cast<std::uint64_t>(seed);
        return VanillaXoroshiro128PlusPlus(bits ^ seedLo_, bits ^ seedHi_);
    }

    VanillaXoroshiro128PlusPlus split(int x, int y, int z) const noexcept {
        return VanillaXoroshiro128PlusPlus(
            seedLo_ ^ rng_detail::coordinateSeed(x, y, z), seedHi_);
    }

    VanillaXoroshiro128PlusPlus split(std::string_view seed) const {
        const auto digest = rng_detail::md5Digest(seed);
        const auto lo = rng_detail::bigEndianWord(digest, 0);
        const auto hi = rng_detail::bigEndianWord(digest, 8);
        return VanillaXoroshiro128PlusPlus(seedLo_ ^ lo, seedHi_ ^ hi);
    }

private:
    std::uint64_t seedLo_ = 0;
    std::uint64_t seedHi_ = 0;
};

inline VanillaXoroshiroSplitter VanillaXoroshiro128PlusPlus::nextSplitter() noexcept {
    const auto seedLo = nextLongBits();
    const auto seedHi = nextLongBits();
    return VanillaXoroshiroSplitter(seedLo, seedHi);
}

inline std::atomic<std::uint64_t>& randomState() noexcept {
    // Store the already-scrambled 48-bit state, matching LocalRandom's state
    // representation and making atomic updates independent of object lifetime.
    static std::atomic<std::uint64_t> state{
        (0x9E3779B97F4A7C15ULL ^ VanillaLocalRandom::kMultiplier) &
        VanillaLocalRandom::kSeedMask};
    return state;
}

inline int nextRandom() noexcept {
    // Legacy gameplay callers expect a non-negative value. This is exactly
    // LocalRandom.next(31), with the transition performed atomically so the
    // server may continue to draw from multiple tick/session threads.
    auto& state = randomState();
    std::uint64_t current = state.load(std::memory_order_relaxed);
    for (;;) {
        const std::uint64_t next =
            (current * VanillaLocalRandom::kMultiplier +
             VanillaLocalRandom::kIncrement) & VanillaLocalRandom::kSeedMask;
        if (state.compare_exchange_weak(current, next,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed))
            return static_cast<int>(next >> 17U);
    }
}

inline void seedRandom(unsigned seed) noexcept {
    const auto raw = static_cast<std::int64_t>(seed);
    randomState().store((static_cast<std::uint64_t>(raw) ^
                         VanillaLocalRandom::kMultiplier) &
                            VanillaLocalRandom::kSeedMask,
                        std::memory_order_relaxed);
}

} // namespace cppfm
