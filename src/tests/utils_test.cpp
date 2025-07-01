#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <array>
#include "../utils/Util.hpp"
using namespace havel;

// Test function
void testChain() {
    std::vector<int> nums = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Complex chaining
    auto result = chain(nums)
        .map([](int x) { return x * 2; })      // {2, 4, 6, 8, 10, 12, 14, 16, 18, 20}
        .filter([](int x) { return x > 5; })   // {6, 8, 10, 12, 14, 16, 18, 20}
        .drop(1)                               // {8, 10, 12, 14, 16, 18, 20}
        .take(3)                               // {8, 10, 12}
        .sum();                                // 30

    std::cout << "Sum: " << result << "\n";

    // String operations
    std::vector<std::string> words = {"foo", "bar", "baz", "qux"};

    auto joined = chain(words)
        .map([](const auto& s) { return s + "!"; })
        .filter([](const auto& s) { return s.length() <= 4; })
        .join(" | ");

    std::cout << "Joined: " << joined << "\n";

    // More complex example
    auto stats = chain(nums)
    .filter([](int x) { return x % 2 == 0; })
    .map([](int x) { return x * x; });

    auto more_stats = stats.map([](int x) { return x + 1; });
    std::cout << "Even squares: ";
    more_stats.for_each([](int x) { std::cout << x << " "; });
    std::cout << "\n";

    std::cout << "Count: " << stats.count() << "\n";
    std::cout << "Max: " << stats.max() << "\n";
    std::cout << "Min: " << stats.min() << "\n";

    // Collect into different containers
    auto vec = stats.to_vector();
    auto set = stats.template collect<std::set>();
}
int main() {
    // Strings
    std::string msg = "  Hello ";
    std::cout << toUpper(trim(msg)) << "\n";

    // Vectors
    std::vector<int> nums = {1, 2, 3, 4, 5};
    auto squared = map(nums, [](int x) { return x * x; });
    std::cout << join(squared, " | ") << "\n";

    // Sets
    std::set<std::string> tags = {"alpha", "beta", "gamma"};
    std::cout << "Random tag: " << toString(choice(tags)) << "\n";

    // Maps
    std::map<std::string, int> dict = {{"a", 1}, {"b", 2}};
    std::cout << join(map(dict, [](auto& p) {
        return p.first + "=" + toString(p.second);
    }), ", ") << "\n";

    // Random
    std::cout << randint(1, 10) << " " << randfloat() << "\n";

    //auto user = OBJECT("name", "age", "score")("Alice", 22, 4.2);
    //printMap(user);
    // → {"name": "Alice", "age": "22", "score": "4.2"}
    std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
    printMap(m);
    // → {"a": "1", "b": "2"}
    for (int i : range(0, 10, 2))
        std::cout << i << " ";  // → 0 2 4 6 8
    std::cout << toJson(m) << "\n";
    // → {"name": "Alice", "age": "22", "score": "4.2"}
    char* c =  ::substring("Hello", 1, 3);
    std::cout << c << "\n";
    ::free(c);
    testChain();
    return 0;
}