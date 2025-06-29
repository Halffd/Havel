#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
#include "./Range.hpp"
#include "./Chain.hpp"
#include "./Object.hpp"
#include "./JSON.hpp"
#include "./CUtil.hpp"

namespace havel {

// ===== Type Traits =====

template<typename T>
constexpr bool is_string = std::is_same_v<std::decay_t<T>, std::string>;

template<typename T, typename = void>
constexpr bool is_iterable = false;

template<typename T>
constexpr bool is_iterable<T, std::void_t<decltype(std::begin(std::declval<T>())),
                                          decltype(std::end(std::declval<T>()))>> = true;

template<typename T>
using ElemType = typename std::decay_t<decltype(*std::begin(std::declval<T>()))>;

// ===== String Utils =====

inline std::string trim(std::string s) {
    auto start = s.find_first_not_of(" \t\n\r");
    auto end = s.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

inline bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ===== Join / Split =====

template<typename Iterable>
std::string join(const Iterable& items, const std::string& delim = ",") {
    std::ostringstream out;
    auto it = std::begin(items);
    if (it == std::end(items)) return "";
    out << *it++;
    while (it != std::end(items)) out << delim << *it++;
    return out.str();
}

inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim)) result.push_back(token);
    return result;
}

// ===== Vector-like operations =====

template<typename T, typename F>
auto map(const T& input, F func) {
    using OutType = decltype(func(*std::begin(input)));
    std::vector<OutType> result;
    for (const auto& item : input) result.push_back(func(item));
    return result;
}

template<typename T, typename Pred>
auto filter(const T& input, Pred pred) {
    std::vector<typename T::value_type> result;
    for (const auto& item : input)
        if (pred(item)) result.push_back(item);
    return result;
}

template<typename T>
auto sum(const T& input) {
    using Value = typename T::value_type;
    return std::accumulate(std::begin(input), std::end(input), Value{});
}

template<typename T>
auto min(const T& input) {
    return *std::min_element(std::begin(input), std::end(input));
}

template<typename T>
auto max(const T& input) {
    return *std::max_element(std::begin(input), std::end(input));
}

template<typename T>
auto reverse(const T& input) {
    T copy = input;
    std::reverse(copy.begin(), copy.end());
    return copy;
}

template<typename T>
bool contains(const T& input, const typename T::value_type& value) {
    return std::find(std::begin(input), std::end(input), value) != std::end(input);
}

// ===== Random =====

inline int randint(int min, int max) {
    static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::uniform_int_distribution<int>(min, max)(rng);
}

inline double randfloat(double min = 0.0, double max = 1.0) {
    static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::uniform_real_distribution<double>(min, max)(rng);
}

template<typename T>
T choice(const T& input) {
    auto size = std::distance(std::begin(input), std::end(input));
    auto it = std::begin(input);
    std::advance(it, randint(0, size - 1));
    return *it;
}

// ===== Misc =====

template<typename T>
std::string toString(const T& val) {
    std::ostringstream oss;
    oss << val;
    return oss.str();
}

template<typename F>
void repeat(int times, F func) {
    for (int i = 0; i < times; ++i) func(i);
}

template<typename K, typename V, typename F>
auto map_values(const std::map<K, V>& m, F f) {
    std::map<K, decltype(f(m.begin()->second))> out;
    for (const auto& [k, v] : m) out[k] = f(v);
    return out;
}
} // namespace havel

