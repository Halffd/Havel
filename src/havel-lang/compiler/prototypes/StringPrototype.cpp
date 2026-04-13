#include "PrototypeRegistry.hpp"
#include <regex>
#include <sstream>

namespace havel::compiler::prototypes {

// Helper: extract string from a Value
static std::string extractString(VM& vm, const Value& v) {
  if (v.isStringValId() && vm.getCurrentChunk()) return vm.getCurrentChunk()->getString(v.asStringValId());
  if (v.isStringId() && vm.getHeap().string(v.asStringId())) return *vm.getHeap().string(v.asStringId());
  return "";
}

// Helper: extract string from args[i] with fallback
static std::string extractStringArg(VM& vm, const std::vector<Value>& args, size_t i, const std::string& fallback) {
  if (i >= args.size()) return fallback;
  if (args[i].isStringValId() && vm.getCurrentChunk()) return vm.getCurrentChunk()->getString(args[i].asStringValId());
  if (args[i].isStringId() && vm.getHeap().string(args[i].asStringId())) return *vm.getHeap().string(args[i].asStringId());
  return fallback;
}

// Helper: allocate a string on the heap
static Value makeString(VM& vm, std::string s) {
  auto ref = vm.getHeap().allocateString(std::move(s));
  return Value::makeStringId(ref.id);
}

// Helper: allocate an array on the heap
static Value makeArray(VM& vm, const std::vector<Value>& items) {
  auto arrRef = vm.getHeap().allocateArray();
  auto* arr = vm.getHeap().array(arrRef.id);
  for (const auto& item : items) {
    arr->push_back(item);
  }
  return Value::makeArrayId(arrRef.id);
}

void registerStringPrototype(VM& vm) {
  auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("string." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("string", method, "string." + method);
  };

  // Variable-arity helper
  auto regProtoVar = [&vm](const std::string& method, BytecodeHostFunction fn) {
    vm.registerHostFunction("string." + method, std::move(fn));
    vm.registerPrototypeMethodByName("string", method, "string." + method);
  };

  // Also register capital as a standalone host function for use in pipes
  vm.registerHostFunction("capital", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    if (s.empty()) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    auto ref = vm.getHeap().allocateString(std::move(s));
    return Value::makeStringId(ref.id);
  });

