/*
 * ModularHostBridges.cpp - Modular bridge component implementations
 */
#include "ModularHostBridges.hpp"
#include "havel-lang/stdlib/HotkeyModule.hpp"
#include "core/ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "core/HardwareDetector.hpp"
#include "core/HotkeyManager.hpp"
#include "core/IO.hpp"
#include "core/ModeManager.hpp"
#include "extensions/gui/clipboard_manager/ClipboardManager.hpp"
#include "extensions/gui/common/GUIManager.hpp"
#include "extensions/gui/screenshot_manager/ScreenshotManager.hpp"
#include "extensions/gui/common/SettingsWindow.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "host/app/AppService.hpp"
#include "host/async/AsyncService.hpp"
#include "host/audio/AudioService.hpp"
#include "extensions/gui/automation_suite/AutomationSuite.hpp"
#include "host/automation/AutomationService.hpp"
#include "host/brightness/BrightnessService.hpp"
#include "host/browser/BrowserService.hpp"
#include "host/chunker/TextChunkerService.hpp"
#include "host/filesystem/FileSystemService.hpp"
#include "host/hotkey/HotkeyService.hpp"
#include "host/io/IOService.hpp"
#include "host/io/MapManagerService.hpp"
#include "host/media/MediaService.hpp"
#include "host/network/NetworkService.hpp"
#include "host/process/ProcessService.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include "host/window/AltTabService.hpp"
#include "host/window/WindowService.hpp"
#include "media/AudioManager.hpp"
#include "media/MPVController.hpp"
#include "process/Launcher.hpp"
#include "window/WindowManager.hpp"
#include "window/WindowManagerDetector.hpp"

#include <QClipboard>
#include <QString>
#include <atomic>
#include <chrono>
#include <climits>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace havel::compiler {

namespace {

struct AsyncTaskRecord {
  bool running = false;
  bool completed = false;
  bool cancelled = false;
  Value result = nullptr;
  std::string error;
};

struct ChannelRecord {
  std::deque<Value> queue;
  bool closed = false;
};

struct ThreadRecord {
  CallbackId callback = INVALID_CALLBACK_ID;
  bool running = true;
  bool paused = false;
};

struct TimerRecord {
  CallbackId callback = INVALID_CALLBACK_ID;
  bool running = true;
  bool paused = false;
  bool repeating = false;
  int64_t delay_ms = 0;
  std::string task_id;
};

std::mutex g_async_mutex;
std::mutex g_vm_invoke_mutex;
std::atomic<uint64_t> g_next_task_id{1};
std::unordered_map<std::string, AsyncTaskRecord> g_async_tasks;
std::unordered_map<std::string, ChannelRecord> g_async_channels;
std::unordered_map<std::string, ThreadRecord> g_threads;
std::unordered_map<std::string, TimerRecord> g_timers;

std::string allocateTaskId() {
  return "task-" + std::to_string(g_next_task_id.fetch_add(1));
}

ObjectRef makeHandleObject(VM *vm, const std::string &kind,
                           const std::string &id) {
  auto obj = vm->createHostObject();
  // TODO: Store strings properly via string pool
  vm->setHostObjectField(obj, "__kind", Value::makeNull());
  vm->setHostObjectField(obj, "__id", Value::makeNull());
  (void)kind; (void)id;
  return obj;
}

std::optional<std::pair<std::string, std::string>>
extractHandle(const std::vector<Value> &args, VM *vm,
              size_t index = 0) {
  if (!vm || index >= args.size() ||
      !args[index].isObjectId()) {
    return std::nullopt;
  }
  // args[index] is already an ObjectId (uint64_t), not an ObjectRef
  uint64_t objId = args[index].asObjectId();
  ObjectRef obj{static_cast<uint32_t>(objId), true};
  Value kind = vm->getHostObjectField(obj, "__kind");
  Value id = vm->getHostObjectField(obj, "__id");
  // TODO: Retrieve strings properly from string pool
  (void)kind; (void)id;
  return std::nullopt; // TODO: implement string retrieval
}

} // namespace

// ============================================================================
// IOBridge Implementation
// ============================================================================

void IOBridge::install(PipelineOptions &options) {
  options.host_functions["send"] = [ctx = ctx_](const auto &args) {
    return handleSend(args, ctx);
  };
  options.host_functions["io.send"] = [ctx = ctx_](const auto &args) {
    return handleSend(args, ctx);
  };
  options.host_functions["io.sendKey"] = [ctx = ctx_](const auto &args) {
    return handleSendKey(args, ctx);
  };
  options.host_functions["io.sendText"] = [ctx = ctx_](const auto &args) {
    return handleSendText(args, ctx);
  };
  options.host_functions["io.wait"] = [ctx = ctx_](const auto &args) {
    return handleWait(args, ctx);
  };
  options.host_functions["wait"] = [ctx = ctx_](const auto &args) {
    return handleWait(args, ctx);
  };
}

Value IOBridge::handleSend(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return Value::makeBool(false);
  }
  ::havel::host::IOService ioService(ctx->io);
  if (false) { // TODO: string support
    return Value::makeNull();
  }
  return Value::makeBool(false);
}

Value IOBridge::handleSendKey(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return Value::makeBool(false);
  }
  ::havel::host::IOService ioService(ctx->io);
  if (false) { // TODO: string support
    return Value::makeNull();
  }
  return Value::makeBool(false);
}

Value IOBridge::handleSendText(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return Value::makeBool(false);
  }
  // TODO: string support - disabled until string pooling is implemented
  (void)args;
  return Value::makeBool(false);
#if 0
  if (false) { // TODO: string support
    // Use clipboard for reliable text input (handles all characters, spaces,
    // newlines)
    if (ctx->clipboardManager) {
      // Backup old clipboard text
      QString oldText = ctx->clipboardManager->getClipboard()->text();

      // Set new text
      ctx->clipboardManager->getClipboard()->setText(
          QString::fromStdString(*text));

      // Minimal delay - just enough for clipboard to sync
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Send Ctrl+V to paste
      ctx->io->Send("{LCtrl down}");
      ctx->io->Send("v");
      ctx->io->Send("{LCtrl up}");

      // Restore old clipboard
      ctx->clipboardManager->getClipboard()->setText(oldText);
    } else {
      // Fallback: use IO::SendText (handles clipboard backup/restore on
      // Windows) or key events on Linux
      ctx->io->SendText(*text);
    }
    return Value::makeBool(true);
  }
  return Value::makeBool(false);
#endif
}

Value IOBridge::handleWait(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  (void)ctx;
  if (args.empty() || !args[0].isInt()) {
    return Value::makeBool(false);
  }
  int64_t ms = args[0].asInt();
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  return Value::makeBool(true);
}

// ============================================================================
// SystemBridge Implementation
// ============================================================================

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
    vm->setHostObjectField(
        systemObj, "detect",
        Value::makeHostFuncId(vm->getHostFunctionIndex("system.detect")));
    vm->setHostObjectField(
        systemObj, "hardware",
        Value::makeHostFuncId(vm->getHostFunctionIndex("system.hardware")));
    vm->setGlobal("system", Value::makeObjectId(systemObj.id));

    // Process object
    auto processObj = vm->createHostObject();
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
    vm->setGlobal("process", Value::makeObjectId(processObj.id));

    // Extension object
    auto extensionObj = vm->createHostObject();
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
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("execute() requires a command");
  }
  const std::string *command = nullptr;
  if (!command) {
    throw std::runtime_error("execute() requires a string command");
  }
  std::ostringstream output;
  FILE *pipe = popen(command->c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("Failed to execute command: " + *command);
  }
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output << buffer;
  }
  pclose(pipe);
  return Value::makeNull();
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
    name = vm ? vm->resolveStringKey(args[0]) : args[0].toString();
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
    name = vm ? vm->resolveStringKey(args[0]) : args[0].toString();
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
    sig = vm ? vm->resolveStringKey(args[1]) : args[1].toString();
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
  const std::string *cmd = nullptr;
  if (!cmd) {
    throw std::runtime_error("process.run() requires a string command");
  }
  auto result = ::havel::Launcher::run(*cmd, {}, {});
  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "pid", Value::makeInt(result.pid));
  vm->setHostObjectField(obj, "exitCode", Value::makeInt(result.exitCode));
  vm->setHostObjectField(obj, "success", Value::makeBool(result.success));
  vm->setHostObjectField(obj, "error", Value::makeNull());
  vm->setHostObjectField(obj, "stdout", Value::makeNull());
  vm->setHostObjectField(obj, "stderr", Value::makeNull());
  return Value::makeObjectId(obj.id);
}

