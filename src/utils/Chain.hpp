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
    private:
        mutable Range range;  // Make it mutable!
    
    public:
        explicit Chain(Range r) : range(std::move(r)) {}
    
        // Fix: Remove const from methods that need to modify views
        template<typename Func>
        auto map(Func&& f) {  // Remove const here
            auto transformed = std::views::transform(range, std::forward<Func>(f));
            return Chain<decltype(transformed)>(std::move(transformed));
        }
    
        template<typename Predicate>
        auto filter(Predicate&& p) {  // Remove const here
            auto filtered = std::views::filter(range, std::forward<Predicate>(p));
            return Chain<decltype(filtered)>(std::move(filtered));
        }
    
        auto take(size_t n) {  // Remove const here
            auto taken = std::views::take(range, n);
            return Chain<decltype(taken)>(std::move(taken));
        }
    
        auto drop(size_t n) {  // Remove const here
            auto dropped = std::views::drop(range, n);
            return Chain<decltype(dropped)>(std::move(dropped));
        }
        auto sum() const {
            using T = std::ranges::range_value_t<Range>;
            if constexpr (std::is_arithmetic_v<T>) {
                T result{};
                for (auto&& v : range) {
                    result += v;
                }
                return result;
            } else {
                // For string concatenation, etc.
                T result{};
                for (auto&& v : range) {
                    result += v;
                }
                return result;
            }
        }
    
        std::string join(const std::string& delim = "") const {
            std::ostringstream oss;
            auto it = std::ranges::begin(range);
            
            if (it == std::ranges::end(range)) return "";
            
            oss << *it++;
            for (auto end = std::ranges::end(range); it != end; ++it) {
                oss << delim << *it;
            }
            return oss.str();
        }
    
        auto count() const {
            return std::ranges::distance(range);
        }
    
        auto min() const {
            return *std::ranges::min_element(range);
        }
    
        auto max() const {
            return *std::ranges::max_element(range);
        }
    
        template<typename Func>
        void for_each(Func f) const {
            std::ranges::for_each(range, f);
        }
    
        auto to_vector() const {
            using T = std::ranges::range_value_t<Range>;
            std::vector<T> result;
            for (auto&& v : range) {
                result.push_back(v);
            }
            return result;
        }
    
        template<template<typename...> class Container>
        auto collect() const {
            using T = std::ranges::range_value_t<Range>;
            Container<T> result;
            for (auto&& v : range) {
                if constexpr (requires { result.insert(v); }) {
                    result.insert(v);
                } else {
                    result.push_back(v);
                }
            }
            return result;
        }
    };
    
    // Deduction guide
    template<typename Range>
    Chain(Range) -> Chain<Range>;
    
    // Helper function
    template<typename Range>
    auto chain(Range&& r) {
        return Chain(std::views::all(std::forward<Range>(r)));
    }
} // namespace havel
