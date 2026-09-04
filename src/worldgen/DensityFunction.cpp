// DensityFunction JSON builder.
#include "DensityFunction.hpp"
#include <algorithm>
#include <cstdlib>

namespace cppfm::worldgen {

NodePtr DensityPipeline::parse(const json::Value& v, std::string* err) const {
    std::string rawType;
    try { rawType = v.at("type").asStr(); } catch (...) {
        if (err) *err = "missing type";
        return nullptr;
    }
    std::string type = rawType;
    if (type.rfind("minecraft:", 0) == 0) type = type.substr(10);
    auto fail = [&](const char* msg) -> NodePtr {
        if (err) *err = msg;
        return nullptr;
    };
    if (type == "constant") return std::make_shared<detail::Constant>(v.at("value").asFloat());
    if (type == "y_clamped_gradient") {
        auto n = std::make_shared<detail::YClampedGradient>();
        n->fromY = v.at("from_y").asInt();
        n->toY = v.at("to_y").asInt();
        n->fromV = v.at("from_value").asFloat();
        n->toV = v.at("to_value").asFloat();
        return n;
    }
    // shift / shift_a / shift_b — Yarn Shift, ShiftA, ShiftB axis variants
    if (type == "shift" || type == "shift_a" || type == "shift_b") {
        std::string key;
        try { key = v.at("noise").asStr(); }
        catch (...) {
            try { key = v.at("offset_noise").asStr(); }
            catch (...) {
                try { key = v.at("argument").at("noise").asStr(); }
                catch (...) {
                    try { key = v.at("argument").asStr(); }
                    catch (...) { key = "minecraft:offset"; }
                }
            }
        }
        if (type == "shift_a") {
            auto n = std::make_shared<detail::ShiftA>(); n->reg = noises_; n->key = key; return n;
        }
        if (type == "shift_b") {
            auto n = std::make_shared<detail::ShiftB>(); n->reg = noises_; n->key = key; return n;
        }
        auto n = std::make_shared<detail::Shift>(); n->reg = noises_; n->key = key; return n;
    }
    if (type == "shifted_noise") {
        auto n = std::make_shared<detail::ShiftedNoise>();
        n->reg = noises_;
        // parse shifts: shift_x / shift_y / shift_z (snake) or shiftX etc
        auto parseChild = [&](const char* a, const char* b) -> NodePtr {
            try {
                const auto& sub = v.at(a);
                return parse(sub, err);
            } catch (...) {
                try {
                    const auto& sub = v.at(b);
                    return parse(sub, err);
                } catch (...) { return nullptr; }
            }
        };
        n->shiftX = parseChild("shift_x", "shiftX");
        n->shiftY = parseChild("shift_y", "shiftY");
        n->shiftZ = parseChild("shift_z", "shiftZ");
        std::string nk;
        const auto& noiseVal = v.at("noise");
        if (noiseVal.isStr()) nk = noiseVal.asStr();
        else if (noiseVal.isObj()) {
            if (auto* p = noiseVal.find("noise")) if (p->isStr()) nk = p->asStr();
            if (nk.empty()) if (auto* p = noiseVal.find("argument")) if (p->isStr()) nk = p->asStr();
        }
        if (nk.empty()) {
            const auto& argVal = v.at("argument");
            if (argVal.isStr()) nk = argVal.asStr();
            else if (argVal.isObj()) if (auto* p = argVal.find("noise")) if (p->isStr()) nk = p->asStr();
        }
        n->key = nk.empty() ? "minecraft:terrain" : nk;
        n->xzScale = v.at("xz_scale").asFloat(1.f);
        // support xzScale alias or outer scale
        if (v.find("xzScale")) n->xzScale = v.at("xzScale").asFloat(1.f);
        n->yScale = v.at("y_scale").asFloat(1.f);
        if (v.find("yScale")) n->yScale = v.at("yScale").asFloat(1.f);
        return n;
    }
    if (type == "noise") {
        auto n = std::make_shared<detail::NoiseNode>();
        n->reg = noises_;
        n->key = v.at("noise").asStr();
        n->xzScale = v.at("xz_scale").asFloat(1.f);
        n->yScale = v.at("y_scale").asFloat(1.f);
        n->xzOffset = 0;
        return n;
    }
    if (type == "abs" || type == "square" ||
        type == "half_negative" || type == "quarter_negative" ||
        type == "squeeze" || type == "neg") {
        auto n = std::make_shared<detail::Unary>();
        if (type == "abs") n->op = detail::Unary::Abs;
        else if (type == "square") n->op = detail::Unary::Square;
        else if (type == "cube") n->op = detail::Unary::Cube;
        else if (type == "half_negative") n->op = detail::Unary::HalfNeg;
        else if (type == "quarter_negative") n->op = detail::Unary::QuarterNeg;
        else if (type == "squeeze") n->op = detail::Unary::Squeeze;
        else n->op = detail::Unary::Neg;
        n->in = parse(v.at("input"), err);
        return n->in ? n : nullptr;
    }
    // type "cube" is alias for unary cube in newer codec
    if (type == "cube") {
        auto n = std::make_shared<detail::Unary>();
        n->op = detail::Unary::Cube;
        n->in = parse(v.at("input"), err);
        if (!n->in) n->in = parse(v.at("argument"), err);
        if (!n->in) return fail("cube missing input");
        return n;
    }
    if (type == "add" || type == "mul" || type == "min" || type == "max") {
        auto n = std::make_shared<detail::Nary>();
        n->op = type == "add" ? detail::Nary::Add
              : type == "mul" ? detail::Nary::Mul
              : type == "min" ? detail::Nary::Min : detail::Nary::Max;
        for (auto& iv : v.at("inputs").arr) {
            NodePtr c = parse(iv, err);
            if (!c) return nullptr;
            n->inputs.push_back(c);
        }
        return n;
    }
    if (type == "clamp") {
        auto n = std::make_shared<detail::Clamp>();
        n->lo = v.at("min").asFloat();
        n->hi = v.at("max").asFloat();
        n->in = parse(v.at("input"), err);
        return n->in ? n : nullptr;
    }
    if (type == "range_choice") {
        auto n = std::make_shared<detail::RangeChoice>();
        // support both min/max and min_inclusive/max_exclusive
        try {
            if (v.find("min_inclusive")) n->lo = v.at("min_inclusive").asFloat();
            else n->lo = v.at("min").asFloat();
        } catch (...) { n->lo = 0; }
        try {
            if (v.find("max_exclusive")) n->hiExclusive = v.at("max_exclusive").asFloat();
            else n->hiExclusive = v.at("max").asFloat();
        } catch (...) { n->hiExclusive = 0; }
        n->in = parse(v.at("input"), err);
        if (!n->in) return nullptr;
        n->whenIn = parse(v.at("when_in_range"), err);
        if (!n->whenIn) return nullptr;
        n->whenOut = parse(v.at("when_out_of_range"), err);
        if (!n->whenOut) return nullptr;
        return n;
    }
    if (type == "beardifier") {
        auto n = std::make_shared<detail::BeardifierNode>();
        n->sampleXZ = beardifierProvider_;
        return n;
    }
    if (type == "old_blended_noise") {
        auto n = std::make_shared<detail::OldBlendedNoiseNode>();
        n->reg = noises_;
        n->in = parse(v.at("input"), err);
        if (!n->in) n->in = parse(v.at("argument"), err);
        auto getF = [&](const char* a, const char* b, double def) -> double {
            try { return v.at(a).asFloat(float(def)); } catch (...) {
                try { return v.at(b).asFloat(float(def)); } catch (...) { return def; }
            }
        };
        n->xzScale = getF("xz_scale", "xzScale", 1.0);
        n->yScale = getF("y_scale", "yScale", 1.0);
        n->xzFactor = getF("xz_factor", "xzFactor", 80.0);
        n->yFactor = getF("y_factor", "yFactor", 160.0);
        n->smearScale = getF("smear_scale_multiplier", "smearScaleMultiplier", 8.0);
        // also support alternative name smear_scale
        try { n->smearScale = v.at("smear_scale").asFloat(float(n->smearScale)); } catch (...) {}
        n->smearScale = std::clamp(n->smearScale, 1.0, 8.0);
        return n;
    }
    if (type == "blend_alpha") {
        return std::make_shared<detail::BlendAlphaNode>();
    }
    if (type == "blend_offset") {
        return std::make_shared<detail::BlendOffsetNode>();
    }
    if (type == "blend_density") {
        auto n = std::make_shared<detail::BlendDensityNode>();
        n->in = parse(v.at("input"), err);
        if (!n->in) n->in = parse(v.at("argument"), err);
        return n;
    }
    if (type == "end_islands") {
        auto n = std::make_shared<detail::EndIslandsNode>();
        n->reg = noises_;
        return n;
    }
    if (type == "weird_scaled_sampler" || type == "interval_select") {
        auto n = std::make_shared<detail::WeirdScaledSamplerNode>();
        n->reg = noises_;
        n->input = parse(v.at("input"), err);
        if (!n->input) n->input = parse(v.at("argument"), err);
        std::string rarityStr;
        try { rarityStr = v.at("rarity_value_mapper").asStr(); } catch (...) {
            try { rarityStr = v.at("rarity").asStr(); } catch (...) {
                try { rarityStr = v.at("mapper").asStr(); } catch (...) {}
            }
        }
        if (rarityStr.find("type_2") != std::string::npos) n->mapper = detail::WeirdScaledSamplerNode::Type2;
        else n->mapper = detail::WeirdScaledSamplerNode::Type1;
        try { n->noiseKey = v.at("noise").asStr(); } catch (...) {
            try { n->noiseKey = v.at("input").at("noise").asStr(); } catch (...) {}
        }
        return n;
    }
    if (type == "cache_2d") {
        auto inner = parse(v.at("input"), err);
        if (!inner) return nullptr;
        auto c = std::make_shared<detail::Cache2d>();
        c->in = inner;
        return c;
    }
    // cache_2d stays column-memoized; both are collected for beginPass resets.
    if (type == "flat_cache") {
        auto n = std::make_shared<detail::FlatCache>();
        n->in = parse(v.at("input"), err);
        if (!n->in) n->in = parse(v.at("argument"), err);
        if (!n->in) return fail("flat_cache missing input");
        return n;
    }
    if (type == "cache_once") {
        auto n = std::make_shared<detail::CacheOnce>();
        n->in = parse(v.at("input"), err);
        if (!n->in) n->in = parse(v.at("argument"), err);
        if (!n->in) return fail("cache_once missing input");
        return n;
    }
    if (type == "interpolated") {
        auto n = std::make_shared<detail::Interpolated>();
        n->in = parse(v.at("input"), err);
        if (!n->in) n->in = parse(v.at("argument"), err);
        if (!n->in) return fail("interpolated missing input");
        return n;
    }
    if (type == "spline") {
        auto n = std::make_shared<detail::Spline>();
        const json::Value* body = &v;
        if (const json::Value* sub = v.find("spline"))
            if (sub->isObj()) body = sub;
        try {
            const auto& coord = body->at("coordinate");
            n->coordinate = parse(coord, err);
        } catch (...) { n->coordinate = nullptr; }
        try {
            for (auto& pv : body->at("points").arr) {
                detail::Spline::Point p{0, 0, 0};
                p.loc = pv.at("location").asFloat(0.0);
                // value: float or nested function evaluated at (loc,0,0)
                try {
                    const auto& vv = pv.at("value");
                    if (vv.isNum()) p.val = vv.asFloat(0.0);
                    else if (auto fn = parse(vv, err)) p.val = fn->eval({p.loc, 0, 0});
                } catch (...) {}
                try {
                    const auto& dv = pv.at("derivative");
                    if (dv.isNum()) p.der = dv.asFloat(0.0);
                    else if (auto fn = parse(dv, err)) p.der = fn->eval({p.loc, 0, 0});
                } catch (...) { p.der = 0; }
                n->points.push_back(p);
            }
        } catch (...) { return fail("spline missing points"); }
        if (n->points.empty()) return fail("spline has no points");
        std::sort(n->points.begin(), n->points.end(),
                  [](const auto& a, const auto& b){ return a.loc < b.loc; });
        return n;
    }
    if (err)
        *err = "unknown density function type: " + type;
    return nullptr;
}

bool DensityPipeline::buildFromJson(const json::Value& root,
                                     std::string* err) {
    root_ = parse(root, err);
    caches_.clear();
    flatCaches_.clear();
    onceCaches_.clear();
    if (root_) detail::collectCaches(root_, caches_, &flatCaches_, &onceCaches_);
    return valid();
}

} // namespace cppfm::worldgen
