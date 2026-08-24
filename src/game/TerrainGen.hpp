// TerrainGen: noise-based overworld-style generation (plan.md Phase 2).
// Deterministic per seed; classic Perlin octaves shaped like Minecraft's
// continentalness/erosion/peaks blend. Not byte-compatible with vanilla
// worldgen (that requires the full density-function pipeline) but produces
// natural rolling terrain, mountains, beaches and oceans.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace cppfm {

class ImprovedNoise {
public:
    explicit ImprovedNoise(std::uint64_t seed) {
        // xorshift-seeded permutation
        std::uint64_t s = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        for (int i = 0; i < 256; ++i) perm_[i] = static_cast<std::uint8_t>(i);
        for (int i = 255; i > 0; --i) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            const int j = static_cast<int>((s >> 33) % (i + 1));
            std::swap(perm_[i], perm_[j]);
        }
        for (int i = 0; i < 512; ++i) perm512_[i] = perm_[i & 255];
    }

    // Classic Perlin in [-1,1]-ish
    double sample(double x, double y, double z) const {
        const double X = std::floor(x), Y = std::floor(y), Z = std::floor(z);
        const int xi = static_cast<int>(X) & 255, yi = static_cast<int>(Y) & 255,
                  zi = static_cast<int>(Z) & 255;
        const double xf = x - X, yf = y - Y, zf = z - Z;
        const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
        const double u = fade(xf), v = fade(yf), w = fade(zf);
        const auto p = [&](int a, int b, int c) { return perm512_[perm512_[perm512_[a] + b] + c]; };
        const auto grad = [](int hash, double gx, double gy, double gz) {
            switch (hash & 15) {
            case 0: return  gx + gy; case 1: return -gx + gy; case 2: return  gx - gy; case 3: return -gx - gy;
            case 4: return  gx + gz; case 5: return -gx + gz; case 6: return  gx - gz; case 7: return -gx - gz;
            case 8: return  gy + gz; case 9: return -gy + gz; case 10: return  gy - gz; case 11: return -gy - gz;
            case 12: return  gx + gy; case 13: return -gx + gy; case 14: return  gy - gx; case 15: return -gy - gx;
            }
            return 0.0;
        };
        const double x1 = lerp(grad(p(xi,yi,zi),     xf,   yf,   zf),   grad(p(xi+1,yi,zi),     xf-1, yf,   zf),   u);
        const double x2 = lerp(grad(p(xi,yi+1,zi),   xf,   yf-1, zf),   grad(p(xi+1,yi+1,zi),   xf-1, yf-1, zf),   u);
        const double x3 = lerp(grad(p(xi,yi,zi+1),   xf,   yf,   zf-1), grad(p(xi+1,yi,zi+1),   xf-1, yf,   zf-1), u);
        const double x4 = lerp(grad(p(xi,yi+1,zi+1), xf,   yf-1, zf-1), grad(p(xi+1,yi+1,zi+1), xf-1, yf-1, zf-1), u);
        return lerp(lerp(x1, x2, v), lerp(x3, x4, v), w);
    }

    // Fractal octaves, result roughly [-1,1]
    double octaves(double x, double y, double z, int octaves, double lacunarity = 2.0,
                   double persistence = 0.5) const {
        double amp = 1, freq = 1, sum = 0, norm = 0;
        for (int i = 0; i < octaves; ++i) {
            sum += amp * sample(x * freq, y * freq, z * freq);
            norm += amp;
            amp *= persistence;
            freq *= lacunarity;
        }
        return sum / norm;
    }

private:
    static double lerp(double a, double b, double t) { return a + (b - a) * t; }
    std::uint8_t perm_[256];
    std::uint8_t perm512_[512];
};

class TerrainGenerator {
public:
    static constexpr int kSeaLevelNormal = 63;
    explicit TerrainGenerator(std::uint64_t seed)
        : cont_(seed ^ 0x9E3779B97F4A7C15ULL),
          ero_(seed ^ 0xC2B2AE3D27D4EB4FULL),
          peak_(seed ^ 0x165667B19E3779F9ULL) {}

    struct ColumnResult { int surfaceY; bool ocean; };

    // Surface height (first air y) for world column (wx,wz), minY=-64.
    ColumnResult column(std::int32_t wx, std::int32_t wz) const {
        const double nx = wx * 0.0015, nz = wz * 0.0015;
        const double cont = cont_.octaves(nx, 100.0, nz, 4);           // large landmasses
        const double ero  = ero_.octaves(wx * 0.004, 50.0, wz * 0.004, 3);
        const double pk   = peak_.octaves(wx * 0.02, 20.0, wz * 0.02, 4);

        // base height: oceans (~30) to highlands (~110)
        double base = 68.0 + cont * 34.0;
        base -= std::max(0.0, -cont) * 18.0;                           // deepen oceans
        base += pk * 14.0 * std::max(0.25, 0.5 + cont * 0.5);          // mountains on land
        base += ero * 6.0;

        const int surface = std::clamp(static_cast<int>(base), -56, 150);
        const bool ocean = surface < kSeaLevelNormal - 2;
        return {surface + 1, ocean};                                    // first air y
    }

private:
    ImprovedNoise cont_, ero_, peak_;
};

} // namespace cppfm
