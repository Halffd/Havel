#include "core/io/KeyMapModule.hpp"
#include "core/io/KeyMap.hpp"
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

namespace {

// Parse the rows array the kymap module passes at load time. Same shape
// allEntries() always produced: one object per name with name/evdev/x11/
// win/aliases/isAlias.
std::vector<KeyRow> parseRows(VM &vm, VMApi &api, const Value &rows) {
  std::vector<KeyRow> parsed;
  if (!rows.isArrayId()) {
    warning("KeyMap: installRows did not receive an array");
    return parsed;
  }
  ArrayRef rowsRef{rows.asArrayId()};
  const size_t count = vm.getHostArrayLength(rowsRef);
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
    out.evdevCode =
        static_cast<int>(api.getField(row, "evdev").asInt());
    out.x11KeySym =
        static_cast<unsigned long>(api.getField(row, "x11").asInt());
    out.windowsVK = static_cast<int>(api.getField(row, "win").asInt());

    // Distinguishes an AddAlias row from an AddKey row. Missing/null
    // means false, so an older module without the field still loads as
    // all-keys.
    Value isAlias = api.getField(row, "isAlias");
    out.isAlias = isAlias.isBool() ? isAlias.asBool() : false;

    Value aliases = api.getField(row, "aliases");
    if (aliases.isArrayId()) {
      ArrayRef aliasRef{aliases.asArrayId()};
      size_t acount = vm.getHostArrayLength(aliasRef);
      out.aliases.reserve(acount);
      for (size_t j = 0; j < acount; j++) {
        out.aliases.push_back(
            api.toString(vm.getHostArrayValue(aliasRef, j)));
      }
    }

    parsed.push_back(std::move(out));
  }
  return parsed;
}

} // namespace

void loadKeyMapFromHavelModule(VM &vm) {
  // The module installs its own rows at load time by calling the
  // keymap.installRows host function with allEntries() as an argument.
  // Host-function arguments are pinned by the calling machinery in every
  // context; the previous C++-side callFunctionSyncPublic(allEntries)
  // return path came back with an empty array in Havel::initialize
  // contexts (REPL, script+repl, daemon) while working from
  // HavelEngine, so those modes ran unhydrated and hotkeys silently
  // mis-resolved (physical key events never matched their bindings).
  VMApi api(vm);
  vm.registerHostFunction(
      "keymap.installRows",
      [&vm, &api](const std::vector<Value> &args) -> Value {
        if (args.empty()) {
          warning("KeyMap: keymap.installRows() called with no rows");
          return Value::makeBool(false);
        }
        auto parsed = parseRows(vm, api, args[0]);
        if (parsed.empty()) {
          warning("KeyMap: {} produced no key rows", kModuleName);
          return Value::makeBool(false);
        }
        if (debugging::debug_io) {
          ::havel::debug("KeyMap: loaded {} key rows from modules/{}.hv",
                         parsed.size(), kModuleName);
        }
        KeyMap::HydrateFromModule(parsed);
        return Value::makeBool(true);
      });

  // Dotted host functions resolve through namespace objects created by
  // buildNamespaceGlobals(); that ran during Modules::install() before
  // keymap.installRows existed, so rebuild the namespaces so the module
  // body can see the keymap object.
  vm.buildNamespaceGlobals();

  Value exports;
  try {
    exports = vm.loadModule(kModuleName);
  } catch (const std::exception &e) {
    warning("KeyMap: failed to load modules/{}.hv: {}", kModuleName,
            e.what());
    return;
  }
  (void)exports;

  if (!KeyMap::IsHydrated()) {
    warning("KeyMap: {} loaded without installing any rows", kModuleName);
  }
}

} // namespace havel
