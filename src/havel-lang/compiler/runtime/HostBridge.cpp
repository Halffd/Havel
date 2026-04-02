/*
 * HostBridge.cpp - Composite bridge delegating to modular components
 *
 * ARCHITECTURE:
 * - HostBridge composes specialized bridge modules
 * - Each module handles a specific capability domain
 * - Execution policy is OPTIONAL (for embedding/sandboxing)
 * - Default: FULL ACCESS (no friction for normal users)
 */
#include "HostBridge.hpp"
#include "../../../host/module/ModularHostBridges.hpp"
#include "../../../modules/window/WindowMonitorModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

#include "../../../host/app/AppService.hpp"
#include "../../../host/media/MediaService.hpp"
#include "../../../host/network/NetworkService.hpp"

namespace havel::compiler {

// Helper function to get type name from BytecodeValue
static std::string getTypeName(const BytecodeValue &value) {
  if (std::holds_alternative<std::nullptr_t>(value))
    return "null";
  if (std::holds_alternative<bool>(value))
    return "bool";
  if (std::holds_alternative<int64_t>(value))
    return "int";
  if (std::holds_alternative<double>(value))
    return "float";
  if (std::holds_alternative<std::string>(value))
    return "string";
  if (std::holds_alternative<ArrayRef>(value))
    return "array";
  if (std::holds_alternative<ObjectRef>(value))
    return "object";
  return "unknown";
}

// Helper function to compare two BytecodeValues for equality
static bool valuesEqual(const BytecodeValue &a, const BytecodeValue &b) {
  if (a.index() != b.index())
    return false;
  if (std::holds_alternative<std::nullptr_t>(a))
    return true;
  if (std::holds_alternative<bool>(a))
    return std::get<bool>(a) == std::get<bool>(b);
  if (std::holds_alternative<int64_t>(a))
    return std::get<int64_t>(a) == std::get<int64_t>(b);
  if (std::holds_alternative<double>(a))
    return std::get<double>(a) == std::get<double>(b);
  if (std::holds_alternative<std::string>(a))
    return std::get<std::string>(a) == std::get<std::string>(b);
  if (std::holds_alternative<ArrayRef>(a))
    return std::get<ArrayRef>(a).id == std::get<ArrayRef>(b).id;
  if (std::holds_alternative<ObjectRef>(a))
    return std::get<ObjectRef>(a).id == std::get<ObjectRef>(b).id;
  return false;
}

HostBridge::HostBridge(const havel::HostContext &ctx)
    : ctx_(&ctx), policy_(ExecutionPolicy::DefaultPolicy()),
      moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<ExtensionLoader>();
  initBridges();
}

HostBridge::HostBridge(const havel::HostContext &ctx,
                       const ExecutionPolicy &policy)
    : ctx_(&ctx), policy_(policy), moduleLoader_(*ctx_) {
  extensionLoader_ = std::make_unique<ExtensionLoader>();
  moduleLoader_.setExecutionPolicy(policy);
  initBridges();
}

HostBridge::~HostBridge() { shutdown(); }

void HostBridge::shutdown() {
  clear();
  options_.host_functions.clear();
  vm_setup_callbacks_.clear();
  vm_setup_callbacks_.shrink_to_fit();
  modules_.clear();
  ioBridge_.reset();
  systemBridge_.reset();
  uiBridge_.reset();
  inputBridge_.reset();
  mediaBridge_.reset();
  networkBridge_.reset();
  audioBridge_.reset();
  displayBridge_.reset();
  configBridge_.reset();
  modeBridge_.reset();
  timerBridge_.reset();
  appBridge_.reset();
  asyncBridge_.reset();
  automationBridge_.reset();
  browserBridge_.reset();
  toolsBridge_.reset();
  extensionLoader_.reset();
}

void HostBridge::clear() {
  mode_bindings_.clear();
  mode_definition_order_.clear();
  hotkey_binding_keys_.clear();
}

void HostBridge::registerModeCallbacks(const std::string &modeName,
                                       CallbackId conditionId,
                                       CallbackId enterId, CallbackId exitId) {
  // Store the mode binding with callback IDs
  ModeBinding binding;
  binding.modeName = modeName;
  binding.condition_id = conditionId;
  binding.enter_id = enterId;
  binding.exit_id = exitId;
  mode_bindings_[modeName] = std::move(binding);
  mode_definition_order_.push_back(modeName);
}

void HostBridge::initBridges() {
  ioBridge_ = std::make_unique<IOBridge>(ctx_);
  systemBridge_ = std::make_unique<SystemBridge>(ctx_);
  uiBridge_ = std::make_unique<UIBridge>(ctx_);
  inputBridge_ = std::make_unique<InputBridge>(ctx_);
  mediaBridge_ = std::make_unique<MediaBridge>(ctx_);
  networkBridge_ = std::make_unique<NetworkBridge>(ctx_);
  audioBridge_ = std::make_unique<AudioBridge>(ctx_);
  mpvBridge_ = std::make_unique<MPVBridge>(ctx_);
  displayBridge_ = std::make_unique<DisplayBridge>(ctx_);
  configBridge_ = std::make_unique<ConfigBridge>(ctx_);
  modeBridge_ = std::make_unique<ModeBridge>(ctx_);
  timerBridge_ = std::make_unique<TimerBridge>(ctx_);
  appBridge_ = std::make_unique<AppBridge>(ctx_);
  asyncBridge_ = std::make_unique<AsyncBridge>(ctx_);
  automationBridge_ = std::make_unique<AutomationBridge>(ctx_);
  browserBridge_ = std::make_unique<BrowserBridge>(ctx_);
  toolsBridge_ = std::make_unique<ToolsBridge>(ctx_);
}

