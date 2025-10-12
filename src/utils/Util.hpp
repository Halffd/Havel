#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <execinfo.h>
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
#include <memory>
#include <atomic>
#include "./Timer.hpp"
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

// Concept for iterable containers
template<typename T>
concept Iterable = requires(const T& t) {
    std::begin(t);
    std::end(t);
};
// Base concept for searchable containers
template<typename T>
concept Searchable = requires(const T& t) {
    std::begin(t);
    std::end(t);
};

// String concept for substring search
template<typename T>
concept StringLike = std::convertible_to<T, std::string_view>;

template<Searchable Container>
constexpr auto find(const Container& container, const typename Container::value_type& value) {
    auto it = std::find(std::begin(container), std::end(container), value);
    if (it != std::end(container)) {
        return std::optional{std::distance(std::begin(container), it)};
    }
    return std::optional<size_t>{};
}

// String overload for substring search
template<StringLike String>
constexpr auto find(const String& str, const String& substr) {
    std::string_view sv{str};
    std::string_view pattern{substr};
    
    auto pos = sv.find(pattern);
    if (pos != std::string_view::npos) {
        return std::optional{pos};
    }
    return std::optional<size_t>{};
}

// Find with custom predicate
template<Searchable Container, typename Predicate>
constexpr auto findIf(const Container& container, Predicate pred) {
    auto it = std::find_if(std::begin(container), std::end(container), pred);
    if (it != std::end(container)) {
        return std::optional{std::distance(std::begin(container), it)};
    }
    return std::optional<size_t>{};
}

// Find last occurrence
template<Searchable Container>
constexpr auto findLast(const Container& container, const typename Container::value_type& value) {
    auto it = std::find(std::rbegin(container), std::rend(container), value);
    if (it != std::rend(container)) {
        return std::optional{std::distance(it, std::rend(container)) - 1};
    }
    return std::optional<size_t>{};
}

// Contains - just a wrapper around find
template<Searchable Container>
constexpr bool contains(const Container& container, const typename Container::value_type& value) {
    return find(container, value).has_value();
}

template<StringLike String>
constexpr bool contains(const String& str, const String& substr) {
    return find(str, substr).has_value();
}

// Overload for std::unordered_map
template<typename K, typename V, typename H, typename E>
bool contains(const std::unordered_map<K, V, H, E>& map, const K& key) {
    return map.find(key) != map.end();
}

inline bool in(const std::string& input, const std::string& substring) {
    return input.find(substring) != std::string::npos;
}
// includes - checks if ALL elements of second container are in first
template<Iterable T1, Iterable T2>
bool includes(const T1& input, const T2& elements) {
    return std::all_of(std::begin(elements), std::end(elements),
        [&input](const auto& element) {
            return contains(input, element);
        });
}

// includes - single element version (subset check)
template<Iterable T>
bool includes(const T& input, const typename T::value_type& value) {
    return contains(input, value);  // Same as contains for single element
}

// String-specific includes (case-insensitive contains)
inline bool insens(const std::string& input, const std::string& substring) {
    std::string input_lower = input;
    std::string sub_lower = substring;
    std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);
    std::transform(sub_lower.begin(), sub_lower.end(), sub_lower.begin(), ::tolower);
    return input_lower.find(sub_lower) != std::string::npos;
}

// indexOf - returns int, -1 if not found
template<Iterable T>
int indexOf(const T& input, const typename T::value_type& value) {
    auto it = std::find(std::begin(input), std::end(input), value);
    if (it == std::end(input)) {
        return -1;  // Not found
    }
    return static_cast<int>(std::distance(std::begin(input), it));
}

