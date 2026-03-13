/*
 * UtilityModule.cpp
 *
 * Utility functions for Havel language.
 * Provides keys, items, list, pairs, range functions.
 */
#include "UtilityModule.hpp"
#include "../havel-lang/runtime/Environment.hpp"

namespace havel::stdlib {

void registerUtilityModule(Environment& env) {

    // =========================================================================
    // keys(obj) - Get keys from object/map
    // =========================================================================
    env.Define("keys", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("keys() requires an object");
        }

        if (!args[0].isObject()) {
            return HavelRuntimeError("keys() requires an object argument");
        }

        auto obj = args[0].asObject();
        if (!obj) {
            return HavelRuntimeError("keys() failed to get object");
        }

        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto& [key, val] : *obj) {
            arr->push_back(HavelValue(key));
        }

        return HavelValue(arr);
    }));

    // =========================================================================
    // items(obj) - Get key-value pairs from object/map
    // =========================================================================
    env.Define("items", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("items() requires an object");
        }

        if (!args[0].isObject()) {
            return HavelRuntimeError("items() requires an object argument");
        }

        auto obj = args[0].asObject();
        if (!obj) {
            return HavelRuntimeError("items() failed to get object");
        }

        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto& [key, val] : *obj) {
            auto pair = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*pair)["key"] = HavelValue(key);
            (*pair)["value"] = val;
            arr->push_back(HavelValue(pair));
        }

        return HavelValue(arr);
    }));

    // =========================================================================
    // list(value) - Convert to list
    // =========================================================================
    env.Define("list", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("list() requires an argument");
        }

        const auto& arg = args[0];

        // If already an array, return it
        if (arg.isArray()) {
            return arg;
        }

        // If object, convert to array of keys
        if (arg.isObject()) {
            auto obj = arg.asObject();
            if (!obj) {
                return HavelRuntimeError("list() failed to get object");
            }
            
            // Convert to vector of pairs and sort by key for deterministic order
            std::vector<std::pair<std::string, HavelValue>> pairs(obj->begin(), obj->end());
            std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });

            auto arr = std::make_shared<std::vector<HavelValue>>();
            for (const auto& [key, val] : pairs) {
                arr->push_back(val);
            }
            return HavelValue(arr);
        }

        // If string, convert to array of characters
        if (arg.isString()) {
            auto str = arg.asString();
            auto arr = std::make_shared<std::vector<HavelValue>>();
            for (char c : str) {
                arr->push_back(HavelValue(std::string(1, c)));
            }
            return HavelValue(arr);
        }

        // Otherwise, wrap in array
        auto arr = std::make_shared<std::vector<HavelValue>>();
        arr->push_back(arg);
        return HavelValue(arr);
    }));

    // =========================================================================
    // pairs(obj) - Iterate over key-value pairs (alias for items)
    // =========================================================================
    env.Define("pairs", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("pairs() requires an object");
        }

        if (!args[0].isObject()) {
            return HavelRuntimeError("pairs() requires an object argument");
        }

        auto obj = args[0].asObject();
        if (!obj) {
            return HavelRuntimeError("pairs() failed to get object");
        }

        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto& [key, val] : *obj) {
            auto pair = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*pair)["key"] = HavelValue(key);
            (*pair)["value"] = val;
            arr->push_back(HavelValue(pair));
        }

        return HavelValue(arr);
    }));

    // =========================================================================
    // range(start, end?, step?) - Generate number sequence
    // =========================================================================
    env.Define("range", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("range() requires at least a start value");
        }

        double start = 0.0;
        double end = args[0].asNumber();
        double step = 1.0;

        if (args.size() >= 2) {
            start = args[0].asNumber();
            end = args[1].asNumber();
        }
        if (args.size() >= 3) {
            step = args[2].asNumber();
        }

        if (step == 0) {
            return HavelRuntimeError("range(): step cannot be zero");
        }

        auto arr = std::make_shared<std::vector<HavelValue>>();
        
        if (step > 0) {
            for (double i = start; i < end; i += step) {
                arr->push_back(HavelValue(i));
            }
        } else {
            for (double i = start; i > end; i += step) {
                arr->push_back(HavelValue(i));
            }
        }

        return HavelValue(arr);
    }));
}

} // namespace havel::stdlib
