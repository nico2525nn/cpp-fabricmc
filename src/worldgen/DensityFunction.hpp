// DensityFunction: vanilla-style density-function pipeline (plan3.md
// "密度関数パイプラインのアーキテクチャ").
//
// A tree of nodes evaluated per (x,y,z) sample point. Nodes are built from
// clean-room JSON definitions shaped like the game's data format:
//   {"type":"constant","value":0.5}
//   {"type":"noise","noise":"cppfm:terrain","xz_scale":1,"y_scale":1}
//   {"type":"shifted_noise","shift_x":{...},"shift_y":{...},"shift_z":{...},"xz_scale":1,"y_scale":1,"noise":"minecraft:terrain"}
//   {"type":"shift","noise":"minecraft:offset"}  // Shift / ShiftA / ShiftB
//   {"type":"clamp","input":{...},"min":-1,"max":1}
//   {"type":"add","inputs":[{...},{...}]}          (also min/max/mul)
//   {"type":"abs"|"square"|"cube"|"half_negative"|"quarter_negative"|"squeeze","input":{...}}
//   {"type":"cube","input":{...}}  // alias of Unary Cube
//   {"type":"range_choice","input":{...},"min_inclusive":a,"max_exclusive":b,
//        "when_in_range":{...},"when_out_of_range":{...}}
//   {"type":"y_clamped_gradient","from_y":..,"to_y":..,"from_value":..,"to_value":..}
//   {"type":"interpolated","input":{...}}           (cell-grid smoothing)
//   {"type":"flat_cache","input":{...}} / {"type":"cache_2d",...} / cache_once
//   {"type":"beardifier"} {"type":"old_blended_noise","input":{...}}
//   {"type":"blend_alpha"} {"type":"blend_offset"} {"type":"blend_density","input":{...}}
//   {"type":"end_islands"} {"type":"weird_scaled_sampler","input":{...},"rarity":"type_1"}
// Noise parameters come from a small registry seeded deterministically.
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/Json.hpp"
#include "../game/TerrainGen.hpp"

namespace cppfm::worldgen {

struct Sample {
    double x, y, z;
};

class NoiseRegistry {
public:
    explicit NoiseRegistry(std::uint64_t seed) : seed_(seed) {}
    const ImprovedNoise& get(const std::string& key) const {
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            std::uint64_t h = seed_;
            for (char c : key) h = h * 1315423911ULL ^ static_cast<std::uint64_t>(c);
            it = cache_.emplace(key, ImprovedNoise(h)).first;
        }
        return it->second;
    }
private:
    mutable std::unordered_map<std::string, ImprovedNoise> cache_;
    std::uint64_t seed_;
};

class DensityNode {
public:
    virtual ~DensityNode() = default;
    virtual double eval(const Sample& s) const = 0;
};

using NodePtr = std::shared_ptr<DensityNode>;