Value
SystemBridge::handleProcessRunDetached(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.runDetached() requires a command");
  }
  const std::string *cmd = nullptr;
  if (!cmd) {
    throw std::runtime_error("process.runDetached() requires a string command");
  }
  auto result = ::havel::Launcher::runDetached(*cmd);
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
  std::cerr << "[DEBUG] handleSystemDetect: ctx=" << ctx
            << " ctx->vm=" << (ctx ? ctx->vm : nullptr) << "\n";

  if (!ctx || !ctx->vm) {
    // Return a minimal object if VM is not available
    std::cerr << "[DEBUG] handleSystemDetect: ctx or vm is null, returning "
                 "empty string\n";
    return Value::makeNull();
  }

  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  std::cerr << "[DEBUG] handleSystemDetect: vm=" << vm << ", creating object\n";
  auto obj = vm->createHostObject();
  std::cerr << "[DEBUG] handleSystemDetect: object created, setting fields\n";

  // Use HardwareDetector for system detection
  auto sysInfo = ::havel::HardwareDetector::detectSystem();
  std::cerr << "[DEBUG] handleSystemDetect: detected OS=" << sysInfo.os << "\n";

  // Allocate strings on heap for non-empty values
  auto makeStr = [vm](const std::string &s) -> Value {
    if (s.empty()) return Value::makeNull();
    auto ref = vm->getHeap().allocateString(s);
    return Value::makeStringId(ref.id);
  };

  vm->setHostObjectField(obj, "os", makeStr(sysInfo.os));
  std::cerr << "[DEBUG] handleSystemDetect: set os field\n";
  vm->setHostObjectField(obj, "shell", makeStr(sysInfo.shell));
  vm->setHostObjectField(obj, "user", makeStr(sysInfo.user));
  vm->setHostObjectField(obj, "home", makeStr(sysInfo.home));
  vm->setHostObjectField(obj, "hostname", makeStr(sysInfo.hostname));
  std::cerr << "[DEBUG] handleSystemDetect: all fields set, returning\n";

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
  for (const auto &device : hwInfo.storage) {
    auto storageObj = vm->createHostObject();
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

// ============================================================================
// UIBridge Implementation
// ============================================================================

void UIBridge::install(PipelineOptions &options) {
  options.host_functions["window.active"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetActive(args, ctx);
  };
  options.host_functions["window.cmd"] = [ctx = ctx_](const auto &args) {
    return handleWindowCmd(args, ctx);
  };
  options.host_functions["window.find"] = [ctx = ctx_](const auto &args) {
    return handleWindowFind(args, ctx);
  };
  options.host_functions["window.close"] = [ctx = ctx_](const auto &args) {
    return handleWindowClose(args, ctx);
  };
  options.host_functions["window.resize"] = [ctx = ctx_](const auto &args) {
    return handleWindowResize(args, ctx);
  };
  options.host_functions["window.move"] = [ctx = ctx_](const auto &args) {
    return handleWindowMove(args, ctx);
  };
  options.host_functions["window.moveToMonitor"] =
      [ctx = ctx_](const auto &args) {
        return handleWindowMoveToMonitor(args, ctx);
      };
  options.host_functions["window.moveToNextMonitor"] =
      [ctx = ctx_](const auto &args) {
        return handleWindowMoveToNextMonitor(args, ctx);
      };
  options.host_functions["window.focus"] = [ctx = ctx_](const auto &args) {
    return handleWindowFocus(args, ctx);
  };
  options.host_functions["window.min"] = [ctx = ctx_](const auto &args) {
    return handleWindowMinimize(args, ctx);
  };
  options.host_functions["window.max"] = [ctx = ctx_](const auto &args) {
    return handleWindowMaximize(args, ctx);
  };
  options.host_functions["window.hide"] = [ctx = ctx_](const auto &args) {
    return handleWindowHide(args, ctx);
  };
  options.host_functions["window.show"] = [ctx = ctx_](const auto &args) {
    return handleWindowShow(args, ctx);
  };
  // Window query functions
  options.host_functions["window.any"] = [ctx = ctx_](const auto &args) {
    return handleWindowAny(args, ctx);
  };
  options.host_functions["window.count"] = [ctx = ctx_](const auto &args) {
    return handleWindowCount(args, ctx);
  };
  options.host_functions["window.filter"] = [ctx = ctx_](const auto &args) {
    return handleWindowFilter(args, ctx);
  };
  // Active window namespace functions
  options.host_functions["active.get"] = [ctx = ctx_](const auto &args) {
    return handleActiveGet(args, ctx);
  };
  options.host_functions["active.title"] = [ctx = ctx_](const auto &args) {
    return handleActiveTitle(args, ctx);
  };
  options.host_functions["active.class"] = [ctx = ctx_](const auto &args) {
    return handleActiveClass(args, ctx);
  };
  options.host_functions["active.exe"] = [ctx = ctx_](const auto &args) {
    return handleActiveExe(args, ctx);
  };
  options.host_functions["active.pid"] = [ctx = ctx_](const auto &args) {
    return handleActivePid(args, ctx);
  };
  options.host_functions["active.close"] = [ctx = ctx_](const auto &args) {
    return handleActiveClose(args, ctx);
  };
  options.host_functions["active.min"] = [ctx = ctx_](const auto &args) {
    return handleActiveMin(args, ctx);
  };
  options.host_functions["active.max"] = [ctx = ctx_](const auto &args) {
    return handleActiveMax(args, ctx);
  };
  options.host_functions["active.hide"] = [ctx = ctx_](const auto &args) {
    return handleActiveHide(args, ctx);
  };
  options.host_functions["active.show"] = [ctx = ctx_](const auto &args) {
    return handleActiveShow(args, ctx);
  };
  options.host_functions["active.move"] = [ctx = ctx_](const auto &args) {
    return handleActiveMove(args, ctx);
  };
  options.host_functions["active.resize"] = [ctx = ctx_](const auto &args) {
    return handleActiveResize(args, ctx);
  };
  // Window object prototype methods (shared, not per-instance)
  options.host_functions["window._close"] = [ctx = ctx_](const auto &args) {
    return handleWindowCloseObj(args, ctx);
  };
  options.host_functions["window._hide"] = [ctx = ctx_](const auto &args) {
    return handleWindowHideObj(args, ctx);
  };
  options.host_functions["window._show"] = [ctx = ctx_](const auto &args) {
    return handleWindowShowObj(args, ctx);
  };
  options.host_functions["window._focus"] = [ctx = ctx_](const auto &args) {
    return handleWindowFocusObj(args, ctx);
  };
  options.host_functions["window._min"] = [ctx = ctx_](const auto &args) {
    return handleWindowMinObj(args, ctx);
  };
  options.host_functions["window._max"] = [ctx = ctx_](const auto &args) {
    return handleWindowMaxObj(args, ctx);
  };
  options.host_functions["window._resize"] = [ctx = ctx_](const auto &args) {
    return handleWindowResizeObj(args, ctx);
  };
  options.host_functions["window._move"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveObj(args, ctx);
  };
  options.host_functions["clipboard.get"] = [ctx = ctx_](const auto &args) {
    return handleClipboardGet(args, ctx);
  };
  options.host_functions["clipboard.set"] = [ctx = ctx_](const auto &args) {
    return handleClipboardSet(args, ctx);
  };
  options.host_functions["clipboard.clear"] = [ctx = ctx_](const auto &args) {
    return handleClipboardClear(args, ctx);
  };
  options.host_functions["io.getClipboard"] = [ctx = ctx_](const auto &args) {
    return handleClipboardGet(args, ctx);
  };
  options.host_functions["screenshot.full"] = [ctx = ctx_](const auto &args) {
    return handleScreenshotFull(args, ctx);
  };
  options.host_functions["screenshot.monitor"] = [ctx =
                                                      ctx_](const auto &args) {
    return handleScreenshotMonitor(args, ctx);
  };
  // GUI notifications
  options.host_functions["gui.notify"] = [ctx = ctx_](const auto &args) {
    return handleGUINotify(args, ctx);
  };
}

// Helper: Create window object with data fields
// Methods are shared static functions that take window ID as first argument
static Value createWindowObject(
    VM *vm, const HostContext *ctx, uint64_t windowId,
    const std::string &title = "", const std::string &windowClass = "",
    const std::string &exe = "", int pid = 0, const std::string &cmdline = "") {
  if (!vm || !ctx || !ctx->windowManager) {
    return Value::makeNull();
  }

  VMApi api(*vm);
  auto obj = api.makeObject();
  api.setField(obj, "id", Value::makeInt(static_cast<int64_t>(windowId)));
  api.setField(obj, "title", Value::makeNull());
  api.setField(obj, "class", Value::makeNull());
  api.setField(obj, "exe", Value::makeNull());
  api.setField(obj, "pid", Value::makeInt(static_cast<int64_t>(pid)));
  api.setField(obj, "cmd", Value::makeNull());

  // Methods are shared - they take the object's id field as receiver
  // win.close() compiles to: window.close(win)
  api.setField(obj, "close", api.makeFunctionRef("window._close"));
  api.setField(obj, "hide", api.makeFunctionRef("window._hide"));
  api.setField(obj, "show", api.makeFunctionRef("window._show"));
  api.setField(obj, "focus", api.makeFunctionRef("window._focus"));
  api.setField(obj, "min", api.makeFunctionRef("window._min"));
  api.setField(obj, "max", api.makeFunctionRef("window._max"));
  api.setField(obj, "resize", api.makeFunctionRef("window._resize"));
  api.setField(obj, "move", api.makeFunctionRef("window._move"));

  return Value::makeObjectId(obj.asObjectId());
}

Value
UIBridge::handleWindowGetActive(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeNull();
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeNull();
  }
  return createWindowObject(static_cast<VM *>(ctx->vm), ctx, info.id,
                            info.title, info.windowClass, info.exe, info.pid,
                            info.cmdline);
}

Value UIBridge::handleWindowCmd(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeNull();
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeNull();

  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getWindowInfo(wid);
  if (!info.valid) {
    return Value::makeNull();
  }
  // TODO: string pool integration - for now return null
  (void)info;
  return Value::makeNull();
}

// Shared window object methods - take object as first argument, extract id
Value
UIBridge::handleWindowCloseObj(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);

  // Extract id from object
  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  ::havel::host::WindowService winService(ctx->windowManager);
  return Value::makeBool(winService.closeWindow(wid));
}

Value
UIBridge::handleWindowHideObj(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);

  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  ::havel::host::WindowService winService(ctx->windowManager);
  winService.hideWindow(wid);
  return Value::makeBool(true);
}

Value
UIBridge::handleWindowShowObj(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);

  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  ::havel::host::WindowService winService(ctx->windowManager);
  winService.showWindow(wid);
  return Value::makeBool(true);
}

Value
UIBridge::handleWindowFocusObj(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);

  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.focusWindow(wid));
}

Value
UIBridge::handleWindowMinObj(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);

  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.minimizeWindow(wid));
}

Value
UIBridge::handleWindowMaxObj(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return Value::makeBool(false);

  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.maximizeWindow(wid));
}

Value
UIBridge::handleWindowResizeObj(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager)
    return Value::makeBool(false);

  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  int w = 0, h = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    w = static_cast<int>(v->asInt());
  else if (auto *v = (args[1].isDouble() ? &args[1] : nullptr))
    w = static_cast<int>(v->asInt());
  if (auto *v = (args[2].isInt() ? &args[2] : nullptr))
    h = static_cast<int>(v->asInt());
  else if (auto *v = (args[2].isDouble() ? &args[2] : nullptr))
    h = static_cast<int>(v->asInt());

  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.resizeWindow(wid, w, h));
}

Value
UIBridge::handleWindowMoveObj(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager)
    return Value::makeBool(false);

  uint64_t wid = 0;
  if (args[0].isObjectId()) {
    auto obj = ObjectRef{args[0].asObjectId(), true};
    auto *vm = static_cast<VM *>(ctx->vm);
    auto idVal = vm->getHostObjectField(obj, "id");
    if (auto *v = (idVal.isInt() ? &idVal : nullptr))
      wid = static_cast<uint64_t>(v->asInt());
  } else if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else {
    return Value::makeBool(false);
  }

  int x = 0, y = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    x = static_cast<int>(v->asInt());
  else if (auto *v = (args[1].isDouble() ? &args[1] : nullptr))
    x = static_cast<int>(v->asInt());
  if (auto *v = (args[2].isInt() ? &args[2] : nullptr))
    y = static_cast<int>(v->asInt());
  else if (auto *v = (args[2].isDouble() ? &args[2] : nullptr))
    y = static_cast<int>(v->asInt());

  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.moveWindow(wid, x, y));
}

Value UIBridge::handleWindowFind(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm) {
    return Value::makeNull();
  }

  // TODO: string selector support disabled
  (void)args;
  return Value::makeNull();
#if 0
  std::string selector;
  if nullptr)
    selector = *v;
  else
    return Value::makeNull();

  // Parse selector: "type value" where type is title/class/exe/pid/cmd
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return Value::makeNull();
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  ::havel::host::WindowService winService(ctx->windowManager);
  auto windows = winService.getAllWindows();

  for (const auto &win : windows) {
    bool match = false;

    if (type == "title") {
      match = (win.title.find(value) != std::string::npos);
    } else if (type == "class") {
      match = (win.windowClass.find(value) != std::string::npos);
    } else if (type == "exe") {
      match = (win.exe.find(value) != std::string::npos);
    } else if (type == "pid") {
      int pid = std::stoi(value);
      match = (win.pid == pid);
    } else if (type == "cmd") {
      match = (win.cmdline.find(value) != std::string::npos);
    }

    if (match) {
      return createWindowObject(static_cast<VM *>(ctx->vm), ctx, win.id,
                                win.title, win.windowClass, win.exe, win.pid,
                                win.cmdline);
    }
  }

#endif
  return Value::makeNull();
}

// Helper: resolve window argument (ID or selector string) to window ID
static uint64_t resolveWindowId(const Value &arg,
                                ::havel::host::WindowService &winService) {
  uint64_t wid = 0;

  // Try as integer ID first
  if (auto *v = (arg.isInt() ? &arg : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else if (false) { // TODO: string support
    // Try as selector string "type value"
    // std::string selector = ...; // TODO: get string from Value
    // Placeholder logic:
    (void)winService;
  }

  return wid;
}

Value
UIBridge::handleWindowClose(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService);
  if (wid == 0)
    return Value::makeBool(false);
  return Value(winService.closeWindow(wid));
}

Value
UIBridge::handleWindowResize(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService);
  if (wid == 0)
    return Value::makeBool(false);
  int w = 0, h = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    w = static_cast<int>(v->asInt());
  else if (auto *v = (args[1].isDouble() ? &args[1] : nullptr))
    w = static_cast<int>(v->asInt());
  if (auto *v = (args[2].isInt() ? &args[2] : nullptr))
    h = static_cast<int>(v->asInt());
  else if (auto *v = (args[2].isDouble() ? &args[2] : nullptr))
    h = static_cast<int>(v->asInt());
  return Value(winService.resizeWindow(wid, w, h));
}

Value
UIBridge::handleWindowMoveToMonitor(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeBool(false);
  int monitor = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    monitor = static_cast<int>(v->asInt());
  else if (auto *v2 = (args[1].isDouble() ? &args[1] : nullptr))
    monitor = static_cast<int>(v2->asDouble());
  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.moveWindowToMonitor(wid, monitor));
}

Value
UIBridge::handleWindowMoveToNextMonitor(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  // TODO: Implement move to next monitor
  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.moveWindowToMonitor(0, 0));
}

Value UIBridge::handleWindowMove(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeBool(false);
  int x = 0, y = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    x = static_cast<int>(v->asInt());
  if (auto *v = (args[2].isInt() ? &args[2] : nullptr))
    y = static_cast<int>(v->asInt());
  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.moveWindow(wid, x, y));
}

Value
UIBridge::handleWindowFocus(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.focusWindow(wid));
}

Value
UIBridge::handleWindowMinimize(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.minimizeWindow(wid));
}

Value
UIBridge::handleWindowMaximize(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  return Value(winService.maximizeWindow(wid));
}

Value UIBridge::handleWindowHide(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  winService.hideWindow(wid);
  return Value::makeBool(true);
}

Value UIBridge::handleWindowShow(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return Value::makeBool(false);
  ::havel::host::WindowService winService(ctx->windowManager);
  winService.showWindow(wid);
  return Value::makeBool(true);
}

// Window query functions implementation
Value UIBridge::handleWindowAny(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeBool(false);
  }
  if (args.empty()) {
    return Value::makeBool(false);
  }

  // Get selector string: "type value" where type is title/class/exe/pid/cmd
  std::string selector;
  // TODO: string support - for now just return false
  (void)selector;
  return Value::makeBool(false);

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return Value::makeBool(false);
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  ::havel::host::WindowService winService(ctx->windowManager);

  // Use anyWindow with predicate
  bool result = winService.anyWindow([&](const ::havel::host::WindowInfo &win) {
    if (type == "title") {
      return win.title.find(value) != std::string::npos;
    } else if (type == "class") {
      return win.windowClass.find(value) != std::string::npos;
    } else if (type == "exe") {
      return win.exe.find(value) != std::string::npos;
    } else if (type == "pid") {
      try {
        int pid = std::stoi(value);
        return win.pid == pid;
      } catch (...) {
        return false;
      }
    } else if (type == "cmd") {
      return win.cmdline.find(value) != std::string::npos;
    }
    return false;
  });

  return Value::makeBool(result);
}

