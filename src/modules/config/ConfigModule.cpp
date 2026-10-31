/*
 * ConfigModule.cpp - Configuration module for bytecode VM
 * Provides config.get, config.set, config.save, config.has, config.list
 * Supports autovivification (__vivify) and auto-save (__autosave_root)
 */
#include "ConfigModule.hpp"
#include "core/config/ConfigManager.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "utils/Logger.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static std::string valueToString(const VMApi &api, const Value &v) {
  return api.toString(v);
}

static Value stringToValue(const VMApi &api, const std::string &s) {
  if (s == "true")
    return Value::makeBool(true);
  if (s == "false")
    return Value::makeBool(false);

  try {
    size_t pos;
    int64_t i = std::stoll(s, &pos);
    if (pos == s.length())
      return Value::makeInt(i);
  } catch (...) {
  }

  try {
    size_t pos;
    double d = std::stod(s, &pos);
    if (pos == s.length())
      return Value::makeDouble(d);
  } catch (...) {
  }

  return api.makeString(s);
}

static void setNestedField(const VMApi &api, Value obj, const std::string &key, Value value);

static std::vector<Value> getActualArgs(const VMApi &api, const std::vector<Value> &args) {
  if (args.empty())
    return args;
  if (args[0].isObjectId()) {
    try {
      if (api.hasField(args[0], "__vivify") || api.hasField(args[0], "__autosave_root")) {
        std::vector<Value> actual;
        for (size_t i = 1; i < args.size(); ++i) {
          actual.push_back(args[i]);
        }
        return actual;
      }
    } catch (...) {
    }
  }
  return args;
}

Value configGet(const VMApi &api, const std::vector<Value> &args) {
  auto actual = getActualArgs(api, args);
  if (actual.empty())
    throw std::runtime_error("config.get() requires key");
  std::string key = valueToString(api, actual[0]);
  std::string defaultVal = (actual.size() > 1) ? valueToString(api, actual[1]) : "";
  return stringToValue(api, Configs::Get().Get<std::string>(key, defaultVal));
}

Value configSet(const VMApi &api, const std::vector<Value> &args) {
  auto actual = getActualArgs(api, args);
  if (actual.size() < 2)
    throw std::runtime_error("config.set() requires key, value");
  Configs::Get().Set(valueToString(api, actual[0]), valueToString(api, actual[1]), true);
  return Value::makeBool(true);
}

Value configSave(const std::vector<Value> &args) {
  (void)args;
  // Returns a report rather than a bare bool: "saved" and "there was
  // nothing to save" are different outcomes, and a caller that overwrites
  // settings needs to see how many keys the write changed or dropped.
  HavelConfigSaveReport rep = Configs::Get().SaveWithReport();
  return Value::makeBool(rep.written);
}

static Value reportToObject(const VMApi &api, const HavelConfigSaveReport &rep) {
  Value o = api.makeObject();
  api.setField(o, "written", Value::makeBool(rep.written));
  api.setField(o, "recovered", Value::makeBool(rep.recovered));
  api.setField(o, "added", Value::makeInt(static_cast<int64_t>(rep.added)));
  api.setField(o, "removed", Value::makeInt(static_cast<int64_t>(rep.removed)));
  api.setField(o, "changed", Value::makeInt(static_cast<int64_t>(rep.changed)));
  api.setField(o, "duplicateSections", Value::makeInt(static_cast<int64_t>(rep.duplicates)));
  api.setField(o, "malformedLines", Value::makeInt(static_cast<int64_t>(rep.malformed)));
  api.setField(o, "backup", api.makeString(rep.backup));
  return o;
}

static Value configStatus(const VMApi &api) {
  Value o = api.makeObject();
  api.setField(o, "path", api.makeString(Configs::Get().Path()));
  api.setField(o, "keyCount",
               Value::makeInt(static_cast<int64_t>(Configs::Get().GetAllKeys().size())));
  api.setField(o, "pendingDiff", reportToObject(api, Configs::Get().DiffToDisk()));
  return o;
}