void HostBridge::install() {
  options_.host_functions.reserve(64);
  vm_setup_callbacks_.reserve(16);

  // Install all bridge modules (policy checks happen at call time if needed)
  ioBridge_->install(options_);
  systemBridge_->install(options_);
  uiBridge_->install(options_);
  inputBridge_->install(options_);
  mediaBridge_->install(options_);
  networkBridge_->install(options_);
  audioBridge_->install(options_);
  mpvBridge_->install(options_);
  displayBridge_->install(options_);
  configBridge_->install(options_);
  modeBridge_->install(options_);
  timerBridge_->install(options_);
  appBridge_->install(options_);
  asyncBridge_->install(options_);
  automationBridge_->install(options_);
  browserBridge_->install(options_);
  toolsBridge_->install(options_);

  // Setup dynamic window globals using existing WindowMonitor from
  // HotkeyManager This integrates window monitoring with bytecode VM without
  // creating duplicate instances
  if (ctx_->windowMonitor && ctx_->vm) {
    VM *vm = static_cast<VM *>(ctx_->vm);
    VMApi api(*vm);
    havel::modules::setupDynamicWindowGlobals(api, ctx_->windowMonitor);
  }

  // Install extension loading functions
  // Use raw 'this' pointer to avoid circular reference (HostBridge outlives VM
  // usage)
  options_.host_functions["extension.load"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
          return BytecodeValue(false);
        }
        std::string name = std::get<std::string>(args[0]);
        return BytecodeValue(extensionLoader_->loadExtensionByName(name));
      };
  options_.host_functions["extension.isLoaded"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
          return BytecodeValue(false);
        }
        std::string name = std::get<std::string>(args[0]);
        return BytecodeValue(extensionLoader_->isLoaded(name));
      };
  options_.host_functions["extension.list"] =
      [this](const std::vector<BytecodeValue> &args) {
        (void)args;
        auto names = extensionLoader_->getLoadedExtensions();
        auto *vm = static_cast<VM *>(ctx_->vm);
        if (!vm) {
          return BytecodeValue(nullptr);
        }
        auto arr = vm->createHostArray();
        for (const auto &name : names) {
          vm->pushHostArrayValue(arr, BytecodeValue(name));
        }
        return BytecodeValue(arr);
      };
  options_.host_functions["extension.addSearchPath"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
          return BytecodeValue(false);
        }
        std::string path = std::get<std::string>(args[0]);
        extensionLoader_->addSearchPath(path);
        return BytecodeValue(true);
      };

  // Register any.* dispatch methods for runtime type-based method calls
  // Use raw 'this' pointer to avoid circular reference
  auto registerAnyMethod = [this](const std::string &methodName) {
    options_.host_functions["any." + methodName] =
        [this, methodName](const std::vector<BytecodeValue> &args) {
          if (args.empty()) {
            return BytecodeValue(nullptr);
          }

          // Determine type and dispatch to appropriate module
          std::string type = getTypeName(args[0]);
          std::string modulePrefix;
          if (type == "string")
            modulePrefix = "string";
          else if (type == "array")
            modulePrefix = "array";
          else if (type == "object")
            modulePrefix = "object";
          else if (type == "struct")
            modulePrefix = "struct";
          else
            return BytecodeValue(nullptr);

          std::string fullName = modulePrefix + "." + methodName;

          // Look up and call the appropriate function
          auto it = options_.host_functions.find(fullName);
          if (it != options_.host_functions.end()) {
            return it->second(args);
          }

          // Also check stdlib modules (http, io, json, fs, etc.)
          for (const auto &mod : {"http", "io", "json", "fs", "net", "time",
                                  "math", "os", "env"}) {
            std::string modFunc = std::string(mod) + "." + methodName;
            auto modIt = options_.host_functions.find(modFunc);
            if (modIt != options_.host_functions.end()) {
              return modIt->second(args);
            }
          }

          return BytecodeValue(nullptr);
        };
  };

  // Register all any.* methods
  registerAnyMethod("len");

  // Global len() function - delegates to any.len dispatch
  options_.host_functions["len"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          return BytecodeValue(nullptr);
        }
        auto it = options_.host_functions.find("any.len");
        if (it != options_.host_functions.end()) {
          return it->second(args);
        }
        return BytecodeValue(nullptr);
      };
  registerAnyMethod("has");
  registerAnyMethod("find");
  registerAnyMethod("trim");
  registerAnyMethod("upper");
  registerAnyMethod("lower");
  registerAnyMethod("includes");
  registerAnyMethod("startswith");
  registerAnyMethod("endswith");
  registerAnyMethod("replace");
  registerAnyMethod("split");
  registerAnyMethod("join");
  registerAnyMethod("sub");
  registerAnyMethod("push");
  registerAnyMethod("pop");
  registerAnyMethod("get");
  registerAnyMethod("set");
  registerAnyMethod("sort");
  registerAnyMethod("filter");
  registerAnyMethod("map");
  registerAnyMethod("reduce");
  registerAnyMethod("foreach");
  registerAnyMethod("send");
  registerAnyMethod("pause");
  registerAnyMethod("resume");
  registerAnyMethod("stop");
  registerAnyMethod("cancel");
  registerAnyMethod("running");

  // Stdlib module methods (http, io, json, fs, net, time, math, os, env)
  registerAnyMethod("print");
  registerAnyMethod("println");
  registerAnyMethod("log");
  registerAnyMethod("debug");
  registerAnyMethod("info");
  registerAnyMethod("warn");
  registerAnyMethod("error");
  registerAnyMethod("read");
  registerAnyMethod("write");
  registerAnyMethod("open");
  registerAnyMethod("close");
  registerAnyMethod("create");
  registerAnyMethod("delete");
  registerAnyMethod("remove");
  registerAnyMethod("copy");
  registerAnyMethod("move");
  registerAnyMethod("rename");
  registerAnyMethod("exists");
  registerAnyMethod("parse");
  registerAnyMethod("stringify");
  registerAnyMethod("encode");
  registerAnyMethod("decode");
  registerAnyMethod("format");
  registerAnyMethod("fetch");
  registerAnyMethod("request");
  registerAnyMethod("post");
  registerAnyMethod("put");
  registerAnyMethod("patch");
  registerAnyMethod("head");
  registerAnyMethod("options");
  registerAnyMethod("connect");
  registerAnyMethod("disconnect");
  registerAnyMethod("listen");
  registerAnyMethod("accept");
  registerAnyMethod("bind");
  registerAnyMethod("resolve");
  registerAnyMethod("query");
  registerAnyMethod("exec");
  registerAnyMethod("spawn");
  registerAnyMethod("kill");
  registerAnyMethod("wait");
  registerAnyMethod("sleep");
  registerAnyMethod("delay");
  registerAnyMethod("timeout");
  registerAnyMethod("interval");
  registerAnyMethod("schedule");
  registerAnyMethod("now");
  registerAnyMethod("today");
  registerAnyMethod("date");
  registerAnyMethod("time");
  registerAnyMethod("datetime");
  registerAnyMethod("timestamp");
  registerAnyMethod("random");
  registerAnyMethod("abs");
  registerAnyMethod("floor");
  registerAnyMethod("ceil");
  registerAnyMethod("round");
  registerAnyMethod("sqrt");
  registerAnyMethod("pow");
  registerAnyMethod("sin");
  registerAnyMethod("cos");
  registerAnyMethod("tan");
  registerAnyMethod("asin");
  registerAnyMethod("acos");
  registerAnyMethod("atan");
  registerAnyMethod("atan2");
  registerAnyMethod("exp");
  registerAnyMethod("log");
  registerAnyMethod("log10");
  registerAnyMethod("log2");
  registerAnyMethod("min");
  registerAnyMethod("max");
  registerAnyMethod("clamp");
  registerAnyMethod("sign");
  registerAnyMethod("mod");
  registerAnyMethod("gcd");
  registerAnyMethod("lcm");
  registerAnyMethod("factorial");
  registerAnyMethod("fibonacci");
  registerAnyMethod("isPrime");
  registerAnyMethod("isFinite");
  registerAnyMethod("isNaN");
  registerAnyMethod("isInteger");
  registerAnyMethod("isFloat");
  registerAnyMethod("isNumber");
  registerAnyMethod("isString");
  registerAnyMethod("isArray");
  registerAnyMethod("isObject");
  registerAnyMethod("isFunction");
  registerAnyMethod("isNull");
  registerAnyMethod("isUndefined");
  registerAnyMethod("isDefined");
  registerAnyMethod("isTruthy");
  registerAnyMethod("isFalsy");
  registerAnyMethod("type");
  registerAnyMethod("typeof");
  registerAnyMethod("instanceof");
  registerAnyMethod("keys");
  registerAnyMethod("values");
  registerAnyMethod("entries");
  registerAnyMethod("fromEntries");
  registerAnyMethod("assign");
  registerAnyMethod("merge");
  registerAnyMethod("clone");
  registerAnyMethod("deepClone");
  registerAnyMethod("freeze");
  registerAnyMethod("seal");
  registerAnyMethod("isFrozen");
  registerAnyMethod("isSealed");
  registerAnyMethod("isExtensible");
  registerAnyMethod("preventExtensions");
  registerAnyMethod("defineProperty");
  registerAnyMethod("defineProperties");
  registerAnyMethod("getOwnPropertyDescriptor");
  registerAnyMethod("getOwnPropertyNames");
  registerAnyMethod("getOwnPropertySymbols");
  registerAnyMethod("getPrototypeOf");
  registerAnyMethod("setPrototypeOf");
  registerAnyMethod("isPrototypeOf");
  registerAnyMethod("propertyIsEnumerable");
  registerAnyMethod("hasOwnProperty");
  registerAnyMethod("toString");
  registerAnyMethod("toNumber");
  registerAnyMethod("toBoolean");
  registerAnyMethod("toArray");
  registerAnyMethod("toObject");
  registerAnyMethod("toJSON");
  registerAnyMethod("fromJSON");
  registerAnyMethod("at");
  registerAnyMethod("charAt");
  registerAnyMethod("charCodeAt");
  registerAnyMethod("codePointAt");
  registerAnyMethod("indexOf");
  registerAnyMethod("lastIndexOf");
  registerAnyMethod("localeCompare");
  registerAnyMethod("match");
  registerAnyMethod("matchAll");
  registerAnyMethod("normalize");
  registerAnyMethod("padEnd");
  registerAnyMethod("padStart");
  registerAnyMethod("raw");
  registerAnyMethod("repeat");
  registerAnyMethod("search");
  registerAnyMethod("slice");
  registerAnyMethod("substr");
  registerAnyMethod("substring");
  registerAnyMethod("toLocaleLowerCase");
  registerAnyMethod("toLocaleUpperCase");
  registerAnyMethod("toLowerCase");
  registerAnyMethod("toUpperCase");
  registerAnyMethod("trimEnd");
  registerAnyMethod("trimStart");
  registerAnyMethod("valueOf");
  registerAnyMethod("concat");
  registerAnyMethod("copyWithin");
  registerAnyMethod("entries");
  registerAnyMethod("every");
  registerAnyMethod("fill");
  registerAnyMethod("flat");
  registerAnyMethod("flatMap");
  registerAnyMethod("forEach");
  registerAnyMethod("group");
  registerAnyMethod("groupToMap");
  registerAnyMethod("includes");
  registerAnyMethod("indexOf");
  registerAnyMethod("join");
  registerAnyMethod("keys");
  registerAnyMethod("lastIndexOf");
  registerAnyMethod("reduceRight");
  registerAnyMethod("reverse");
  registerAnyMethod("shift");
  registerAnyMethod("slice");
  registerAnyMethod("some");
  registerAnyMethod("splice");
  registerAnyMethod("toReversed");
  registerAnyMethod("toSorted");
  registerAnyMethod("toSpliced");
  registerAnyMethod("unshift");
  registerAnyMethod("values");
  registerAnyMethod("with");
  registerAnyMethod("isSafeInteger");
  registerAnyMethod("isInteger");
  registerAnyMethod("parseFloat");
  registerAnyMethod("parseInt");
  registerAnyMethod("MAX_VALUE");
  registerAnyMethod("MIN_VALUE");
  registerAnyMethod("MAX_SAFE_INTEGER");
  registerAnyMethod("MIN_SAFE_INTEGER");
  registerAnyMethod("EPSILON");
  registerAnyMethod("POSITIVE_INFINITY");
  registerAnyMethod("NEGATIVE_INFINITY");
  registerAnyMethod("NaN");
  registerAnyMethod("prototype");
  registerAnyMethod("constructor");
  registerAnyMethod("__proto__");
  registerAnyMethod("__defineGetter__");
  registerAnyMethod("__defineSetter__");
  registerAnyMethod("__lookupGetter__");
  registerAnyMethod("__lookupSetter__");
  registerAnyMethod("hasOwn");
  registerAnyMethod("is");
  registerAnyMethod("from");
  registerAnyMethod("of");
  registerAnyMethod("apply");
  registerAnyMethod("bind");
  registerAnyMethod("call");
  registerAnyMethod("toSource");
  registerAnyMethod("toLocaleString");
  registerAnyMethod("toDateString");
  registerAnyMethod("toTimeString");
  registerAnyMethod("toUTCString");
  registerAnyMethod("toISOString");
  registerAnyMethod("toGMTString");
  registerAnyMethod("toJSON");
  registerAnyMethod("getDate");
  registerAnyMethod("getDay");
  registerAnyMethod("getFullYear");
  registerAnyMethod("getHours");
  registerAnyMethod("getMilliseconds");
  registerAnyMethod("getMinutes");
  registerAnyMethod("getMonth");
  registerAnyMethod("getSeconds");
  registerAnyMethod("getTime");
  registerAnyMethod("getTimezoneOffset");
  registerAnyMethod("getUTCDate");
  registerAnyMethod("getUTCDay");
  registerAnyMethod("getUTCFullYear");
  registerAnyMethod("getUTCHours");
  registerAnyMethod("getUTCMilliseconds");
  registerAnyMethod("getUTCMinutes");
  registerAnyMethod("getUTCMonth");
  registerAnyMethod("getUTCSeconds");
  registerAnyMethod("getYear");
  registerAnyMethod("setDate");
  registerAnyMethod("setFullYear");
  registerAnyMethod("setHours");
  registerAnyMethod("setMilliseconds");
  registerAnyMethod("setMinutes");
  registerAnyMethod("setMonth");
  registerAnyMethod("setSeconds");
  registerAnyMethod("setTime");
  registerAnyMethod("setUTCDate");
  registerAnyMethod("setUTCFullYear");
  registerAnyMethod("setUTCHours");
  registerAnyMethod("setUTCMilliseconds");
  registerAnyMethod("setUTCMinutes");
  registerAnyMethod("setUTCMonth");
  registerAnyMethod("setUTCSeconds");
  registerAnyMethod("setYear");
  registerAnyMethod("toStringTag");
  registerAnyMethod("hasInstance");
  registerAnyMethod("isConcatSpreadable");
  registerAnyMethod("iterator");
  registerAnyMethod("asyncIterator");
  registerAnyMethod("match");
  registerAnyMethod("replace");
  registerAnyMethod("search");
  registerAnyMethod("split");
  registerAnyMethod("species");
  registerAnyMethod("toPrimitive");
  registerAnyMethod("unscopables");
  registerAnyMethod("dispose");
  registerAnyMethod("asyncDispose");
  registerAnyMethod("metadata");

  // Chain method aliases (LINQ-style naming)
  registerAnyMethod("where");   // alias for filter
  registerAnyMethod("select");  // alias for map
  registerAnyMethod("count");   // alias for len
  registerAnyMethod("list");    // terminal
  registerAnyMethod("sum");     // aggregation
  registerAnyMethod("max");     // aggregation
  registerAnyMethod("min");     // aggregation
  registerAnyMethod("orderby"); // sorting
  registerAnyMethod("groupby"); // grouping
  registerAnyMethod("concat");  // concatenation
  registerAnyMethod("merge");   // merging
  registerAnyMethod("join");    // joining
  auto truthy = [](const BytecodeValue &value) -> bool {
    if (std::holds_alternative<std::nullptr_t>(value))
      return false;
    if (std::holds_alternative<bool>(value))
      return std::get<bool>(value);
    if (std::holds_alternative<int64_t>(value))
      return std::get<int64_t>(value) != 0;
    if (std::holds_alternative<double>(value))
      return std::get<double>(value) != 0.0;
    if (std::holds_alternative<std::string>(value))
      return !std::get<std::string>(value).empty();
    return true;
  };

  options_.host_functions["any.map"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          return BytecodeValue(nullptr);
        auto out = ctx_->vm->createHostArray();
        auto iter = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iter);
          if (!std::holds_alternative<ObjectRef>(step))
            break;
          auto stepObj = std::get<ObjectRef>(step);
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (std::holds_alternative<bool>(done) && std::get<bool>(done))
            break;
          auto value = ctx_->vm->getHostObjectField(stepObj, "value");
          auto mapped = ctx_->vm->callFunction(args[1], {value});
          ctx_->vm->pushHostArrayValue(out, mapped);
        }
        return BytecodeValue(out);
      };

  options_.host_functions["any.filter"] =
      [this, truthy](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          return BytecodeValue(nullptr);
        auto out = ctx_->vm->createHostArray();
        auto iter = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iter);
          if (!std::holds_alternative<ObjectRef>(step))
            break;
          auto stepObj = std::get<ObjectRef>(step);
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (std::holds_alternative<bool>(done) && std::get<bool>(done))
            break;
          auto value = ctx_->vm->getHostObjectField(stepObj, "value");
          auto keep = ctx_->vm->callFunction(args[1], {value});
          if (truthy(keep)) {
            ctx_->vm->pushHostArrayValue(out, value);
          }
        }
        return BytecodeValue(out);
      };

  options_.host_functions["any.reduce"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 3)
          return BytecodeValue(nullptr);
        BytecodeValue acc = args[2];
        auto iter = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iter);
          if (!std::holds_alternative<ObjectRef>(step))
            break;
          auto stepObj = std::get<ObjectRef>(step);
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (std::holds_alternative<bool>(done) && std::get<bool>(done))
            break;
          auto value = ctx_->vm->getHostObjectField(stepObj, "value");
          acc = ctx_->vm->callFunction(args[1], {acc, value});
        }
        return acc;
      };

  options_.host_functions["any.foreach"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          return BytecodeValue(nullptr);
        auto iter = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iter);
          if (!std::holds_alternative<ObjectRef>(step))
            break;
          auto stepObj = std::get<ObjectRef>(step);
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (std::holds_alternative<bool>(done) && std::get<bool>(done))
            break;
          auto value = ctx_->vm->getHostObjectField(stepObj, "value");
          (void)ctx_->vm->callFunction(args[1], {value});
        }
        return BytecodeValue(nullptr);
      };

  // Array methods (for any.* dispatch fallback)
  options_.host_functions["array.len"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        return BytecodeValue(
            int64_t(ctx_->vm->getHostArrayLength(std::get<ArrayRef>(args[0]))));
      };
  options_.host_functions["array.push"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        ctx_->vm->pushHostArrayValue(std::get<ArrayRef>(args[0]), args[1]);
        return args[0];
      };
  options_.host_functions["array.pop"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        return ctx_->vm->popHostArrayValue(std::get<ArrayRef>(args[0]));
      };
  options_.host_functions["array.has"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(false);
        return BytecodeValue(
            ctx_->vm->arrayContains(std::get<ArrayRef>(args[0]), args[1]));
      };
  options_.host_functions["array.find"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(int64_t(-1));
        auto arrRef = std::get<ArrayRef>(args[0]);
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          if (valuesEqual(ctx_->vm->getHostArrayValue(arrRef, i), args[1])) {
            return BytecodeValue(int64_t(i));
          }
        }
        return BytecodeValue(int64_t(-1));
      };
  options_.host_functions["array.map"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        auto arrRef = std::get<ArrayRef>(args[0]);
        auto resultRef = ctx_->vm->createHostArray();
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          auto mapped = ctx_->vm->callFunction(args[1], {elem});
          ctx_->vm->pushHostArrayValue(resultRef, mapped);
        }
        return BytecodeValue(resultRef);
      };
  options_.host_functions["array.filter"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        auto arrRef = std::get<ArrayRef>(args[0]);
        auto resultRef = ctx_->vm->createHostArray();
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          auto keep = ctx_->vm->callFunction(args[1], {elem});
          if (std::holds_alternative<bool>(keep) && std::get<bool>(keep)) {
            ctx_->vm->pushHostArrayValue(resultRef, elem);
          }
        }
        return BytecodeValue(resultRef);
      };
  options_.host_functions["array.reduce"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 3 || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        auto arrRef = std::get<ArrayRef>(args[0]);
        BytecodeValue acc = args[2];
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          acc = ctx_->vm->callFunction(args[1], {acc, elem});
        }
        return acc;
      };
  options_.host_functions["array.foreach"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        auto arrRef = std::get<ArrayRef>(args[0]);
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          auto elem = ctx_->vm->getHostArrayValue(arrRef, i);
          ctx_->vm->callFunction(args[1], {elem});
        }
        return BytecodeValue(nullptr);
      };
  options_.host_functions["array.sort"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<ArrayRef>(args[0]))
          return BytecodeValue(nullptr);
        auto arrRef = std::get<ArrayRef>(args[0]);
        size_t len = ctx_->vm->getHostArrayLength(arrRef);
        for (size_t i = 0; i < len; i++) {
          for (size_t j = 0; j < len - i - 1; j++) {
            auto a = ctx_->vm->getHostArrayValue(arrRef, j);
            auto b = ctx_->vm->getHostArrayValue(arrRef, j + 1);
            bool swap = false;
            if (args.size() >= 2) {
              auto cmp = ctx_->vm->callFunction(args[1], {a, b});
              if (std::holds_alternative<bool>(cmp)) {
                swap = std::get<bool>(cmp);
              }
            } else {
              if (std::holds_alternative<int64_t>(a) &&
                  std::holds_alternative<int64_t>(b)) {
                swap = std::get<int64_t>(a) > std::get<int64_t>(b);
              }
            }
            if (swap) {
              auto temp = a;
              ctx_->vm->setHostArrayValue(arrRef, j, b);
              ctx_->vm->setHostArrayValue(arrRef, j + 1, temp);
            }
          }
        }
        return BytecodeValue(arrRef);
      };

  // any.in(value, container) - membership test
  options_.host_functions["string.len"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0]))
          return BytecodeValue(nullptr);
        return BytecodeValue(int64_t(std::get<std::string>(args[0]).length()));
      };
  options_.host_functions["string.trim"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0]))
          return BytecodeValue(nullptr);
        std::string s = std::get<std::string>(args[0]);
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos)
          return BytecodeValue(std::string(""));
        size_t end = s.find_last_not_of(" \t\n\r");
        return BytecodeValue(s.substr(start, end - start + 1));
      };
  options_.host_functions["string.upper"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0]))
          return BytecodeValue(nullptr);
        std::string s = std::get<std::string>(args[0]);
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return BytecodeValue(s);
      };
  options_.host_functions["string.lower"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0]))
          return BytecodeValue(nullptr);
        std::string s = std::get<std::string>(args[0]);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return BytecodeValue(s);
      };
  options_.host_functions["string.includes"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(false);
        const std::string &s = std::get<std::string>(args[0]);
        const std::string &sub = std::get<std::string>(args[1]);
        return BytecodeValue(s.find(sub) != std::string::npos);
      };
  options_.host_functions["string.startswith"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(false);
        const std::string &s = std::get<std::string>(args[0]);
        const std::string &pre = std::get<std::string>(args[1]);
        return BytecodeValue(s.size() >= pre.size() &&
                             s.compare(0, pre.size(), pre) == 0);
      };
  options_.host_functions["string.endswith"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(false);
        const std::string &s = std::get<std::string>(args[0]);
        const std::string &suf = std::get<std::string>(args[1]);
        return BytecodeValue(
            s.size() >= suf.size() &&
            s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
      };

  // Struct field access
  // TODO: Implement field name to index lookup in VM
  // For now, these are disabled since getStructField/setStructField expect
  // numeric indices, not string field names
  /*
  options_.host_functions["struct.get"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<StructRef>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(nullptr);
        auto structRef = std::get<StructRef>(args[0]);
        const std::string &fieldName = std::get<std::string>(args[1]);
        // Need VM method to lookup field index by name
        return ctx_->vm->getStructField(structRef, 0); // placeholder
      };
  options_.host_functions["struct.set"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 3 || !std::holds_alternative<StructRef>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(nullptr);
        auto structRef = std::get<StructRef>(args[0]);
        const std::string &fieldName = std::get<std::string>(args[1]);
        // Need VM method to lookup field index by name
        ctx_->vm->setStructField(structRef, 0, args[2]); // placeholder
        return args[2];
      };
  */

  // Object methods (for any.* dispatch)
  options_.host_functions["object.len"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<ObjectRef>(args[0]))
          return BytecodeValue(nullptr);
        int64_t count = 0;
        auto iterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(step))
            break;
          auto stepObj = std::get<ObjectRef>(step);
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (std::holds_alternative<bool>(done) && std::get<bool>(done))
            break;
          count++;
        }
        return BytecodeValue(count);
      };
  options_.host_functions["object.has"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ObjectRef>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(false);
        return BytecodeValue(ctx_->vm->objectHasKey(
            std::get<ObjectRef>(args[0]), std::get<std::string>(args[1])));
      };
  options_.host_functions["object.get"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 || !std::holds_alternative<ObjectRef>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(nullptr);
        return ctx_->vm->getHostObjectField(std::get<ObjectRef>(args[0]),
                                            std::get<std::string>(args[1]));
      };
  options_.host_functions["object.set"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 3 || !std::holds_alternative<ObjectRef>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          return BytecodeValue(nullptr);
        ctx_->vm->setHostObjectField(std::get<ObjectRef>(args[0]),
                                     std::get<std::string>(args[1]), args[2]);
        return args[2];
      };
  options_.host_functions["object.keys"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<ObjectRef>(args[0]))
          return BytecodeValue(nullptr);
        auto result = ctx_->vm->createHostArray();
        auto iterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(step))
            break;
          auto stepObj = std::get<ObjectRef>(step);
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (std::holds_alternative<bool>(done) && std::get<bool>(done))
            break;
          auto key = ctx_->vm->getHostObjectField(stepObj, "key");
          if (std::holds_alternative<std::string>(key)) {
            ctx_->vm->pushHostArrayValue(result, key);
          }
        }
        return BytecodeValue(result);
      };
  options_.host_functions["object.values"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<ObjectRef>(args[0]))
          return BytecodeValue(nullptr);
        auto result = ctx_->vm->createHostArray();
        auto iterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto step = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(step))
            break;
          auto stepObj = std::get<ObjectRef>(step);
          auto done = ctx_->vm->getHostObjectField(stepObj, "done");
          if (std::holds_alternative<bool>(done) && std::get<bool>(done))
            break;
          auto val = ctx_->vm->getHostObjectField(stepObj, "value");
          ctx_->vm->pushHostArrayValue(result, val);
        }
        return BytecodeValue(result);
      };

  // LINQ-style filter and map functions for query expressions
  options_.host_functions["filter"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2 ||
            !std::holds_alternative<HostFunctionRef>(args[1])) {
          return BytecodeValue(nullptr);
        }
        const auto &iterable = args[0];
        const auto &predicate = args[1];
        const std::string &fnName = std::get<HostFunctionRef>(predicate).name;

        ArrayRef result = ctx_->vm->createHostArray();

        // Create iterator
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (std::holds_alternative<std::nullptr_t>(valueVal))
            continue;

          // Call predicate
          std::vector<BytecodeValue> predArgs{valueVal};
          auto predResult = ctx_->vm->callFunction(
              BytecodeValue(HostFunctionRef{fnName}), predArgs);

          if (std::holds_alternative<bool>(predResult) &&
              std::get<bool>(predResult)) {
            ctx_->vm->pushHostArrayValue(result, valueVal);
          }
        }
        return BytecodeValue(result);
      };

  options_.host_functions["map"] = [this](
                                       const std::vector<BytecodeValue> &args) {
    if (args.size() < 2 || !std::holds_alternative<HostFunctionRef>(args[1])) {
      return BytecodeValue(nullptr);
    }
    const auto &iterable = args[0];
    const auto &transform = args[1];
    const std::string &fnName = std::get<HostFunctionRef>(transform).name;

    ArrayRef result = ctx_->vm->createHostArray();

    // Create iterator
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);

    while (true) {
      auto iterResult = ctx_->vm->iteratorNext(iterRef);
      if (!std::holds_alternative<ObjectRef>(iterResult))
        break;
      auto resultObjRef = std::get<ObjectRef>(iterResult);

      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
        break;

      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
      if (std::holds_alternative<std::nullptr_t>(valueVal))
        continue;

      // Call transform
      std::vector<BytecodeValue> transArgs{valueVal};
      auto transResult = ctx_->vm->callFunction(
          BytecodeValue(HostFunctionRef{fnName}), transArgs);

      ctx_->vm->pushHostArrayValue(result, transResult);
    }
    return BytecodeValue(result);
  };

  // Terminal operation - convert to set
  options_.host_functions["set"] = [this](
                                       const std::vector<BytecodeValue> &args) {
    if (args.empty()) {
      return BytecodeValue(nullptr);
    }
    const auto &iterable = args[0];

    // Create set and add unique elements
    ObjectRef result = ctx_->vm->createHostObject();
    ctx_->vm->setHostObjectField(result, "__set_marker__", BytecodeValue(true));
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);

    while (true) {
      auto iterResult = ctx_->vm->iteratorNext(iterRef);
      if (!std::holds_alternative<ObjectRef>(iterResult))
        break;
      auto resultObjRef = std::get<ObjectRef>(iterResult);

      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
        break;

      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
      if (!std::holds_alternative<std::nullptr_t>(valueVal)) {
        ctx_->vm->setHostObjectField(result, std::to_string(valueVal.index()),
                                     valueVal);
      }
    }
    return BytecodeValue(result);
  };

  // Terminal operation - convert to object with key-value pairs
  options_.host_functions["object"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          return BytecodeValue(nullptr);
        }
        const auto &iterable = args[0];

        ObjectRef result = ctx_->vm->createHostObject();
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto pairVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          // Expect pairs like [key, value] or {key: ..., value: ...}
          if (std::holds_alternative<ArrayRef>(pairVal)) {
            auto arr = std::get<ArrayRef>(pairVal);
            auto key = ctx_->vm->getHostArrayValue(arr, 0);
            auto val = ctx_->vm->getHostArrayValue(arr, 1);
            if (std::holds_alternative<std::string>(key)) {
              ctx_->vm->setHostObjectField(result, std::get<std::string>(key),
                                           val);
            }
          }
        }
        return BytecodeValue(result);
      };

  // Aggregation - sum all values
  options_.host_functions["sum"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          return BytecodeValue(nullptr);
        }
        const auto &iterable = args[0];

        double total = 0;
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (std::holds_alternative<int64_t>(valueVal)) {
            total += std::get<int64_t>(valueVal);
          } else if (std::holds_alternative<double>(valueVal)) {
            total += std::get<double>(valueVal);
          }
        }
        return BytecodeValue(total);
      };

  // Aggregation - find max value
  options_.host_functions["max"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          return BytecodeValue(nullptr);
        }
        const auto &iterable = args[0];

        double maxVal = std::numeric_limits<double>::lowest();
        bool hasValue = false;

        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          double num = 0;
          if (std::holds_alternative<int64_t>(valueVal)) {
            num = std::get<int64_t>(valueVal);
          } else if (std::holds_alternative<double>(valueVal)) {
            num = std::get<double>(valueVal);
          } else {
            continue;
          }

          if (!hasValue || num > maxVal) {
            maxVal = num;
            hasValue = true;
          }
        }
        return hasValue ? BytecodeValue(maxVal) : BytecodeValue(nullptr);
      };

  // Aggregation - find min value
  options_.host_functions["min"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          return BytecodeValue(nullptr);
        }
        const auto &iterable = args[0];

        double minVal = std::numeric_limits<double>::max();
        bool hasValue = false;

        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          double num = 0;
          if (std::holds_alternative<int64_t>(valueVal)) {
            num = std::get<int64_t>(valueVal);
          } else if (std::holds_alternative<double>(valueVal)) {
            num = std::get<double>(valueVal);
          } else {
            continue;
          }

          if (!hasValue || num < minVal) {
            minVal = num;
            hasValue = true;
          }
        }
        return hasValue ? BytecodeValue(minVal) : BytecodeValue(nullptr);
      };

  // Advanced LINQ methods - orderby (sort with key selector)
  options_.host_functions["orderby"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          return BytecodeValue(nullptr);
        }
        const auto &iterable = args[0];

        // Collect all elements first
        std::vector<BytecodeValue> elements;
        IteratorRef iterRef = ctx_->vm->createIterator(iterable);

        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!std::holds_alternative<std::nullptr_t>(valueVal)) {
            elements.push_back(valueVal);
          }
        }

        // Sort elements
        std::sort(elements.begin(), elements.end(),
                  [](const BytecodeValue &a, const BytecodeValue &b) {
                    // Numeric comparison
                    if (std::holds_alternative<int64_t>(a) &&
                        std::holds_alternative<int64_t>(b)) {
                      return std::get<int64_t>(a) < std::get<int64_t>(b);
                    }
                    if (std::holds_alternative<double>(a) &&
                        std::holds_alternative<double>(b)) {
                      return std::get<double>(a) < std::get<double>(b);
                    }
                    if (std::holds_alternative<int64_t>(a) &&
                        std::holds_alternative<double>(b)) {
                      return std::get<int64_t>(a) < std::get<double>(b);
                    }
                    if (std::holds_alternative<double>(a) &&
                        std::holds_alternative<int64_t>(b)) {
                      return std::get<double>(a) < std::get<int64_t>(b);
                    }
                    // String comparison
                    if (std::holds_alternative<std::string>(a) &&
                        std::holds_alternative<std::string>(b)) {
                      return std::get<std::string>(a) <
                             std::get<std::string>(b);
                    }
                    return false;
                  });

        // Create result array
        ArrayRef result = ctx_->vm->createHostArray();
        for (const auto &elem : elements) {
          ctx_->vm->pushHostArrayValue(result, elem);
        }
        return BytecodeValue(result);
      };

  // groupby - group elements by key selector
  options_.host_functions["groupby"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2) {
          return BytecodeValue(nullptr);
        }
        const auto &iterable = args[0];
        const auto &keySelector = args[1];

        // Group elements by key
        std::unordered_map<std::string, std::vector<BytecodeValue>> groups;

        IteratorRef iterRef = ctx_->vm->createIterator(iterable);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (std::holds_alternative<std::nullptr_t>(valueVal))
            continue;

          // Get key from selector
          std::vector<BytecodeValue> selectorArgs{valueVal};
          auto keyVal = ctx_->vm->callFunction(keySelector, selectorArgs);

          std::string keyStr;
          if (std::holds_alternative<std::string>(keyVal)) {
            keyStr = std::get<std::string>(keyVal);
          } else if (std::holds_alternative<int64_t>(keyVal)) {
            keyStr = std::to_string(std::get<int64_t>(keyVal));
          } else if (std::holds_alternative<double>(keyVal)) {
            keyStr = std::to_string(std::get<double>(keyVal));
          } else {
            keyStr = "null";
          }

          groups[keyStr].push_back(valueVal);
        }

        // Create result object with groups
        ObjectRef result = ctx_->vm->createHostObject();
        for (const auto &pair : groups) {
          ArrayRef groupArray = ctx_->vm->createHostArray();
          for (const auto &elem : pair.second) {
            ctx_->vm->pushHostArrayValue(groupArray, elem);
          }
          ctx_->vm->setHostObjectField(result, pair.first,
                                       BytecodeValue(groupArray));
        }
        return BytecodeValue(result);
      };

  // concat - concatenate two iterables
  options_.host_functions["concat"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2) {
          return BytecodeValue(nullptr);
        }

        ArrayRef result = ctx_->vm->createHostArray();

        // Add elements from first iterable
        IteratorRef iterRef1 = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef1);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!std::holds_alternative<std::nullptr_t>(valueVal)) {
            ctx_->vm->pushHostArrayValue(result, valueVal);
          }
        }

        // Add elements from second iterable
        IteratorRef iterRef2 = ctx_->vm->createIterator(args[1]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef2);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!std::holds_alternative<std::nullptr_t>(valueVal)) {
            ctx_->vm->pushHostArrayValue(result, valueVal);
          }
        }

        return BytecodeValue(result);
      };

  // merge - merge two objects
  options_.host_functions["merge"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2) {
          return BytecodeValue(nullptr);
        }

        ObjectRef result = ctx_->vm->createHostObject();

        // Copy properties from first object
        IteratorRef iterRef1 = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef1);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto keyVal = ctx_->vm->getHostObjectField(resultObjRef, "key");
          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!std::holds_alternative<std::nullptr_t>(keyVal) &&
              !std::holds_alternative<std::nullptr_t>(valueVal)) {
            ctx_->vm->setHostObjectField(result, std::get<std::string>(keyVal),
                                         valueVal);
          }
        }

        // Copy properties from second object
        IteratorRef iterRef2 = ctx_->vm->createIterator(args[1]);
        while (true) {
          auto iterResult = ctx_->vm->iteratorNext(iterRef2);
          if (!std::holds_alternative<ObjectRef>(iterResult))
            break;
          auto resultObjRef = std::get<ObjectRef>(iterResult);

          auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
          if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal))
            break;

          auto keyVal = ctx_->vm->getHostObjectField(resultObjRef, "key");
          auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
          if (!std::holds_alternative<std::nullptr_t>(keyVal) &&
              !std::holds_alternative<std::nullptr_t>(valueVal)) {
            ctx_->vm->setHostObjectField(result, std::get<std::string>(keyVal),
                                         valueVal);
          }
        }

        return BytecodeValue(result);
      };

  // join - join two iterables on matching keys
  options_.host_functions["join"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 4) {
          return BytecodeValue(nullptr);
        }
        // args: inner, outerKeySelector, innerKeySelector, resultSelector
        const auto &inner = args[0];
        const auto &outerKeySelector = args[1];
        const auto &innerKeySelector = args[2];
        const auto &resultSelector = args[3];

        // For now, return a simplified join (inner join)
        // This is a placeholder - full join requires more context
        ArrayRef result = ctx_->vm->createHostArray();

        // Create map of inner elements by key
        std::unordered_map<std::string, std::vector<BytecodeValue>> innerMap;
        IteratorRef innerIterRef = ctx_->vm->createIterator(inner);
        while (true) {
          auto innerIterResult = ctx_->vm->iteratorNext(innerIterRef);
          if (!std::holds_alternative<ObjectRef>(innerIterResult))
            break;
          auto innerResultObjRef = std::get<ObjectRef>(innerIterResult);

          auto innerDoneVal =
              ctx_->vm->getHostObjectField(innerResultObjRef, "done");
          if (std::holds_alternative<bool>(innerDoneVal) &&
              std::get<bool>(innerDoneVal))
            break;

          auto innerValueVal =
              ctx_->vm->getHostObjectField(innerResultObjRef, "value");
          if (std::holds_alternative<std::nullptr_t>(innerValueVal))
            continue;

          // Get key from selector
          std::vector<BytecodeValue> innerSelectorArgs{innerValueVal};
          auto innerKeyVal =
              ctx_->vm->callFunction(innerKeySelector, innerSelectorArgs);

          std::string innerKeyStr;
          if (std::holds_alternative<std::string>(innerKeyVal)) {
            innerKeyStr = std::get<std::string>(innerKeyVal);
          } else if (std::holds_alternative<int64_t>(innerKeyVal)) {
            innerKeyStr = std::to_string(std::get<int64_t>(innerKeyVal));
          } else if (std::holds_alternative<double>(innerKeyVal)) {
            innerKeyStr = std::to_string(std::get<double>(innerKeyVal));
          } else {
            innerKeyStr = "null";
          }

          innerMap[innerKeyStr].push_back(innerValueVal);
        }

        // Iterate over outer elements and join
        IteratorRef outerIterRef = ctx_->vm->createIterator(args[0]);
        while (true) {
          auto outerIterResult = ctx_->vm->iteratorNext(outerIterRef);
          if (!std::holds_alternative<ObjectRef>(outerIterResult))
            break;
          auto outerResultObjRef = std::get<ObjectRef>(outerIterResult);

          auto outerDoneVal =
              ctx_->vm->getHostObjectField(outerResultObjRef, "done");
          if (std::holds_alternative<bool>(outerDoneVal) &&
              std::get<bool>(outerDoneVal))
            break;

          auto outerValueVal =
              ctx_->vm->getHostObjectField(outerResultObjRef, "value");
          if (std::holds_alternative<std::nullptr_t>(outerValueVal))
            continue;

          // Get key from selector
          std::vector<BytecodeValue> outerSelectorArgs{outerValueVal};
          auto outerKeyVal =
              ctx_->vm->callFunction(outerKeySelector, outerSelectorArgs);

          std::string outerKeyStr;
          if (std::holds_alternative<std::string>(outerKeyVal)) {
            outerKeyStr = std::get<std::string>(outerKeyVal);
          } else if (std::holds_alternative<int64_t>(outerKeyVal)) {
            outerKeyStr = std::to_string(std::get<int64_t>(outerKeyVal));
          } else if (std::holds_alternative<double>(outerKeyVal)) {
            outerKeyStr = std::to_string(std::get<double>(outerKeyVal));
          } else {
            outerKeyStr = "null";
          }

          // Join with inner elements
          if (innerMap.find(outerKeyStr) != innerMap.end()) {
            for (const auto &innerValue : innerMap[outerKeyStr]) {
              // Call result selector
              std::vector<BytecodeValue> resultArgs{outerValueVal, innerValue};
              auto resultVal =
                  ctx_->vm->callFunction(resultSelector, resultArgs);
              ctx_->vm->pushHostArrayValue(result, resultVal);
            }
          }
        }

        return BytecodeValue(result);
      };

  // Standalone aliases for pipeline support (e.g., clipboard.get | upper |
  // trim)
  options_.host_functions["upper"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0]))
          return BytecodeValue(nullptr);
        std::string s = std::get<std::string>(args[0]);
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return BytecodeValue(s);
      };
  options_.host_functions["lower"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0]))
          return BytecodeValue(nullptr);
        std::string s = std::get<std::string>(args[0]);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return BytecodeValue(s);
      };
  options_.host_functions["trim"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.empty() || !std::holds_alternative<std::string>(args[0]))
          return BytecodeValue(nullptr);
        std::string s = std::get<std::string>(args[0]);
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos)
          return BytecodeValue(std::string(""));
        size_t end = s.find_last_not_of(" \t\n\r");
        return BytecodeValue(s.substr(start, end - start + 1));
      };
  options_.host_functions["replace"] = options_.host_functions["any.replace"];

  // any.in(value, container) - membership test
  options_.host_functions["any.in"] =
      [this](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2) {
          return BytecodeValue(false);
        }
        const auto &value = args[0];
        const auto &container = args[1];

        // Check based on container type
        if (std::holds_alternative<ArrayRef>(container)) {
          return BytecodeValue(
              ctx_->vm->arrayContains(std::get<ArrayRef>(container), value));
        } else if (std::holds_alternative<std::string>(container)) {
          const auto &str = std::get<std::string>(container);
          if (std::holds_alternative<std::string>(value)) {
            const auto &substr = std::get<std::string>(value);
            return BytecodeValue(str.find(substr) != std::string::npos);
          }
          return BytecodeValue(false);
        } else if (std::holds_alternative<ObjectRef>(container)) {
          if (std::holds_alternative<std::string>(value)) {
            const auto &key = std::get<std::string>(value);
            return BytecodeValue(
                ctx_->vm->objectHasKey(std::get<ObjectRef>(container), key));
          }
          return BytecodeValue(false);
        } else if (std::holds_alternative<RangeRef>(container)) {
          if (!std::holds_alternative<int64_t>(value)) {
            return BytecodeValue(false);
          }
          int64_t val = std::get<int64_t>(value);
          return BytecodeValue(
              ctx_->vm->isInRange(std::get<RangeRef>(container), val));
        }
        return BytecodeValue(false);
      };

  // any.not_in(value, container) - negated membership test
  options_.host_functions["any.not_in"] =
      [this](const std::vector<BytecodeValue> &args) {
        auto result = options_.host_functions["any.in"](args);
        if (std::holds_alternative<bool>(result)) {
          return BytecodeValue(!std::get<bool>(result));
        }
        return BytecodeValue(true); // If in() failed, not_in is true
      };

  // any(iterable, predicate) - check if any element satisfies predicate
  options_.host_functions["any"] = [this](
                                       const std::vector<BytecodeValue> &args) {
    if (args.size() < 2) {
      return BytecodeValue(false);
    }

    const auto &iterable = args[0];
    const auto &predicate = args[1];

    // Predicate should be a function
    if (!std::holds_alternative<HostFunctionRef>(predicate)) {
      return BytecodeValue(false);
    }

    const std::string &fnName = std::get<HostFunctionRef>(predicate).name;

    // Create iterator
    IteratorRef iterRef = ctx_->vm->createIterator(iterable);

    // Iterate and check predicate
    while (true) {
      auto result = ctx_->vm->iteratorNext(iterRef);

      // Check if done using helper
      if (!std::holds_alternative<ObjectRef>(result)) {
        return BytecodeValue(false);
      }
      auto resultObjRef = std::get<ObjectRef>(result);

      // Get done flag
      auto doneVal = ctx_->vm->getHostObjectField(resultObjRef, "done");
      if (std::holds_alternative<bool>(doneVal) && std::get<bool>(doneVal)) {
        return BytecodeValue(false); // Reached end, no match found
      }

      // Get value
      auto valueVal = ctx_->vm->getHostObjectField(resultObjRef, "value");
      if (std::holds_alternative<std::nullptr_t>(valueVal)) {
        continue;
      }

      // Call predicate with value
      std::vector<BytecodeValue> predArgs;
      predArgs.push_back(valueVal);
      auto predResult = ctx_->vm->callFunction(
          BytecodeValue(HostFunctionRef{fnName}), predArgs);

      if (std::holds_alternative<bool>(predResult) &&
          std::get<bool>(predResult)) {
        return BytecodeValue(true); // Found a match
      }
    }
  };

  // Type system functions
  options_.host_functions["type.of"] =
      [](const std::vector<BytecodeValue> &args) {
        if (args.empty()) {
          return BytecodeValue(std::string("null"));
        }
        const auto &val = args[0];
        if (std::holds_alternative<std::nullptr_t>(val))
          return BytecodeValue(std::string("null"));
        if (std::holds_alternative<bool>(val))
          return BytecodeValue(std::string("bool"));
        if (std::holds_alternative<int64_t>(val))
          return BytecodeValue(std::string("int"));
        if (std::holds_alternative<double>(val))
          return BytecodeValue(std::string("num"));
        if (std::holds_alternative<std::string>(val))
          return BytecodeValue(std::string("string"));
        if (std::holds_alternative<ArrayRef>(val))
          return BytecodeValue(std::string("array"));
        if (std::holds_alternative<ObjectRef>(val))
          return BytecodeValue(std::string("object"));
        if (std::holds_alternative<RangeRef>(val))
          return BytecodeValue(std::string("range"));
        return BytecodeValue(std::string("unknown"));
      };

  options_.host_functions["type.is"] =
      [](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2) {
          return BytecodeValue(false);
        }
        const auto &val = args[0];
        if (!std::holds_alternative<std::string>(args[1])) {
          return BytecodeValue(false);
        }
        std::string typeName = std::get<std::string>(args[1]);
        if (typeName == "null")
          return BytecodeValue(std::holds_alternative<std::nullptr_t>(val));
        if (typeName == "bool")
          return BytecodeValue(std::holds_alternative<bool>(val));
        if (typeName == "int")
          return BytecodeValue(std::holds_alternative<int64_t>(val));
        if (typeName == "num" || typeName == "float")
          return BytecodeValue(std::holds_alternative<double>(val));
        if (typeName == "string")
          return BytecodeValue(std::holds_alternative<std::string>(val));
        if (typeName == "array")
          return BytecodeValue(std::holds_alternative<ArrayRef>(val));
        if (typeName == "object")
          return BytecodeValue(std::holds_alternative<ObjectRef>(val));
        return BytecodeValue(false);
      };

  options_.host_functions["implements"] =
      [](const std::vector<BytecodeValue> &args) {
        // Placeholder - full trait system requires type metadata
        // For now, return false for all checks
        (void)args;
        return BytecodeValue(false);
      };

  // Run vm_setup callbacks
  for (auto &setupFn : vm_setup_callbacks_) {
    setupFn(*static_cast<VM *>(ctx_->vm));
  }
}

void HostBridge::registerModule(const HostModule &module) {
  modules_.push_back(module);
}

void HostBridge::addVmSetup(std::function<void(VM &)> setupFn) {
  vm_setup_callbacks_.push_back(std::move(setupFn));
}

void HostBridge::registerExtensionFunction(const std::string &fullName,
                                           BytecodeHostFunction fn) {
  options_.host_functions[fullName] = std::move(fn);
}

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx) {
  return std::make_shared<HostBridge>(ctx);
}

std::shared_ptr<HostBridge> createHostBridge(const havel::HostContext &ctx,
                                             const ExecutionPolicy &policy) {
  return std::make_shared<HostBridge>(ctx, policy);
}

bool HostBridge::import(const std::string &importSpec) {
  if (!ctx_->vm) {
    return false;
  }
  return moduleLoader_.import(importSpec, *ctx_->vm);
}

void HostBridge::loadExtension(const std::string &name) {
  if (extensionLoader_) {
    extensionLoader_->loadExtensionByName(name);
  }
}

} // namespace havel::compiler