Value
UIBridge::handleWindowCount(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeInt(static_cast<int64_t>(0));
  }

  ::havel::host::WindowService winService(ctx->windowManager);

  // If no selector provided, count all windows
  if (args.empty()) {
    auto windows = winService.getAllWindows();
    return Value::makeInt(static_cast<int64_t>(windows.size()));
  }

  // Get selector string
  std::string selector;
  // TODO: string support - for now just count all windows
  (void)selector;
  auto windows = winService.getAllWindows();
  return Value::makeInt(static_cast<int64_t>(windows.size()));

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    auto windows = winService.getAllWindows();
    return Value::makeInt(static_cast<int64_t>(windows.size()));
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  // Use countWindows with predicate
  int count = winService.countWindows([&](const ::havel::host::WindowInfo &win) {
    if (type == "title") {
      return win.title.find(value) != std::string::npos;
    } else if (type == "class") {
      return win.windowClass.find(value) != std::string::npos;
    } else if (type == "exe") {
      return win.exe.find(value) != std::string::npos;
    } else if (type == "pid") {
      try {
        int pid = std::stoi(value);
        return win.pid == pid;
      } catch (...) {
        return false;
      }
    } else if (type == "cmd") {
      return win.cmdline.find(value) != std::string::npos;
    }
    return false;
  });

  return Value::makeInt(static_cast<int64_t>(count));
}

Value
UIBridge::handleWindowFilter(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return Value::makeNull();
  }
  if (args.empty()) {
    return Value::makeNull();
  }

  // Get selector string
  std::string selector;
  // TODO: string support - for now return null
  (void)selector;
  return Value::makeNull();

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return Value::makeNull();
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  ::havel::host::WindowService winService(ctx->windowManager);

  // Use filterWindows with predicate
  auto matchingWindows =
      winService.filterWindows([&](const ::havel::host::WindowInfo &win) {
        if (type == "title") {
          return win.title.find(value) != std::string::npos;
        } else if (type == "class") {
          return win.windowClass.find(value) != std::string::npos;
        } else if (type == "exe") {
          return win.exe.find(value) != std::string::npos;
        } else if (type == "pid") {
          try {
            int pid = std::stoi(value);
            return win.pid == pid;
          } catch (...) {
            return false;
          }
        } else if (type == "cmd") {
          return win.cmdline.find(value) != std::string::npos;
        }
        return false;
      });

  // Create array of window objects
  auto *vm = static_cast<VM *>(ctx->vm);
  auto arr = vm->createHostArray();
  for (const auto &win : matchingWindows) {
    auto winObj =
        createWindowObject(vm, ctx, win.id, win.title, win.windowClass, win.exe,
                           win.pid, win.cmdline);
    vm->pushHostArrayValue(arr, winObj);
  }

  return Value::makeArrayId(arr.id);
}

Value
UIBridge::handleClipboardGet(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return Value::makeNull();
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return Value::makeNull();
  }
  // TODO: string pool integration - for now return null
  (void)clipboard;
  return Value::makeNull();
}

Value
UIBridge::handleClipboardSet(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->clipboardManager) {
    return Value::makeBool(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return Value::makeBool(false);
  }
  if (false) { // TODO: string support
    // clipboard->setText(QString::fromStdString(...));
    return Value::makeBool(true);
  }
  return Value::makeBool(false);
}

Value
UIBridge::handleClipboardClear(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return Value::makeBool(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return Value::makeBool(false);
  }
  clipboard->clear();
  return Value::makeBool(true);
}

Value
UIBridge::handleScreenshotFull(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto& service = ::havel::host::ScreenshotService::getInstance();
  auto data = service.captureFullDesktop();
  (void)data;
  return Value::makeNull();
}

Value
UIBridge::handleScreenshotMonitor(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)ctx;
  int monitor = 0;
  if (!args.empty()) {
    if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
      monitor = static_cast<int>(v->asInt());
  }
  auto& service = ::havel::host::ScreenshotService::getInstance();
  auto data = service.captureMonitor(monitor);
  (void)data;
  return Value::makeNull();
}

Value UIBridge::handleGUINotify(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx || !ctx->guiManager) {
    return Value::makeBool(false);
  }
  if (args.size() < 2) {
    throw std::runtime_error("gui.notify() requires title and message");
  }

  const std::string *title = nullptr;
  const std::string *message = nullptr;

  if (!title || !message) {
    throw std::runtime_error("gui.notify() requires string arguments");
  }

  std::string icon = "info";
  int durationMs = 0;

  if (args.size() > 2 && args[2].isStringValId()) {
    icon = args[2].toString();
  }
  if (args.size() > 3 && args[3].isInt()) {
    durationMs = static_cast<int>(args[3].asInt());
  }

  ctx->guiManager->showNotification(*title, *message, icon, durationMs);
  return Value::makeBool(true);
}

// ============================================================================
// InputBridge Implementation
// ============================================================================

void InputBridge::install(PipelineOptions &options) {
  options.host_functions["hotkey.register"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyRegister(args, ctx);
  };
  options.host_functions["hotkey.trigger"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyTrigger(args, ctx);
  };
  options.host_functions["hotkey.list"] = [ctx = ctx_](const auto &args) {
    return handleHotkeyList(args, ctx);
  };
  options.host_functions["mapmanager.map"] = [ctx = ctx_](const auto &args) {
    return handleMapManagerMap(args, ctx);
  };
  options.host_functions["mapmanager.getCurrentProfile"] =
      [ctx = ctx_](const auto &args) {
        return handleMapManagerGetCurrentProfile(args, ctx);
      };
  options.host_functions["alttab.show"] = [ctx = ctx_](const auto &args) {
    return handleAltTabShow(args, ctx);
  };
  options.host_functions["alttab.hide"] = [ctx = ctx_](const auto &args) {
    return handleAltTabHide(args, ctx);
  };
  options.host_functions["alttab.toggle"] = [ctx = ctx_](const auto &args) {
    return handleAltTabToggle(args, ctx);
  };
  options.host_functions["alttab.next"] = [ctx = ctx_](const auto &args) {
    return handleAltTabNext(args, ctx);
  };
  options.host_functions["alttab.previous"] = [ctx = ctx_](const auto &args) {
    return handleAltTabPrevious(args, ctx);
  };
  options.host_functions["alttab.select"] = [ctx = ctx_](const auto &args) {
    return handleAltTabSelect(args, ctx);
  };
  options.host_functions["alttab.getWindows"] = [ctx = ctx_](const auto &args) {
    return handleAltTabGetWindows(args, ctx);
  };
}

Value
InputBridge::handleHotkeyRegister(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  // Args: [hotkey_string, callback_closure]
  if (args.size() < 2) {
    return Value::makeNull();
  }

  if (!ctx || !ctx->hotkeyManager || !ctx->vm || !ctx->io) {
    return Value::makeNull();
  }

  // Get hotkey string
  std::string hotkeyStr;
  if (args[0].isStringValId()) {
    hotkeyStr = args[0].toString();
  } else {
    return Value::makeNull();
  }

  // Generate unique hotkey ID
  std::string hotkeyId =
      "hotkey_" + std::to_string(std::hash<std::string>{}(hotkeyStr));

  // Register closure as a callback - this pins it as a GC root
  CallbackId callbackId = ctx->vm->registerCallback(args[1]);

  // Create hotkey context object using HotkeyModule
  auto *vm = static_cast<VM *>(ctx->vm);
  auto hotkeyContext = ::havel::stdlib::HotkeyModule::createHotkeyContext(
      vm, hotkeyId, hotkeyStr, hotkeyStr, "",
      "Hotkey registered via hotkey.register", callbackId);

  // Get event queue for thread-safe dispatch to main event loop
  auto *eventQueue = ctx->eventQueue;

  // Register hotkey with thread-safe EventQueue dispatch
  // When the hotkey fires, the callback is pushed to the event queue
  // and executed in the main event loop, giving access to shared globals/heap
  bool success = ctx->hotkeyManager->AddHotkey(
      hotkeyStr, [vm, callbackId, hotkeyContext, eventQueue]() {
        if (eventQueue) {
          // Thread-safe: push callback to event queue for main loop execution
          eventQueue->push([vm, callbackId, hotkeyContext]() {
            vm->invokeCallback(callbackId, {hotkeyContext});
          });
        } else {
          // Fallback: direct execution (no event queue configured)
          vm->invokeCallback(callbackId, {hotkeyContext});
        }
      });

  // Return hotkey context object on success, null on failure
  // This allows: let hk = ^t => { ... }
  if (success) {
    return hotkeyContext;
  }
  return Value::makeNull();
}

Value
InputBridge::handleHotkeyTrigger(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

Value
InputBridge::handleHotkeyList(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->hotkeyManager || !ctx->vm) {
    return Value::makeNull();
  }

  auto *vm = static_cast<VM *>(ctx->vm);
  auto hotkeys = ctx->hotkeyManager->getHotkeyList();
  auto conditionalHotkeys = ctx->hotkeyManager->getConditionalHotkeyList();

  auto result = vm->createHostArray();

  // Add simple hotkeys
  for (const auto &hk : hotkeys) {
    auto ctxObj = ::havel::stdlib::HotkeyModule::createHotkeyContext(
        vm, "hotkey_" + std::to_string(hk.id), hk.alias, "", "",
        hk.enabled ? "enabled" : "disabled", 0);
    vm->pushHostArrayValue(result, ctxObj);
  }

  // Add conditional hotkeys
  for (const auto &hk : conditionalHotkeys) {
    auto ctxObj = ::havel::stdlib::HotkeyModule::createHotkeyContext(
        vm, "hotkey_" + std::to_string(hk.id), hk.key, hk.key, hk.condition,
        hk.active ? "active" : (hk.enabled ? "enabled" : "disabled"), 0);
    vm->pushHostArrayValue(result, ctxObj);
  }

  return Value::makeArrayId(result.id);
}

Value
InputBridge::handleMapManagerMap(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

Value InputBridge::handleMapManagerGetCurrentProfile(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  return Value::makeNull();
}

Value
InputBridge::handleAltTabShow(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AltTabService altTab;
  altTab.show();
  return Value::makeBool(true);
}

Value
InputBridge::handleAltTabHide(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AltTabService altTab;
  altTab.hide();
  return Value::makeBool(true);
}

Value
InputBridge::handleAltTabToggle(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AltTabService altTab;
  altTab.toggle();
  return Value::makeBool(true);
}

Value
InputBridge::handleAltTabNext(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AltTabService altTab;
  altTab.next();
  return Value::makeBool(true);
}

Value
InputBridge::handleAltTabPrevious(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AltTabService altTab;
  altTab.previous();
  return Value::makeBool(true);
}

Value
InputBridge::handleAltTabSelect(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AltTabService altTab;
  altTab.select();
  return Value::makeBool(true);
}

Value
InputBridge::handleAltTabGetWindows(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  ::havel::host::AltTabService altTab;
  auto windows = altTab.getWindows();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  auto arr = vm->createHostArray();
  for (const auto &win : windows) {
    auto winObj = vm->createHostObject();
    // TODO: string pool integration - for now return null for strings
    (void)win.title; (void)win.className; (void)win.processName;
    vm->setHostObjectField(winObj, "title", Value::makeNull());
    vm->setHostObjectField(winObj, "className", Value::makeNull());
    vm->setHostObjectField(winObj, "processName", Value::makeNull());
    vm->setHostObjectField(winObj, "windowId",
                           Value::makeInt(static_cast<int64_t>(win.windowId)));
    vm->setHostObjectField(winObj, "active", Value::makeBool(win.active));
    vm->pushHostArrayValue(arr, Value::makeObjectId(winObj.id));
  }
  return Value::makeArrayId(arr.id);
}

Value AsyncBridge::handleSleep(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("sleep() requires milliseconds");
  }
  int64_t ms = 0;
  if (args[0].isInt()) {
    ms = args[0].asInt();
  } else if (args[0].isDouble()) {
    ms = static_cast<int64_t>(args[0].asDouble());
  } else {
    throw std::runtime_error("sleep() requires a number");
  }
  if (ms < 0) {
    throw std::runtime_error("sleep() milliseconds must be non-negative");
  }
  if (ctx->asyncService) {
    ctx->asyncService->sleep(static_cast<int>(ms));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
  return Value::makeNull();
}

Value AsyncBridge::handleTimeNow(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();
  return Value::makeInt(static_cast<int64_t>(timestamp));
}

// Async task handlers
Value
AsyncBridge::handleAsyncRun(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.run() requires a function argument");
  }

  if (!ctx || !ctx->vm) {
    throw std::runtime_error("async.run() requires an active VM context");
  }

  // Execute VM callback immediately and persist result under a task id.
  // This removes placeholder behavior and allows async.await() to return
  // actual closure results while keeping VM interaction single-threaded.
  auto *vm = static_cast<VM *>(ctx->vm);
  std::string taskId = allocateTaskId();
  AsyncTaskRecord record;
  record.running = true;

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_async_tasks[taskId] = record;
  }

  try {
    CallbackId callback = vm->registerCallback(args[0]);
    Value result = vm->invokeCallback(callback, {});
    vm->releaseCallback(callback);

    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto &task = g_async_tasks[taskId];
    task.running = false;
    task.completed = true;
    task.result = std::move(result);
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto &task = g_async_tasks[taskId];
    task.running = false;
    task.completed = true;
    task.error = e.what();
  }

  // TODO: string pool integration - for now return null
  (void)taskId;
  return Value::makeNull();
}

