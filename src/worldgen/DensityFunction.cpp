// DensityFunction JSON builder.
#include "DensityFunction.hpp"
#include <cstdlib>

namespace cppfm::worldgen {

NodePtr DensityPipeline::parse(const json::Value& v, std::string* err) const {
    std::string type;
    try { type = v.at("type").asStr(); } catch (...) {
        if (err) *err = "missing type";
        return nullptr;
    }
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
    // shift: {"type":"shift","noise":"minecraft:offset"} or shift_a / shift_b
    if (type == "shift" || type == "shift_a" || type == "shift_b") {
        auto n = std::make_shared<detail::Shift>();
        n->reg = noises_;
        // try various key spellings
        try { n->key = v.at("noise").asStr(); }
        catch (...) {
            try { n->key = v.at("offset_noise").asStr(); }
            catch (...) {
                try { n->key = v.at("argument").at("noise").asStr(); }
                catch (...) { n->key = "minecraft:offset"; }
            }
        }
        return n;
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
        // noise key
        try { n->key = v.at("noise").asStr(); }
        catch (...) {
            try { n->key = v.at("argument").asStr(); }
            catch (...) { n->key = "minecraft:terrain"; }
        }
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
    if (type == "abs" || type == "square" || type == "cube" ||
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
        try {
            n->in = parse(v.at("input"), err);
        } catch (...) {
            // try argument
            try { n->in = parse(v.at("argument"), err); } catch (...) { return fail("cube missing input"); }
        }
        return n->in ? n : nullptr;
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
        try { n->in = parse(v.at("input"), err); } catch (...) { n->in = nullptr; }
        // fallback to inner if present, otherwise pass-through 0
        if (n->in) return n;
        try { return parse(v.at("input"), err); } catch (...) { return n; }
    }
    if (type == "blend_alpha") {
        return std::make_shared<detail::BlendAlphaNode>();
    }
    if (type == "blend_offset") {
        return std::make_shared<detail::BlendOffsetNode>();
    }
    if (type == "blend_density") {
        auto n = std::make_shared<detail::BlendDensityNode>();
        try { n->in = parse(v.at("input"), err); } catch (...) { n->in = nullptr; }
        return n;
    }
    if (type == "end_islands") {
        return std::make_shared<detail::EndIslandsNode>();
    }
    if (type == "weird_scaled_sampler") {
        auto n = std::make_shared<detail::WeirdScaledSamplerNode>();
        try { n->input = parse(v.at("input"), err); } catch (...) { try { n->input = parse(v.at("argument"), err); } catch (...) {} }
        // rarity mapper string
        std::string rarityStr;
        try { rarityStr = v.at("rarity").asStr(); } catch (...) {
            try { rarityStr = v.at("rarity_value_mapper").asStr(); } catch (...) {
                try { rarityStr = v.at("mapper").asStr(); } catch (...) {}
            }
        }
        if (rarityStr == "type_1" || rarityStr.find("type_1") != std::string::npos) n->rarity = 1.0;
        else if (rarityStr == "type_2" || rarityStr.find("type_2") != std::string::npos) n->rarity = 2.0;
        else {
            // numeric or default
            try { n->rarity = v.at("rarity").asFloat(1.f); } catch (...) { n->rarity = 1.0; }
        }
        return n;
    }
    if (type == "cache_2d" || type == "flat_cache") {
        auto inner = parse(v.at("input"), err);
        if (!inner) return nullptr;
        auto c = std::make_shared<detail::Cache2d>();
        c->in = inner;
        return c;
    }
    // pass-through aliases for forward compatibility
    if (type == "interpolated" || type == "cache_once")
        return parse(v.at("input"), err);
    if (err)
        *err = "unknown density function type: " + type;
    return nullptr;
}

bool DensityPipeline::buildFromJson(const json::Value& root,
                                     std::string* err) {
    root_ = parse(root, err);
    caches_.clear();
    if (root_) detail::collectCaches(root_, caches_);
    return valid();
}

} // namespace cppfm::worldgen