namespace detail {

struct Constant final : DensityNode {
    double v;
    explicit Constant(double x) : v(x) {}
    double eval(const Sample&) const override { return v; }
};
struct YClampedGradient final : DensityNode {
    int fromY, toY; double fromV, toV;
    double eval(const Sample& s) const override {
        if (s.y <= fromY) return fromV;
        if (s.y >= toY) return toV;
        const double t = (s.y - fromY) / double(toY - fromY);
        return fromV + (toV - fromV) * t;
    }
};
struct NoiseNode final : DensityNode {
    std::shared_ptr<NoiseRegistry> reg;
    std::string key; double xzScale, yScale, xzOffset;
    double eval(const Sample& s) const override {
        return reg->get(key).sample((s.x + xzOffset) * xzScale,
                                    s.y * yScale,
                                    (s.z + xzOffset) * xzScale);
    }
};
// Yarn Shift: offsetNoise * 4.0 (block unit shift)
struct Shift final : DensityNode {
    std::shared_ptr<NoiseRegistry> reg;
    std::string key;
    double eval(const Sample& s) const override {
        return reg->get(key).sample(s.x * 0.25, s.y * 0.25, s.z * 0.25) * 4.0;
    }
};
struct ShiftedNoise final : DensityNode {
    NodePtr shiftX, shiftY, shiftZ;
    std::shared_ptr<NoiseRegistry> reg;
    std::string key; double xzScale = 1, yScale = 1;
    double eval(const Sample& s) const override {
        const double dx = shiftX ? shiftX->eval(s) : 0;
        const double dy = shiftY ? shiftY->eval(s) : 0;
        const double dz = shiftZ ? shiftZ->eval(s) : 0;
        const double sx = s.x * xzScale * 0.25 + dx;
        const double sy = s.y * yScale * 0.25 + dy;
        const double sz = s.z * xzScale * 0.25 + dz;
        return reg->get(key).sample(sx, sy, sz);
    }
};
struct Unary final : DensityNode {
    enum Op { Abs, Square, Cube, HalfNeg, QuarterNeg, Squeeze, Neg } op;
    NodePtr in;
    double eval(const Sample& s) const override {
        const double v = in->eval(s);
        switch (op) {
        case Abs: return std::abs(v);
        case Square: return v * v;
        case Cube: return v * v * v;
        case HalfNeg: return v > 0 ? v : v * 0.5;
        case QuarterNeg: return v > 0 ? v : v * 0.25;
        case Squeeze: {
            const double c = v < -1.5 ? -1.5 : v > 1.5 ? 1.5 : v;
            const double d = c / 1.5;
            return d / 2 / (1 + d * d);
        }
        case Neg: return -v;
        }
        return v;
    }
};
struct Nary final : DensityNode {
    enum Op { Add, Mul, Min, Max } op;
    std::vector<NodePtr> inputs;
    double eval(const Sample& s) const override {
        if (op == Add) {
            double sum = 0;
            for (auto& n : inputs) sum += n->eval(s);
            return sum;
        }
        double acc = inputs.empty() ? 0 : inputs.front()->eval(s);
        for (std::size_t i = 1; i < inputs.size(); ++i) {
            const double v = inputs[i]->eval(s);
            if (op == Mul) acc *= v;
            else if (op == Min) acc = std::min(acc, v);
            else acc = std::max(acc, v);
        }
        return acc;
    }
};
struct Clamp final : DensityNode {
    NodePtr in; double lo, hi;
    double eval(const Sample& s) const override {
        return std::clamp(in->eval(s), lo, hi);
    }
};
struct RangeChoice final : DensityNode {
    NodePtr in, whenIn, whenOut; double lo = 0, hiExclusive = 0;
    double eval(const Sample& s) const override {
        const double v = in->eval(s);
        // Yarn: v >= minInclusive && v < maxExclusive
        return (v >= lo && v < hiExclusive) ? whenIn->eval(s) : whenOut->eval(s);
    }
};
struct BeardifierNode final : DensityNode {
    std::function<double(int,int)> sampleXZ;
    double eval(const Sample& s) const override {
        if (!sampleXZ) return 0;
        const double v = sampleXZ(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.z)));
        const double yFactor = std::clamp(1.0 - std::abs(s.y - 10.0)/40.0, 0.0, 1.0);
        return v * yFactor;
    }
};
struct OldBlendedNoiseNode final : DensityNode {
    NodePtr in;
    double eval(const Sample& s) const override {
        return in ? in->eval(s) : 0;
    }
};
struct BlendAlphaNode final : DensityNode {
    double eval(const Sample&) const override { return 1.0; }
};
struct BlendOffsetNode final : DensityNode {
    double eval(const Sample&) const override { return 0.0; }
};
struct BlendDensityNode final : DensityNode {
    NodePtr in;
    double eval(const Sample& s) const override { return in ? in->eval(s) : 0; }
};
struct EndIslandsNode final : DensityNode {
    double eval(const Sample& s) const override {
        const double dx = s.x / 384.0, dz = s.z / 384.0;
        const double r = std::hypot(dx, dz);
        if (r < 1.0) return 0;
        const double envelope = std::sin(r * 0.4) * 2.0 - 1.0;
        return envelope * 0.5;
    }
};
struct WeirdScaledSamplerNode final : DensityNode {
    NodePtr input; double rarity = 1.0;
    double eval(const Sample& s) const override {
        const double v = input ? input->eval(s) : 0;
        const double w = std::abs(v);
        if (w < 0.2) return v * 1.0;
        if (w < 0.6) return v * (1.0 + rarity * 0.5);
        return v * (1.0 + rarity);
    }
};
struct Cache2d final : DensityNode {   // memoize per column within one chunk
    NodePtr in;
    mutable std::unordered_map<std::uint64_t, double> memo;
    mutable std::uint64_t lastRevKey = 0;
    void beginPass() { memo.clear(); }
    double eval(const Sample& s) const override {
        const auto k = chunkColumnKey(s.x, s.z);
        auto it = memo.find(k);
        if (it != memo.end()) return it->second;
        const double v = in->eval(s);
        memo.emplace(k, v);
        return v;
    }
    static std::uint64_t chunkColumnKey(double x, double z) {
        const auto xi = static_cast<std::int64_t>(std::floor(x));
        const auto zi = static_cast<std::int64_t>(std::floor(z));
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(xi)) << 32)
             | static_cast<std::uint32_t>(zi);
    }
};
using Cache2dPtr = std::shared_ptr<Cache2d>;