static Value configValidate(const VMApi &api, const std::vector<Value> &args) {
  std::string filename = args.empty() ? "havel.cfg" : api.toString(args[0]);
  size_t dups = 0, bad = 0;
  int problems = Configs::Get().Validate(filename, &dups, &bad);
  Value o = api.makeObject();
  // -1 means "file does not exist", which is not the same as "broken".
  api.setField(o, "exists", Value::makeBool(problems >= 0));
  api.setField(o, "valid", Value::makeBool(problems == 0));
  api.setField(o, "problems", Value::makeInt(problems));
  api.setField(o, "duplicateSections", Value::makeInt(static_cast<int64_t>(dups)));
  api.setField(o, "malformedLines", Value::makeInt(static_cast<int64_t>(bad)));
  return o;
}

Value configKeys(const VMApi &api, const std::vector<Value> &args) {
  (void)args;
  auto keys = Configs::Get().GetAllKeys();
  auto arr = api.makeArray();
  for (const auto &key : keys)
    api.push(arr, api.makeString(key));
  return arr;
}

static Value configDispatch(const VMApi &api, const std::vector<Value> &args) {
  if (args.empty()) {
    throw std::runtime_error("cfg() requires a command (load, get, set, save, keys)");
  }
  std::string cmd = api.toString(args[0]);
  if (cmd == "load") {
    if (args.size() < 2)
      throw std::runtime_error("cfg.load() requires path");
    Configs::Get().Load(api.toString(args[1]));
    return Value::makeBool(true);
  }
  if (cmd == "get") {
    return configGet(api, args);
  }
  if (cmd == "set") {
    return configSet(api, args);
  }
  if (cmd == "save") {
    return configSave(args);
  }
  if (cmd == "keys" || cmd == "list") {
    return configKeys(api, args);
  }
  throw std::runtime_error("cfg(): unknown command '" + cmd + "'");
}

static void setNestedField(const VMApi &api, Value obj, const std::string &key, Value value);

