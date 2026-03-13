/*
 * ObjectModule.cpp
 *
 * Object manipulation functions for Havel standard library.
 */
#include "ObjectModule.hpp"
#include <algorithm>

namespace havel::stdlib {

void registerObjectModule(Environment& env) {

  // ============================================================================
  // Object static methods
  // ============================================================================

  // Object.keys(obj) - get array of keys (sorted for deterministic order)
  env.Define("Object.keys", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("Object.keys() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("Object.keys() arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("Object.keys() failed to get object");

    auto keys = std::make_shared<std::vector<HavelValue>>();
    for (const auto& [key, val] : *obj) {
      keys->push_back(HavelValue(key));
    }
    // Sort keys alphabetically for deterministic order
    std::sort(keys->begin(), keys->end(), [](const HavelValue& a, const HavelValue& b) {
      return a.asString() < b.asString();
    });
    return HavelValue(keys);
  }));

  // Object.values(obj) - get array of values (sorted by key for deterministic order)
  env.Define("Object.values", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("Object.values() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("Object.values() arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("Object.values() failed to get object");

    // Collect key-value pairs and sort by key
    std::vector<std::pair<std::string, HavelValue>> pairs;
    for (const auto& [key, val] : *obj) {
      pairs.push_back({key, val});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
      return a.first < b.first;
    });

    auto values = std::make_shared<std::vector<HavelValue>>();
    for (const auto& [key, val] : pairs) {
      values->push_back(val);
    }
    return HavelValue(values);
  }));

  // Object.entries(obj) - get array of [key, value] pairs (sorted by key)
  env.Define("Object.entries", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("Object.entries() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("Object.entries() arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("Object.entries() failed to get object");

    // Collect and sort by key
    std::vector<std::pair<std::string, HavelValue>> pairs;
    for (const auto& [key, val] : *obj) {
      pairs.push_back({key, val});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
      return a.first < b.first;
    });

    auto entries = std::make_shared<std::vector<HavelValue>>();
    for (const auto& [key, val] : pairs) {
      auto entry = std::make_shared<std::vector<HavelValue>>();
      entry->push_back(HavelValue(key));
      entry->push_back(val);
      entries->push_back(HavelValue(entry));
    }
    return HavelValue(entries);
  }));

  // Object.assign(target, ...sources) - copy properties from sources to target
  env.Define("Object.assign", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("Object.assign() requires target object");
    if (!args[0].isObject()) return HavelRuntimeError("Object.assign() target must be object");

    auto target = args[0].asObject();
    if (!target) return HavelRuntimeError("Object.assign() failed to get target");

    for (size_t i = 1; i < args.size(); ++i) {
      if (args[i].isObject()) {
        auto source = args[i].asObject();
        if (source) {
          for (const auto& [key, val] : *source) {
            (*target)[key] = val;
          }
        }
      }
    }
    return HavelValue(target);
  }));

  // Object.hasOwn(obj, key) - check if object has own property
  env.Define("Object.hasOwn", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("Object.hasOwn() requires (obj, key)");
    if (!args[0].isObject()) return HavelRuntimeError("Object.hasOwn() first arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("Object.hasOwn() failed to get object");

    std::string key;
    if (args[1].isString()) {
      key = args[1].asString();
    } else if (args[1].isNumber()) {
      key = std::to_string(static_cast<long long>(args[1].asNumber()));
    } else {
      return HavelRuntimeError("Object.hasOwn() key must be string or number");
    }

    return HavelValue(obj->find(key) != obj->end());
  }));

  // Object.freeze(obj) - mark object as immutable (shallow freeze)
  env.Define("Object.freeze", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("Object.freeze() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("Object.freeze() arg must be object");
    // Note: This is a no-op for now, just returns the object
    // Full implementation would require marking the object as frozen
    return args[0];
  }));

  // Object.seal(obj) - seal object (prevent new properties)
  env.Define("Object.seal", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("Object.seal() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("Object.seal() arg must be object");
    // Note: This is a no-op for now
    return args[0];
  }));

  // Object.fromEntries(entries) - create object from entries array
  env.Define("Object.fromEntries", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("Object.fromEntries() requires entries array");
    if (!args[0].isArray()) return HavelRuntimeError("Object.fromEntries() arg must be array");

    auto entries = args[0].asArray();
    if (!entries) return HavelRuntimeError("Object.fromEntries() failed to get array");

    auto obj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    for (const auto& entry : *entries) {
      if (entry.isArray()) {
        auto pair = entry.asArray();
        if (pair && pair->size() >= 2) {
          std::string key;
          if ((*pair)[0].isString()) {
            key = (*pair)[0].asString();
          } else if ((*pair)[0].isNumber()) {
            key = std::to_string(static_cast<long long>((*pair)[0].asNumber()));
          }
          if (!key.empty()) {
            (*obj)[key] = (*pair)[1];
          }
        }
      }
    }
    return HavelValue(obj);
  }));

  // Object.getOwnPropertyDescriptor(obj, key) - get property descriptor
  env.Define("Object.getOwnPropertyDescriptor", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.size() < 2) return HavelRuntimeError("Object.getOwnPropertyDescriptor() requires (obj, key)");
    if (!args[0].isObject()) return HavelRuntimeError("Object.getOwnPropertyDescriptor() first arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("Object.getOwnPropertyDescriptor() failed to get object");

    std::string key;
    if (args[1].isString()) {
      key = args[1].asString();
    } else {
      return HavelRuntimeError("Object.getOwnPropertyDescriptor() key must be string");
    }

    auto it = obj->find(key);
    if (it == obj->end()) {
      return HavelValue(nullptr);
    }

    auto descriptor = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    (*descriptor)["value"] = it->second;
    (*descriptor)["writable"] = HavelValue(true);
    (*descriptor)["enumerable"] = HavelValue(true);
    (*descriptor)["configurable"] = HavelValue(true);
    return HavelValue(descriptor);
  }));

  // Object.keys equivalent as standalone function (for pipeline style, sorted)
  env.Define("keys", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("keys() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("keys() arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("keys() failed to get object");

    auto keys = std::make_shared<std::vector<HavelValue>>();
    for (const auto& [key, val] : *obj) {
      keys->push_back(HavelValue(key));
    }
    // Sort keys alphabetically for deterministic order
    std::sort(keys->begin(), keys->end(), [](const HavelValue& a, const HavelValue& b) {
      return a.asString() < b.asString();
    });
    return HavelValue(keys);
  }));

  // Object.values equivalent as standalone function (sorted by key)
  env.Define("values", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("values() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("values() arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("values() failed to get object");

    // Collect key-value pairs and sort by key
    std::vector<std::pair<std::string, HavelValue>> pairs;
    for (const auto& [key, val] : *obj) {
      pairs.push_back({key, val});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
      return a.first < b.first;
    });

    auto values = std::make_shared<std::vector<HavelValue>>();
    for (const auto& [key, val] : pairs) {
      values->push_back(val);
    }
    return HavelValue(values);
  }));

  // Object.entries equivalent as standalone function (sorted by key)
  env.Define("entries", BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
    if (args.empty()) return HavelRuntimeError("entries() requires object");
    if (!args[0].isObject()) return HavelRuntimeError("entries() arg must be object");

    auto obj = args[0].asObject();
    if (!obj) return HavelRuntimeError("entries() failed to get object");

    // Collect and sort by key
    std::vector<std::pair<std::string, HavelValue>> pairs;
    for (const auto& [key, val] : *obj) {
      pairs.push_back({key, val});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
      return a.first < b.first;
    });

    auto entries = std::make_shared<std::vector<HavelValue>>();
    for (const auto& [key, val] : pairs) {
      auto entry = std::make_shared<std::vector<HavelValue>>();
      entry->push_back(HavelValue(key));
      entry->push_back(val);
      entries->push_back(HavelValue(entry));
    }
    return HavelValue(entries);
  }));
}

} // namespace havel::stdlib