Value
AsyncBridge::handleAsyncAwait(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.await() requires a task ID");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("async.await() requires a string task ID");
  }

  std::string taskId = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_tasks.find(taskId);
    if (it != g_async_tasks.end()) {
      if (!it->second.error.empty()) {
        throw std::runtime_error("async.await() task failed: " +
                                 it->second.error);
      }
      return it->second.result;
    }
  }

  if (ctx && ctx->asyncService) {
    bool completed = ctx->asyncService->await(taskId);
    return Value::makeBool(completed);
  }

  return Value::makeBool(false);
}

Value
AsyncBridge::handleAsyncCancel(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.cancel() requires a task ID");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("async.cancel() requires a string task ID");
  }

  std::string taskId = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_tasks.find(taskId);
    if (it != g_async_tasks.end()) {
      it->second.cancelled = true;
      it->second.running = false;
      return Value::makeBool(true);
    }
  }

  if (ctx && ctx->asyncService) {
    bool cancelled = ctx->asyncService->cancel(taskId);
    return Value::makeBool(cancelled);
  }

  return Value::makeBool(false);
}

Value
AsyncBridge::handleAsyncIsRunning(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.isRunning() requires a task ID");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("async.isRunning() requires a string task ID");
  }

  std::string taskId = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_tasks.find(taskId);
    if (it != g_async_tasks.end()) {
      return Value::makeBool(it->second.running);
    }
  }

  if (ctx && ctx->asyncService) {
    bool running = ctx->asyncService->isRunning(taskId);
    return Value::makeBool(running);
  }

  return Value::makeBool(false);
}

// Channel handlers
Value
AsyncBridge::handleChannelCreate(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.channel() requires a channel name");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("async.channel() requires a string name");
  }

  std::string name = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto &channel = g_async_channels[name];
    channel.closed = false;
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->createChannel(name);
  }
  return Value::makeBool(true);
}

Value
AsyncBridge::handleChannelSend(const std::vector<Value> &args,
                               const HostContext *ctx) {
  if (args.size() < 2) {
    throw std::runtime_error("async.send() requires channel name and value");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("async.send() requires a string channel name");
  }

  std::string name = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end() || it->second.closed) {
      return Value::makeBool(false);
    }
    it->second.queue.push_back(args[1]);
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->send(name, toString(args[1]));
  }
  return Value::makeBool(true);
}

Value
AsyncBridge::handleChannelReceive(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.receive() requires a channel name");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("async.receive() requires a string channel name");
  }

  std::string name = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end() || it->second.queue.empty()) {
      return Value::makeNull();
    }
    Value value = it->second.queue.front();
    it->second.queue.pop_front();
    return value;
  }
}

Value
AsyncBridge::handleChannelTryReceive(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.tryReceive() requires a channel name");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error(
        "async.tryReceive() requires a string channel name");
  }

  std::string name = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end() || it->second.queue.empty()) {
      return Value::makeNull();
    }
    Value value = it->second.queue.front();
    it->second.queue.pop_front();
    return value;
  }
}

Value
AsyncBridge::handleChannelClose(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.channel.close() requires a channel name");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error(
        "async.channel.close() requires a string channel name");
  }

  std::string name = args[0].toString();

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end()) {
      return Value::makeBool(false);
    }
    it->second.closed = true;
    it->second.queue.clear();
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->closeChannel(name);
  }
  return Value::makeBool(true);
}

Value
AsyncBridge::handleThreadCreate(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.empty()) {
    throw std::runtime_error("thread(fn) requires VM context and callback");
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  CallbackId callback = vm->registerCallback(args[0]);
  const std::string id = allocateTaskId();
  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_threads[id] =
        ThreadRecord{.callback = callback, .running = true, .paused = false};
  }
  return Value::makeObjectId(makeHandleObject(vm, "thread", id).id);
}

Value
AsyncBridge::handleThreadSend(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.size() < 2) {
    throw std::runtime_error("thread.send(handle, message) requires 2 args");
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return Value::makeBool(false);
  }

  ThreadRecord record;
  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_threads.find(handle->second);
    if (it == g_threads.end() || !it->second.running || it->second.paused) {
      return Value::makeBool(false);
    }
    record = it->second;
  }

  {
    std::lock_guard<std::mutex> invoke_lock(g_vm_invoke_mutex);
    (void)vm->invokeCallback(record.callback, {args[1]});
  }
  return Value::makeBool(true);
}

Value
AsyncBridge::handleThreadPause(const std::vector<Value> &args,
                               const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return Value::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end())
    return Value::makeBool(false);
  it->second.paused = true;
  return Value::makeBool(true);
}

Value
AsyncBridge::handleThreadResume(const std::vector<Value> &args,
                                const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return Value::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end())
    return Value::makeBool(false);
  it->second.paused = false;
  return Value::makeBool(true);
}

Value
AsyncBridge::handleThreadStop(const std::vector<Value> &args,
                              const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return Value::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end())
    return Value::makeBool(false);
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  it->second.running = false;
  it->second.paused = false;
  return Value::makeBool(true);
}

Value
AsyncBridge::handleThreadRunning(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return Value::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  return Value::makeBool(it != g_threads.end() && it->second.running);
}

Value
AsyncBridge::handleIntervalCreate(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.size() < 2) {
    throw std::runtime_error("interval(ms, fn) requires delay and callback");
  }
  int64_t delay_ms = 0;
  if (args[0].isInt()) {
    delay_ms = args[0].asInt();
  } else if (args[0].isDouble()) {
    delay_ms = static_cast<int64_t>(args[0].asDouble());
  } else {
    throw std::runtime_error("interval delay must be number");
  }
  if (delay_ms < 1)
    delay_ms = 1;

  auto *vm = static_cast<VM *>(ctx->vm);
  const CallbackId callback = vm->registerCallback(args[1]);
  const std::string id = allocateTaskId();

  TimerRecord timer{.callback = callback,
                    .running = true,
                    .paused = false,
                    .repeating = true,
                    .delay_ms = delay_ms,
                    .task_id = ""};
  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_timers[id] = timer;
  }

  if (ctx->asyncService) {
    std::string task_id =
        ctx->asyncService->spawn([ctx, id, callback, delay_ms]() {
          auto *vm_local = static_cast<VM *>(ctx->vm);
          while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            bool should_run = false;
            {
              std::lock_guard<std::mutex> lock(g_async_mutex);
              auto it = g_timers.find(id);
              if (it == g_timers.end() || !it->second.running) {
                break;
              }
              should_run = !it->second.paused;
            }
            if (!should_run || !vm_local) {
              continue;
            }
            std::lock_guard<std::mutex> invoke_lock(g_vm_invoke_mutex);
            try {
              (void)vm_local->invokeCallback(callback, {});
            } catch (...) {
              // Keep timer alive; script side can stop it explicitly.
            }
          }
        });
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_timers[id].task_id = task_id;
  }

  return Value::makeObjectId(makeHandleObject(vm, "interval", id).id);
}

Value
AsyncBridge::handleIntervalPause(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval")
    return Value::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return Value::makeBool(false);
  it->second.paused = true;
  return Value::makeBool(true);
}

Value
AsyncBridge::handleIntervalResume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval")
    return Value::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return Value::makeBool(false);
  it->second.paused = false;
  return Value::makeBool(true);
}

Value
AsyncBridge::handleIntervalStop(const std::vector<Value> &args,
                                const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval")
    return Value::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return Value::makeBool(false);
  it->second.running = false;
  if (ctx && ctx->asyncService && !it->second.task_id.empty()) {
    (void)ctx->asyncService->cancel(it->second.task_id);
  }
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  return Value::makeBool(true);
}

Value
AsyncBridge::handleTimeoutCreate(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.size() < 2) {
    throw std::runtime_error("timeout(ms, fn) requires delay and callback");
  }
  int64_t delay_ms = 0;
  if (args[0].isInt()) {
    delay_ms = args[0].asInt();
  } else if (args[0].isDouble()) {
    delay_ms = static_cast<int64_t>(args[0].asDouble());
  } else {
    throw std::runtime_error("timeout delay must be number");
  }
  if (delay_ms < 1)
    delay_ms = 1;

  auto *vm = static_cast<VM *>(ctx->vm);
  const CallbackId callback = vm->registerCallback(args[1]);
  const std::string id = allocateTaskId();
  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_timers[id] = TimerRecord{.callback = callback,
                               .running = true,
                               .paused = false,
                               .repeating = false,
                               .delay_ms = delay_ms,
                               .task_id = ""};
  }

  if (ctx->asyncService) {
    std::string task_id = ctx->asyncService->spawn([ctx, id, callback,
                                                    delay_ms]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      auto *vm_local = static_cast<VM *>(ctx->vm);
      bool should_run = false;
      {
        std::lock_guard<std::mutex> lock(g_async_mutex);
        auto it = g_timers.find(id);
        if (it != g_timers.end() && it->second.running && !it->second.paused) {
          should_run = true;
          it->second.running = false;
        }
      }
      if (should_run && vm_local) {
        std::lock_guard<std::mutex> invoke_lock(g_vm_invoke_mutex);
        try {
          (void)vm_local->invokeCallback(callback, {});
        } catch (...) {
        }
      }
    });
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_timers[id].task_id = task_id;
  }

  return Value::makeObjectId(makeHandleObject(vm, "timeout", id).id);
}

Value
AsyncBridge::handleTimeoutCancel(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "timeout")
    return Value::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return Value::makeBool(false);
  it->second.running = false;
  if (ctx && ctx->asyncService && !it->second.task_id.empty()) {
    (void)ctx->asyncService->cancel(it->second.task_id);
  }
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  return Value::makeBool(true);
}

// ============================================================================
// AutomationBridge Implementation
// ============================================================================

void AutomationBridge::install(PipelineOptions &options) {
  options.host_functions["automation.createAutoClicker"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationCreateAutoClicker(args, ctx);
      };
  options.host_functions["automation.createAutoRunner"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationCreateAutoRunner(args, ctx);
      };
  options.host_functions["automation.createAutoKeyPresser"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationCreateAutoKeyPresser(args, ctx);
      };
  options.host_functions["automation.hasTask"] = [ctx =
                                                      ctx_](const auto &args) {
    return handleAutomationHasTask(args, ctx);
  };
  options.host_functions["automation.removeTask"] =
      [ctx = ctx_](const auto &args) {
        return handleAutomationRemoveTask(args, ctx);
      };
  options.host_functions["automation.stopAll"] = [ctx =
                                                      ctx_](const auto &args) {
    return handleAutomationStopAll(args, ctx);
  };
}

