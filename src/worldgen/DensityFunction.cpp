// DensityFunction JSON builder.
#include "DensityFunction.hpp"
#include <cstdlib>

namespace cppfm::worldgen {

NodePtr DensityPipeline::parse(const json::Value& v, std::string* err) const {
    const std::string type = v.at("type").asStr();
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
    if (type == "noise" || type == "shifted_noise") {
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
        n->lo = v.at("min").asFloat();
        n->hiInclusive = v.at("max").asFloat();
        n->in = parse(v.at("input"), err);
        if (!n->in) return nullptr;
        n->whenIn = parse(v.at("when_in_range"), err);
        if (!n->whenIn) return nullptr;
        n->whenOut = parse(v.at("when_out_of_range"), err);
        if (!n->whenOut) return nullptr;
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