inline void collectCaches(const NodePtr& n, std::vector<Cache2dPtr>& out) {
    if (!n) return;
    if (auto c = std::dynamic_pointer_cast<Cache2d>(n)) {
        out.push_back(c);
        collectCaches(c->in, out);
        return;
    }
    if (auto u = std::dynamic_pointer_cast<Unary>(n)) collectCaches(u->in, out);
    else if (auto cl = std::dynamic_pointer_cast<Clamp>(n)) collectCaches(cl->in, out);
    else if (auto rc = std::dynamic_pointer_cast<RangeChoice>(n)) {
        collectCaches(rc->in, out); collectCaches(rc->whenIn, out);
        collectCaches(rc->whenOut, out);
    } else if (auto na = std::dynamic_pointer_cast<Nary>(n))
        for (auto& c : na->inputs) collectCaches(c, out);
    else if (auto sn = std::dynamic_pointer_cast<ShiftedNoise>(n)) {
        if (sn->shiftX) collectCaches(sn->shiftX, out);
        if (sn->shiftY) collectCaches(sn->shiftY, out);
        if (sn->shiftZ) collectCaches(sn->shiftZ, out);
    } else if (auto sh = std::dynamic_pointer_cast<Shift>(n)) {
        (void)sh;
    } else if (auto beard = std::dynamic_pointer_cast<BeardifierNode>(n)) {
        (void)beard;
    } else if (auto old = std::dynamic_pointer_cast<OldBlendedNoiseNode>(n)) {
        collectCaches(old->in, out);
    } else if (auto ws = std::dynamic_pointer_cast<WeirdScaledSamplerNode>(n)) {
        collectCaches(ws->input, out);
    } else if (auto bd = std::dynamic_pointer_cast<BlendDensityNode>(n)) {
        collectCaches(bd->in, out);
    }
}

} // namespace detail

class DensityPipeline {
public:
    DensityPipeline() : noises_(std::make_shared<NoiseRegistry>(1337ULL)) {}

    void setSeed(std::uint64_t s) { noises_ = std::make_shared<NoiseRegistry>(s); }
    void setBeardifierProvider(std::function<double(int,int)> f) { beardifierProvider_ = std::move(f); }

    bool buildFromJson(const json::Value& root, std::string* err = nullptr);
    // Evaluate at world coordinates.
    double sample(double x, double y, double z) const {
        for (auto& c : caches_) c->beginPass();
        if (!root_) return 0;
        return root_->eval({x, y, z});
    }
    bool valid() const { return root_ != nullptr; }

private:
    NodePtr parse(const json::Value& v, std::string* err) const;

    std::shared_ptr<NoiseRegistry> noises_;
    NodePtr root_;
    std::vector<detail::Cache2dPtr> caches_;
    std::function<double(int,int)> beardifierProvider_;
};

} // namespace cppfm::worldgen
