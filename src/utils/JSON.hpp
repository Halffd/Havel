#include <map>
#include <unordered_map>
#include <sstream>
#include <iostream>

namespace havel {

template<typename K, typename V>
std::string toJson(const std::map<K, V>& m) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [k, v] : m) {
        if (!first) oss << ", ";
        oss << "\"" << k << "\": \"" << v << "\"";
        first = false;
    }
    oss << "}";
    return oss.str();
}

template<typename K, typename V>
std::string toJson(const std::unordered_map<K, V>& m) {
    return toJson(std::map<K, V>(m.begin(), m.end()));
}

template<typename K, typename V>
void printMap(const std::map<K, V>& m) {
    std::cout << toJson(m) << "\n";
}

template<typename K, typename V>
void printMap(const std::unordered_map<K, V>& m) {
    std::cout << toJson(m) << "\n";
}

}