Value AutomationBridge::handleAutomationCreateAutoClicker(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

Value AutomationBridge::handleAutomationCreateAutoRunner(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

Value AutomationBridge::handleAutomationCreateAutoKeyPresser(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

Value AutomationBridge::handleAutomationHasTask(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

Value AutomationBridge::handleAutomationRemoveTask(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

Value AutomationBridge::handleAutomationStopAll(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeBool(false);
}

// ============================================================================
// BrowserBridge Implementation
// ============================================================================

void BrowserBridge::install(PipelineOptions &options) {
  options.host_functions["browser.connect"] = [ctx = ctx_](const auto &args) {
    return handleBrowserConnect(args, ctx);
  };
  options.host_functions["browser.connectFirefox"] =
      [ctx = ctx_](const auto &args) {
        return handleBrowserConnectFirefox(args, ctx);
      };
  options.host_functions["browser.disconnect"] = [ctx =
                                                      ctx_](const auto &args) {
    return handleBrowserDisconnect(args, ctx);
  };
  options.host_functions["browser.isConnected"] = [ctx =
                                                       ctx_](const auto &args) {
    return handleBrowserIsConnected(args, ctx);
  };
  options.host_functions["browser.open"] = [ctx = ctx_](const auto &args) {
    return handleBrowserOpen(args, ctx);
  };
  options.host_functions["browser.newTab"] = [ctx = ctx_](const auto &args) {
    return handleBrowserNewTab(args, ctx);
  };
  options.host_functions["browser.goto"] = [ctx = ctx_](const auto &args) {
    return handleBrowserGoto(args, ctx);
  };
  options.host_functions["browser.back"] = [ctx = ctx_](const auto &args) {
    return handleBrowserBack(args, ctx);
  };
  options.host_functions["browser.forward"] = [ctx = ctx_](const auto &args) {
    return handleBrowserForward(args, ctx);
  };
  options.host_functions["browser.reload"] = [ctx = ctx_](const auto &args) {
    return handleBrowserReload(args, ctx);
  };
  options.host_functions["browser.listTabs"] = [ctx = ctx_](const auto &args) {
    return handleBrowserListTabs(args, ctx);
  };
}

Value
BrowserBridge::handleBrowserConnect(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)ctx;
  std::string browserUrl = "http://localhost:9222";
  if (!args.empty()) {
    if (false) { // TODO: string support
      // browserUrl = ...; // TODO: get string from Value
    }
  }
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.connect(browserUrl));
}

Value BrowserBridge::handleBrowserConnectFirefox(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)ctx;
  int port = 2828;
  if (!args.empty()) {
    if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
      port = static_cast<int>(v->asInt());
    }
  }
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.connectFirefox(port));
}

Value
BrowserBridge::handleBrowserDisconnect(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::BrowserService browser;
  browser.disconnect();
  return Value::makeBool(true);
}

Value
BrowserBridge::handleBrowserIsConnected(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.isConnected());
}

Value
BrowserBridge::handleBrowserOpen(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.open() requires a URL");
  }
  const std::string *url = nullptr;
  if (!url) {
    throw std::runtime_error("browser.open() requires a string URL");
  }
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.open(*url));
}

Value
BrowserBridge::handleBrowserNewTab(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  (void)ctx;
  std::string url;
  if (!args.empty()) {
    if (false) { // TODO: string support
      // url = ...; // TODO: get string from Value
    }
  }
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.newTab(url));
}

Value
BrowserBridge::handleBrowserGoto(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.goto() requires a URL");
  }
  const std::string *url = nullptr;
  if (!url) {
    throw std::runtime_error("browser.goto() requires a string URL");
  }
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.gotoUrl(*url));
}

Value
BrowserBridge::handleBrowserBack(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.back());
}

Value
BrowserBridge::handleBrowserForward(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.forward());
}

Value
BrowserBridge::handleBrowserReload(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  (void)ctx;
  bool ignoreCache = false;
  if (!args.empty()) {
    if (auto *b = (args[0].isBool() ? &args[0] : nullptr)) {
      ignoreCache = b->asBool();
    }
  }
  ::havel::host::BrowserService browser;
  return Value::makeBool(browser.reload(ignoreCache));
}

Value
BrowserBridge::handleBrowserListTabs(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  (void)args;
  ::havel::host::BrowserService browser;
  auto tabs = browser.listTabs();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  auto arr = vm->createHostArray();
  for (const auto &tab : tabs) {
    auto tabObj = vm->createHostObject();
    vm->setHostObjectField(tabObj, "id",
                           Value::makeInt(static_cast<int64_t>(tab.id)));
    // TODO: string pool integration - for now return null for strings
    (void)tab.title; (void)tab.url; (void)tab.type;
    vm->setHostObjectField(tabObj, "title", Value::makeNull());
    vm->setHostObjectField(tabObj, "url", Value::makeNull());
    vm->setHostObjectField(tabObj, "type", Value::makeNull());
    vm->pushHostArrayValue(arr, Value::makeObjectId(tabObj.id));
  }
  return Value::makeArrayId(arr.id);
}

// ============================================================================
// ToolsBridge Implementation
// ============================================================================

namespace {
::havel::host::TextChunkerService g_textChunker;
}

void ToolsBridge::install(PipelineOptions &options) {
  options.host_functions["textchunker.setText"] = [ctx =
                                                       ctx_](const auto &args) {
    return handleTextChunkerSetText(args, ctx);
  };
  options.host_functions["textchunker.getText"] = [ctx =
                                                       ctx_](const auto &args) {
    return handleTextChunkerGetText(args, ctx);
  };
  options.host_functions["textchunker.setChunkSize"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerSetChunkSize(args, ctx);
      };
  options.host_functions["textchunker.getTotalChunks"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetTotalChunks(args, ctx);
      };
  options.host_functions["textchunker.getCurrentChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetCurrentChunk(args, ctx);
      };
  options.host_functions["textchunker.setCurrentChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerSetCurrentChunk(args, ctx);
      };
  options.host_functions["textchunker.getChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetChunk(args, ctx);
      };
  options.host_functions["textchunker.getNextChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetNextChunk(args, ctx);
      };
  options.host_functions["textchunker.getPreviousChunk"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGetPreviousChunk(args, ctx);
      };
  options.host_functions["textchunker.goToFirst"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGoToFirst(args, ctx);
      };
  options.host_functions["textchunker.goToLast"] =
      [ctx = ctx_](const auto &args) {
        return handleTextChunkerGoToLast(args, ctx);
      };
  options.host_functions["textchunker.clear"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerClear(args, ctx);
  };
}

Value
ToolsBridge::handleTextChunkerSetText(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setText() requires text");
  }
  const std::string *text = nullptr;
  if (!text) {
    throw std::runtime_error("textchunker.setText() requires a string");
  }
  g_textChunker.setText(*text);
  return Value::makeBool(true);
}

Value
ToolsBridge::handleTextChunkerGetText(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getText();
  return Value::makeNull();
}

Value ToolsBridge::handleTextChunkerSetChunkSize(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setChunkSize() requires a size");
  }
  int64_t size = 20000;
  if (args[0].isInt()) {
    size = args[0].asInt();
  }
  g_textChunker.setChunkSize(static_cast<size_t>(size));
  return Value::makeBool(true);
}

Value ToolsBridge::handleTextChunkerGetTotalChunks(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeInt(static_cast<int64_t>(g_textChunker.getTotalChunks()));
}

Value ToolsBridge::handleTextChunkerGetCurrentChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return Value::makeInt(static_cast<int64_t>(g_textChunker.getCurrentChunk()));
}

Value ToolsBridge::handleTextChunkerSetCurrentChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error(
        "textchunker.setCurrentChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (args[0].isInt()) {
    index = args[0].asInt();
  }
  g_textChunker.setCurrentChunk(static_cast<int>(index));
  return Value::makeBool(true);
}

Value
ToolsBridge::handleTextChunkerGetChunk(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.getChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (args[0].isInt()) {
    index = args[0].asInt();
  }
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getChunk(static_cast<int>(index));
  return Value::makeNull();
}

Value ToolsBridge::handleTextChunkerGetNextChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getNextChunk();
  return Value::makeNull();
}

Value ToolsBridge::handleTextChunkerGetPreviousChunk(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  (void)g_textChunker.getPreviousChunk();
  return Value::makeNull();
}

Value
ToolsBridge::handleTextChunkerGoToFirst(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToFirst();
  return Value::makeBool(true);
}

Value
ToolsBridge::handleTextChunkerGoToLast(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToLast();
  return Value::makeBool(true);
}

Value
ToolsBridge::handleTextChunkerClear(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.clear();
  return Value::makeBool(true);
}

// ============================================================================
// MediaBridge Implementation
// ============================================================================

void MediaBridge::install(PipelineOptions &options) {
  options.host_functions["media.playPause"] = [ctx = ctx_](const auto &args) {
    return handleMediaPlayPause(args, ctx);
  };
  options.host_functions["media.play"] = [ctx = ctx_](const auto &args) {
    return handleMediaPlay(args, ctx);
  };
  options.host_functions["media.pause"] = [ctx = ctx_](const auto &args) {
    return handleMediaPause(args, ctx);
  };
  options.host_functions["media.stop"] = [ctx = ctx_](const auto &args) {
    return handleMediaStop(args, ctx);
  };
  options.host_functions["media.next"] = [ctx = ctx_](const auto &args) {
    return handleMediaNext(args, ctx);
  };
  options.host_functions["media.previous"] = [ctx = ctx_](const auto &args) {
    return handleMediaPrevious(args, ctx);
  };
  options.host_functions["media.getVolume"] = [ctx = ctx_](const auto &args) {
    return handleMediaGetVolume(args, ctx);
  };
  options.host_functions["media.setVolume"] = [ctx = ctx_](const auto &args) {
    return handleMediaSetVolume(args, ctx);
  };
  options.host_functions["media.getActivePlayer"] =
      [ctx = ctx_](const auto &args) {
        return handleMediaGetActivePlayer(args, ctx);
      };
  options.host_functions["media.setActivePlayer"] =
      [ctx = ctx_](const auto &args) {
        return handleMediaSetActivePlayer(args, ctx);
      };
  options.host_functions["media.getAvailablePlayers"] =
      [ctx = ctx_](const auto &args) {
        return handleMediaGetAvailablePlayers(args, ctx);
      };
}

Value
MediaBridge::handleMediaPlayPause(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.playPause();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value
MediaBridge::handleMediaPlay(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.play();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value
MediaBridge::handleMediaPause(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.pause();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value
MediaBridge::handleMediaStop(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.stop();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value
MediaBridge::handleMediaNext(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.next();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value
MediaBridge::handleMediaPrevious(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    media.previous();
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value
MediaBridge::handleMediaGetVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    return Value::makeDouble(media.getVolume());
  } catch (...) {
    return Value::makeDouble(0.0);
  }
}

Value
MediaBridge::handleMediaSetVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error(
        "media.setVolume() requires a volume value (0.0-1.0)");
  }
  double volume = 0.0;
  if (args[0].isDouble()) {
    volume = args[0].asDouble();
  } else if (args[0].isInt()) {
    volume = static_cast<double>(args[0].asInt()) / 100.0;
  }
  try {
    ::havel::host::MediaService media;
    media.setVolume(volume);
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value
MediaBridge::handleMediaGetActivePlayer(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  try {
    ::havel::host::MediaService media;
    // TODO: string pool integration - for now return null
    (void)media;
    return Value::makeNull();
  } catch (...) {
    return Value::makeNull();
  }
}

Value
MediaBridge::handleMediaSetActivePlayer(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("media.setActivePlayer() requires a player name");
  }
  const std::string *name = nullptr;
  if (!name) {
    throw std::runtime_error("media.setActivePlayer() requires a string");
  }
  try {
    ::havel::host::MediaService media;
    media.setActivePlayer(*name);
    return Value::makeBool(true);
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value MediaBridge::handleMediaGetAvailablePlayers(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }
  try {
    ::havel::host::MediaService media;
    auto players = media.getAvailablePlayers();
    auto arr = vm->createHostArray();
    for (const auto &player : players) {
      // TODO: string pool integration - for now return null
      (void)player;
      vm->pushHostArrayValue(arr, Value::makeNull());
    }
    return Value::makeArrayId(arr.id);
  } catch (...) {
    return Value::makeNull();
  }
}

// ============================================================================
// NetworkBridge Implementation
// ============================================================================

void NetworkBridge::install(PipelineOptions &options) {
  // HTTP module (aliases for network.*)
  options.host_functions["http.get"] = [ctx = ctx_](const auto &args) {
    return handleNetworkGet(args, ctx);
  };
  options.host_functions["http.post"] = [ctx = ctx_](const auto &args) {
    return handleNetworkPost(args, ctx);
  };
  options.host_functions["http.download"] = [ctx = ctx_](const auto &args) {
    return handleNetworkDownload(args, ctx);
  };

  // Legacy network.* names
  options.host_functions["network.get"] = [ctx = ctx_](const auto &args) {
    return handleNetworkGet(args, ctx);
  };
  options.host_functions["network.post"] = [ctx = ctx_](const auto &args) {
    return handleNetworkPost(args, ctx);
  };
  options.host_functions["network.isOnline"] = [ctx = ctx_](const auto &args) {
    return handleNetworkIsOnline(args, ctx);
  };
  options.host_functions["network.getExternalIp"] =
      [ctx = ctx_](const auto &args) {
        return handleNetworkGetExternalIp(args, ctx);
      };
}

Value
NetworkBridge::handleNetworkGet(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("http.get() requires a URL");
  }

  // Try to get string from variant
  std::string url;
  if (args[0].isStringValId()) {
    url = args[0].toString();
  } else {
    throw std::runtime_error(
        "http.get() requires a string URL");
  }

  int timeout_ms = 30000;
  if (args.size() > 1 && args[1].isInt()) {
    timeout_ms = static_cast<int>(args[1].asInt());
  }
  try {
    ::havel::host::NetworkService net;
    auto response = net.get(url, timeout_ms);
    if (response.success) {
      // TODO: string pool integration - for now return null
      (void)response;
      return Value::makeNull();
    } else {
      return Value::makeNull();
    }
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string("http.get() failed: ") + e.what());
  }
}

Value
NetworkBridge::handleNetworkPost(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("network.post() requires URL and data");
  }
  const std::string *url = nullptr;
  const std::string *data = nullptr;
  if (!url || !data) {
    throw std::runtime_error("network.post() requires string arguments");
  }
  std::string content_type = "application/json";
  if (args.size() > 2 && args[2].isStringValId()) {
    content_type = args[2].toString();
  }
  int timeout_ms = 30000;
  if (args.size() > 3 && args[3].isInt()) {
    timeout_ms = static_cast<int>(args[3].asInt());
  }
  try {
    ::havel::host::NetworkService net;
    auto response = net.post(*url, *data, content_type, timeout_ms);
    if (response.success) {
      // TODO: string pool integration - for now return null
      (void)response;
      return Value::makeNull();
    } else {
      return Value::makeNull();
    }
  } catch (...) {
    return Value::makeNull();
  }
}

Value
NetworkBridge::handleNetworkIsOnline(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    ::havel::host::NetworkService net;
    return Value::makeBool(net.isOnline());
  } catch (...) {
    return Value::makeBool(false);
  }
}

