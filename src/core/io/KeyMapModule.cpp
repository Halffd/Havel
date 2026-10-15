// Loads the key table from the kymap Havel module into KeyMap.
//
// This lives apart from KeyMap.cpp because it needs the VM headers while
// KeyMap.hpp pulls in <linux/input-event-codes.h> and the X11-dependent IO
// stack; keeping them apart also keeps the VM-facing code out of the hot,
// X11-adjacent translation units.
#include "KeyMapModule.hpp"
#include "KeyMap.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"
#include "utils/DebugFlags.hpp"

namespace havel {

using compiler::ArrayRef;
using compiler::Value;
using compiler::VM;
using compiler::VMApi;

// Modules that are resolvable as "app/kymap" live under modules/app/.
static const char *kModuleName = "app/kymap";

void loadKeyMapFromHavelModule(VM &vm) {
  Value exports;
  try {
    exports = vm.loadModule(kModuleName);
  } catch (const std::exception &e) {
    warning("KeyMap: failed to load modules/{}.hv: {}", kModuleName, e.what());
    return;
  }

  if (!exports.isObjectId()) {
    warning("KeyMap: {} did not export an object", kModuleName);
    return;
  }

  VMApi api(vm);

  Value entriesFn = api.getField(exports, "allEntries");
  if (!entriesFn.isClosureId() && !entriesFn.isHostFuncId()) {
    warning("KeyMap: {} has no allEntries() export", kModuleName);
    return;
  }

  Value rows;
  try {
    rows = vm.callFunctionSyncPublic(entriesFn, {});
  } catch (const std::exception &e) {
    warning("KeyMap: allEntries() failed: {}", e.what());
    return;
  }

  if (!rows.isArrayId()) {
    warning("KeyMap: allEntries() did not return an array");
    return;
  }

  // Keep the resolved rows alive for the duration of the call: they are heap
  // values and hydrating triggers no allocation that could collect them, but a
  // root makes that guarantee explicit rather than incidental.
  VM::GCRoot rowsRoot = api.root(rows);

  std::vector<KeyRow> parsed;
  ArrayRef rowsRef{rows.asArrayId()};
  size_t count = vm.getHostArrayLength(rowsRef);
  parsed.reserve(count);

  for (size_t i = 0; i < count; i++) {
    Value row = vm.getHostArrayValue(rowsRef, i);
    if (!row.isObjectId()) {
      continue;
    }

    KeyRow out;
    out.name = api.toString(api.getField(row, "name"));
    if (out.name.empty()) {
      continue;
    }

    out.evdevCode = static_cast<int>(api.getField(row, "evdev").asInt());
    out.x11KeySym = static_cast<unsigned long>(api.getField(row, "x11").asInt());
    out.windowsVK = static_cast<int>(api.getField(row, "win").asInt());

    // Distinguishes an AddAlias row from an AddKey row. Missing/null means
    // false, so an older module without the field still loads as all-keys.
    Value isAlias = api.getField(row, "isAlias");
    out.isAlias = isAlias.isBool() ? isAlias.asBool() : false;

    Value aliases = api.getField(row, "aliases");
    if (aliases.isArrayId()) {
      ArrayRef aliasRef{aliases.asArrayId()};
      size_t acount = vm.getHostArrayLength(aliasRef);
      out.aliases.reserve(acount);
      for (size_t j = 0; j < acount; j++) {
        out.aliases.push_back(api.toString(vm.getHostArrayValue(aliasRef, j)));
      }
    }

    parsed.push_back(std::move(out));
  }

  if (parsed.empty()) {
    warning("KeyMap: {} produced no key rows", kModuleName);
    return;
  }

  if (debugging::debug_io)
    ::havel::debug("KeyMap: loaded {} key rows from modules/{}.hv", parsed.size(),
                   kModuleName);

  KeyMap::HydrateFromModule(parsed);
}

} // namespace havel