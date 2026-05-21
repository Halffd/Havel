#include "ArrayModule.hpp"
#include "../compiler/vm/VMApi.hpp"
#include "../compiler/vm/VM.hpp"
#include <vector>
#include <stdexcept>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerArrayModule(const VMApi &api) {
  auto &vm = api.vm();
  Value exports;
  try {
    exports = vm.loadModule("array");
  } catch (...) {
    return;
  }

  if (!exports.isObjectId()) return;

  api.setGlobal("Array", exports);
  api.setGlobal("array", exports);

  auto *obj = vm.getHeap().object(exports.asObjectId());
  if (obj) {
    for (const auto& [name, value] : *obj) {
      if (name.empty() || name[0] == '_') continue;
      api.setGlobal(name, value);
    }
  }
}

} // namespace havel::stdlib