void registerConfigModule(const VMApi &api) {
  // Idempotent registration: if a `config` global object already exists
  // (this plugin can be registered more than once - eager scan + lazy
  // module load both call register_fn), REUSE it and repopulate in place.
  // Building a fresh object each time left two configObj instances alive:
  // whichever one a script captured (`cfg = _store` in std/config.hv, or
  // the bare global) saw the OTHER one's section data, so cfg.Gaming
  // resolved to a marker-only vivify shell instead of the populated
  // section (measured: cfg.Gaming.Classes -> {} while config.Gaming had
  // the keys; scripts/tests chain probes Oct 27).
  Value configObj = Value::makeNull();
  auto &globals = api.vm().getGlobals();
  auto existing = globals.find("config");
  if (existing != globals.end() && existing->second.isObjectId()) {
    configObj = existing->second;
  } else {
    configObj = api.makeObject();
    // Markers must be set BEFORE repopulating: setNestedField copies
    // __vivify down to each section it creates, so sections built from
    // keys already on disk would otherwise get no marker.
    // __vivify: cfg.Debug.X = Y auto-creates an object at cfg.Debug.
    api.setField(configObj, "__vivify", Value::makeBool(true));
  }
  // Marks this global object as the module contract for `use "std/config"`:
  // loadModule binds it instead of the .hv wrapper, whose namespace lacks
  // the live section objects (cfg.Gaming.Classes read the wrapper as a
  // marker-only shell). Explicit so other plugin objects - bit, math_native,
  // log, random carry only private fns while their public API lives in an
  // .hv sidecar - keep the normal module path (binding them regressed
  // bit.arshift to null in the smoke suite, Oct 31).
  api.setField(configObj, "__module_object", Value::makeBool(true));
  // NOTE: __autosave_root is deliberately NOT enabled. Enabling it makes
  // the VM's OBJECT_SET hook mirror `cfg.X.Y = z` writes into the config
  // store, and that path currently persists the WRONG value: for
  // `cfg.Fresh.S = "hello"` the hook sees the assigned name as a string and
  // resolves it to the script path, writing
  //   Fresh.S = "/path/to/havel"
  // Booleans and ints come through correctly; strings do not. Until that is
  // fixed, enabling autosave silently corrupts the config file. The hook's
  // __cfg_path handling (see VMCollections.cpp) accepts both string kinds and
  // is correct; the value it is handed is not.
  for (const auto &key : Configs::Get().GetAllKeys()) {
    setNestedField(api, configObj, key,
                   stringToValue(api, Configs::Get().Get<std::string>(key, "")));
  }

  // Call-time lookup of the config object: the closures below must mutate
  // the SAME object the script reads (the `config` global), not whatever
  // instance was current at registration - plugin registration can run in
  // more than one VM context, and a captured configObj left the script's
  // global holding section shells that never received later config.set
  // values (measured: config.set("Debug.ForceMinimal", 0) updated the C map
  // but config.Debug.ForceMinimal stayed a vivified shell).
  // Capture VMApi BY VALUE (it is a shallow {VM*, ServiceRegistry*} handle).
  // Capturing the parameter by reference dangled: registerConfigModule
  // returns long before a script calls config.set, and the referenced
  // VMApi lived in activateLazyModule's stack frame - the later call read
  // a garbage vm_ and died with EINVAL in GCHeap's mutex (gdb, Oct 30).
  auto configObjAtCall = [api]() -> Value {
    auto &g = api.vm().getGlobals();
    auto it = g.find("config");
    if (it != g.end() && it->second.isObjectId())
      return it->second;
    return Value::makeNull();
  };
  auto repopulate = [api](Value configObj) {
    for (const auto &key : Configs::Get().GetAllKeys()) {
      setNestedField(api, configObj, key,
                     stringToValue(api, Configs::Get().Get<std::string>(key, "")));
    }
  };

  // Register namespace methods for config.load(), config.get(), etc.
  api.registerFunction("config.load", 1, [api, configObjAtCall, repopulate](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("config.load() requires path");
    Configs::Get().Load(api.toString(args[0]));
    // Repopulate config object from C-level Configs after load
    repopulate(configObjAtCall());
    return Value::makeBool(true);
  });
  api.registerFunction("config.get",
                       [api](const std::vector<Value> &args) { return configGet(api, args); });
  api.registerFunction("config.set", [api, configObjAtCall, repopulate](const std::vector<Value> &args) {
    auto result = configSet(api, args);
    // Repopulate config object from C-level Configs after set
    repopulate(configObjAtCall());
    return result;
  });
  api.registerFunction("config.save",
                       [](const std::vector<Value> &args) { return configSave(args); });
  api.registerFunction("config.keys",
                       [api](const std::vector<Value> &args) { return configKeys(api, args); });
  // Whole config as one flat object. The std/config module's all(), section()
  // and toToml() all call this, and it was never registered - those three
  // functions could only fail at runtime.
  api.registerFunction("config.getAll", [api](const std::vector<Value> &args) {
    (void)args;
    Value o = api.makeObject();
    for (const auto &key : Configs::Get().GetAllKeys())
      api.setField(o, key, stringToValue(api, Configs::Get().Get<std::string>(key, "")));
    return o;
  });
  // What a save would change, without writing anything.
  api.registerFunction("config.diff", [api](const std::vector<Value> &args) {
    (void)args;
    return reportToObject(api, Configs::Get().DiffToDisk());
  });
  // Save and return the full report, for callers that need the numbers rather
  // than a bool.
  api.registerFunction("config.saveReport", [api](const std::vector<Value> &args) {
    (void)args;
    return reportToObject(api, Configs::Get().SaveWithReport());
  });
  api.registerFunction("config.status", [api](const std::vector<Value> &args) {
    (void)args;
    return configStatus(api);
  });
  api.registerFunction("config.validate",
                       [api](const std::vector<Value> &args) { return configValidate(api, args); });
  api.registerFunction("config.backups", [api](const std::vector<Value> &args) {
    std::string filename = args.empty() ? "havel.cfg" : api.toString(args[0]);
    auto arr = api.makeArray();
    for (const auto &p : Configs::Get().ListBackups(filename))
      api.push(arr, api.makeString(p));
    return arr;
  });
  api.registerFunction("config.restore", [api, configObj](const std::vector<Value> &args) {
    if (args.empty())
      throw std::runtime_error("config.restore() requires a backup index (0 = newest)");
    long long index = std::stoll(api.toString(args[0]));
    if (index < 0)
      throw std::runtime_error("config.restore() index must be >= 0");
    bool ok = Configs::Get().RestoreBackup(static_cast<size_t>(index));
    if (ok) {
      // Reflect the restored values in the config object, the same way load
      // and set do.
      for (const auto &key : Configs::Get().GetAllKeys())
        setNestedField(api, configObj, key,
                       stringToValue(api, Configs::Get().Get<std::string>(key, "")));
    }
    return Value::makeBool(ok);
  });

  // Attach methods to config object so config.load(), config.get(), etc. work
  api.setField(configObj, "load", api.makeFunctionRef("config.load"));
  api.setField(configObj, "get", api.makeFunctionRef("config.get"));
  api.setField(configObj, "set", api.makeFunctionRef("config.set"));
  api.setField(configObj, "save", api.makeFunctionRef("config.save"));
  api.setField(configObj, "keys", api.makeFunctionRef("config.keys"));
  api.setField(configObj, "getAll", api.makeFunctionRef("config.getAll"));
  api.setField(configObj, "diff", api.makeFunctionRef("config.diff"));
  api.setField(configObj, "saveReport", api.makeFunctionRef("config.saveReport"));
  api.setField(configObj, "status", api.makeFunctionRef("config.status"));
  api.setField(configObj, "validate", api.makeFunctionRef("config.validate"));
  api.setField(configObj, "backups", api.makeFunctionRef("config.backups"));
  api.setField(configObj, "restore", api.makeFunctionRef("config.restore"));
  // Propagate __vivify to config methods so the object itself behaves as a namespace
  api.setField(configObj, "__vivify", Value::makeBool(true));

  api.setGlobal("config", configObj);

  // cfg and conf point to the config object (not a HostFuncId) so that
  // cfg.Debug.VerboseKeyLogging = DOLOG works via OBJECT_SET on a real object,
  // and cfg.load(...) resolves "load" as a field on the object (CALL_METHOD).
  api.setGlobal("cfg", configObj);
  api.setGlobal("conf", configObj);

  // Keep cfg host function for backward compatibility with cfg("load", ...) syntax
  api.registerFunction("cfg",
                       [api](const std::vector<Value> &args) { return configDispatch(api, args); });
}

