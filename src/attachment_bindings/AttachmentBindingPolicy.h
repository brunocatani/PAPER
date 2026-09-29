#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace paper::attachment_bindings::policy
{
    struct RemapNames { std::string_view node, connection; };

    [[nodiscard]] constexpr std::optional<RemapNames> parseNodeRemap(std::string_view name)
    {
        constexpr std::string_view prefix = "remapNode_";
        if (name.size() > 255 || !name.starts_with(prefix)) return std::nullopt;
        name.remove_prefix(prefix.size());
        const auto separator = name.find('_');
        if (separator == std::string_view::npos || separator == 0) return std::nullopt;
        const auto node = name.substr(0, separator);
        const auto connection = name.substr(separator + 1);
        // Native dispatch tokenizes on every underscore. Do not invent node
        // names by joining its arguments, or execute connection-metadata remaps.
        if (node.starts_with("P-") || !connection.starts_with("P-") ||
            connection.size() <= 2 || connection.find('_') != std::string_view::npos) return std::nullopt;
        return RemapNames{ node, connection };
    }

    struct Identity
    {
        std::uintptr_t root{0}, instance{0};
        std::uint64_t weaponGeneration{0};
        std::uint32_t form{0}, worldGeneration{0}, skeletonGeneration{0};
        [[nodiscard]] bool operator==(const Identity&) const = default;
        [[nodiscard]] constexpr bool valid() const { return root && instance && weaponGeneration && form; }
    };

    template <class Node> struct SearchResult
    {
        Node* match{nullptr};
        bool complete{true}, ambiguous{false};
        std::size_t visited{0};
    };

    // Assembly-time only; no allocation, recursion or first-match ambiguity.
    template <class Node, class Name, class Count, class Child>
    [[nodiscard]] SearchResult<Node> findUnique(Node* root, std::string_view wanted,
        Name name, Count childCount, Child child)
    {
        struct Entry { Node* node; std::size_t depth; };
        std::array<Entry, 512> stack{};
        std::size_t count = 0;
        SearchResult<Node> result;
        if (root) stack[count++] = {root, 0};
        while (count) {
            const auto current = stack[--count];
            if (!current.node) continue;
            if (++result.visited > 4096 || current.depth > 64) { result.complete = false; return result; }
            if (name(current.node) == wanted) {
                if (result.match) result.ambiguous = true;
                result.match = current.node;
            }
            const auto children = childCount(current.node);
            if (children > stack.size() - count) { result.complete = false; return result; }
            for (std::size_t i = 0; i < children; ++i) stack[count++] = {child(current.node, i), current.depth + 1};
        }
        return result;
    }
}
