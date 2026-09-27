// SystemBridge implementation — extracted from ModularHostBridges.cpp when
// the file was split per-domain. No behavior change.

#include "../ModularHostBridges.hpp"
#include "BridgesInternal.hpp"

namespace havel::compiler {
void SystemBridge::install(PipelineOptions &options) {
  // Register internal function handlers
  options.host_functions["system.detect"] = [ctx = ctx_](const auto &args) {
    return handleSystemDetect(args, ctx);
  };
  options.host_functions["system.hardware"] = [ctx = ctx_](const auto &args) {
    return handleSystemHardware(args, ctx);
  };

  // Create system objects and extension object via initializer
  // This runs after all host functions are registered
  options.system_object_initializer = [](compiler::VM *vm) {
    // System object
    auto systemObj = vm->createHostObject();
    auto systemObjGuard = vm->makeRoot(Value::makeObjectId(systemObj.id));
    vm->setHostObjectField(
        systemObj, "detect",
        Value::makeHostFuncId(vm->getHostFunctionIndex("system.detect")));
    vm->setHostObjectField(
        systemObj, "hardware",
        Value::makeHostFuncId(vm->getHostFunctionIndex("system.hardware")));
    vm->setGlobal("system", Value::makeObjectId(systemObj.id));

    // Process object
    auto processObj = vm->createHostObject();
    auto processObjGuard = vm->makeRoot(Value::makeObjectId(processObj.id));
    vm->setHostObjectField(
        processObj, "find",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.find")));
    vm->setHostObjectField(
        processObj, "exists",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.exists")));
    vm->setHostObjectField(
        processObj, "kill",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.kill")));
    vm->setHostObjectField(
        processObj, "nice",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.nice")));
    vm->setHostObjectField(
        processObj, "run",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.run")));
    vm->setHostObjectField(
        processObj, "runDetached",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.runDetached")));
    vm->setHostObjectField(
        processObj, "spawn",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.spawn")));
    vm->setHostObjectField(
        processObj, "wait",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.wait")));
    vm->setHostObjectField(
        processObj, "killObj",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.killObj")));
    vm->setHostObjectField(
        processObj, "exit",
        Value::makeHostFuncId(vm->getHostFunctionIndex("sys.exit")));
    vm->setHostObjectField(
        processObj, "pid",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.pid")));
    vm->setHostObjectField(
        processObj, "ppid",
        Value::makeHostFuncId(vm->getHostFunctionIndex("process.ppid")));
    vm->setGlobal("process", Value::makeObjectId(processObj.id));

    // Extension object
    auto extensionObj = vm->createHostObject();
    auto extensionObjGuard = vm->makeRoot(Value::makeObjectId(extensionObj.id));
    vm->setHostObjectField(
        extensionObj, "load",
        Value::makeHostFuncId(vm->getHostFunctionIndex("extension.load")));
    vm->setHostObjectField(
        extensionObj, "isLoaded",
        Value::makeHostFuncId(vm->getHostFunctionIndex("extension.isLoaded")));
    vm->setHostObjectField(
        extensionObj, "list",
        Value::makeHostFuncId(vm->getHostFunctionIndex("extension.list")));
    vm->setHostObjectField(
        extensionObj, "addSearchPath",
        Value::makeHostFuncId(vm->getHostFunctionIndex("extension.addSearchPath")));
    vm->setGlobal("extension", Value::makeObjectId(extensionObj.id));

  // Mouse object
    auto mouseObj = vm->createHostObject();
    auto mouseObjGuard = vm->makeRoot(Value::makeObjectId(mouseObj.id));
    vm->setHostObjectField(
 mouseObj, "click",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.click")));
 vm->setHostObjectField(
 mouseObj, "down",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.down")));
 vm->setHostObjectField(
 mouseObj, "up",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.up")));
 vm->setHostObjectField(
 mouseObj, "move",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.move")));
 vm->setHostObjectField(
 mouseObj, "moveRel",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.moveRel")));
 vm->setHostObjectField(
 mouseObj, "scroll",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.scroll")));
 vm->setHostObjectField(
 mouseObj, "pos",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.pos")));
 vm->setHostObjectField(
 mouseObj, "setSpeed",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.setSpeed")));
 vm->setHostObjectField(
 mouseObj, "setAccel",
 Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.setAccel")));
  vm->setHostObjectField(
  mouseObj, "setDPI",
  Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.setDPI")));
vm->setHostObjectField(
    mouseObj, "state",
    Value::makeHostFuncId(vm->getHostFunctionIndex("io._mouseState")));
vm->setHostObjectField(
    mouseObj, "lastButton",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.lastButton")));
vm->setHostObjectField(
    mouseObj, "lastState",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.lastButtonState")));
vm->setHostObjectField(
    mouseObj, "buttons",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.buttons")));
  vm->setHostObjectField(
mouseObj, "scroll",
    Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.scroll")));
  vm->setHostObjectField(
    mouseObj, "reset",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.releaseAll")));
  vm->setGlobal("mouse", Value::makeObjectId(mouseObj.id));

  // IO object
    auto ioObj = vm->createHostObject();
    auto ioObjGuard = vm->makeRoot(Value::makeObjectId(ioObj.id));
    vm->setHostObjectField(
 ioObj, "send",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.send")));
 vm->setHostObjectField(
 ioObj, "sendKey",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.sendKey")));
 vm->setHostObjectField(
 ioObj, "sendText",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.sendText")));
 vm->setHostObjectField(
 ioObj, "wait",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.wait")));
 vm->setHostObjectField(
 ioObj, "click",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.click")));
 vm->setHostObjectField(
 ioObj, "mouseMoveTo",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.mouseMoveTo")));
 vm->setHostObjectField(
 ioObj, "mouseMoveRel",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.mouseMoveRel")));
  vm->setHostObjectField(
  ioObj, "mouseScroll",
  Value::makeHostFuncId(vm->getHostFunctionIndex("io.mouseScroll")));
  vm->setHostObjectField(
  ioObj, "scroll",
  Value::makeHostFuncId(vm->getHostFunctionIndex("io.scroll")));
  vm->setHostObjectField(
  ioObj, "getKey",
  Value::makeHostFuncId(vm->getHostFunctionIndex("io.getKey")));
  vm->setHostObjectField(
  ioObj, "isKeyPressed",
  Value::makeHostFuncId(vm->getHostFunctionIndex("io.isKeyPressed")));
  vm->setHostObjectField(
  ioObj, "mouseDown",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.mouseDown")));
 vm->setHostObjectField(
 ioObj, "mouseUp",
 Value::makeHostFuncId(vm->getHostFunctionIndex("io.mouseUp")));
 vm->setHostObjectField(
 ioObj, "suspend",
 Value::makeHostFuncId(vm->getHostFunctionIndex("suspend")));
    vm->setHostObjectField(
        ioObj, "getClipboard",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io.getClipboard")));
    vm->setHostObjectField(
        ioObj, "getExecutorMode",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io.getExecutorMode")));
    vm->setHostObjectField(
        ioObj, "setExecutorMode",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io.setExecutorMode")));
    vm->setHostObjectField(
        ioObj, "state",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io.state")));
    vm->setHostObjectField(
        ioObj, "modifiers",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io._getCurrentModifiers")));
    vm->setHostObjectField(
        ioObj, "sendModifiers",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io.sendModifiers")));
    vm->setHostObjectField(
        ioObj, "setDevice",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io._addDevice")));
    vm->setHostObjectField(
        ioObj, "device",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io._devices")));
    vm->setHostObjectField(
        ioObj, "setLock",
        Value::makeHostFuncId(vm->getHostFunctionIndex("io._setLock")));
    vm->setHostObjectField(
ioObj, "locks",
    Value::makeHostFuncId(vm->getHostFunctionIndex("io._locks")));
  vm->setHostObjectField(
    ioObj, "keys",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.keys")));
  vm->setHostObjectField(
    ioObj, "lastKey",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.lastKey")));
  vm->setHostObjectField(
    ioObj, "lastState",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.lastState")));
  vm->setHostObjectField(
    ioObj, "lastDevice",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.lastDevice")));
  vm->setHostObjectField(
    ioObj, "lastModifiers",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.lastModifiers")));
  vm->setHostObjectField(
    ioObj, "lastLocks",
    Value::makeHostFuncId(vm->getHostFunctionIndex("io._lastLocks")));
  vm->setHostObjectField(
    ioObj, "lastKeys",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.lastKeys")));
  vm->setHostObjectField(
    ioObj, "reset",
    Value::makeHostFuncId(vm->getHostFunctionIndex("eventListener.reset")));
  vm->setGlobal("io", Value::makeObjectId(ioObj.id));

  // altTab object
  auto altTabObj = vm->createHostObject();
  auto altTabObjGuard = vm->makeRoot(Value::makeObjectId(altTabObj.id));
  vm->setHostObjectField(
      altTabObj, "show",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._show")));
  vm->setHostObjectField(
      altTabObj, "hide",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._hide")));
  vm->setHostObjectField(
      altTabObj, "toggle",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._toggle")));
  vm->setHostObjectField(
      altTabObj, "next",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._next")));
  vm->setHostObjectField(
      altTabObj, "prev",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._prev")));
  vm->setHostObjectField(
      altTabObj, "select",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._select")));
  vm->setHostObjectField(
      altTabObj, "refresh",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._refresh")));
  vm->setHostObjectField(
      altTabObj, "getWindows",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._getWindows")));
  vm->setHostObjectField(
      altTabObj, "getWindowCount",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._getWindowCount")));
  vm->setHostObjectField(
      altTabObj, "setThumbnailSize",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._setThumbnailSize")));
  vm->setHostObjectField(
      altTabObj, "getThumbnailWidth",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._getThumbnailWidth")));
  vm->setHostObjectField(
      altTabObj, "getThumbnailHeight",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._getThumbnailHeight")));
  vm->setHostObjectField(
      altTabObj, "setMaxVisibleWindows",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._setMaxVisibleWindows")));
  vm->setHostObjectField(
      altTabObj, "getMaxVisibleWindows",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._getMaxVisibleWindows")));
  vm->setHostObjectField(
      altTabObj, "setAnimationsEnabled",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._setAnimationsEnabled")));
  vm->setHostObjectField(
      altTabObj, "isAnimationsEnabled",
      Value::makeHostFuncId(vm->getHostFunctionIndex("altTab._isAnimationsEnabled")));
  vm->setGlobal("altTab", Value::makeObjectId(altTabObj.id));

  // automation object
  auto automationObj = vm->createHostObject();
  auto automationObjGuard = vm->makeRoot(Value::makeObjectId(automationObj.id));
  vm->setHostObjectField(
      automationObj, "createAutoClicker",
      Value::makeHostFuncId(vm->getHostFunctionIndex("automation.createAutoClicker")));
  vm->setHostObjectField(
      automationObj, "createAutoRunner",
      Value::makeHostFuncId(vm->getHostFunctionIndex("automation.createAutoRunner")));
  vm->setHostObjectField(
      automationObj, "createAutoKeyPresser",
      Value::makeHostFuncId(vm->getHostFunctionIndex("automation.createAutoKeyPresser")));
  vm->setHostObjectField(
      automationObj, "hasTask",
      Value::makeHostFuncId(vm->getHostFunctionIndex("automation.hasTask")));
  vm->setHostObjectField(
      automationObj, "removeTask",
      Value::makeHostFuncId(vm->getHostFunctionIndex("automation.removeTask")));
  vm->setHostObjectField(
      automationObj, "stopAll",
      Value::makeHostFuncId(vm->getHostFunctionIndex("automation.stopAll")));
  vm->setHostObjectField(
      automationObj, "getTaskNames",
      Value::makeHostFuncId(vm->getHostFunctionIndex("automation.getTaskNames")));
  vm->setGlobal("automation", Value::makeObjectId(automationObj.id));
  vm->setGlobal("automation", Value::makeObjectId(automationObj.id));
    vm->setGlobal("scroll",
        Value::makeHostFuncId(vm->getHostFunctionIndex("mouse.scroll")));
  };

  // File operations
  options.host_functions["readFile"] = [ctx = ctx_](const auto &args) {
    return handleFileRead(args, ctx);
  };
  options.host_functions["writeFile"] = [ctx = ctx_](const auto &args) {
    return handleFileWrite(args, ctx);
  };
  options.host_functions["fileExists"] = [ctx = ctx_](const auto &args) {
    return handleFileExists(args, ctx);
  };
  options.host_functions["fileSize"] = [ctx = ctx_](const auto &args) {
    return handleFileSize(args, ctx);
  };
  options.host_functions["deleteFile"] = [ctx = ctx_](const auto &args) {
    return handleFileDelete(args, ctx);
  };
  options.host_functions["execute"] = [ctx = ctx_](const auto &args) {
    return handleProcessExecute(args, ctx);
  };
  options.host_functions["getpid"] = [ctx = ctx_](const auto &args) {
    return handleProcessGetPid(args, ctx);
  };
  options.host_functions["getppid"] = [ctx = ctx_](const auto &args) {
    return handleProcessGetPpid(args, ctx);
  };
  options.host_functions["process.find"] = [ctx = ctx_](const auto &args) {
    return handleProcessFind(args, ctx);
  };
  options.host_functions["process.exists"] = [ctx = ctx_](const auto &args) {
    return handleProcessExists(args, ctx);
  };
  options.host_functions["process.kill"] = [ctx = ctx_](const auto &args) {
    return handleProcessKill(args, ctx);
  };
  options.host_functions["process.nice"] = [ctx = ctx_](const auto &args) {
    return handleProcessNice(args, ctx);
  };
  options.host_functions["process.run"] = [ctx = ctx_](const auto &args) {
    return handleProcessRun(args, ctx);
  };
  options.host_functions["process.runDetached"] = [ctx =
                                                       ctx_](const auto &args) {
    return handleProcessRunDetached(args, ctx);
  };
  // Global aliases for convenience
  options.host_functions["run"] = [ctx = ctx_](const auto &args) {
    return handleProcessRun(args, ctx);
  };
  options.host_functions["runCapture"] = [ctx = ctx_](const auto &args) {
    return handleProcessRunCapture(args, ctx);
  };
  options.host_functions["runDetached"] = [ctx = ctx_](const auto &args) {
    return handleProcessRunDetached(args, ctx);
  };
  options.host_functions["play"] = [ctx = ctx_](const auto &args) {
    return handleMediaPlay(args, ctx);
  };
}


Value
SystemBridge::handleFileRead(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("readFile() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("readFile() requires a string path");
  }
  ::havel::host::FileSystemService fs;
  std::string content = fs.readFile(*path);
  if (content.empty()) {
    return Value::makeNull();
  }
  return Value::makeNull();
}


Value
SystemBridge::handleFileWrite(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("writeFile() requires path and content");
  }
  const std::string *path = nullptr;
  const std::string *content = nullptr;
  if (!path || !content) {
    throw std::runtime_error("writeFile() requires string arguments");
  }
  ::havel::host::FileSystemService fs;
  return Value::makeNull();
}


Value
SystemBridge::handleFileExists(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileExists() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("fileExists() requires a string path");
  }
  ::havel::host::FileSystemService fs;
  return Value::makeNull();
}


Value
SystemBridge::handleFileSize(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileSize() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("fileSize() requires a string path");
  }
  ::havel::host::FileSystemService fs;
  if (!fs.exists(*path)) {
    return Value::makeInt(static_cast<int64_t>(0));
  }
  return Value::makeNull();
}


Value
SystemBridge::handleFileDelete(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("deleteFile() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("deleteFile() requires a string path");
  }
  ::havel::host::FileSystemService fs;
  return Value::makeNull();
}


Value
SystemBridge::handleProcessExecute(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("execute() requires a command");
  }
  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }
  std::string command;
  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  if (args[0].isStringValId() || args[0].isStringId()) {
    command = vm->resolveStringKey(args[0]);
  } else {
    throw std::runtime_error("execute() requires a string command");
  }

  auto result = ::havel::Launcher::runSync(command);
  auto outRef = vm->getHeap().allocateString(result.stdout);
  return Value::makeStringId(outRef.id);
}


Value
SystemBridge::handleProcessGetPid(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeInt(
      static_cast<int64_t>(::havel::host::ProcessService::getCurrentPid()));
}


Value
SystemBridge::handleProcessGetPpid(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeInt(
      static_cast<int64_t>(::havel::host::ProcessService::getParentPid()));
}


Value
SystemBridge::handleProcessFind(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("process.find() requires a process name");
  }
  std::string name;
  if (args[0].isStringValId() || args[0].isStringId()) {
    auto *vm = static_cast<VM *>(ctx->vm);
    name = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
  } else {
    throw std::runtime_error("process.find() requires a string argument");
  }
  auto pids = ::havel::host::ProcessService::findProcesses(name);
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  auto arr = vm->createHostArray();
  for (int32_t pid : pids) {
    vm->pushHostArrayValue(arr, Value::makeInt(static_cast<int64_t>(pid)));
  }
  return Value::makeArrayId(arr.id);
}


Value
SystemBridge::handleProcessExists(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.exists() requires a process name or PID");
  }
  if (args[0].isInt()) {
    int32_t pid = static_cast<int32_t>(args[0].asInt());
    return Value(::havel::host::ProcessService::isProcessAlive(pid));
  }
  std::string name;
  if (args[0].isStringValId() || args[0].isStringId()) {
    auto *vm = static_cast<VM *>(ctx->vm);
    name = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
  } else {
    throw std::runtime_error("process.exists() requires a string or number");
  }
  return Value(::havel::host::ProcessService::processExists(name));
}


Value
SystemBridge::handleProcessKill(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("process.kill() requires PID and signal");
  }
  int32_t pid = 0;
  if (args[0].isInt()) {
    pid = static_cast<int32_t>(args[0].asInt());
  } else {
    throw std::runtime_error("process.kill() requires a number PID");
  }
  std::string sig;
  if (args[1].isStringValId() || args[1].isStringId()) {
    auto *vm = static_cast<VM *>(ctx->vm);
    sig = vm ? vm->resolveStringKey(args[1]) : strVal(args[1], ctx ? ctx->vm : nullptr);
  } else {
    throw std::runtime_error("process.kill() requires a string signal");
  }
  int signal_num = 15; // Default SIGTERM
  if (sig == "SIGKILL" || sig == "kill")
    signal_num = 9;
  else if (sig == "SIGTERM" || sig == "term")
    signal_num = 15;
  else if (sig == "SIGHUP" || sig == "hangup")
    signal_num = 1;
  else if (sig == "SIGINT" || sig == "int")
    signal_num = 2;
  return Value::makeBool(
      ::havel::host::ProcessService::sendSignal(pid, signal_num));
}


Value
SystemBridge::handleProcessNice(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("process.nice() requires PID and nice value");
  }
  int32_t pid = 0;
  if (args[0].isInt()) {
    pid = static_cast<int32_t>(args[0].asInt());
  } else {
    throw std::runtime_error("process.nice() requires a number PID");
  }
  int64_t nice = 0;
  if (args[1].isInt()) {
    nice = args[1].asInt();
  } else {
    throw std::runtime_error("process.nice() requires a number nice value");
  }
  // Nice range: -20 (highest priority) to 19 (lowest)
  if (nice < -20 || nice > 19) {
    throw std::runtime_error(
        "process.nice() nice value must be between -20 and 19");
  }
  return Value::makeBool(
      ::havel::host::ProcessService::setNice(pid, static_cast<int>(nice)));
}


Value
SystemBridge::handleProcessRun(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.run() requires a command");
  }
  std::string cmd;
  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  if (args[0].isStringValId() || args[0].isStringId()) {
    cmd = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
  } else if (args[0].isArrayId()) {
    auto arr = ArrayRef{args[0].asArrayId()};
    size_t len = vm->getHostArrayLength(arr);
    if (len == 0) {
      throw std::runtime_error("process.run() requires a non-empty array");
    }
    cmd = vm->resolveStringKey(vm->getHostArrayValue(arr, 0));
    for (size_t i = 1; i < len; ++i) {
      cmd += " " + vm->resolveStringKey(vm->getHostArrayValue(arr, i));
    }
  } else {
    throw std::runtime_error("process.run() requires a string or array command");
  }
  // Subprocess spawn+wait: runs on a worker under the A+C model; the
  // result object is built VM-side on resume.
  auto *vm_ = vm;
  compiler::VMApi api(*vm);
  return api.runBlocking(
      [cmd]() -> compiler::AsyncCxxResult {
        auto result = ::havel::Launcher::run(cmd, ::havel::LaunchParams{});
        return std::static_pointer_cast<void>(
            std::make_shared<::havel::ProcessResult>(std::move(result)));
      },
      [vm_](const compiler::AsyncCxxResult &cell) -> Value {
        auto result =
            std::static_pointer_cast<::havel::ProcessResult>(cell);
        auto obj = vm_->createHostObject();
        auto guard = vm_->makeRoot(Value::makeObjectId(obj.id));
        vm_->setHostObjectField(obj, "pid", Value::makeInt(result->pid));
        vm_->setHostObjectField(obj, "exitCode", Value::makeInt(result->exitCode));
        vm_->setHostObjectField(obj, "success", Value::makeBool(result->success));
        if (result->error.empty()) {
            vm_->setHostObjectField(obj, "error", Value::makeNull());
        } else {
            auto errRef = vm_->getHeap().allocateString(result->error);
            vm_->setHostObjectField(obj, "error", Value::makeStringId(errRef.id));
        }
        auto outRef = vm_->getHeap().allocateString(result->stdout);
        vm_->setHostObjectField(obj, "stdout", Value::makeStringId(outRef.id));
        auto errOutRef = vm_->getHeap().allocateString(result->stderr);
        vm_->setHostObjectField(obj, "stderr", Value::makeStringId(errOutRef.id));
        return Value::makeObjectId(obj.id);
      });
}


Value
SystemBridge::handleProcessRunCapture(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("runCapture() requires a command");
  }
  std::string cmd;
  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  if (args[0].isStringValId() || args[0].isStringId()) {
    cmd = vm ? vm->toString(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
  } else if (args[0].isArrayId()) {
    auto arr = ArrayRef{args[0].asArrayId()};
    size_t len = vm->getHostArrayLength(arr);
    if (len == 0) {
      throw std::runtime_error("runCapture() requires a non-empty array");
    }
    cmd = vm->resolveStringKey(vm->getHostArrayValue(arr, 0));
    for (size_t i = 1; i < len; ++i) {
      cmd += " " + vm->resolveStringKey(vm->getHostArrayValue(arr, i));
    }
  } else {
    throw std::runtime_error("runCapture() requires a string or array command");
  }
  // Subprocess spawn+wait+capture: worker-side under the A+C model.
  auto *vm_ = vm;
  compiler::VMApi api(*vm);
  return api.runBlocking(
      [cmd]() -> compiler::AsyncCxxResult {
        auto result = ::havel::Launcher::runSync(cmd);
        return std::static_pointer_cast<void>(
            std::make_shared<std::string>(std::move(result.stdout)));
      },
      [vm_](const compiler::AsyncCxxResult &cell) -> Value {
        auto out = std::static_pointer_cast<std::string>(cell);
        auto strRef = vm_->getHeap().allocateString(*out);
        return Value::makeStringId(strRef.id);
      });
}


Value
SystemBridge::handleProcessRunDetached(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.runDetached() requires a command");
  }
  std::string cmd;
  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  if (args[0].isStringValId() || args[0].isStringId()) {
    cmd = vm ? vm->resolveStringKey(args[0]) : strVal(args[0], ctx ? ctx->vm : nullptr);
  } else if (args[0].isArrayId()) {
    auto arr = ArrayRef{args[0].asArrayId()};
    size_t len = vm->getHostArrayLength(arr);
    if (len == 0) {
      throw std::runtime_error("process.runDetached() requires a non-empty array");
    }
    cmd = vm->resolveStringKey(vm->getHostArrayValue(arr, 0));
    for (size_t i = 1; i < len; ++i) {
      cmd += " " + vm->resolveStringKey(vm->getHostArrayValue(arr, i));
    }
  } else {
    throw std::runtime_error("process.runDetached() requires a string or array command");
  }
  auto result = ::havel::Launcher::runDetached(cmd);
  return Value::makeInt(result.pid);
}


// Alias implementations - forward to appropriate bridge handlers
Value
SystemBridge::handleMediaPlay(const std::vector<Value> &args,
                              const HostContext *ctx) {
  return MediaBridge::handleMediaPlay(args, ctx);
}

// ============================================================================
// System Detection Implementation
// ============================================================================


Value
SystemBridge::handleSystemDetect(const std::vector<Value> &args,
                                  const HostContext *ctx) {
    (void)args;
    if (debugging::debug_io) ::havel::warn("[SystemDetect] ctx={} ctx->vm={}", static_cast<const void*>(ctx),
        ctx ? static_cast<const void*>(ctx->vm) : nullptr);

    if (!ctx || !ctx->vm) {
        return Value::makeNull();
    }

  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  auto guard = vm->makeRoot(Value::makeObjectId(obj.id));

  auto sysInfo = ::havel::HardwareDetector::detectSystem();
    if (debugging::debug_io) ::havel::warn("[SystemDetect] detected OS={}", sysInfo.os);

  // Allocate strings on heap for non-empty values
  auto makeStr = [vm](const std::string &s) -> Value {
    if (s.empty()) return Value::makeNull();
    auto ref = vm->getHeap().allocateString(s);
    return Value::makeStringId(ref.id);
  };

    vm->setHostObjectField(obj, "os", makeStr(sysInfo.os));
    if (debugging::debug_io) ::havel::warn("[SystemDetect] set os field");
    vm->setHostObjectField(obj, "shell", makeStr(sysInfo.shell));
    vm->setHostObjectField(obj, "user", makeStr(sysInfo.user));
    vm->setHostObjectField(obj, "home", makeStr(sysInfo.home));
    vm->setHostObjectField(obj, "hostname", makeStr(sysInfo.hostname));
    if (debugging::debug_io) ::havel::warn("[SystemDetect] all fields set, returning");

  // Linux-specific fields (always set, even if empty)
  vm->setHostObjectField(obj, "displayProtocol",
                         makeStr(sysInfo.displayProtocol));
  vm->setHostObjectField(obj, "display", makeStr(sysInfo.display));
  const std::string window_manager =
      sysInfo.windowManager.empty() ? "unknown" : sysInfo.windowManager;
  vm->setHostObjectField(obj, "windowManager", makeStr(window_manager));
  vm->setHostObjectField(obj, "desktopEnv",
                         makeStr(sysInfo.desktopEnv));

  return Value::makeObjectId(obj.id);
}


Value
SystemBridge::handleSystemHardware(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  (void)args;

  if (!ctx || !ctx->vm) {
    return Value::makeNull();
  }

  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  auto guard = vm->makeRoot(Value::makeObjectId(obj.id));

  // Use HardwareDetector for hardware detection
  auto hwInfo = ::havel::HardwareDetector::detectHardware();

  // Helper to create string or null
  auto makeStr = [vm](const std::string &s) -> Value {
    if (s.empty()) return Value::makeNull();
    auto ref = vm->getHeap().allocateString(s);
    return Value::makeStringId(ref.id);
  };

  vm->setHostObjectField(obj, "cpu", makeStr(hwInfo.cpu));
  vm->setHostObjectField(obj, "cpuCores",
                         Value::makeInt(static_cast<int64_t>(hwInfo.cpuCores)));
  vm->setHostObjectField(
      obj, "cpuThreads",
      Value::makeInt(static_cast<int64_t>(hwInfo.cpuThreads)));
  vm->setHostObjectField(obj, "cpuFrequency",
                         Value::makeDouble(hwInfo.cpuFrequency));
  vm->setHostObjectField(obj, "cpuUsage", Value::makeDouble(hwInfo.cpuUsage));
  vm->setHostObjectField(obj, "gpu", makeStr(hwInfo.gpu));
  vm->setHostObjectField(obj, "gpuTemperature",
                         Value::makeDouble(hwInfo.gpuTemperature));

  // Memory info (all in bytes)
  vm->setHostObjectField(obj, "ramTotal",
                         Value::makeInt(static_cast<int64_t>(hwInfo.ramTotal)));
  vm->setHostObjectField(obj, "ramUsed",
                         Value::makeInt(static_cast<int64_t>(hwInfo.ramUsed)));
  vm->setHostObjectField(obj, "ramFree",
                         Value::makeInt(static_cast<int64_t>(hwInfo.ramFree)));

  // Swap info (in bytes)
  vm->setHostObjectField(obj, "swapTotal",
                         Value::makeInt(static_cast<int64_t>(hwInfo.swapTotal)));
  vm->setHostObjectField(obj, "swapUsed",
                         Value::makeInt(static_cast<int64_t>(hwInfo.swapUsed)));
  vm->setHostObjectField(obj, "swapFree",
                         Value::makeInt(static_cast<int64_t>(hwInfo.swapFree)));

  vm->setHostObjectField(obj, "motherboard", makeStr(hwInfo.motherboard));
  vm->setHostObjectField(obj, "bios", makeStr(hwInfo.bios));
  vm->setHostObjectField(obj, "cpuTemperature",
                         Value::makeDouble(hwInfo.cpuTemperature));

  // Storage array
  auto storageArr = vm->createHostArray();
  auto storageArrGuard = vm->makeRoot(Value::makeArrayId(storageArr.id));
  for (const auto &device : hwInfo.storage) {
    auto storageObj = vm->createHostObject();
    auto storageObjGuard = vm->makeRoot(Value::makeObjectId(storageObj.id));
    vm->setHostObjectField(storageObj, "name", makeStr(device.name));
    vm->setHostObjectField(storageObj, "model", makeStr(device.model));
    vm->setHostObjectField(storageObj, "size",
                           Value::makeInt(static_cast<int64_t>(device.size)));
    vm->setHostObjectField(storageObj, "used",
                           Value::makeInt(static_cast<int64_t>(device.used)));
    vm->setHostObjectField(storageObj, "free",
                           Value::makeInt(static_cast<int64_t>(device.free)));
    vm->setHostObjectField(storageObj, "type", makeStr(device.type));
    vm->setHostObjectField(storageObj, "mountPoint",
                           makeStr(device.mountPoint));
    vm->setHostObjectField(storageObj, "filesystem",
                           makeStr(device.filesystem));
    vm->pushHostArrayValue(storageArr, Value::makeObjectId(storageObj.id));
  }
  vm->setHostObjectField(obj, "storage", Value::makeArrayId(storageArr.id));

  return Value::makeObjectId(obj.id);
}

} // namespace havel::compiler