static void setNestedField(const VMApi &api, Value obj, const std::string &key, Value value) {
  size_t dot = key.find('.');
  if (dot == std::string::npos) {
    api.setField(obj, key, std::move(value));
    return;
  }
  std::string head = key.substr(0, dot), tail = key.substr(dot + 1);
  Value sub = api.hasField(obj, head) ? api.getField(obj, head) : Value::makeNull();
  if (!sub.isObjectId()) {
    sub = api.makeObject();
    api.setField(obj, head, sub);
  }
  if (api.hasField(obj, "__vivify"))
    api.setField(sub, "__vivify", api.getField(obj, "__vivify"));
  if (api.hasField(obj, "__autosave_root"))
    api.setField(sub, "__autosave_root", api.getField(obj, "__autosave_root"));
  std::string p = api.hasField(obj, "__cfg_path")
                      ? (api.toString(api.getField(obj, "__cfg_path")) + "." + head)
                      : head;
  api.setField(sub, "__cfg_path", api.makeString(p));
  setNestedField(api, sub, tail, std::move(value));
}

void autoLoadConfig(const VMApi &api) {
  (void)api;
}
} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL_A2(config, "1.0.0", "Configuration module", "cfg", "conf",
                            havel::modules::registerConfigModule(*api);)
#endif
