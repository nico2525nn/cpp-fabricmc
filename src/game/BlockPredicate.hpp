#pragma once

#include "../generated/BlockStates.hpp"
#include "TagManager.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm::block_predicate {

struct ParsedBlockArgument {
    std::string name;
    std::uint16_t state = 0;
    bool hasBlockEntityNbt = false;
    std::vector<std::pair<std::string, std::string>> properties;
};

inline std::vector<std::pair<std::string, std::string>> parseBlockProperties(
    std::string_view body) {
    if (body.empty())
        throw std::runtime_error("block-state properties cannot be empty");
    std::vector<std::pair<std::string, std::string>> properties;
    std::size_t cursor = 0;
    while (cursor < body.size()) {
        const auto equal = body.find('=', cursor);
        const auto comma = body.find(',', cursor);
        if (equal == std::string_view::npos ||
            (comma != std::string_view::npos && equal > comma))
            throw std::runtime_error("malformed block-state property");
        const auto end = comma == std::string_view::npos ? body.size() : comma;
        const auto trim = [](std::string_view value) {
            const auto first = value.find_first_not_of(" \t");
            if (first == std::string_view::npos) return std::string{};
            const auto last = value.find_last_not_of(" \t");
            return std::string(value.substr(first, last - first + 1));
        };
        auto key = trim(body.substr(cursor, equal - cursor));
        auto value = trim(body.substr(equal + 1, end - equal - 1));
        if (key.empty() || value.empty() ||
            std::any_of(properties.begin(), properties.end(),
                [&](const auto& property) { return property.first == key; }))
            throw std::runtime_error(
                "empty or duplicate block-state property");
        properties.emplace_back(std::move(key), std::move(value));
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
        if (cursor == body.size())
            throw std::runtime_error("block-state property cannot be empty");
    }
    return properties;
}

inline ParsedBlockArgument parseBlockArgument(std::string_view raw) {
    const auto stateStart = raw.find_first_of("[{\t ");
    ParsedBlockArgument result;
    result.name = std::string(raw.substr(0, stateStart));
    result.hasBlockEntityNbt = raw.find('{') != std::string_view::npos;
    if (result.name.empty())
        throw std::runtime_error("block identifier is empty");
    if (result.name.find(':') == std::string::npos)
        result.name = "minecraft:" + result.name;
    const auto* definition = gen::blockByName(result.name);
    if (!definition)
        throw std::runtime_error("unknown block: " + result.name);

    const auto open = raw.find('[');
    if (open == std::string_view::npos) {
        if (result.name.size() != raw.size() && !result.hasBlockEntityNbt)
            throw std::runtime_error("unexpected data after block identifier");
        result.state = static_cast<std::uint16_t>(definition->defaultState);
        return result;
    }
    const auto close = raw.find(']', open + 1);
    const auto nbtStart = raw.find('{');
    if (close == std::string_view::npos ||
        (nbtStart != std::string_view::npos && close > nbtStart))
        throw std::runtime_error("malformed block-state properties");
    const auto suffix = raw.substr(close + 1);
    if (!suffix.empty() && suffix.front() != '{')
        throw std::runtime_error("unexpected data after block-state properties");

    result.properties = parseBlockProperties(
        raw.substr(open + 1, close - open - 1));

    std::vector<std::pair<std::string_view, std::string_view>> requested;
    requested.reserve(result.properties.size());
    for (const auto& [key, value] : result.properties)
        requested.emplace_back(key, value);
    const auto resolved = gen::stateWithProps(*definition, requested);
    const auto actual = gen::propsOf(resolved);
    for (const auto& [key, value] : requested) {
        const auto found = std::find_if(actual.begin(), actual.end(),
            [&](const auto& property) { return property.first == key; });
        if (found == actual.end() || found->second != value)
            throw std::runtime_error(
                "invalid block-state property " + std::string(key) + "=" +
                std::string(value) + " for " + result.name);
    }
    result.state = static_cast<std::uint16_t>(resolved);
    return result;
}

struct ParsedBlockPredicate {
    std::string name;
    bool tag = false;
    std::vector<std::pair<std::string, std::string>> properties;
};

inline ParsedBlockPredicate parseBlockPredicate(std::string_view raw) {
    if (raw.empty() || raw.front() != '#') {
        const auto block = parseBlockArgument(raw);
        if (block.hasBlockEntityNbt)
            throw std::runtime_error(
                "Block-entity NBT is not supported by block predicates");
        return {block.name, false, block.properties};
    }

    if (raw.find('{') != std::string_view::npos)
        throw std::runtime_error(
            "Block-entity NBT is not supported by block predicates");
    const auto open = raw.find('[');
    const auto nameEnd = open == std::string_view::npos ? raw.size() : open;
    auto name = std::string(raw.substr(1, nameEnd - 1));
    if (name.empty()) throw std::runtime_error("block tag is empty");
    if (name.find(':') == std::string::npos)
        name = "minecraft:" + name;

    std::vector<std::pair<std::string, std::string>> properties;
    if (open != std::string_view::npos) {
        const auto close = raw.find(']', open + 1);
        if (close == std::string_view::npos || close + 1 != raw.size())
            throw std::runtime_error("malformed block-tag predicate");
        properties = parseBlockProperties(
            raw.substr(open + 1, close - open - 1));
    }
    return {std::move(name), true, std::move(properties)};
}

inline void validateBlockPredicate(const ParsedBlockPredicate& predicate,
                            const TagManager& tags) {
    if (!predicate.tag) return;
    const auto tag = tags.blockTags.find(predicate.name);
    if (tag == tags.blockTags.end())
        throw std::runtime_error("unknown block tag: " + predicate.name);
    if (predicate.properties.empty()) return;

    for (const auto defaultState : tag->second) {
        const auto* block = gen::blockByState(
            static_cast<std::uint16_t>(defaultState));
        if (!block) continue;
        std::vector<std::pair<std::string_view, std::string_view>> requested;
        requested.reserve(predicate.properties.size());
        for (const auto& [key, value] : predicate.properties)
            requested.emplace_back(key, value);
        const auto candidate = gen::stateWithProps(*block, requested);
        const auto actual = gen::propsOf(candidate);
        const bool compatible = std::all_of(
            predicate.properties.begin(), predicate.properties.end(),
            [&](const auto& requestedProperty) {
                const auto found = std::find_if(
                    actual.begin(), actual.end(), [&](const auto& property) {
                        return property.first == requestedProperty.first;
                    });
                return found != actual.end() &&
                       found->second == requestedProperty.second;
            });
        if (compatible) return;
    }
    throw std::runtime_error(
        "no block in tag " + predicate.name +
        " has the requested state properties");
}

inline bool blockMatchesPredicate(const ParsedBlockPredicate& predicate,
                           std::uint16_t state, const TagManager& tags) {
    const auto* block = gen::blockByState(state);
    if (!block) return false;
    if (predicate.tag) {
        const auto tag = tags.blockTags.find(predicate.name);
        if (tag == tags.blockTags.end() ||
            !tag->second.contains(block->defaultState))
            return false;
    } else if (block->name != predicate.name) {
        return false;
    }

    const auto actual = gen::propsOf(state);
    return std::all_of(
        predicate.properties.begin(), predicate.properties.end(),
        [&](const auto& requested) {
            const auto found = std::find_if(
                actual.begin(), actual.end(), [&](const auto& property) {
                    return property.first == requested.first;
                });
            return found != actual.end() && found->second == requested.second;
        });
}

} // namespace cppfm::block_predicate