Value NetworkBridge::handleNetworkGetExternalIp(
    const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    ::havel::host::NetworkService net;
    // TODO: string pool integration - for now return null
    (void)net;
    return Value::makeNull();
  } catch (...) {
    return Value::makeNull();
  }
}

Value
NetworkBridge::handleNetworkDownload(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("http.download() requires URL and path");
  }
  const std::string *url = nullptr;
  const std::string *path = nullptr;
  if (!url || !path) {
    throw std::runtime_error("http.download() requires string URL and path");
  }
  int timeout_ms = 30000;
  if (args.size() > 2 && args[2].isInt()) {
    timeout_ms = static_cast<int>(args[2].asInt());
  }
  try {
    ::havel::host::NetworkService net;
    bool success = net.download(*url, *path, timeout_ms);
    return Value::makeBool(success);
  } catch (...) {
    return Value::makeBool(false);
  }
}

// ============================================================================
// AudioBridge Implementation
// ============================================================================

void AudioBridge::install(PipelineOptions &options) {
  options.host_functions["audio.getVolume"] = [ctx = ctx_](const auto &args) {
    return handleGetVolume(args, ctx);
  };
  options.host_functions["audio.setVolume"] = [ctx = ctx_](const auto &args) {
    return handleSetVolume(args, ctx);
  };
  options.host_functions["audio.isMuted"] = [ctx = ctx_](const auto &args) {
    return handleIsMuted(args, ctx);
  };
  options.host_functions["audio.setMute"] = [ctx = ctx_](const auto &args) {
    return handleSetMute(args, ctx);
  };
  options.host_functions["audio.toggleMute"] = [ctx = ctx_](const auto &args) {
    return handleToggleMute(args, ctx);
  };
  options.host_functions["audio.getDevices"] = [ctx = ctx_](const auto &args) {
    return handleGetDevices(args, ctx);
  };
  options.host_functions["audio.findDeviceByIndex"] =
      [ctx = ctx_](const auto &args) {
        return handleFindDeviceByIndex(args, ctx);
      };
  options.host_functions["audio.findDeviceByName"] =
      [ctx = ctx_](const auto &args) {
        return handleFindDeviceByName(args, ctx);
      };
  options.host_functions["audio.setDefaultOutput"] =
      [ctx = ctx_](const auto &args) {
        return handleSetDefaultOutput(args, ctx);
      };
  options.host_functions["audio.getDefaultOutput"] =
      [ctx = ctx_](const auto &args) {
        return handleGetDefaultOutput(args, ctx);
      };
  options.host_functions["audio.playTestSound"] =
      [ctx = ctx_](const auto &args) { return handlePlayTestSound(args, ctx); };
  options.host_functions["audio.increaseVolume"] =
      [ctx = ctx_](const auto &args) {
        return handleIncreaseVolume(args, ctx);
      };
  options.host_functions["audio.decreaseVolume"] =
      [ctx = ctx_](const auto &args) {
        return handleDecreaseVolume(args, ctx);
      };
}

Value
AudioBridge::handleGetVolume(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value(1.0); // Default volume
  }
  // Check for device-specific overload: getVolume(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = args[0].toString();
    return Value::makeDouble(ctx->audioManager->getVolume(device));
  }
  // Default device
  return Value::makeDouble(ctx->audioManager->getVolume());
}

Value
AudioBridge::handleSetVolume(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  if (args.empty()) {
    throw std::runtime_error(
        "audio.setVolume() requires at least a volume number");
  }

  // Check for (device, volume) overload
  if (args.size() >= 2) {
    if (args[0].isStringValId()) {
      std::string device = args[0].toString();
      double volume = 1.0;
      if (args[1].isDouble()) {
        volume = args[1].asDouble();
      } else if (args[1].isInt()) {
        volume = static_cast<double>(args[1].asInt());
      } else {
        throw std::runtime_error(
            "audio.setVolume(device, volume) requires volume as number");
      }
      return Value::makeBool(ctx->audioManager->setVolume(device, volume));
    }
  }

  // Single argument: setVolume(volume) for default device
  double volume = 1.0;
  if (args[0].isDouble()) {
    volume = args[0].asDouble();
  } else if (args[0].isInt()) {
    volume = static_cast<double>(args[0].asInt());
  } else {
    throw std::runtime_error("audio.setVolume() requires a number");
  }
  return Value::makeBool(ctx->audioManager->setVolume(volume));
}

Value AudioBridge::handleIsMuted(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  // Check for device-specific overload: isMuted(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = args[0].toString();
    return Value::makeBool(ctx->audioManager->isMuted(device));
  }
  // Default device
  return Value::makeBool(ctx->audioManager->isMuted());
}

Value AudioBridge::handleSetMute(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  // Check for (device, muted) overload
  if (args.size() >= 2) {
    if (args[0].isStringValId() &&
        args[1].isBool()) {
      std::string device = args[0].toString();
      bool muted = args[1].asBool();
      return Value::makeBool(ctx->audioManager->setMute(device, muted));
    }
  }
  // Single argument: setMute(muted) for default device
  if (args.empty() || !args[0].isBool()) {
    throw std::runtime_error("audio.setMute() requires a boolean");
  }
  bool muted = args[0].asBool();
  return Value::makeBool(ctx->audioManager->setMute(muted));
}

Value
AudioBridge::handleToggleMute(const std::vector<Value> &args,
                              const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  // Check for device-specific overload: toggleMute(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = args[0].toString();
    return Value::makeBool(ctx->audioManager->toggleMute(device));
  }
  // Default device
  return Value::makeBool(ctx->audioManager->toggleMute());
}

Value
AudioBridge::handleGetDevices(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }

  const auto &devices = ctx->audioManager->getDevices();
  auto arr = vm->createHostArray();

  for (const auto &dev : devices) {
    auto obj = vm->createHostObject();
    // TODO: string pool integration - for now return null for strings
    (void)dev.name; (void)dev.description;
    vm->setHostObjectField(obj, "name", Value::makeNull());
    vm->setHostObjectField(obj, "description", Value::makeNull());
    vm->setHostObjectField(obj, "index",
                           Value::makeInt(static_cast<int64_t>(dev.index)));
    vm->setHostObjectField(obj, "isDefault", Value::makeBool(dev.isDefault));
    vm->setHostObjectField(obj, "isMuted", Value::makeBool(dev.isMuted));
    vm->setHostObjectField(obj, "volume", Value::makeDouble(dev.volume));
    vm->setHostObjectField(obj, "channels",
                           Value::makeInt(static_cast<int64_t>(dev.channels)));
    vm->pushHostArrayValue(arr, Value::makeObjectId(obj.id));
  }

  return Value::makeArrayId(arr.id);
}

Value
AudioBridge::handleFindDeviceByIndex(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  if (args.empty() || !args[0].isInt()) {
    throw std::runtime_error("audio.findDeviceByIndex() requires an index");
  }
  uint32_t index = static_cast<uint32_t>(args[0].asInt());

  auto *dev = ctx->audioManager->findDeviceByIndex(index);
  if (!dev) {
    return Value::makeNull();
  }

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }

  auto obj = vm->createHostObject();
  // TODO: string pool integration - for now return null for strings
  (void)dev->name; (void)dev->description;
  vm->setHostObjectField(obj, "name", Value::makeNull());
  vm->setHostObjectField(obj, "description", Value::makeNull());
  vm->setHostObjectField(obj, "index",
                         Value::makeInt(static_cast<int64_t>(dev->index)));
  vm->setHostObjectField(obj, "isDefault", Value::makeBool(dev->isDefault));
  vm->setHostObjectField(obj, "isMuted", Value::makeBool(dev->isMuted));
  vm->setHostObjectField(obj, "volume", Value::makeDouble(dev->volume));
  vm->setHostObjectField(obj, "channels",
                         Value::makeInt(static_cast<int64_t>(dev->channels)));

  return Value::makeObjectId(obj.id);
}

Value
AudioBridge::handleFindDeviceByName(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("audio.findDeviceByName() requires a name string");
  }
  std::string name = args[0].toString();

  auto *dev = ctx->audioManager->findDeviceByName(name);
  if (!dev) {
    return Value::makeNull();
  }

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return Value::makeNull();
  }

  auto obj = vm->createHostObject();
  // TODO: string pool integration - for now return null for strings
  (void)dev->name; (void)dev->description;
  vm->setHostObjectField(obj, "name", Value::makeNull());
  vm->setHostObjectField(obj, "description", Value::makeNull());
  vm->setHostObjectField(obj, "index",
                         Value::makeInt(static_cast<int64_t>(dev->index)));
  vm->setHostObjectField(obj, "isDefault", Value::makeBool(dev->isDefault));
  vm->setHostObjectField(obj, "isMuted", Value::makeBool(dev->isMuted));
  vm->setHostObjectField(obj, "volume", Value::makeDouble(dev->volume));
  vm->setHostObjectField(obj, "channels",
                         Value::makeInt(static_cast<int64_t>(dev->channels)));

  return Value::makeObjectId(obj.id);
}

Value
AudioBridge::handleSetDefaultOutput(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("audio.setDefaultOutput() requires a device name");
  }
  std::string device = args[0].toString();
  return Value::makeBool(ctx->audioManager->setDefaultOutput(device));
}

Value
AudioBridge::handleGetDefaultOutput(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return Value::makeNull();
  }
  // TODO: string pool integration - for now return null
  (void)ctx->audioManager;
  return Value::makeNull();
}

Value
AudioBridge::handlePlayTestSound(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }
  return Value::makeBool(ctx->audioManager->playTestSound());
}

Value
AudioBridge::handleIncreaseVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }

  double amount = 0.05;
  std::string device;

  // Parse arguments: can be (amount) or (device, amount)
  if (args.size() >= 2) {
    // (device, amount)
    if (args[0].isStringValId()) {
      device = args[0].toString();
    }
    if (args[1].isDouble()) {
      amount = args[1].asDouble();
    } else if (args[1].isInt()) {
      amount = static_cast<double>(args[1].asInt());
    }
  } else if (args.size() == 1) {
    // (amount) or (device)
    if (args[0].isDouble()) {
      amount = args[0].asDouble();
    } else if (args[0].isInt()) {
      amount = static_cast<double>(args[0].asInt());
    } else if (args[0].isStringValId()) {
      device = args[0].toString();
    }
  }

  if (device.empty()) {
    return Value::makeDouble(ctx->audioManager->increaseVolume(amount));
  } else {
    return Value::makeDouble(ctx->audioManager->increaseVolume(device, amount));
  }
}

Value
AudioBridge::handleDecreaseVolume(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return Value::makeBool(false);
  }

  double amount = 0.05;
  std::string device;

  // Parse arguments: can be (amount) or (device, amount)
  if (args.size() >= 2) {
    // (device, amount)
    if (args[0].isStringValId()) {
      device = args[0].toString();
    }
    if (args[1].isDouble()) {
      amount = args[1].asDouble();
    } else if (args[1].isInt()) {
      amount = static_cast<double>(args[1].asInt());
    }
  } else if (args.size() == 1) {
    // (amount) or (device)
    if (args[0].isDouble()) {
      amount = args[0].asDouble();
    } else if (args[0].isInt()) {
      amount = static_cast<double>(args[0].asInt());
    } else if (args[0].isStringValId()) {
      device = args[0].toString();
    }
  }

  if (device.empty()) {
    return Value::makeDouble(ctx->audioManager->decreaseVolume(amount));
  } else {
    return Value::makeDouble(ctx->audioManager->decreaseVolume(device, amount));
  }
}

