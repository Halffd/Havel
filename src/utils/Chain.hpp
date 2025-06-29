#pragma once
#include <vector>
#include <functional>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <type_traits>
#include <ranges>
#include <string>
#include <sstream>
#include <iostream>

namespace havel {

template<typename Range>
class Chain {
    Range range;
public:
    explicit Chain(Range r) : range(std::move(r)) {}

    // View range
    auto view() const { return range; }

    // map() - returns new Chain with correct type
    template<typename Func>
    auto map(Func f) const {
        auto mapped = range | std::views::transform(f);
        return Chain<decltype(mapped)>(std::move(mapped));
    }

    // filter() - returns new Chain with correct type
    template<typename Predicate>
    auto filter(Predicate p) const {
        auto filtered = range | std::views::filter(p);
        return Chain<decltype(filtered)>(std::move(filtered));
    }

    // take(n) - returns new Chain with correct type
    auto take(size_t n) const {
        auto limited = range | std::views::take(n);
        return Chain<decltype(limited)>(std::move(limited));
    }

    // drop(n) - returns new Chain with correct type
    auto drop(size_t n) const {
        auto skipped = range | std::views::drop(n);
        return Chain<decltype(skipped)>(std::move(skipped));
    }

    // reverse() - returns new Chain with correct type
    auto reverse() const requires std::ranges::bidirectional_range<Range> {
        auto reversed = range | std::views::reverse;
        return Chain<decltype(reversed)>(std::move(reversed));
    }

    // enumerate() - returns new Chain with index-value pairs
    auto enumerate() const {
        auto enumerated = range | std::views::enumerate;
        return Chain<decltype(enumerated)>(std::move(enumerated));
    }

    // unique() - consecutive duplicates only
    auto unique() const {
        // Note: std::views::unique might not be available in all compilers yet
        // This is a placeholder - you might need to implement custom unique view
        auto uniqued = range; // Placeholder
        return Chain<decltype(uniqued)>(std::move(uniqued));
    }

    // Terminal operations (consume the chain)
    
    // to_vector()
    auto to_vector() const {
        using T = std::ranges::range_value_t<Range>;
        std::vector<T> result;
        result.reserve(std::ranges::size(range)); // if possible
        for (auto&& v : range) {
            result.push_back(std::forward<decltype(v)>(v));
        }
        return result;
    }

    // join (to_string with delimiter)
    std::string join(const std::string& delim = "") const {
        std::ostringstream oss;
        auto it = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        
        if (it == end) return "";
        
        oss << *it;
        ++it;
        
        while (it != end) {
            oss << delim << *it;
            ++it;
        }
        return oss.str();
    }

    // sum
    auto sum() const {
        using T = std::ranges::range_value_t<Range>;
        if constexpr (std::is_arithmetic_v<T>) {
            return std::accumulate(std::ranges::begin(range), 
                                 std::ranges::end(range), T{});
        } else {
            // For non-arithmetic types, try to use their default constructor
            T result{};
            for (auto&& v : range) {
                result += v;
            }
            return result;
        }
    }

    // min / max
    auto min() const {
        auto it = std::ranges::min_element(range);
        if (it == std::ranges::end(range)) {
            throw std::runtime_error("min() called on empty range");
        }
        return *it;
    }

    auto max() const {
        auto it = std::ranges::max_element(range);
        if (it == std::ranges::end(range)) {
            throw std::runtime_error("max() called on empty range");
        }
        return *it;
    }

    // count
    auto count() const {
        return std::ranges::distance(range);
    }

    // count_if
    template<typename Predicate>
    auto count_if(Predicate p) const {
        return std::ranges::count_if(range, p);
    }

    // any / all / none
    template<typename Predicate>
    bool any(Predicate p) const {
        return std::ranges::any_of(range, p);
    }

    template<typename Predicate>
    bool all(Predicate p) const {
        return std::ranges::all_of(range, p);
    }

    template<typename Predicate>
    bool none(Predicate p) const {
        return std::ranges::none_of(range, p);
    }

    // find
    template<typename T>
    auto find(const T& value) const {
        return std::ranges::find(range, value);
    }

    template<typename Predicate>
    auto find_if(Predicate p) const {
        return std::ranges::find_if(range, p);
    }

    // for_each (terminal)
    template<typename Func>
    void for_each(Func f) const {
        std::ranges::for_each(range, f);
    }

    // collect into different containers
    template<template<typename...> class Container>
    auto collect() const {
        using T = std::ranges::range_value_t<Range>;
        Container<T> result;
        
        if constexpr (requires { result.reserve(1); }) {
            if constexpr (std::ranges::sized_range<Range>) {
                result.reserve(std::ranges::size(range));
            }
        }
        
        for (auto&& v : range) {
            if constexpr (requires { result.push_back(v); }) {
                result.push_back(std::forward<decltype(v)>(v));
            } else if constexpr (requires { result.insert(v); }) {
                result.insert(std::forward<decltype(v)>(v));
            }
        }
        return result;
    }

    // Iterator support for range-based for loops
    auto begin() const { return std::ranges::begin(range); }
    auto end() const { return std::ranges::end(range); }
};

// Deduction helper
template<typename Range>
auto chain(Range&& r) {
    return Chain<std::views::all_t<Range>>(std::views::all(std::forward<Range>(r)));
}

// Convenience overloads for common containers
template<typename T>
auto chain(std::vector<T>& v) {
    return Chain<std::views::all_t<std::vector<T>&>>(std::views::all(v));
}

template<typename T>
auto chain(const std::vector<T>& v) {
    return Chain<std::views::all_t<const std::vector<T>&>>(std::views::all(v));
}

} // namespace havel
