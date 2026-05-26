#include "ArrayModule.hpp"
#include "../compiler/vm/VMApi.hpp"
#include "../compiler/vm/VM.hpp"
#include <vector>
#include <stdexcept>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerArrayModule(const VMApi &api) {
  // array.insert(arr, index, value) - Insert value at index
  api.registerFunction("array.insert", [api](const std::vector<Value> &args) {
    if (args.size() < 3)
      throw std::runtime_error("array.insert() requires array, index, and value");
    if (!args[0].isArrayId())
      throw std::runtime_error("array.insert() first arg must be array");

    uint32_t index = static_cast<uint32_t>(args[1].asInt());
    api.insertAt(args[0], index, args[2]);
    return args[0];
  });

  // array.remove(arr, index) - Remove and return value at index
  api.registerFunction("array.remove", [api](const std::vector<Value> &args) {
    if (args.size() < 2)
      throw std::runtime_error("array.remove() requires array and index");
    if (!args[0].isArrayId())
      throw std::runtime_error("array.remove() first arg must be array");

    uint32_t index = static_cast<uint32_t>(args[1].asInt());
    return api.removeAt(args[0], index);
  });

  // Prototype methods for arrays
  api.registerPrototypeMethodByName("array", "insert", "array.insert");
  api.registerPrototypeMethodByName("array", "remove", "array.remove");

  // Register global Array/array namespace
  auto arrObj = api.makeObject();
  api.setField(arrObj, "insert", api.makeFunctionRef("array.insert"));
  api.setField(arrObj, "remove", api.makeFunctionRef("array.remove"));

  // Load Havel-side array module for additional methods
  auto &vm = api.vm();
  Value exports;
  try {
    exports = vm.loadModule("array");
  } catch (...) {
    api.setGlobal("Array", arrObj);
    api.setGlobal("array", arrObj);
    return;
  }

  if (exports.isObjectId()) {
    auto *obj = vm.getHeap().object(exports.asObjectId());
    if (obj) {
      for (const auto& [name, value] : *obj) {
        if (name.empty() || name[0] == '_') continue;
        api.setField(arrObj, name, value);
        api.setGlobal(name, value);
      }
    }
  }

  api.setGlobal("Array", arrObj);
  api.setGlobal("array", arrObj);
}

} // namespace havel::stdlib