// ============================================================================
// MPVBridge Implementation
// ============================================================================

void MPVBridge::install(PipelineOptions &options) {
  options.host_functions["mpv.volumeUp"] = [ctx = ctx_](const auto &args) {
    return handleVolumeUp(args, ctx);
  };
  options.host_functions["mpv.volumeDown"] = [ctx = ctx_](const auto &args) {
    return handleVolumeDown(args, ctx);
  };
  options.host_functions["mpv.toggleMute"] = [ctx = ctx_](const auto &args) {
    return handleToggleMute(args, ctx);
  };
  options.host_functions["mpv.stop"] = [ctx = ctx_](const auto &args) {
    return handleStop(args, ctx);
  };
  options.host_functions["mpv.next"] = [ctx = ctx_](const auto &args) {
    return handleNext(args, ctx);
  };
  options.host_functions["mpv.previous"] = [ctx = ctx_](const auto &args) {
    return handlePrevious(args, ctx);
  };
  options.host_functions["mpv.seek"] = [ctx = ctx_](const auto &args) {
    return handleSeek(args, ctx);
  };
  options.host_functions["mpv.subSeek"] = [ctx = ctx_](const auto &args) {
    return handleSubSeek(args, ctx);
  };
  options.host_functions["mpv.addSpeed"] = [ctx = ctx_](const auto &args) {
    return handleAddSpeed(args, ctx);
  };
  options.host_functions["mpv.addSubScale"] = [ctx = ctx_](const auto &args) {
    return handleAddSubScale(args, ctx);
  };
  options.host_functions["mpv.addSubDelay"] = [ctx = ctx_](const auto &args) {
    return handleAddSubDelay(args, ctx);
  };
  options.host_functions["mpv.cycle"] = [ctx = ctx_](const auto &args) {
    return handleCycle(args, ctx);
  };
  options.host_functions["mpv.copySubtitle"] = [ctx = ctx_](const auto &args) {
    return handleCopySubtitle(args, ctx);
  };
  options.host_functions["mpv.ipcSet"] = [ctx = ctx_](const auto &args) {
    return handleIPCSet(args, ctx);
  };
  options.host_functions["mpv.ipcReset"] = [ctx = ctx_](const auto &args) {
    return handleIPCRestart(args, ctx);
  };
  options.host_functions["mpv.screenshot"] = [ctx = ctx_](const auto &args) {
    return handleScreenshot(args, ctx);
  };
  options.host_functions["mpv.cmd"] = [ctx = ctx_](const auto &args) {
    return handleCmd(args, ctx);
  };
}

Value MPVBridge::handleVolumeUp(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->VolumeUp();
  return Value::makeBool(true);
}

Value
MPVBridge::handleVolumeDown(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->VolumeDown();
  return Value::makeBool(true);
}

Value
MPVBridge::handleToggleMute(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->ToggleMute();
  return Value::makeBool(true);
}

Value MPVBridge::handleStop(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->Stop();
  return Value::makeBool(true);
}

Value MPVBridge::handleNext(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->Next();
  return Value::makeBool(true);
}

Value MPVBridge::handlePrevious(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->Previous();
  return Value::makeBool(true);
}

Value MPVBridge::handleSeek(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty()) {
    throw std::runtime_error("mpv.seek() requires a seconds argument");
  }
  if (args[0].isStringValId()) {
    std::string seconds = args[0].toString();
    ctx->mpvController->SendCommand({"seek", seconds});
  } else if (args[0].isInt()) {
    int seconds = static_cast<int>(args[0].asInt());
    ctx->mpvController->Seek(seconds);
  } else {
    throw std::runtime_error("mpv.seek() requires a string or number");
  }
  return Value::makeBool(true);
}

Value MPVBridge::handleSubSeek(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty()) {
    throw std::runtime_error("mpv.subSeek() requires an index argument");
  }
  if (args[0].isStringValId()) {
    std::string index = args[0].toString();
    ctx->mpvController->SendCommand({"sub-seek", index});
  } else if (args[0].isInt()) {
    int index = static_cast<int>(args[0].asInt());
    ctx->mpvController->SubSeek(index);
  } else {
    throw std::runtime_error("mpv.subSeek() requires a string or number");
  }
  return Value::makeBool(true);
}

Value MPVBridge::handleAddSpeed(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isDouble()) {
    throw std::runtime_error("mpv.addSpeed() requires a number");
  }
  double delta = args[0].asDouble();
  ctx->mpvController->AddSpeed(delta);
  return Value::makeBool(true);
}

Value
MPVBridge::handleAddSubScale(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isDouble()) {
    throw std::runtime_error("mpv.addSubScale() requires a number");
  }
  double delta = args[0].asDouble();
  ctx->mpvController->AddSubScale(delta);
  return Value::makeBool(true);
}

Value
MPVBridge::handleAddSubDelay(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isDouble()) {
    throw std::runtime_error("mpv.addSubDelay() requires a number");
  }
  double delta = args[0].asDouble();
  ctx->mpvController->AddSubDelay(delta);
  return Value::makeBool(true);
}

Value MPVBridge::handleCycle(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("mpv.cycle() requires a property name");
  }
  std::string property = args[0].toString();
  ctx->mpvController->Cycle(property);
  return Value::makeBool(true);
}

Value
MPVBridge::handleCopySubtitle(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeNull();
  }
  // TODO: string pool integration - for now return null
  (void)ctx->mpvController;
  return Value::makeNull();
}

Value MPVBridge::handleIPCSet(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("mpv.ipcSet() requires a socket path");
  }
  std::string path = args[0].toString();
  ctx->mpvController->SetIPC(path);
  return Value::makeBool(true);
}

Value
MPVBridge::handleIPCRestart(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->IPCRestart();
  return Value::makeBool(true);
}

Value
MPVBridge::handleScreenshot(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  ctx->mpvController->SendCommand({"screenshot"});
  return Value::makeBool(true);
}

Value MPVBridge::handleCmd(const std::vector<Value> &args,
                                   const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return Value::makeBool(false);
  }
  if (args.empty()) {
    throw std::runtime_error("mpv.cmd() requires at least a command name");
  }

  // Build command list from arguments
  std::vector<std::string> cmd;
  for (const auto &arg : args) {
    if (arg.isStringValId()) {
      cmd.push_back(arg.toString());
    } else if (arg.isInt()) {
      cmd.push_back(std::to_string(arg.asInt()));
    } else if (arg.isDouble()) {
      cmd.push_back(std::to_string(arg.asDouble()));
    } else if (arg.isBool()) {
      cmd.push_back(arg.asBool() ? "yes" : "no");
    } else {
      cmd.push_back("null");
    }
  }

  ctx->mpvController->SendCommand(cmd);
  return Value::makeBool(true);
}

// ============================================================================
// DisplayBridge Implementation
// ============================================================================

void DisplayBridge::install(PipelineOptions &options) {
  options.host_functions["display.getMonitors"] =
      [ctx = ctx_](const auto &args) { return handleGetMonitors(args, ctx); };
  options.host_functions["display.getPrimary"] =
      [ctx = ctx_](const auto &args) { return handleGetPrimary(args, ctx); };
  options.host_functions["display.getCount"] = [ctx = ctx_](const auto &args) {
    return handleGetCount(args, ctx);
  };
  options.host_functions["display.getMonitorsArea"] =
      [ctx = ctx_](const auto &args) {
        return handleGetMonitorsArea(args, ctx);
      };
}

Value
DisplayBridge::handleGetMonitors(const std::vector<Value> &args,
                                 const HostContext *ctx) {
  (void)args;
  if (!ctx)
    return Value::makeNull();
  // Return array of monitor info objects
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  auto monitors = ::havel::DisplayManager::GetMonitors();
  auto arr = vm->createHostArray();

  for (const auto &mon : monitors) {
    auto obj = vm->createHostObject();
    // TODO: string pool integration - for now return null for name
    (void)mon.name;
    vm->setHostObjectField(obj, "name", Value::makeNull());
    vm->setHostObjectField(obj, "x",
                           Value::makeInt(static_cast<int64_t>(mon.x)));
    vm->setHostObjectField(obj, "y",
                           Value::makeInt(static_cast<int64_t>(mon.y)));
    vm->setHostObjectField(obj, "width",
                           Value::makeInt(static_cast<int64_t>(mon.width)));
    vm->setHostObjectField(obj, "height",
                           Value::makeInt(static_cast<int64_t>(mon.height)));
    vm->setHostObjectField(obj, "isPrimary", Value::makeBool(mon.isPrimary));
    vm->pushHostArrayValue(arr, Value::makeObjectId(obj.id));
  }

  return Value::makeArrayId(arr.id);
}

Value
DisplayBridge::handleGetPrimary(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx)
    return Value::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return Value::makeNull();

  auto mon = ::havel::DisplayManager::GetPrimaryMonitor();
  auto obj = vm->createHostObject();
  // TODO: string pool integration - for now return null for name
  (void)mon.name;
  vm->setHostObjectField(obj, "name", Value::makeNull());
  vm->setHostObjectField(obj, "x", Value::makeInt(static_cast<int64_t>(mon.x)));
  vm->setHostObjectField(obj, "y", Value::makeInt(static_cast<int64_t>(mon.y)));
  vm->setHostObjectField(obj, "width",
                         Value::makeInt(static_cast<int64_t>(mon.width)));
  vm->setHostObjectField(obj, "height",
                         Value::makeInt(static_cast<int64_t>(mon.height)));
  vm->setHostObjectField(obj, "isPrimary", Value::makeBool(mon.isPrimary));

  return Value::makeObjectId(obj.id);
}

Value
DisplayBridge::handleGetCount(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto monitors = ::havel::DisplayManager::GetMonitors();
  return Value::makeInt(static_cast<int64_t>(monitors.size()));
}

Value
DisplayBridge::handleGetMonitorsArea(const std::vector<Value> &args,
                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto monitors = ::havel::DisplayManager::GetMonitors();

  // Calculate total area
  int64_t totalWidth = 0;
  int64_t totalHeight = 0;
  int64_t minX = INT64_MAX;
  int64_t minY = INT64_MAX;
  int64_t maxX = INT64_MIN;
  int64_t maxY = INT64_MIN;

  for (const auto &mon : monitors) {
    if (static_cast<int64_t>(mon.x) < minX)
      minX = mon.x;
    if (static_cast<int64_t>(mon.y) < minY)
      minY = mon.y;
    if (static_cast<int64_t>(mon.x) + static_cast<int64_t>(mon.width) > maxX) {
      maxX = mon.x + mon.width;
    }
    if (static_cast<int64_t>(mon.y) + static_cast<int64_t>(mon.height) > maxY) {
      maxY = mon.y + mon.height;
    }
  }

  if (minX != INT64_MAX)
    totalWidth = maxX - minX;
  if (minY != INT64_MIN)
    totalHeight = maxY - minY;

  auto *vm = static_cast<VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "width", Value::makeInt(totalWidth));
  vm->setHostObjectField(obj, "height", Value::makeInt(totalHeight));
  vm->setHostObjectField(obj, "totalArea",
                         Value::makeInt(totalWidth * totalHeight));
  vm->setHostObjectField(obj, "x", Value::makeInt(minX == INT64_MAX ? 0 : minX));
  vm->setHostObjectField(obj, "y", Value::makeInt(minY == INT64_MAX ? 0 : minY));

  return Value::makeObjectId(obj.id);
}

// ============================================================================
// ConfigBridge Implementation
// ============================================================================

void ConfigBridge::install(PipelineOptions &options) {
  options.host_functions["config.get"] = [ctx = ctx_](const auto &args) {
    return handleGet(args, ctx);
  };
  options.host_functions["config.set"] = [ctx = ctx_](const auto &args) {
    return handleSet(args, ctx);
  };
  options.host_functions["config.save"] = [ctx = ctx_](const auto &args) {
    return handleSave(args, ctx);
  };
}

Value ConfigBridge::handleGet(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("config.get() requires key and default value");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("config.get() key must be a string");
  }

  std::string key = args[0].toString();
  auto &config = ::havel::Configs::Get();

  // Return value based on default type
  if (args[1].isStringValId()) {
    std::string def = args[1].toString();
    // TODO: string pool integration - for now return null
    (void)config; (void)key; (void)def;
    return Value::makeNull();
  } else if (args[1].isInt()) {
    int64_t def = args[1].asInt();
    return Value::makeInt(config.Get(key, def));
  } else if (args[1].isDouble()) {
    double def = args[1].asDouble();
    return Value::makeDouble(config.Get(key, def));
  } else if (args[1].isBool()) {
    bool def = args[1].asBool();
    return Value::makeBool(config.Get(key, def));
  }

  return Value::makeNull();
}

