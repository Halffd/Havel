#include "PrototypeRegistry.hpp"

namespace havel::compiler::prototypes {

void registerStringPrototype(VM& vm) {
  auto regProto = [&vm](const std::string& method, size_t arity, BytecodeHostFunction fn) {
    vm.registerHostFunction("string." + method, arity, std::move(fn));
    vm.registerPrototypeMethodByName("string", method, "string." + method);
  };

  regProto("len", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeInt(0);
    const auto& v = args[0];
    if (v.isStringValId() && vm.getCurrentChunk()) {
      return Value::makeInt(static_cast<int64_t>(vm.getCurrentChunk()->getString(v.asStringValId()).size()));
    }
    if (v.isStringId()) {
      if (auto *s = vm.getHeap().string(v.asStringId())) return Value::makeInt(static_cast<int64_t>(s->size()));
    }
    return Value::makeInt(0);
  });

  regProto("upper", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s;
    if (args[0].isStringValId() && vm.getCurrentChunk()) s = vm.getCurrentChunk()->getString(args[0].asStringValId());
    else if (args[0].isStringId() && vm.getHeap().string(args[0].asStringId())) s = *vm.getHeap().string(args[0].asStringId());
    else return Value::makeNull();
    std::string result = s;
    for (auto& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto ref = vm.getHeap().allocateString(std::move(result));
    return Value::makeStringId(ref.id);
  });

  regProto("lower", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s;
    if (args[0].isStringValId() && vm.getCurrentChunk()) s = vm.getCurrentChunk()->getString(args[0].asStringValId());
    else if (args[0].isStringId() && vm.getHeap().string(args[0].asStringId())) s = *vm.getHeap().string(args[0].asStringId());
    else return Value::makeNull();
    std::string result = s;
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto ref = vm.getHeap().allocateString(std::move(result));
    return Value::makeStringId(ref.id);
  });

  regProto("has", 2, [&vm](const std::vector<Value>& args) {
    if (args.size() < 2) return Value::makeBool(false);
    std::string s, sub;
    if (args[0].isStringValId() && vm.getCurrentChunk()) s = vm.getCurrentChunk()->getString(args[0].asStringValId());
    else if (args[0].isStringId() && vm.getHeap().string(args[0].asStringId())) s = *vm.getHeap().string(args[0].asStringId());
    if (args[1].isStringValId() && vm.getCurrentChunk()) sub = vm.getCurrentChunk()->getString(args[1].asStringValId());
    else if (args[1].isStringId() && vm.getHeap().string(args[1].asStringId())) sub = *vm.getHeap().string(args[1].asStringId());
    return Value::makeBool(!s.empty() && !sub.empty() && s.find(sub) != std::string::npos);
  });

  regProto("split", 2, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s;
    if (args[0].isStringValId() && vm.getCurrentChunk()) s = vm.getCurrentChunk()->getString(args[0].asStringValId());
    else if (args[0].isStringId() && vm.getHeap().string(args[0].asStringId())) s = *vm.getHeap().string(args[0].asStringId());
    else return Value::makeNull();
    std::string delim = ",";
    if (args.size() > 1 && args[1].isStringValId() && vm.getCurrentChunk()) delim = vm.getCurrentChunk()->getString(args[1].asStringValId());
    auto arrRef = vm.getHeap().allocateArray();
    auto* arr = vm.getHeap().array(arrRef.id);
    size_t pos = 0, prev = 0;
    while ((pos = s.find(delim, prev)) != std::string::npos) {
      auto ref = vm.getHeap().allocateString(s.substr(prev, pos - prev));
      arr->push_back(Value::makeStringId(ref.id));
      prev = pos + delim.size();
    }
    auto ref = vm.getHeap().allocateString(s.substr(prev));
    arr->push_back(Value::makeStringId(ref.id));
    return Value::makeArrayId(arrRef.id);
  });

  regProto("trim", 1, [&vm](const std::vector<Value>& args) {
    if (args.empty()) return Value::makeNull();
    std::string s;
    if (args[0].isStringValId() && vm.getCurrentChunk()) s = vm.getCurrentChunk()->getString(args[0].asStringValId());
    else if (args[0].isStringId() && vm.getHeap().string(args[0].asStringId())) s = *vm.getHeap().string(args[0].asStringId());
    else return Value::makeNull();
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) { auto ref = vm.getHeap().allocateString(""); return Value::makeStringId(ref.id); }
    size_t end = s.find_last_not_of(" \t\n\r");
    auto ref = vm.getHeap().allocateString(s.substr(start, end - start + 1));
    return Value::makeStringId(ref.id);
  });

  regProto("sub", 3, [&vm](const std::vector<Value>& args) {
    if (args.size() < 3) return Value::makeNull();
    std::string s;
    if (args[0].isStringValId() && vm.getCurrentChunk()) s = vm.getCurrentChunk()->getString(args[0].asStringValId());
    else if (args[0].isStringId() && vm.getHeap().string(args[0].asStringId())) s = *vm.getHeap().string(args[0].asStringId());
    else return Value::makeNull();
    int64_t start = args[1].isInt() ? args[1].asInt() : 0;
    int64_t len = args[2].isInt() ? args[2].asInt() : static_cast<int64_t>(s.size());
    if (start < 0) start = std::max(static_cast<int64_t>(0), static_cast<int64_t>(s.size()) + start);
    auto ref = vm.getHeap().allocateString(s.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
    return Value::makeStringId(ref.id);
  });
}

} // namespace havel::compiler::prototypes