// String-specific indexOf
inline int indexOf(const std::string& input, const std::string& substring) {
    auto pos = input.find(substring);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

// indexOf with start position
template<Iterable T>
int indexOf(const T& input, const typename T::value_type& value, size_t start) {
    if (start >= input.size()) return -1;
    
    auto begin_it = std::begin(input);
    std::advance(begin_it, start);
    auto it = std::find(begin_it, std::end(input), value);
    
    if (it == std::end(input)) {
        return -1;
    }
    return static_cast<int>(std::distance(std::begin(input), it));
}

inline int indexOf(const std::string& input, const std::string& substring, size_t start) {
    auto pos = input.find(substring, start);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

// lastIndexOf - returns int, -1 if not found
template<Iterable T>
int lastIndexOf(const T& input, const typename T::value_type& value) {
    auto it = std::find(std::rbegin(input), std::rend(input), value);
    if (it == std::rend(input)) {
        return -1;  // Not found
    }
    // Convert reverse iterator to forward index
    return static_cast<int>(std::distance(it, std::rend(input)) - 1);
}

// String-specific lastIndexOf
inline int lastIndexOf(const std::string& input, const std::string& substring) {
    auto pos = input.rfind(substring);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

template<Iterable T>
size_t count(const T& input, const typename T::value_type& value) {
    return std::count(std::begin(input), std::end(input), value);
}

template<Iterable T>
bool startsWith(const T& input, const T& prefix) {
    if (prefix.size() > input.size()) return false;
    return std::equal(std::begin(prefix), std::end(prefix), std::begin(input));
}

inline bool startsWith(const std::string& input, const std::string& prefix) {
    return input.size() >= prefix.size() && 
           input.compare(0, prefix.size(), prefix) == 0;
}

template<Iterable T>
bool endsWith(const T& input, const T& suffix) {
    if (suffix.size() > input.size()) return false;
    return std::equal(std::rbegin(suffix), std::rend(suffix), std::rbegin(input));
}

inline bool endsWith(const std::string& input, const std::string& suffix) {
    return input.size() >= suffix.size() && 
           input.compare(input.size() - suffix.size(), suffix.size(), suffix) == 0;
}

template<typename T>
bool isEmpty(const T& input) requires requires { input.empty(); } {
    return input.empty();
}

template<typename T>
bool isNotEmpty(const T& input) requires requires { input.empty(); } {
    return !input.empty();
}

template<typename T>
bool isNull(T* input) {
    return input == nullptr;
}

template<typename T>
bool isNotNull(T* input) {
    return input != nullptr;
}

template<typename T>
bool isNull(const std::unique_ptr<T>& input) {
    return input == nullptr;
}

template<typename T>
bool isNull(const std::shared_ptr<T>& input) {
    return input == nullptr;
}

template<typename T>
bool isNotNull(const std::unique_ptr<T>& input) {
    return input != nullptr;
}

template<typename T>
bool isNotNull(const std::shared_ptr<T>& input) {
    return input != nullptr;
}

template<typename T>
bool isNull(const std::optional<T>& input) {
    return !input.has_value();
}

template<typename T>
bool isNotNull(const std::optional<T>& input) {
    return input.has_value();
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
template<typename Container>
auto choice(const Container& container) {
    if (container.empty()) {
        throw std::runtime_error("choice() called on empty container");
    }
    
    auto it = container.begin();
    std::advance(it, rand() % container.size());
    return *it;  // This returns the element, not the container
}

// ===== Misc =====

// Forward declaration for recursive types
template<typename T>
std::string toString(const T& val);

// Helper concepts
template<typename T>
concept Streamable = requires(std::ostringstream& oss, const T& t) {
    oss << t;
};

template<typename T>
concept PairLike = requires(const T& t) {
    t.first;
    t.second;
};

template<typename T>
concept TupleLike = requires(const T& t) {
    std::tuple_size<T>::value;
} && !PairLike<T>;

// Helper for tuple printing
template<typename Tuple, std::size_t... I>
std::string tupleToString(const Tuple& t, std::index_sequence<I...>) {
    std::ostringstream oss;
    oss << "(";
    ((oss << (I == 0 ? "" : ", ") << toString(std::get<I>(t))), ...);
    oss << ")";
    return oss.str();
}

// Main toString implementations
template<typename T>
std::string toString(const T& val) {
    if constexpr (std::is_same_v<T, std::string>) {
        return val;
    }
    else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
        return std::string(val);
    }
    else if constexpr (std::is_same_v<T, char>) {
        return std::string(1, val);
    }
    else if constexpr (std::is_same_v<T, bool>) {
        return val ? "true" : "false";
    }
    else if constexpr (std::is_null_pointer_v<T>) {
        return "nullptr";
    }
    else if constexpr (std::is_pointer_v<T>) {
        if (val == nullptr) {
            return "nullptr";
        }
        std::ostringstream oss;
        oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(val);
        return oss.str();
    }
    else if constexpr (PairLike<T>) {
        return "(" + toString(val.first) + ", " + toString(val.second) + ")";
    }
    else if constexpr (TupleLike<T>) {
        return tupleToString(val, std::make_index_sequence<std::tuple_size_v<T>>{});
    }
    else if constexpr (Iterable<T>) {
        std::ostringstream oss;
        
        // Detect container type for different brackets
        if constexpr (requires { typename T::key_type; }) {
            // Map-like containers
            oss << "{";
        } else if constexpr (std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>>) {
            oss << "[";
        } else if constexpr (std::is_same_v<T, std::set<typename T::value_type, typename T::key_compare, typename T::allocator_type>>) {
            oss << "{";
        } else {
            oss << "[";
        }
        
        bool first = true;
        for (const auto& item : val) {
            if (!first) oss << ", ";
            oss << toString(item);
            first = false;
        }
        
        // Matching closing bracket
        if constexpr (requires { typename T::key_type; }) {
            oss << "}";
        } else if constexpr (std::is_same_v<T, std::vector<typename T::value_type, typename T::allocator_type>>) {
            oss << "]";
        } else if constexpr (std::is_same_v<T, std::set<typename T::value_type, typename T::key_compare, typename T::allocator_type>>) {
            oss << "}";
        } else {
            oss << "]";
        }
        
        return oss.str();
    }
    else if constexpr (Streamable<T>) {
        std::ostringstream oss;
        oss << val;
        return oss.str();
    }
    else {
        // Fallback for non-streamable types
        return "<unprintable:" + std::string(typeid(T).name()) + ">";
    }
}

// Specializations for common types that need special handling
template<>
inline std::string toString(const std::string& val) {
    return val;
}

// Optional support
template<typename T>
std::string toString(const std::optional<T>& opt) {
    if (opt.has_value()) {
        return "Some(" + toString(*opt) + ")";
    }
    return "None";
}

// Variant support
template<typename... Types>
std::string toString(const std::variant<Types...>& var) {
    return std::visit([](const auto& v) { return toString(v); }, var);
}

// Smart pointer support
template<typename T>
std::string toString(const std::unique_ptr<T>& ptr) {
    if (ptr) {
        return "unique_ptr(" + toString(*ptr) + ")";
    }
    return "unique_ptr(nullptr)";
}

template<typename T>
std::string toString(const std::shared_ptr<T>& ptr) {
    if (ptr) {
        return "shared_ptr(" + toString(*ptr) + ")";
    }
    return "shared_ptr(nullptr)";
}

// Array support
template<typename T, std::size_t N>
std::string toString(const std::array<T, N>& arr) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) oss << ", ";
        oss << toString(arr[i]);
    }
    oss << "]";
    return oss.str();
}

// C-style array support
template<typename T, std::size_t N>
std::string toString(const T (&arr)[N]) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) oss << ", ";
        oss << toString(arr[i]);
    }
    oss << "]";
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
void printStackTrace(int len = 256);
} // namespace havel