Value ConfigBridge::handleSet(const std::vector<Value> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("config.set() requires key and value");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("config.set() key must be a string");
  }

  std::string key = args[0].toString();
  auto &config = ::havel::Configs::Get();

  bool save = (args.size() > 2 && args[2].isBool() &&
               args[2].asBool());

  if (args[1].isStringValId()) {
    config.Set(key, args[1].toString(), save);
  } else if (args[1].isInt()) {
    config.Set(key, args[1].asInt(), save);
  } else if (args[1].isDouble()) {
    config.Set(key, args[1].asDouble(), save);
  } else if (args[1].isBool()) {
    config.Set(key, args[1].asBool(), save);
  }

  return Value::makeBool(true);
}

Value ConfigBridge::handleSave(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto &config = ::havel::Configs::Get();
  config.Save();
  return Value::makeBool(true);
}

// ============================================================================
// ModeBridge Implementation
// ============================================================================

void ModeBridge::install(PipelineOptions &options) {
  // mode() - returns current mode name (for comparisons like mode == "gaming")
  options.host_functions["mode"] = [ctx = ctx_](const auto &args) {
    (void)args;
    if (!ctx || !ctx->modeManager) {
      return Value::makeNull();
    }
    // TODO: string pool integration - for now return null
    (void)ctx->modeManager;
    return Value::makeNull();
  };
  options.host_functions["mode.register"] = [ctx = ctx_](const auto &args) {
    return handleRegister(args, ctx);
  };
  options.host_functions["mode.current"] = [ctx = ctx_](const auto &args) {
    return handleGetCurrent(args, ctx);
  };
  options.host_functions["mode.set"] = [ctx = ctx_](const auto &args) {
    return handleSet(args, ctx);
  };
  options.host_functions["mode.previous"] = [ctx = ctx_](const auto &args) {
    return handleGetPrevious(args, ctx);
  };
}

Value ModeBridge::handleRegister(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  // Args: name, priority, condition, enter, exit, onEnterFromMode, onEnterFrom,
  // onExitToMode, onExitTo
  if (args.size() < 9) {
    return Value::makeBool(false);
  }

  if (!ctx || !ctx->modeManager || !ctx->vm) {
    return Value::makeBool(false);
  }

  // Get mode name
  std::string modeName;
  if (args[0].isStringValId()) {
    modeName = args[0].toString();
  } else {
    return Value::makeBool(false);
  }

  // Get priority
  int priority = 0;
  if (args[1].isInt()) {
    priority = static_cast<int>(args[1].asInt());
  }

  auto *vm = static_cast<VM *>(ctx->vm);

  // Helper to register a callback from Value
  auto registerCallbackIfValid = [&](const Value &val) -> CallbackId {
    if (val.isClosureId() ||
        val.isFunctionObjId()) {
      return vm->registerCallback(val);
    }
    return INVALID_CALLBACK_ID;
  };

  // Register callbacks
  CallbackId conditionId = registerCallbackIfValid(args[2]);
  CallbackId enterId = registerCallbackIfValid(args[3]);
  CallbackId exitId = registerCallbackIfValid(args[4]);
  CallbackId onEnterFromId = registerCallbackIfValid(args[6]);
  CallbackId onExitToId = registerCallbackIfValid(args[8]);

  // Create mode definition
  ::havel::ModeManager::ModeDefinition mode;
  mode.name = modeName;
  mode.priority = priority;

  // Condition callback - wraps the VM callback invocation
  if (conditionId != INVALID_CALLBACK_ID) {
    mode.conditionCallback = [vm, conditionId]() -> bool {
      try {
        auto result = vm->invokeCallback(conditionId);
        // Convert result to boolean
        if (result.isBool()) {
          return result.asBool();
        }
        if (result.isInt()) {
          return result.asInt() != 0;
        }
        return false;
      } catch (...) {
        // Callback failed, treat as false
        return false;
      }
    };
  }

  // Enter callback
  if (enterId != INVALID_CALLBACK_ID) {
    mode.onEnter = [vm, enterId]() {
      try {
        vm->invokeCallback(enterId);
      } catch (...) {
        // Callback failed, ignore
      }
    };
  }

  // Exit callback
  if (exitId != INVALID_CALLBACK_ID) {
    mode.onExit = [vm, exitId]() {
      try {
        vm->invokeCallback(exitId);
      } catch (...) {
        // Callback failed, ignore
      }
    };
  }

  // onEnterFrom callback (transition from specific mode)
  if (onEnterFromId != INVALID_CALLBACK_ID) {
    mode.onEnterFrom = [vm, onEnterFromId](const std::string &fromMode) {
      (void)fromMode;
      try {
        vm->invokeCallback(onEnterFromId);
      } catch (...) {
        // Callback failed, ignore
      }
    };
  }

  // onExitTo callback (transition to specific mode)
  if (onExitToId != INVALID_CALLBACK_ID) {
    mode.onExitTo = [vm, onExitToId](const std::string &toMode) {
      (void)toMode;
      try {
        vm->invokeCallback(onExitToId);
      } catch (...) {
        // Callback failed, ignore
      }
    };
  }

  // Register with ModeManager
  ctx->modeManager->defineMode(std::move(mode));

  // Store callback IDs in HostBridge for cleanup
  // This is stored in mode_bindings_ map
  ctx->hostBridge->registerModeCallbacks(modeName, conditionId, enterId,
                                         exitId);

  info("Mode registered: {} with priority {}", modeName, priority);
  return Value::makeBool(true);
}

Value
ModeBridge::handleGetCurrent(const std::vector<Value> &args,
                             const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->modeManager) {
    return Value::makeNull();
  }
  // TODO: string pool integration - for now return null
  (void)ctx;
  return Value::makeNull();
}

Value ModeBridge::handleSet(const std::vector<Value> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->modeManager) {
    return Value::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("mode.set() requires a mode name string");
  }
  std::string modeName = args[0].toString();
  ctx->modeManager->setMode(modeName);
  // TODO: string pool integration - for now return null
  (void)ctx;
  return Value::makeNull();
}

Value
ModeBridge::handleGetPrevious(const std::vector<Value> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->modeManager) {
    return Value::makeNull();
  }
  // TODO: string pool integration - for now return null
  (void)ctx->modeManager;
  return Value::makeNull();
}

// ============================================================================
// TimerBridge Implementation
// ============================================================================

void TimerBridge::install(PipelineOptions &options) {
  // Timer functions are available but require closure support for callbacks
  // For now, use sleep() for simple delays
  options.host_functions["timer.after"] = [ctx = ctx_](const auto &args) {
    return handleAfter(args, ctx);
  };
  options.host_functions["timer.every"] = [ctx = ctx_](const auto &args) {
    return handleEvery(args, ctx);
  };
}

Value TimerBridge::handleAfter(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("timer.after() requires delay_ms");
  }

  if (!args[0].isInt()) {
    throw std::runtime_error("timer.after() delay must be an integer");
  }

  int64_t delay_ms = args[0].asInt();

  // Simple implementation: just sleep
  // For callback support, use: sleep(delay_ms); callback()
  if (ctx && ctx->asyncService) {
    ctx->asyncService->sleep(static_cast<int>(delay_ms));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }

  return Value::makeNull();
}

Value TimerBridge::handleEvery(const std::vector<Value> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // timer.every requires closure/callback support
  // For now, just return without doing anything
  // Users should use a while loop with sleep instead:
  // while (true) { body; sleep(interval); }
  throw std::runtime_error("timer.every() requires closure support - use while "
                           "loop with sleep() instead");
}

// ============================================================================
// AppBridge Implementation
// ============================================================================

void AppBridge::install(PipelineOptions &options) {
  options.host_functions["app.getName"] = [ctx = ctx_](const auto &args) {
    return handleAppGetName(args, ctx);
  };
  options.host_functions["app.getVersion"] = [ctx = ctx_](const auto &args) {
    return handleAppGetVersion(args, ctx);
  };
  options.host_functions["app.getOS"] = [ctx = ctx_](const auto &args) {
    return handleAppGetOS(args, ctx);
  };
  options.host_functions["app.getHostname"] = [ctx = ctx_](const auto &args) {
    return handleAppGetHostname(args, ctx);
  };
  options.host_functions["app.getUsername"] = [ctx = ctx_](const auto &args) {
    return handleAppGetUsername(args, ctx);
  };
  options.host_functions["app.getHomeDir"] = [ctx = ctx_](const auto &args) {
    return handleAppGetHomeDir(args, ctx);
  };
  options.host_functions["app.getCpuCores"] = [ctx = ctx_](const auto &args) {
    return handleAppGetCpuCores(args, ctx);
  };
  options.host_functions["app.getEnv"] = [ctx = ctx_](const auto &args) {
    return handleAppGetEnv(args, ctx);
  };
  options.host_functions["app.setEnv"] = [ctx = ctx_](const auto &args) {
    return handleAppSetEnv(args, ctx);
  };
  options.host_functions["app.openUrl"] = [ctx = ctx_](const auto &args) {
    return handleAppOpenUrl(args, ctx);
  };
  options.host_functions["app.exit"] = [ctx = ctx_](const auto &args) {
    std::exit(0);
    return Value();
  };
  options.host_functions["app.restart"] = [ctx = ctx_](const auto &args) {
    std::exit(42);
    return Value();
  };
}

Value
AppBridge::handleAppGetName(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}

Value
AppBridge::handleAppGetVersion(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}

Value AppBridge::handleAppGetOS(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}

Value
AppBridge::handleAppGetHostname(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}

Value
AppBridge::handleAppGetUsername(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}

Value
AppBridge::handleAppGetHomeDir(const std::vector<Value> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app;
  return Value::makeNull();
}

Value
AppBridge::handleAppGetCpuCores(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  ::havel::host::AppService app;
  return Value::makeInt(static_cast<int64_t>(app.getCpuCores()));
}

Value AppBridge::handleAppGetEnv(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.getEnv() requires a variable name");
  }
  const std::string *name = nullptr;
  if (!name) {
    throw std::runtime_error("app.getEnv() requires a string");
  }
  ::havel::host::AppService app;
  // TODO: string pool integration - for now return null
  (void)app; (void)name;
  return Value::makeNull();
}

Value AppBridge::handleAppSetEnv(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2) {
    throw std::runtime_error("app.setEnv() requires name and value");
  }
  const std::string *name = nullptr;
  const std::string *value = nullptr;
  if (!name || !value) {
    throw std::runtime_error("app.setEnv() requires string arguments");
  }
  ::havel::host::AppService app;
  // TODO: bool return - for now return null
  (void)app; (void)name; (void)value;
  return Value::makeNull();
}

Value
AppBridge::handleAppOpenUrl(const std::vector<Value> &args,
                            const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.openUrl() requires a URL");
  }
  const std::string *url = nullptr;
  if (!url) {
    throw std::runtime_error("app.openUrl() requires a string URL");
  }
  ::havel::host::AppService app;
  // TODO: bool return - for now return null
  (void)app; (void)url;
  return Value::makeNull();
}

// Active window namespace implementations
Value UIBridge::handleActiveGet(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  return handleWindowGetActive(args, ctx);
}

Value
UIBridge::handleActiveTitle(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeNull();
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeNull();
  }
  // TODO: string pool integration - for now return null
  (void)info;
  return Value::makeNull();
}

Value
UIBridge::handleActiveClass(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  return Value::makeNull();
}

Value UIBridge::handleActiveExe(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // TODO: string pool integration - for now return null
  return Value::makeNull();
}

Value UIBridge::handleActivePid(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeInt(static_cast<int64_t>(0));
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeInt(static_cast<int64_t>(0));
  }
  return Value::makeInt(static_cast<int64_t>(info.pid));
}

Value
UIBridge::handleActiveClose(const std::vector<Value> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.closeWindow(info.id);
  return Value::makeBool(true);
}

Value UIBridge::handleActiveMin(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.minimizeWindow(info.id);
  return Value::makeBool(true);
}

Value UIBridge::handleActiveMax(const std::vector<Value> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.maximizeWindow(info.id);
  return Value::makeBool(true);
}

Value UIBridge::handleActiveHide(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.hideWindow(info.id);
  return Value::makeBool(true);
}

Value UIBridge::handleActiveShow(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return Value::makeBool(false);
  }
  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.showWindow(info.id);
  return Value::makeBool(true);
}

Value UIBridge::handleActiveMove(const std::vector<Value> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  int64_t x = 0, y = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    x = v->asInt();
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    y = v->asInt();

  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.moveWindow(info.id, static_cast<int>(x), static_cast<int>(y));
  return Value::makeBool(true);
}

Value
UIBridge::handleActiveResize(const std::vector<Value> &args,
                             const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return Value::makeBool(false);
  }
  int64_t w = 0, h = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    w = v->asInt();
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    h = v->asInt();

  ::havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return Value::makeBool(false);
  }
  winService.resizeWindow(info.id, static_cast<int>(w), static_cast<int>(h));
  return Value::makeBool(true);
}

} // namespace havel::compiler