  regProto("len", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeInt(0);
    return Value::makeInt(static_cast<int64_t>(extractString(vm, args[0]).size()));
  });

  regProto("upper", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    if (s.empty()) return Value::makeNull();
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto ref = vm.getHeap().allocateString(std::move(s));
    return Value::makeStringId(ref.id);
  });

  regProto("lower", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    if (s.empty()) return Value::makeNull();
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto ref = vm.getHeap().allocateString(std::move(s));
    return Value::makeStringId(ref.id);
  });

  regProto("has", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    std::string s = extractString(vm, args[0]), sub = extractStringArg(vm, args, 1, "");
    return Value::makeBool(!s.empty() && !sub.empty() && s.find(sub) != std::string::npos);
  });

  regProto("includes", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    std::string s = extractString(vm, args[0]), sub = extractStringArg(vm, args, 1, "");
    return Value::makeBool(!s.empty() && !sub.empty() && s.find(sub) != std::string::npos);
  });

  regProto("split", 2, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    std::string delim = extractStringArg(vm, args, 1, ",");

    // Check if delimiter is a regex string
    bool isRegex = !delim.empty() && delim.front() == '/' && delim.size() > 2 && delim.back() == '/';
    if (isRegex) {
      std::string regexPattern = delim.substr(1, delim.size() - 2);
      try {
        std::regex re(regexPattern);
        std::vector<Value> parts;
        auto it = std::sregex_token_iterator(s.begin(), s.end(), re, -1);
        auto end = std::sregex_token_iterator();
        while (it != end) {
          parts.push_back(makeString(vm, *it));
          ++it;
        }
        return makeArray(vm, parts);
      } catch (const std::regex_error&) {
        // Fall back to plain string split
      }
    }

    // Plain string split
    auto arrRef = vm.getHeap().allocateArray();
    auto* arr = vm.getHeap().array(arrRef.id);
    size_t pos = 0, prev = 0;
    while ((pos = s.find(delim, prev)) != std::string::npos) {
      arr->push_back(makeString(vm, s.substr(prev, pos - prev)));
      prev = pos + delim.size();
    }
    arr->push_back(makeString(vm, s.substr(prev)));
    return Value::makeArrayId(arrRef.id);
  });

  regProto("trim", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    size_t end = s.find_last_not_of(" \t\n\r");
    auto ref = vm.getHeap().allocateString(s.substr(start, end - start + 1));
    return Value::makeStringId(ref.id);
  });

  regProto("capital", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    if (s.empty()) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    auto ref = vm.getHeap().allocateString(std::move(s));
    return Value::makeStringId(ref.id);
  });

  regProto("sub", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    int64_t start = args[1].isInt() ? args[1].asInt() : 0;
    int64_t len = args[2].isInt() ? args[2].asInt() : static_cast<int64_t>(s.size());
    if (start < 0) start = std::max(static_cast<int64_t>(0), static_cast<int64_t>(s.size()) + start);
    auto ref = vm.getHeap().allocateString(s.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
    return Value::makeStringId(ref.id);
  });

  regProto("startsWith", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    std::string s = extractString(vm, args[0]), prefix = extractStringArg(vm, args, 1, "");
    return Value::makeBool(s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0);
  });

  regProto("endsWith", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    std::string s = extractString(vm, args[0]), suffix = extractStringArg(vm, args, 1, "");
    if (s.size() < suffix.size()) return Value::makeBool(false);
    return Value::makeBool(s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
  });

  regProto("repeat", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    int64_t count = args[1].isInt() ? args[1].asInt() : 0;
    if (count <= 0) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    std::string result;
    result.reserve(s.size() * count);
    for (int64_t i = 0; i < count; ++i) result += s;
    auto ref = vm.getHeap().allocateString(std::move(result));
    return Value::makeStringId(ref.id);
  });

  regProto("padStart", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    std::string s = extractString(vm, args[0]), pad = extractStringArg(vm, args, 2, " ");
    int64_t targetLen = args[1].isInt() ? args[1].asInt() : static_cast<int64_t>(s.size());
    if (static_cast<int64_t>(s.size()) >= targetLen) { auto ref = vm.getHeap().allocateString(s); return Value::makeStringId(ref.id); }
    std::string result;
    result.reserve(targetLen);
    int64_t needed = targetLen - s.size();
    while (static_cast<int64_t>(result.size()) < needed) result += pad;
    result.resize(needed);
    result += s;
    auto ref = vm.getHeap().allocateString(std::move(result));
    return Value::makeStringId(ref.id);
  });

  regProto("padEnd", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    std::string s = extractString(vm, args[0]), pad = extractStringArg(vm, args, 2, " ");
    int64_t targetLen = args[1].isInt() ? args[1].asInt() : static_cast<int64_t>(s.size());
    if (static_cast<int64_t>(s.size()) >= targetLen) { auto ref = vm.getHeap().allocateString(s); return Value::makeStringId(ref.id); }
    std::string result = s;
    result.reserve(targetLen);
    while (static_cast<int64_t>(result.size()) < targetLen) result += pad;
    result.resize(targetLen);
    auto ref = vm.getHeap().allocateString(std::move(result));
    return Value::makeStringId(ref.id);
  });

  regProto("left", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    int64_t n = args[1].isInt() ? args[1].asInt() : 0;
    if (n <= 0) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    if (n > static_cast<int64_t>(s.size())) n = s.size();
    auto ref = vm.getHeap().allocateString(s.substr(0, n));
    return Value::makeStringId(ref.id);
  });

  regProto("right", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    int64_t n = args[1].isInt() ? args[1].asInt() : 0;
    if (n <= 0) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    if (n > static_cast<int64_t>(s.size())) n = s.size();
    auto ref = vm.getHeap().allocateString(s.substr(s.size() - n));
    return Value::makeStringId(ref.id);
  });

  regProto("count", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeInt(0);
    std::string s = extractString(vm, args[0]), sub = extractStringArg(vm, args, 1, "");
    if (sub.empty()) return Value::makeInt(0);
    int64_t count = 0;
    size_t pos = 0;
    while ((pos = s.find(sub, pos)) != std::string::npos) { count++; pos += sub.size(); }
    return Value::makeInt(count);
  });

  regProto("indexOf", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeInt(-1);
    std::string s = extractString(vm, args[0]), sub = extractStringArg(vm, args, 1, "");
    size_t pos = s.find(sub);
    return Value::makeInt(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
  });

  regProto("find", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    std::string s = extractString(vm, args[0]), sub = extractStringArg(vm, args, 1, "");
    size_t pos = s.find(sub);
    if (pos == std::string::npos) return Value::makeNull();
    return Value::makeInt(static_cast<int64_t>(pos));
  });

  regProto("replace", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    std::string pattern = extractStringArg(vm, args, 1, "");
    std::string replacement = extractStringArg(vm, args, 2, "");

    // Check if pattern is a regex string (has regex flags marker or looks like /pattern/)
    bool isRegex = !pattern.empty() && pattern.front() == '/' && pattern.size() > 2 && pattern.back() == '/';
    if (isRegex) {
      std::string regexPattern = pattern.substr(1, pattern.size() - 2);
      try {
        std::regex re(regexPattern);
        std::string result = std::regex_replace(s, re, replacement);
        return makeString(vm, std::move(result));
      } catch (const std::regex_error&) {
        return Value::makeNull();
      }
    }

    // Plain string replacement (first occurrence)
    size_t pos = s.find(pattern);
    if (pos == std::string::npos) return makeString(vm, s);
    s.replace(pos, pattern.size(), replacement);
    return makeString(vm, std::move(s));
  });

  // match: "hello".match(r"l+") -> 2 (first match index) or null
  // Also returns array of all match groups when there are captures
  regProto("match", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    std::string pattern = extractStringArg(vm, args, 1, "");

    // Check if pattern is a regex string
    bool isRegex = !pattern.empty() && pattern.front() == '/' && pattern.size() > 2 && pattern.back() == '/';
    std::string regexPattern;
    if (isRegex) {
      regexPattern = pattern.substr(1, pattern.size() - 2);
    } else {
      regexPattern = std::regex_replace(pattern, std::regex(R"([.^$|()\\[\]{}*+?])"), R"(\$&)");
    }

    try {
      std::regex re(regexPattern);
      std::smatch m;
      if (std::regex_search(s, m, re)) {
        // If there are capture groups, return array of groups
        if (m.size() > 1) {
          std::vector<Value> groups;
          for (size_t i = 1; i < m.size(); ++i) {
            groups.push_back(makeString(vm, m[i].str()));
          }
          return makeArray(vm, groups);
        }
        // Otherwise return match index
        return Value::makeInt(static_cast<int64_t>(m.position(0)));
      }
    } catch (const std::regex_error&) {
      // Fall back to plain string find
      size_t pos = s.find(pattern);
      if (pos != std::string::npos) return Value::makeInt(static_cast<int64_t>(pos));
    }
    return Value::makeNull();
  });

  regProto("slice", 4, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    if (s.empty()) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }

    int64_t sz = static_cast<int64_t>(s.size());

    // Check which args are specified
    bool start_specified = args.size() > 1 && !args[1].isNull();
    bool end_specified = args.size() > 2 && !args[2].isNull();
    bool step_specified = args.size() > 3 && !args[3].isNull();

    // Parse step first (affects defaults for start/end)
    int64_t step = 1;
    if (step_specified) {
      step = args[3].isInt() ? args[3].asInt() : 1;
      if (step == 0) step = 1;
    }

    // Parse start with proper defaults
    int64_t start;
    if (start_specified) {
      start = args[1].isInt() ? args[1].asInt() : 0;
      if (start < 0) start = sz + start;
      if (start < 0) start = 0;
      if (start > sz) start = sz;
    } else {
      start = (step > 0) ? 0 : sz - 1;
    }

    // Parse end with proper defaults
    int64_t end;
    if (end_specified) {
      end = args[2].isInt() ? args[2].asInt() : sz;
      if (end < 0) end = sz + end;
      if (end < -1) end = -1;
      if (end > sz) end = sz;
    } else {
      end = (step > 0) ? sz : -1;
    }

    // Check bounds
    if (step > 0 && start >= end) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    if (step < 0 && start <= end) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }

    std::string result;
    if (step > 0) {
      for (int64_t i = start; i < end; i += step) {
        result += s[static_cast<size_t>(i)];
      }
    } else {
      for (int64_t i = start; i > end; i += step) {
        result += s[static_cast<size_t>(i)];
      }
    }
    auto ref = vm.getHeap().allocateString(std::move(result));
    return Value::makeStringId(ref.id);
  });

  // substr alias for sub
  regProto("substr", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    int64_t start = args[1].isInt() ? args[1].asInt() : 0;
    int64_t len = args[2].isInt() ? args[2].asInt() : static_cast<int64_t>(s.size());
    if (start < 0) start = std::max(static_cast<int64_t>(0), static_cast<int64_t>(s.size()) + start);
    auto ref = vm.getHeap().allocateString(s.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
    return Value::makeStringId(ref.id);
  });

  // concat: "hello".concat(" world") -> "hello world"
  regProto("concat", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    std::string other = extractStringArg(vm, args, 1, "");
    auto ref = vm.getHeap().allocateString(s + other);
    return Value::makeStringId(ref.id);
  });

  // reversed: "hello".reversed() -> "olleh"
  regProto("reversed", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s = extractString(vm, args[0]);
    std::reverse(s.begin(), s.end());
    auto ref = vm.getHeap().allocateString(std::move(s));
    return Value::makeStringId(ref.id);
  });

}
} // namespace havel::compiler::prototypes
