#pragma once
#include <array>
#include <utility>
#include <algorithm>
#include <numeric>
#include <cstddef>
#include <optional>

namespace havel {

template<typename T, size_t N, typename F>
constexpr auto map(const std::array<T, N>& arr, F f) {
    std::array<decltype(f(arr[0])), N> out{};
    for (size_t i = 0; i < N; ++i)
        out[i] = f(arr[i]);
    return out;
}

template<typename T, size_t N, typename Pred>
constexpr auto filter(const std::array<T, N>& arr, Pred pred) {
    std::array<std::optional<T>, N> result{};
    size_t idx = 0;
    for (size_t i = 0; i < N; ++i)
        if (pred(arr[i])) result[idx++] = arr[i];
    return result;
}

template<typename T, size_t N>
constexpr T sum(const std::array<T, N>& arr) {
    T total = {};
    for (auto x : arr) total += x;
    return total;
}

} // namespace havel
