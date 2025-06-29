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
#include <map>
#define OBJECT(...) havel::makeObject([&]{ return std::make_tuple(__VA_ARGS__); }, #__VA_ARGS__)

namespace havel {

template<typename Tuple, size_t... I>
std::map<std::string, std::string> tupleToMap(const Tuple& t, std::index_sequence<I...>, const std::vector<std::string>& names) {
    std::map<std::string, std::string> out;
    ((out[names[I]] = toString(std::get<I>(t))), ...);
    return out;
}

inline std::vector<std::string> parseNames(const std::string& s) {
    std::vector<std::string> names;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t start = token.find_first_not_of(" \t\n\r");
        size_t end = token.find_last_not_of(" \t\n\r");
        names.push_back(token.substr(start, end - start + 1));
    }
    return names;
}

template<typename F>
auto makeObject(F f, const std::string& names) {
    auto tup = f();
    constexpr auto N = std::tuple_size_v<decltype(tup)>;
    return tupleToMap(tup, std::make_index_sequence<N>(), parseNames(names));
}

}
