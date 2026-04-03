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
#include "gui/ClipboardManager.hpp"
#include "gui/GUIManager.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "host/app/AppService.hpp"
#include "host/async/AsyncService.hpp"
#include "host/audio/AudioService.hpp"
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
  BytecodeValue result = nullptr;
  std::string error;
};

struct ChannelRecord {
  std::deque<BytecodeValue> queue;
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
  vm->setHostObjectField(obj, "__kind", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "__id", BytecodeValue::makeNull());
  (void)kind; (void)id;
  return obj;
}

std::optional<std::pair<std::string, std::string>>
extractHandle(const std::vector<BytecodeValue> &args, VM *vm,
              size_t index = 0) {
  if (!vm || index >= args.size() ||
      !args[index].isObjectId()) {
    return std::nullopt;
  }
  ObjectRef obj{args[index].id, true};
  BytecodeValue kind = vm->getHostObjectField(obj, "__kind");
  BytecodeValue id = vm->getHostObjectField(obj, "__id");
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
}

BytecodeValue IOBridge::handleSend(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::IOService ioService(ctx->io);
  if (false) { // TODO: string support
    return BytecodeValue::makeNull();
  }
  return BytecodeValue::makeBool(false);
}

BytecodeValue IOBridge::handleSendKey(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::IOService ioService(ctx->io);
  if (false) { // TODO: string support
    return BytecodeValue::makeNull();
  }
  return BytecodeValue::makeBool(false);
}

BytecodeValue IOBridge::handleSendText(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue::makeBool(false);
  }
  // TODO: string support - disabled until string pooling is implemented
  (void)args;
  return BytecodeValue::makeBool(false);
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
    return BytecodeValue::makeBool(true);
  }
  return BytecodeValue::makeBool(false);
#endif
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

  // Create system object with proper namespacing
  // This will be set globally by HostBridge after VM is available
  options.system_object_initializer = [](compiler::VM *vm) {
    auto systemObj = vm->createHostObject();
    // TODO: Register host functions properly
    vm->setHostObjectField(
        systemObj, "detect",
        BytecodeValue::makeNull());
    vm->setHostObjectField(
        systemObj, "hardware",
        BytecodeValue::makeNull());
    vm->setGlobal("system", BytecodeValue::makeObjectId(systemObj.id));
  };

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
  // System detection
  options.host_functions["system.detect"] = [ctx = ctx_](const auto &args) {
    return handleSystemDetect(args, ctx);
  };
  options.host_functions["system.hardware"] = [ctx = ctx_](const auto &args) {
    return handleSystemHardware(args, ctx);
  };
}

BytecodeValue
SystemBridge::handleFileRead(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("readFile() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("readFile() requires a string path");
  }
  havel::host::FileSystemService fs;
  std::string content = fs.readFile(*path);
  if (content.empty()) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue::makeNull();
}

BytecodeValue
SystemBridge::handleFileWrite(const std::vector<BytecodeValue> &args,
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
  havel::host::FileSystemService fs;
  return BytecodeValue::makeNull();
}

BytecodeValue
SystemBridge::handleFileExists(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileExists() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("fileExists() requires a string path");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue::makeNull();
}

BytecodeValue
SystemBridge::handleFileSize(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileSize() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("fileSize() requires a string path");
  }
  havel::host::FileSystemService fs;
  if (!fs.exists(*path)) {
    return BytecodeValue::makeInt(static_cast<int64_t>(0));
  }
  return BytecodeValue::makeNull();
}

BytecodeValue
SystemBridge::handleFileDelete(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("deleteFile() requires a file path");
  }
  const std::string *path = nullptr;
  if (!path) {
    throw std::runtime_error("deleteFile() requires a string path");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue::makeNull();
}

BytecodeValue
SystemBridge::handleProcessExecute(const std::vector<BytecodeValue> &args,
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
  return BytecodeValue::makeNull();
}

BytecodeValue
SystemBridge::handleProcessGetPid(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeInt(
      static_cast<int64_t>(havel::host::ProcessService::getCurrentPid()));
}

BytecodeValue
SystemBridge::handleProcessGetPpid(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeInt(
      static_cast<int64_t>(havel::host::ProcessService::getParentPid()));
}

BytecodeValue
SystemBridge::handleProcessFind(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("process.find() requires a process name");
  }
  const std::string *name = nullptr;
  if (!name) {
    throw std::runtime_error("process.find() requires a string argument");
  }
  auto pids = havel::host::ProcessService::findProcesses(*name);
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue::makeNull();
  }
  auto arr = vm->createHostArray();
  for (int32_t pid : pids) {
    vm->pushHostArrayValue(arr, BytecodeValue::makeInt(static_cast<int64_t>(pid)));
  }
  return BytecodeValue::makeArrayId(arr.id);
}

BytecodeValue
SystemBridge::handleProcessExists(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.exists() requires a process name or PID");
  }
  if (args[0].isInt()) {
    int32_t pid = static_cast<int32_t>(args[0].asInt());
    return BytecodeValue(havel::host::ProcessService::isProcessAlive(pid));
  }
  const std::string *name = nullptr;
  if (!name) {
    throw std::runtime_error("process.exists() requires a string or number");
  }
  return BytecodeValue(havel::host::ProcessService::processExists(*name));
}

BytecodeValue
SystemBridge::handleProcessKill(const std::vector<BytecodeValue> &args,
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
  const std::string *sig = nullptr;
  if (!sig) {
    throw std::runtime_error("process.kill() requires a string signal");
  }
  int signal_num = 15; // Default SIGTERM
  if (*sig == "SIGKILL" || *sig == "kill")
    signal_num = 9;
  else if (*sig == "SIGTERM" || *sig == "term")
    signal_num = 15;
  else if (*sig == "SIGHUP" || *sig == "hangup")
    signal_num = 1;
  else if (*sig == "SIGINT" || *sig == "int")
    signal_num = 2;
  return BytecodeValue::makeBool(
      havel::host::ProcessService::sendSignal(pid, signal_num));
}

BytecodeValue
SystemBridge::handleProcessNice(const std::vector<BytecodeValue> &args,
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
  return BytecodeValue::makeBool(
      havel::host::ProcessService::setNice(pid, static_cast<int>(nice)));
}

BytecodeValue
SystemBridge::handleProcessRun(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.run() requires a command");
  }
  const std::string *cmd = nullptr;
  if (!cmd) {
    throw std::runtime_error("process.run() requires a string command");
  }
  auto result = havel::Launcher::run(*cmd, {}, {});
  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "pid", BytecodeValue::makeInt(result.pid));
  vm->setHostObjectField(obj, "exitCode", BytecodeValue::makeInt(result.exitCode));
  vm->setHostObjectField(obj, "success", BytecodeValue::makeBool(result.success));
  vm->setHostObjectField(obj, "error", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "stdout", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "stderr", BytecodeValue::makeNull());
  return BytecodeValue::makeObjectId(obj.id);
}

BytecodeValue
SystemBridge::handleProcessRunDetached(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.runDetached() requires a command");
  }
  const std::string *cmd = nullptr;
  if (!cmd) {
    throw std::runtime_error("process.runDetached() requires a string command");
  }
  auto result = havel::Launcher::runDetached(*cmd);
  return BytecodeValue::makeInt(result.pid);
}

// Alias implementations - forward to appropriate bridge handlers
BytecodeValue
SystemBridge::handleMediaPlay(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  return MediaBridge::handleMediaPlay(args, ctx);
}

// ============================================================================
// System Detection Implementation
// ============================================================================

BytecodeValue
SystemBridge::handleSystemDetect(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)args;
  std::cerr << "[DEBUG] handleSystemDetect: ctx=" << ctx
            << " ctx->vm=" << (ctx ? ctx->vm : nullptr) << "\n";

  if (!ctx || !ctx->vm) {
    // Return a minimal object if VM is not available
    std::cerr << "[DEBUG] handleSystemDetect: ctx or vm is null, returning "
                 "empty string\n";
    return BytecodeValue::makeNull();
  }

  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  std::cerr << "[DEBUG] handleSystemDetect: vm=" << vm << ", creating object\n";
  auto obj = vm->createHostObject();
  std::cerr << "[DEBUG] handleSystemDetect: object created, setting fields\n";

  // Use HardwareDetector for system detection
  auto sysInfo = havel::HardwareDetector::detectSystem();
  std::cerr << "[DEBUG] handleSystemDetect: detected OS=" << sysInfo.os << "\n";

  vm->setHostObjectField(obj, "os", BytecodeValue::makeNull());
  std::cerr << "[DEBUG] handleSystemDetect: set os field\n";
  vm->setHostObjectField(obj, "shell", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "user", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "home", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "hostname", BytecodeValue::makeNull());
  std::cerr << "[DEBUG] handleSystemDetect: all fields set, returning\n";

  // Linux-specific fields
  if (!sysInfo.displayProtocol.empty()) {
    vm->setHostObjectField(obj, "displayProtocol",
                           BytecodeValue::makeNull());
  }
  if (!sysInfo.display.empty()) {
    vm->setHostObjectField(obj, "display", BytecodeValue::makeNull());
  }
  if (!sysInfo.windowManager.empty()) {
    vm->setHostObjectField(obj, "windowManager",
                           BytecodeValue::makeNull());
  }
  if (!sysInfo.desktopEnv.empty()) {
    vm->setHostObjectField(obj, "desktopEnv",
                           BytecodeValue::makeNull());
  }

  return BytecodeValue::makeObjectId(obj.id);
}

BytecodeValue
SystemBridge::handleSystemHardware(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  (void)args;

  if (!ctx || !ctx->vm) {
    return BytecodeValue::makeNull();
  }

  auto *vm = static_cast<compiler::VM *>(ctx->vm);
  auto obj = vm->createHostObject();

  // Use HardwareDetector for hardware detection
  auto hwInfo = havel::HardwareDetector::detectHardware();

  vm->setHostObjectField(obj, "cpu", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "cpuCores",
                         BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.cpuCores)));
  vm->setHostObjectField(
      obj, "cpuThreads",
      BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.cpuThreads)));
  vm->setHostObjectField(obj, "cpuFrequency",
                         BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "cpuUsage", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "gpu", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "gpuTemperature",
                         BytecodeValue::makeNull());

  // Memory info (all in bytes)
  vm->setHostObjectField(obj, "ramTotal",
                         BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.ramTotal)));
  vm->setHostObjectField(obj, "ramUsed",
                         BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.ramUsed)));
  vm->setHostObjectField(obj, "ramFree",
                         BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.ramFree)));

  // Swap info (in bytes)
  vm->setHostObjectField(obj, "swapTotal",
                         BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.swapTotal)));
  vm->setHostObjectField(obj, "swapUsed",
                         BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.swapUsed)));
  vm->setHostObjectField(obj, "swapFree",
                         BytecodeValue::makeInt(static_cast<int64_t>(hwInfo.swapFree)));

  vm->setHostObjectField(obj, "motherboard", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "bios", BytecodeValue::makeNull());
  vm->setHostObjectField(obj, "cpuTemperature",
                         BytecodeValue::makeNull());

  // Storage array
  auto storageArr = vm->createHostArray();
  for (const auto &device : hwInfo.storage) {
    auto storageObj = vm->createHostObject();
    vm->setHostObjectField(storageObj, "name", BytecodeValue::makeNull());
    vm->setHostObjectField(storageObj, "model", BytecodeValue::makeNull());
    vm->setHostObjectField(storageObj, "size",
                           BytecodeValue::makeInt(static_cast<int64_t>(device.size)));
    vm->setHostObjectField(storageObj, "used",
                           BytecodeValue::makeInt(static_cast<int64_t>(device.used)));
    vm->setHostObjectField(storageObj, "free",
                           BytecodeValue::makeInt(static_cast<int64_t>(device.free)));
    vm->setHostObjectField(storageObj, "type", BytecodeValue::makeNull());
    vm->setHostObjectField(storageObj, "mountPoint",
                           BytecodeValue::makeNull());
    vm->setHostObjectField(storageObj, "filesystem",
                           BytecodeValue::makeNull());
    vm->pushHostArrayValue(storageArr, BytecodeValue::makeObjectId(storageObj.id));
  }
  vm->setHostObjectField(obj, "storage", BytecodeValue::makeArrayId(storageArr.id));

  return BytecodeValue::makeObjectId(obj.id);
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
static BytecodeValue createWindowObject(
    VM *vm, const HostContext *ctx, uint64_t windowId,
    const std::string &title = "", const std::string &windowClass = "",
    const std::string &exe = "", int pid = 0, const std::string &cmdline = "") {
  if (!vm || !ctx || !ctx->windowManager) {
    return BytecodeValue::makeNull();
  }

  VMApi api(*vm);
  auto obj = api.makeObject();
  api.setField(obj, "id", BytecodeValue::makeInt(static_cast<int64_t>(windowId)));
  api.setField(obj, "title", BytecodeValue::makeNull());
  api.setField(obj, "class", BytecodeValue::makeNull());
  api.setField(obj, "exe", BytecodeValue::makeNull());
  api.setField(obj, "pid", BytecodeValue::makeInt(static_cast<int64_t>(pid)));
  api.setField(obj, "cmd", BytecodeValue::makeNull());

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

  return BytecodeValue::makeObjectId(obj.id);
}

BytecodeValue
UIBridge::handleWindowGetActive(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager || !ctx->vm) {
    return BytecodeValue::makeNull();
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeNull();
  }
  return createWindowObject(static_cast<VM *>(ctx->vm), ctx, info.id,
                            info.title, info.windowClass, info.exe, info.pid,
                            info.cmdline);
}

BytecodeValue UIBridge::handleWindowCmd(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue::makeNull();
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeNull();

  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getWindowInfo(wid);
  if (!info.valid) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue(info.cmdline);
}

// Shared window object methods - take object as first argument, extract id
BytecodeValue
UIBridge::handleWindowCloseObj(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
  }

  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.closeWindow(wid));
}

BytecodeValue
UIBridge::handleWindowHideObj(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
  }

  havel::host::WindowService winService(ctx->windowManager);
  winService.hideWindow(wid);
  return BytecodeValue::makeBool(true);
}

BytecodeValue
UIBridge::handleWindowShowObj(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
  }

  havel::host::WindowService winService(ctx->windowManager);
  winService.showWindow(wid);
  return BytecodeValue::makeBool(true);
}

BytecodeValue
UIBridge::handleWindowFocusObj(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
  }

  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.focusWindow(wid));
}

BytecodeValue
UIBridge::handleWindowMinObj(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
  }

  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.minimizeWindow(wid));
}

BytecodeValue
UIBridge::handleWindowMaxObj(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
  }

  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.maximizeWindow(wid));
}

BytecodeValue
UIBridge::handleWindowResizeObj(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
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

  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.resizeWindow(wid, w, h));
}

BytecodeValue
UIBridge::handleWindowMoveObj(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager)
    return BytecodeValue::makeBool(false);

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
    return BytecodeValue::makeBool(false);
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

  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindow(wid, x, y));
}

BytecodeValue UIBridge::handleWindowFind(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager || !ctx->vm) {
    return BytecodeValue::makeNull();
  }

  // TODO: string selector support disabled
  (void)args;
  return BytecodeValue::makeNull();
#if 0
  std::string selector;
  if nullptr)
    selector = *v;
  else
    return BytecodeValue::makeNull();

  // Parse selector: "type value" where type is title/class/exe/pid/cmd
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return BytecodeValue::makeNull();
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  havel::host::WindowService winService(ctx->windowManager);
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
  return BytecodeValue::makeNull();
}

// Helper: resolve window argument (ID or selector string) to window ID
static uint64_t resolveWindowId(const BytecodeValue &arg,
                                havel::host::WindowService &winService) {
  uint64_t wid = 0;

  // Try as integer ID first
  if (auto *v = (arg.isInt() ? &arg : nullptr)) {
    wid = static_cast<uint64_t>(v->asInt());
  } else if (false) { // TODO: string support
    // Try as selector string "type value"
    std::string selector = *v;
    size_t spacePos = selector.find(' ');
    if (spacePos != std::string::npos) {
      std::string type = selector.substr(0, spacePos);
      std::string value = selector.substr(spacePos + 1);

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
          try {
            int pid = std::stoi(value);
            match = (win.pid == pid);
          } catch (...) {
          }
        } else if (type == "cmd") {
          match = (win.cmdline.find(value) != std::string::npos);
        }

        if (match) {
          return win.id;
        }
      }
    }
  }

  return wid;
}

BytecodeValue
UIBridge::handleWindowClose(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService);
  if (wid == 0)
    return BytecodeValue::makeBool(false);
  return BytecodeValue(winService.closeWindow(wid));
}

BytecodeValue
UIBridge::handleWindowResize(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::WindowService winService(ctx->windowManager);
  uint64_t wid = resolveWindowId(args[0], winService);
  if (wid == 0)
    return BytecodeValue::makeBool(false);
  int w = 0, h = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    w = static_cast<int>(v->asInt());
  else if (auto *v = (args[1].isDouble() ? &args[1] : nullptr))
    w = static_cast<int>(v->asInt());
  if (auto *v = (args[2].isInt() ? &args[2] : nullptr))
    h = static_cast<int>(v->asInt());
  else if (auto *v = (args[2].isDouble() ? &args[2] : nullptr))
    h = static_cast<int>(v->asInt());
  return BytecodeValue(winService.resizeWindow(wid, w, h));
}

BytecodeValue
UIBridge::handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeBool(false);
  int monitor = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    monitor = static_cast<int>(*v);
  else if (auto *v = (args[1].isDouble() ? &args[1] : nullptr))
    monitor = static_cast<int>(*v);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindowToMonitor(wid, monitor));
}

BytecodeValue
UIBridge::handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  // TODO: Implement move to next monitor
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindowToMonitor(0, 0));
}

BytecodeValue UIBridge::handleWindowMove(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeBool(false);
  int x = 0, y = 0;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    x = static_cast<int>(v->asInt());
  if (auto *v = (args[2].isInt() ? &args[2] : nullptr))
    y = static_cast<int>(v->asInt());
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindow(wid, x, y));
}

BytecodeValue
UIBridge::handleWindowFocus(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeBool(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.focusWindow(wid));
}

BytecodeValue
UIBridge::handleWindowMinimize(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeBool(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.minimizeWindow(wid));
}

BytecodeValue
UIBridge::handleWindowMaximize(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeBool(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.maximizeWindow(wid));
}

BytecodeValue UIBridge::handleWindowHide(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeBool(false);
  havel::host::WindowService winService(ctx->windowManager);
  winService.hideWindow(wid);
  return BytecodeValue::makeBool(true);
}

BytecodeValue UIBridge::handleWindowShow(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  uint64_t wid = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    wid = static_cast<uint64_t>(v->asInt());
  else
    return BytecodeValue::makeBool(false);
  havel::host::WindowService winService(ctx->windowManager);
  winService.showWindow(wid);
  return BytecodeValue::makeBool(true);
}

// Window query functions implementation
BytecodeValue UIBridge::handleWindowAny(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty()) {
    return BytecodeValue::makeBool(false);
  }

  // Get selector string: "type value" where type is title/class/exe/pid/cmd
  std::string selector;
  if (false) { // TODO: string support
    selector = *v;
  } else {
    return BytecodeValue::makeBool(false);
  }

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return BytecodeValue::makeBool(false);
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  havel::host::WindowService winService(ctx->windowManager);

  // Use anyWindow with predicate
  bool result = winService.anyWindow([&](const havel::host::WindowInfo &win) {
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

  return BytecodeValue(result);
}

BytecodeValue
UIBridge::handleWindowCount(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return BytecodeValue::makeInt(static_cast<int64_t>(0));
  }

  havel::host::WindowService winService(ctx->windowManager);

  // If no selector provided, count all windows
  if (args.empty()) {
    auto windows = winService.getAllWindows();
    return BytecodeValue::makeInt(static_cast<int64_t>(windows.size()));
  }

  // Get selector string
  std::string selector;
  if (false) { // TODO: string support
    selector = *v;
  } else {
    auto windows = winService.getAllWindows();
    return BytecodeValue::makeInt(static_cast<int64_t>(windows.size()));
  }

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    auto windows = winService.getAllWindows();
    return BytecodeValue::makeInt(static_cast<int64_t>(windows.size()));
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  // Use countWindows with predicate
  int count = winService.countWindows([&](const havel::host::WindowInfo &win) {
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

  return BytecodeValue::makeInt(static_cast<int64_t>(count));
}

BytecodeValue
UIBridge::handleWindowFilter(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (!ctx->windowManager || !ctx->vm) {
    return BytecodeValue::makeNull();
  }
  if (args.empty()) {
    return BytecodeValue::makeNull();
  }

  // Get selector string
  std::string selector;
  if (false) { // TODO: string support
    selector = *v;
  } else {
    return BytecodeValue::makeNull();
  }

  // Parse selector
  size_t spacePos = selector.find(' ');
  if (spacePos == std::string::npos) {
    return BytecodeValue::makeNull();
  }

  std::string type = selector.substr(0, spacePos);
  std::string value = selector.substr(spacePos + 1);

  havel::host::WindowService winService(ctx->windowManager);

  // Use filterWindows with predicate
  auto matchingWindows =
      winService.filterWindows([&](const havel::host::WindowInfo &win) {
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

  return BytecodeValue::makeArrayId(arr.id);
}

BytecodeValue
UIBridge::handleClipboardGet(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return BytecodeValue::makeNull();
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue(clipboard->text().toStdString());
}

BytecodeValue
UIBridge::handleClipboardSet(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (args.empty() || !ctx->clipboardManager) {
    return BytecodeValue::makeBool(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue::makeBool(false);
  }
  if (false) { // TODO: string support
    clipboard->setText(QString::fromStdString(*val));
    return BytecodeValue::makeBool(true);
  }
  return BytecodeValue::makeBool(false);
}

BytecodeValue
UIBridge::handleClipboardClear(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return BytecodeValue::makeBool(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue::makeBool(false);
  }
  clipboard->clear();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
UIBridge::handleScreenshotFull(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::ScreenshotService service;
  auto data = service.captureFullDesktop();
  (void)data;
  return BytecodeValue::makeNull();
}

BytecodeValue
UIBridge::handleScreenshotMonitor(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  (void)ctx;
  int monitor = 0;
  if (!args.empty()) {
    if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
      monitor = static_cast<int>(*v);
  }
  havel::host::ScreenshotService service;
  auto data = service.captureMonitor(monitor);
  (void)data;
  return BytecodeValue::makeNull();
}

BytecodeValue UIBridge::handleGUINotify(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  if (!ctx || !ctx->guiManager) {
    return BytecodeValue::makeBool(false);
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
  return BytecodeValue::makeBool(true);
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

BytecodeValue
InputBridge::handleHotkeyRegister(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  // Args: [hotkey_string, callback_closure]
  if (args.size() < 2) {
    return BytecodeValue::makeBool(false);
  }

  if (!ctx || !ctx->hotkeyManager || !ctx->vm || !ctx->io) {
    return BytecodeValue::makeBool(false);
  }

  // Get hotkey string
  std::string hotkeyStr;
  if (args[0].isStringValId()) {
    hotkeyStr = args[0].toString();
  } else {
    return BytecodeValue::makeBool(false);
  }

  // Generate unique hotkey ID
  std::string hotkeyId =
      "hotkey_" + std::to_string(std::hash<std::string>{}(hotkeyStr));

  // Register closure as a callback - this pins it as a GC root
  CallbackId callbackId = ctx->vm->registerCallback(args[1]);

  // Create hotkey context object using HotkeyModule
  auto *vm = static_cast<VM *>(ctx->vm);
  auto hotkeyContext = havel::stdlib::HotkeyModule::createHotkeyContext(
      vm, hotkeyId, hotkeyStr, hotkeyStr, "",
      "Hotkey registered via hotkey.register", callbackId);

  // Get HotkeyExecutor for thread-safe execution
  auto *executor = ctx->io->GetHotkeyExecutor();

  // Register hotkey with execution context isolation
  // This is the CORRECT way: each hotkey execution gets its own isolated
  // stack/locals but shares globals/heap
  bool success = ctx->hotkeyManager->AddHotkey(
      hotkeyStr, [vm, callbackId, hotkeyContext, executor]() {
        if (executor) {
          // Use execution context for thread-safe isolated execution
          executor->submitExecutionContext(
              *vm, [callbackId, hotkeyContext](
                       havel::compiler::VM::VMExecutionContext &ctx) {
                // Execute in isolated context with @ context as first arg
                ctx.invokeCallback(callbackId, {hotkeyContext});
              });
        } else {
          // Fallback: direct execution (not thread-safe, for testing only)
          auto execCtx = vm->createExecutionContext();
          execCtx.invokeCallback(callbackId, {hotkeyContext});
        }
      });

  return BytecodeValue(success);
}

BytecodeValue
InputBridge::handleHotkeyTrigger(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
}

BytecodeValue
InputBridge::handleMapManagerMap(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
}

BytecodeValue InputBridge::handleMapManagerGetCurrentProfile(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(std::string("default"));
}

BytecodeValue
InputBridge::handleAltTabShow(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.show();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
InputBridge::handleAltTabHide(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.hide();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
InputBridge::handleAltTabToggle(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.toggle();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
InputBridge::handleAltTabNext(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.next();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
InputBridge::handleAltTabPrevious(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.previous();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
InputBridge::handleAltTabSelect(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.select();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
InputBridge::handleAltTabGetWindows(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  (void)args;
  havel::host::AltTabService altTab;
  auto windows = altTab.getWindows();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue::makeNull();
  }
  auto arr = vm->createHostArray();
  for (const auto &win : windows) {
    auto winObj = vm->createHostObject();
    vm->setHostObjectField(winObj, "title", BytecodeValue(win.title));
    vm->setHostObjectField(winObj, "className", BytecodeValue(win.className));
    vm->setHostObjectField(winObj, "processName",
                           BytecodeValue(win.processName));
    vm->setHostObjectField(winObj, "windowId",
                           BytecodeValue::makeInt(static_cast<int64_t>(win.windowId)));
    vm->setHostObjectField(winObj, "active", BytecodeValue(win.active));
    vm->pushHostArrayValue(arr, BytecodeValue(winObj));
  }
  return BytecodeValue::makeArrayId(arr.id);
}

// ============================================================================
// AsyncBridge Implementation
// ============================================================================

void AsyncBridge::install(PipelineOptions &options) {
  options.host_functions["sleep"] = [ctx = ctx_](const auto &args) {
    return handleSleep(args, ctx);
  };
  options.host_functions["time.now"] = [ctx = ctx_](const auto &args) {
    return handleTimeNow(args, ctx);
  };

  // Async module functions
  options.host_functions["async.run"] = [ctx = ctx_](const auto &args) {
    return handleAsyncRun(args, ctx);
  };
  options.host_functions["async.await"] = [ctx = ctx_](const auto &args) {
    return handleAsyncAwait(args, ctx);
  };
  options.host_functions["async.cancel"] = [ctx = ctx_](const auto &args) {
    return handleAsyncCancel(args, ctx);
  };
  options.host_functions["async.isRunning"] = [ctx = ctx_](const auto &args) {
    return handleAsyncIsRunning(args, ctx);
  };

  // Channel functions
  options.host_functions["async.channel"] = [ctx = ctx_](const auto &args) {
    return handleChannelCreate(args, ctx);
  };
  options.host_functions["async.send"] = [ctx = ctx_](const auto &args) {
    return handleChannelSend(args, ctx);
  };
  options.host_functions["async.receive"] = [ctx = ctx_](const auto &args) {
    return handleChannelReceive(args, ctx);
  };
  options.host_functions["async.tryReceive"] = [ctx = ctx_](const auto &args) {
    return handleChannelTryReceive(args, ctx);
  };
  options.host_functions["async.channel.close"] =
      [ctx = ctx_](const auto &args) { return handleChannelClose(args, ctx); };

  // Concurrency primitives
  options.host_functions["thread"] = [ctx = ctx_](const auto &args) {
    return handleThreadCreate(args, ctx);
  };
  options.host_functions["thread.send"] = [ctx = ctx_](const auto &args) {
    return handleThreadSend(args, ctx);
  };
  options.host_functions["thread.pause"] = [ctx = ctx_](const auto &args) {
    return handleThreadPause(args, ctx);
  };
  options.host_functions["thread.resume"] = [ctx = ctx_](const auto &args) {
    return handleThreadResume(args, ctx);
  };
  options.host_functions["thread.stop"] = [ctx = ctx_](const auto &args) {
    return handleThreadStop(args, ctx);
  };
  options.host_functions["thread.running"] = [ctx = ctx_](const auto &args) {
    return handleThreadRunning(args, ctx);
  };
  options.host_functions["interval"] = [ctx = ctx_](const auto &args) {
    return handleIntervalCreate(args, ctx);
  };
  options.host_functions["interval.pause"] = [ctx = ctx_](const auto &args) {
    return handleIntervalPause(args, ctx);
  };
  options.host_functions["interval.resume"] = [ctx = ctx_](const auto &args) {
    return handleIntervalResume(args, ctx);
  };
  options.host_functions["interval.stop"] = [ctx = ctx_](const auto &args) {
    return handleIntervalStop(args, ctx);
  };
  options.host_functions["timeout"] = [ctx = ctx_](const auto &args) {
    return handleTimeoutCreate(args, ctx);
  };
  options.host_functions["timeout.cancel"] = [ctx = ctx_](const auto &args) {
    return handleTimeoutCancel(args, ctx);
  };

  // Method-style dispatch via any.*
  options.host_functions["object.send"] = [ctx = ctx_](const auto &args) {
    return handleThreadSend(args, ctx);
  };
  options.host_functions["object.pause"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value())
      return BytecodeValue::makeBool(false);
    if (handle->first == "thread")
      return handleThreadPause(args, ctx);
    if (handle->first == "interval")
      return handleIntervalPause(args, ctx);
    return BytecodeValue::makeBool(false);
  };
  options.host_functions["object.resume"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value())
      return BytecodeValue::makeBool(false);
    if (handle->first == "thread")
      return handleThreadResume(args, ctx);
    if (handle->first == "interval")
      return handleIntervalResume(args, ctx);
    return BytecodeValue::makeBool(false);
  };
  options.host_functions["object.stop"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value())
      return BytecodeValue::makeBool(false);
    if (handle->first == "thread")
      return handleThreadStop(args, ctx);
    if (handle->first == "interval")
      return handleIntervalStop(args, ctx);
    return BytecodeValue::makeBool(false);
  };
  options.host_functions["object.cancel"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value() || handle->first != "timeout")
      return BytecodeValue::makeBool(false);
    return handleTimeoutCancel(args, ctx);
  };
  options.host_functions["object.running"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value())
      return BytecodeValue::makeBool(false);
    if (handle->first == "thread")
      return handleThreadRunning(args, ctx);
    if (handle->first == "interval") {
      std::lock_guard<std::mutex> lock(g_async_mutex);
      auto it = g_timers.find(handle->second);
      return BytecodeValue(it != g_timers.end() && it->second.running);
    }
    if (handle->first == "timeout") {
      std::lock_guard<std::mutex> lock(g_async_mutex);
      auto it = g_timers.find(handle->second);
      return BytecodeValue(it != g_timers.end() && it->second.running);
    }
    return BytecodeValue::makeBool(false);
  };
}

BytecodeValue AsyncBridge::handleSleep(const std::vector<BytecodeValue> &args,
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
  return BytecodeValue::makeNull();
}

BytecodeValue AsyncBridge::handleTimeNow(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();
  return BytecodeValue::makeInt(static_cast<int64_t>(timestamp));
}

// Async task handlers
BytecodeValue
AsyncBridge::handleAsyncRun(const std::vector<BytecodeValue> &args,
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
    BytecodeValue result = vm->invokeCallback(callback, {});
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

  return BytecodeValue(taskId);
}

BytecodeValue
AsyncBridge::handleAsyncAwait(const std::vector<BytecodeValue> &args,
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
    return BytecodeValue(completed);
  }

  return BytecodeValue::makeBool(false);
}

BytecodeValue
AsyncBridge::handleAsyncCancel(const std::vector<BytecodeValue> &args,
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
      return BytecodeValue::makeBool(true);
    }
  }

  if (ctx && ctx->asyncService) {
    bool cancelled = ctx->asyncService->cancel(taskId);
    return BytecodeValue(cancelled);
  }

  return BytecodeValue::makeBool(false);
}

BytecodeValue
AsyncBridge::handleAsyncIsRunning(const std::vector<BytecodeValue> &args,
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
      return BytecodeValue(it->second.running);
    }
  }

  if (ctx && ctx->asyncService) {
    bool running = ctx->asyncService->isRunning(taskId);
    return BytecodeValue(running);
  }

  return BytecodeValue::makeBool(false);
}

// Channel handlers
BytecodeValue
AsyncBridge::handleChannelCreate(const std::vector<BytecodeValue> &args,
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
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleChannelSend(const std::vector<BytecodeValue> &args,
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
      return BytecodeValue::makeBool(false);
    }
    it->second.queue.push_back(args[1]);
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->send(name, toString(args[1]));
  }
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleChannelReceive(const std::vector<BytecodeValue> &args,
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
      return BytecodeValue::makeNull();
    }
    BytecodeValue value = it->second.queue.front();
    it->second.queue.pop_front();
    return value;
  }
}

BytecodeValue
AsyncBridge::handleChannelTryReceive(const std::vector<BytecodeValue> &args,
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
      return BytecodeValue::makeNull();
    }
    BytecodeValue value = it->second.queue.front();
    it->second.queue.pop_front();
    return value;
  }
}

BytecodeValue
AsyncBridge::handleChannelClose(const std::vector<BytecodeValue> &args,
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
      return BytecodeValue::makeBool(false);
    }
    it->second.closed = true;
    it->second.queue.clear();
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->closeChannel(name);
  }
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleThreadCreate(const std::vector<BytecodeValue> &args,
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
  return BytecodeValue(makeHandleObject(vm, "thread", id));
}

BytecodeValue
AsyncBridge::handleThreadSend(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.size() < 2) {
    throw std::runtime_error("thread.send(handle, message) requires 2 args");
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue::makeBool(false);
  }

  ThreadRecord record;
  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_threads.find(handle->second);
    if (it == g_threads.end() || !it->second.running || it->second.paused) {
      return BytecodeValue::makeBool(false);
    }
    record = it->second;
  }

  {
    std::lock_guard<std::mutex> invoke_lock(g_vm_invoke_mutex);
    (void)vm->invokeCallback(record.callback, {args[1]});
  }
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleThreadPause(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end())
    return BytecodeValue::makeBool(false);
  it->second.paused = true;
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleThreadResume(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end())
    return BytecodeValue::makeBool(false);
  it->second.paused = false;
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleThreadStop(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end())
    return BytecodeValue::makeBool(false);
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  it->second.running = false;
  it->second.paused = false;
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleThreadRunning(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue::makeBool(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  return BytecodeValue(it != g_threads.end() && it->second.running);
}

BytecodeValue
AsyncBridge::handleIntervalCreate(const std::vector<BytecodeValue> &args,
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

  return BytecodeValue(makeHandleObject(vm, "interval", id));
}

BytecodeValue
AsyncBridge::handleIntervalPause(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval")
    return BytecodeValue::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return BytecodeValue::makeBool(false);
  it->second.paused = true;
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleIntervalResume(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval")
    return BytecodeValue::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return BytecodeValue::makeBool(false);
  it->second.paused = false;
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleIntervalStop(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval")
    return BytecodeValue::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return BytecodeValue::makeBool(false);
  it->second.running = false;
  if (ctx && ctx->asyncService && !it->second.task_id.empty()) {
    (void)ctx->asyncService->cancel(it->second.task_id);
  }
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  return BytecodeValue::makeBool(true);
}

BytecodeValue
AsyncBridge::handleTimeoutCreate(const std::vector<BytecodeValue> &args,
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

  return BytecodeValue(makeHandleObject(vm, "timeout", id));
}

BytecodeValue
AsyncBridge::handleTimeoutCancel(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "timeout")
    return BytecodeValue::makeBool(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end())
    return BytecodeValue::makeBool(false);
  it->second.running = false;
  if (ctx && ctx->asyncService && !it->second.task_id.empty()) {
    (void)ctx->asyncService->cancel(it->second.task_id);
  }
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  return BytecodeValue::makeBool(true);
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

BytecodeValue AutomationBridge::handleAutomationCreateAutoClicker(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoRunner(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoKeyPresser(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
}

BytecodeValue AutomationBridge::handleAutomationHasTask(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
}

BytecodeValue AutomationBridge::handleAutomationRemoveTask(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
}

BytecodeValue AutomationBridge::handleAutomationStopAll(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeBool(false);
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

BytecodeValue
BrowserBridge::handleBrowserConnect(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  (void)ctx;
  std::string browserUrl = "http://localhost:9222";
  if (!args.empty()) {
    if (false) { // TODO: string support
      browserUrl = *s;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.connect(browserUrl));
}

BytecodeValue BrowserBridge::handleBrowserConnectFirefox(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)ctx;
  int port = 2828;
  if (!args.empty()) {
    if (auto *v = (args[0].isInt() ? &args[0] : nullptr)) {
      port = static_cast<int>(*v);
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.connectFirefox(port));
}

BytecodeValue
BrowserBridge::handleBrowserDisconnect(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  browser.disconnect();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
BrowserBridge::handleBrowserIsConnected(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.isConnected());
}

BytecodeValue
BrowserBridge::handleBrowserOpen(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.open() requires a URL");
  }
  const std::string *url = nullptr;
  if (!url) {
    throw std::runtime_error("browser.open() requires a string URL");
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.open(*url));
}

BytecodeValue
BrowserBridge::handleBrowserNewTab(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  (void)ctx;
  std::string url;
  if (!args.empty()) {
    if (false) { // TODO: string support
      url = *s;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.newTab(url));
}

BytecodeValue
BrowserBridge::handleBrowserGoto(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.goto() requires a URL");
  }
  const std::string *url = nullptr;
  if (!url) {
    throw std::runtime_error("browser.goto() requires a string URL");
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.gotoUrl(*url));
}

BytecodeValue
BrowserBridge::handleBrowserBack(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.back());
}

BytecodeValue
BrowserBridge::handleBrowserForward(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.forward());
}

BytecodeValue
BrowserBridge::handleBrowserReload(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  (void)ctx;
  bool ignoreCache = false;
  if (!args.empty()) {
    if (auto *b = (args[0].isBool() ? &args[0] : nullptr)) {
      ignoreCache = *b;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.reload(ignoreCache));
}

BytecodeValue
BrowserBridge::handleBrowserListTabs(const std::vector<BytecodeValue> &args,
                                     const HostContext *ctx) {
  (void)args;
  havel::host::BrowserService browser;
  auto tabs = browser.listTabs();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue::makeNull();
  }
  auto arr = vm->createHostArray();
  for (const auto &tab : tabs) {
    auto tabObj = vm->createHostObject();
    vm->setHostObjectField(tabObj, "id",
                           BytecodeValue::makeInt(static_cast<int64_t>(tab.id)));
    vm->setHostObjectField(tabObj, "title", BytecodeValue(tab.title));
    vm->setHostObjectField(tabObj, "url", BytecodeValue(tab.url));
    vm->setHostObjectField(tabObj, "type", BytecodeValue(tab.type));
    vm->pushHostArrayValue(arr, BytecodeValue(tabObj));
  }
  return BytecodeValue::makeArrayId(arr.id);
}

// ============================================================================
// ToolsBridge Implementation
// ============================================================================

namespace {
havel::host::TextChunkerService g_textChunker;
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

BytecodeValue
ToolsBridge::handleTextChunkerSetText(const std::vector<BytecodeValue> &args,
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
  return BytecodeValue::makeBool(true);
}

BytecodeValue
ToolsBridge::handleTextChunkerGetText(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getText());
}

BytecodeValue ToolsBridge::handleTextChunkerSetChunkSize(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setChunkSize() requires a size");
  }
  int64_t size = 20000;
  if (args[0].isInt()) {
    size = args[0].asInt();
  }
  g_textChunker.setChunkSize(static_cast<size_t>(size));
  return BytecodeValue::makeBool(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGetTotalChunks(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeInt(static_cast<int64_t>(g_textChunker.getTotalChunks()));
}

BytecodeValue ToolsBridge::handleTextChunkerGetCurrentChunk(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue::makeInt(static_cast<int64_t>(g_textChunker.getCurrentChunk()));
}

BytecodeValue ToolsBridge::handleTextChunkerSetCurrentChunk(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
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
  return BytecodeValue::makeBool(true);
}

BytecodeValue
ToolsBridge::handleTextChunkerGetChunk(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.getChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (args[0].isInt()) {
    index = args[0].asInt();
  }
  return BytecodeValue(g_textChunker.getChunk(static_cast<int>(index)));
}

BytecodeValue ToolsBridge::handleTextChunkerGetNextChunk(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getNextChunk());
}

BytecodeValue ToolsBridge::handleTextChunkerGetPreviousChunk(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getPreviousChunk());
}

BytecodeValue
ToolsBridge::handleTextChunkerGoToFirst(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToFirst();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
ToolsBridge::handleTextChunkerGoToLast(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToLast();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
ToolsBridge::handleTextChunkerClear(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.clear();
  return BytecodeValue::makeBool(true);
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

BytecodeValue
MediaBridge::handleMediaPlayPause(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.playPause();
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue
MediaBridge::handleMediaPlay(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.play();
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue
MediaBridge::handleMediaPause(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.pause();
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue
MediaBridge::handleMediaStop(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.stop();
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue
MediaBridge::handleMediaNext(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.next();
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue
MediaBridge::handleMediaPrevious(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.previous();
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue
MediaBridge::handleMediaGetVolume(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    return BytecodeValue(media.getVolume());
  } catch (...) {
    return BytecodeValue(0.0);
  }
}

BytecodeValue
MediaBridge::handleMediaSetVolume(const std::vector<BytecodeValue> &args,
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
    havel::host::MediaService media;
    media.setVolume(volume);
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue
MediaBridge::handleMediaGetActivePlayer(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    return BytecodeValue(media.getActivePlayer());
  } catch (...) {
    return BytecodeValue::makeNull();
  }
}

BytecodeValue
MediaBridge::handleMediaSetActivePlayer(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("media.setActivePlayer() requires a player name");
  }
  const std::string *name = nullptr;
  if (!name) {
    throw std::runtime_error("media.setActivePlayer() requires a string");
  }
  try {
    havel::host::MediaService media;
    media.setActivePlayer(*name);
    return BytecodeValue::makeBool(true);
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue MediaBridge::handleMediaGetAvailablePlayers(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue::makeNull();
  }
  try {
    havel::host::MediaService media;
    auto players = media.getAvailablePlayers();
    auto arr = vm->createHostArray();
    for (const auto &player : players) {
      vm->pushHostArrayValue(arr, BytecodeValue(player));
    }
    return BytecodeValue::makeArrayId(arr.id);
  } catch (...) {
    return BytecodeValue::makeNull();
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

BytecodeValue
NetworkBridge::handleNetworkGet(const std::vector<BytecodeValue> &args,
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
        "http.get() requires a string URL, got type index " +
        std::to_string(args[0].index()));
  }

  int timeout_ms = 30000;
  if (args.size() > 1 && args[1].isInt()) {
    timeout_ms = static_cast<int>(args[1].asInt());
  }
  try {
    havel::host::NetworkService net;
    auto response = net.get(url, timeout_ms);
    if (response.success) {
      return BytecodeValue(response.body);
    } else {
      return BytecodeValue::makeNull();
    }
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string("http.get() failed: ") + e.what());
  }
}

BytecodeValue
NetworkBridge::handleNetworkPost(const std::vector<BytecodeValue> &args,
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
    havel::host::NetworkService net;
    auto response = net.post(*url, *data, content_type, timeout_ms);
    if (response.success) {
      return BytecodeValue(response.body);
    } else {
      return BytecodeValue::makeNull();
    }
  } catch (...) {
    return BytecodeValue::makeNull();
  }
}

BytecodeValue
NetworkBridge::handleNetworkIsOnline(const std::vector<BytecodeValue> &args,
                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    havel::host::NetworkService net;
    return BytecodeValue(net.isOnline());
  } catch (...) {
    return BytecodeValue::makeBool(false);
  }
}

BytecodeValue NetworkBridge::handleNetworkGetExternalIp(
    const std::vector<BytecodeValue> &args, const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    havel::host::NetworkService net;
    return BytecodeValue(net.getExternalIp());
  } catch (...) {
    return BytecodeValue::makeNull();
  }
}

BytecodeValue
NetworkBridge::handleNetworkDownload(const std::vector<BytecodeValue> &args,
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
    havel::host::NetworkService net;
    bool success = net.download(*url, *path, timeout_ms);
    return BytecodeValue(success);
  } catch (...) {
    return BytecodeValue::makeBool(false);
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

BytecodeValue
AudioBridge::handleGetVolume(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue(1.0); // Default volume
  }
  // Check for device-specific overload: getVolume(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = args[0].toString();
    return BytecodeValue(ctx->audioManager->getVolume(device));
  }
  // Default device
  return BytecodeValue(ctx->audioManager->getVolume());
}

BytecodeValue
AudioBridge::handleSetVolume(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
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
      return BytecodeValue(ctx->audioManager->setVolume(device, volume));
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
  return BytecodeValue(ctx->audioManager->setVolume(volume));
}

BytecodeValue AudioBridge::handleIsMuted(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
  }
  // Check for device-specific overload: isMuted(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = args[0].toString();
    return BytecodeValue(ctx->audioManager->isMuted(device));
  }
  // Default device
  return BytecodeValue(ctx->audioManager->isMuted());
}

BytecodeValue AudioBridge::handleSetMute(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
  }
  // Check for (device, muted) overload
  if (args.size() >= 2) {
    if (args[0].isStringValId() &&
        args[1].isBool()) {
      std::string device = args[0].toString();
      bool muted = args[1].asBool();
      return BytecodeValue(ctx->audioManager->setMute(device, muted));
    }
  }
  // Single argument: setMute(muted) for default device
  if (args.empty() || !args[0].isBool()) {
    throw std::runtime_error("audio.setMute() requires a boolean");
  }
  bool muted = args[0].asBool();
  return BytecodeValue(ctx->audioManager->setMute(muted));
}

BytecodeValue
AudioBridge::handleToggleMute(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
  }
  // Check for device-specific overload: toggleMute(device)
  if (!args.empty() && args[0].isStringValId()) {
    std::string device = args[0].toString();
    return BytecodeValue(ctx->audioManager->toggleMute(device));
  }
  // Default device
  return BytecodeValue(ctx->audioManager->toggleMute());
}

BytecodeValue
AudioBridge::handleGetDevices(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeNull();
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue::makeNull();
  }

  const auto &devices = ctx->audioManager->getDevices();
  auto arr = vm->createHostArray();

  for (const auto &dev : devices) {
    auto obj = vm->createHostObject();
    vm->setHostObjectField(obj, "name", BytecodeValue(dev.name));
    vm->setHostObjectField(obj, "description", BytecodeValue(dev.description));
    vm->setHostObjectField(obj, "index",
                           BytecodeValue::makeInt(static_cast<int64_t>(dev.index)));
    vm->setHostObjectField(obj, "isDefault", BytecodeValue(dev.isDefault));
    vm->setHostObjectField(obj, "isMuted", BytecodeValue(dev.isMuted));
    vm->setHostObjectField(obj, "volume", BytecodeValue(dev.volume));
    vm->setHostObjectField(obj, "channels",
                           BytecodeValue::makeInt(static_cast<int64_t>(dev.channels)));
    vm->pushHostArrayValue(arr, BytecodeValue::makeObjectId(obj.id));
  }

  return BytecodeValue::makeArrayId(arr.id);
}

BytecodeValue
AudioBridge::handleFindDeviceByIndex(const std::vector<BytecodeValue> &args,
                                     const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeNull();
  }
  if (args.empty() || !args[0].isInt()) {
    throw std::runtime_error("audio.findDeviceByIndex() requires an index");
  }
  uint32_t index = static_cast<uint32_t>(args[0].asInt());

  auto *dev = ctx->audioManager->findDeviceByIndex(index);
  if (!dev) {
    return BytecodeValue::makeNull();
  }

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue::makeNull();
  }

  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "name", BytecodeValue(dev->name));
  vm->setHostObjectField(obj, "description", BytecodeValue(dev->description));
  vm->setHostObjectField(obj, "index",
                         BytecodeValue::makeInt(static_cast<int64_t>(dev->index)));
  vm->setHostObjectField(obj, "isDefault", BytecodeValue(dev->isDefault));
  vm->setHostObjectField(obj, "isMuted", BytecodeValue(dev->isMuted));
  vm->setHostObjectField(obj, "volume", BytecodeValue(dev->volume));
  vm->setHostObjectField(obj, "channels",
                         BytecodeValue::makeInt(static_cast<int64_t>(dev->channels)));

  return BytecodeValue::makeObjectId(obj.id);
}

BytecodeValue
AudioBridge::handleFindDeviceByName(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeNull();
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("audio.findDeviceByName() requires a name string");
  }
  std::string name = args[0].toString();

  auto *dev = ctx->audioManager->findDeviceByName(name);
  if (!dev) {
    return BytecodeValue::makeNull();
  }

  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue::makeNull();
  }

  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "name", BytecodeValue(dev->name));
  vm->setHostObjectField(obj, "description", BytecodeValue(dev->description));
  vm->setHostObjectField(obj, "index",
                         BytecodeValue::makeInt(static_cast<int64_t>(dev->index)));
  vm->setHostObjectField(obj, "isDefault", BytecodeValue(dev->isDefault));
  vm->setHostObjectField(obj, "isMuted", BytecodeValue(dev->isMuted));
  vm->setHostObjectField(obj, "volume", BytecodeValue(dev->volume));
  vm->setHostObjectField(obj, "channels",
                         BytecodeValue::makeInt(static_cast<int64_t>(dev->channels)));

  return BytecodeValue::makeObjectId(obj.id);
}

BytecodeValue
AudioBridge::handleSetDefaultOutput(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("audio.setDefaultOutput() requires a device name");
  }
  std::string device = args[0].toString();
  return BytecodeValue(ctx->audioManager->setDefaultOutput(device));
}

BytecodeValue
AudioBridge::handleGetDefaultOutput(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue(ctx->audioManager->getDefaultOutput());
}

BytecodeValue
AudioBridge::handlePlayTestSound(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
  }
  return BytecodeValue(ctx->audioManager->playTestSound());
}

BytecodeValue
AudioBridge::handleIncreaseVolume(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
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
    return BytecodeValue(ctx->audioManager->increaseVolume(amount));
  } else {
    return BytecodeValue(ctx->audioManager->increaseVolume(device, amount));
  }
}

BytecodeValue
AudioBridge::handleDecreaseVolume(const std::vector<BytecodeValue> &args,
                                  const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue::makeBool(false);
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
    return BytecodeValue(ctx->audioManager->decreaseVolume(amount));
  } else {
    return BytecodeValue(ctx->audioManager->decreaseVolume(device, amount));
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

BytecodeValue MPVBridge::handleVolumeUp(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->VolumeUp();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
MPVBridge::handleVolumeDown(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->VolumeDown();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
MPVBridge::handleToggleMute(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->ToggleMute();
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handleStop(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->Stop();
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handleNext(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->Next();
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handlePrevious(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->Previous();
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handleSeek(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
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
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handleSubSeek(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
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
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handleAddSpeed(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty() || !args[0].isDouble()) {
    throw std::runtime_error("mpv.addSpeed() requires a number");
  }
  double delta = args[0].asDouble();
  ctx->mpvController->AddSpeed(delta);
  return BytecodeValue::makeBool(true);
}

BytecodeValue
MPVBridge::handleAddSubScale(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty() || !args[0].isDouble()) {
    throw std::runtime_error("mpv.addSubScale() requires a number");
  }
  double delta = args[0].asDouble();
  ctx->mpvController->AddSubScale(delta);
  return BytecodeValue::makeBool(true);
}

BytecodeValue
MPVBridge::handleAddSubDelay(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty() || !args[0].isDouble()) {
    throw std::runtime_error("mpv.addSubDelay() requires a number");
  }
  double delta = args[0].asDouble();
  ctx->mpvController->AddSubDelay(delta);
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handleCycle(const std::vector<BytecodeValue> &args,
                                     const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("mpv.cycle() requires a property name");
  }
  std::string property = args[0].toString();
  ctx->mpvController->Cycle(property);
  return BytecodeValue::makeBool(true);
}

BytecodeValue
MPVBridge::handleCopySubtitle(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue(ctx->mpvController->CopyCurrentSubtitle());
}

BytecodeValue MPVBridge::handleIPCSet(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("mpv.ipcSet() requires a socket path");
  }
  std::string path = args[0].toString();
  ctx->mpvController->SetIPC(path);
  return BytecodeValue::makeBool(true);
}

BytecodeValue
MPVBridge::handleIPCRestart(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->IPCRestart();
  return BytecodeValue::makeBool(true);
}

BytecodeValue
MPVBridge::handleScreenshot(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
  }
  ctx->mpvController->SendCommand({"screenshot"});
  return BytecodeValue::makeBool(true);
}

BytecodeValue MPVBridge::handleCmd(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  if (!ctx || !ctx->mpvController) {
    return BytecodeValue::makeBool(false);
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
  return BytecodeValue::makeBool(true);
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

BytecodeValue
DisplayBridge::handleGetMonitors(const std::vector<BytecodeValue> &args,
                                 const HostContext *ctx) {
  (void)args;
  if (!ctx)
    return BytecodeValue::makeNull();
  // Return array of monitor info objects
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return BytecodeValue::makeNull();

  auto monitors = havel::DisplayManager::GetMonitors();
  auto arr = vm->createHostArray();

  for (const auto &mon : monitors) {
    auto obj = vm->createHostObject();
    vm->setHostObjectField(obj, "name", BytecodeValue(mon.name));
    vm->setHostObjectField(obj, "x",
                           BytecodeValue::makeInt(static_cast<int64_t>(mon.x)));
    vm->setHostObjectField(obj, "y",
                           BytecodeValue::makeInt(static_cast<int64_t>(mon.y)));
    vm->setHostObjectField(obj, "width",
                           BytecodeValue::makeInt(static_cast<int64_t>(mon.width)));
    vm->setHostObjectField(obj, "height",
                           BytecodeValue::makeInt(static_cast<int64_t>(mon.height)));
    vm->setHostObjectField(obj, "isPrimary", BytecodeValue(mon.isPrimary));
    vm->pushHostArrayValue(arr, BytecodeValue::makeObjectId(obj.id));
  }

  return BytecodeValue::makeArrayId(arr.id);
}

BytecodeValue
DisplayBridge::handleGetPrimary(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  (void)args;
  if (!ctx)
    return BytecodeValue::makeNull();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm)
    return BytecodeValue::makeNull();

  auto mon = havel::DisplayManager::GetPrimaryMonitor();
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "name", BytecodeValue(mon.name));
  vm->setHostObjectField(obj, "x", BytecodeValue::makeInt(static_cast<int64_t>(mon.x)));
  vm->setHostObjectField(obj, "y", BytecodeValue::makeInt(static_cast<int64_t>(mon.y)));
  vm->setHostObjectField(obj, "width",
                         BytecodeValue::makeInt(static_cast<int64_t>(mon.width)));
  vm->setHostObjectField(obj, "height",
                         BytecodeValue::makeInt(static_cast<int64_t>(mon.height)));
  vm->setHostObjectField(obj, "isPrimary", BytecodeValue(mon.isPrimary));

  return BytecodeValue::makeObjectId(obj.id);
}

BytecodeValue
DisplayBridge::handleGetCount(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto monitors = havel::DisplayManager::GetMonitors();
  return BytecodeValue::makeInt(static_cast<int64_t>(monitors.size()));
}

BytecodeValue
DisplayBridge::handleGetMonitorsArea(const std::vector<BytecodeValue> &args,
                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto monitors = havel::DisplayManager::GetMonitors();

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
  vm->setHostObjectField(obj, "width", BytecodeValue(totalWidth));
  vm->setHostObjectField(obj, "height", BytecodeValue(totalHeight));
  vm->setHostObjectField(obj, "totalArea",
                         BytecodeValue(totalWidth * totalHeight));
  vm->setHostObjectField(obj, "x", BytecodeValue(minX == INT64_MAX ? 0 : minX));
  vm->setHostObjectField(obj, "y", BytecodeValue(minY == INT64_MAX ? 0 : minY));

  return BytecodeValue::makeObjectId(obj.id);
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

BytecodeValue ConfigBridge::handleGet(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("config.get() requires key and default value");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("config.get() key must be a string");
  }

  std::string key = args[0].toString();
  auto &config = havel::Configs::Get();

  // Return value based on default type
  if (args[1].isStringValId()) {
    std::string def = args[1].toString();
    return BytecodeValue(config.Get(key, def));
  } else if (args[1].isInt()) {
    int64_t def = args[1].asInt();
    return BytecodeValue(config.Get(key, def));
  } else if (args[1].isDouble()) {
    double def = args[1].asDouble();
    return BytecodeValue(config.Get(key, def));
  } else if (args[1].isBool()) {
    bool def = args[1].asBool();
    return BytecodeValue(config.Get(key, def));
  }

  return BytecodeValue::makeNull();
}

BytecodeValue ConfigBridge::handleSet(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("config.set() requires key and value");
  }

  if (!args[0].isStringValId()) {
    throw std::runtime_error("config.set() key must be a string");
  }

  std::string key = args[0].toString();
  auto &config = havel::Configs::Get();

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

  return BytecodeValue::makeBool(true);
}

BytecodeValue ConfigBridge::handleSave(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto &config = havel::Configs::Get();
  config.Save();
  return BytecodeValue::makeBool(true);
}

// ============================================================================
// ModeBridge Implementation
// ============================================================================

void ModeBridge::install(PipelineOptions &options) {
  // mode() - returns current mode name (for comparisons like mode == "gaming")
  options.host_functions["mode"] = [ctx = ctx_](const auto &args) {
    (void)args;
    if (!ctx || !ctx->modeManager) {
      return BytecodeValue::makeNull();
    }
    return BytecodeValue(ctx->modeManager->getCurrentMode());
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

BytecodeValue ModeBridge::handleRegister(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  // Args: name, priority, condition, enter, exit, onEnterFromMode, onEnterFrom,
  // onExitToMode, onExitTo
  if (args.size() < 9) {
    return BytecodeValue::makeBool(false);
  }

  if (!ctx || !ctx->modeManager || !ctx->vm) {
    return BytecodeValue::makeBool(false);
  }

  // Get mode name
  std::string modeName;
  if (args[0].isStringValId()) {
    modeName = args[0].toString();
  } else {
    return BytecodeValue::makeBool(false);
  }

  // Get priority
  int priority = 0;
  if (args[1].isInt()) {
    priority = static_cast<int>(args[1].asInt());
  }

  auto *vm = static_cast<VM *>(ctx->vm);

  // Helper to register a callback from BytecodeValue
  auto registerCallbackIfValid = [&](const BytecodeValue &val) -> CallbackId {
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
  havel::ModeManager::ModeDefinition mode;
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
  return BytecodeValue::makeBool(true);
}

BytecodeValue
ModeBridge::handleGetCurrent(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->modeManager) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue(ctx->modeManager->getCurrentMode());
}

BytecodeValue ModeBridge::handleSet(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->modeManager) {
    return BytecodeValue::makeBool(false);
  }
  if (args.empty() || !args[0].isStringValId()) {
    throw std::runtime_error("mode.set() requires a mode name string");
  }
  std::string modeName = args[0].toString();
  ctx->modeManager->setMode(modeName);
  return BytecodeValue::makeBool(true);
}

BytecodeValue
ModeBridge::handleGetPrevious(const std::vector<BytecodeValue> &args,
                              const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->modeManager) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue(ctx->modeManager->getPreviousMode());
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

BytecodeValue TimerBridge::handleAfter(const std::vector<BytecodeValue> &args,
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

  return BytecodeValue::makeNull();
}

BytecodeValue TimerBridge::handleEvery(const std::vector<BytecodeValue> &args,
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
    return BytecodeValue();
  };
  options.host_functions["app.restart"] = [ctx = ctx_](const auto &args) {
    std::exit(42);
    return BytecodeValue();
  };
}

BytecodeValue
AppBridge::handleAppGetName(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getAppName());
}

BytecodeValue
AppBridge::handleAppGetVersion(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getAppVersion());
}

BytecodeValue AppBridge::handleAppGetOS(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getOS());
}

BytecodeValue
AppBridge::handleAppGetHostname(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getHostname());
}

BytecodeValue
AppBridge::handleAppGetUsername(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getUsername());
}

BytecodeValue
AppBridge::handleAppGetHomeDir(const std::vector<BytecodeValue> &args,
                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getHomeDir());
}

BytecodeValue
AppBridge::handleAppGetCpuCores(const std::vector<BytecodeValue> &args,
                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue::makeInt(static_cast<int64_t>(app.getCpuCores()));
}

BytecodeValue AppBridge::handleAppGetEnv(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.getEnv() requires a variable name");
  }
  const std::string *name = nullptr;
  if (!name) {
    throw std::runtime_error("app.getEnv() requires a string");
  }
  havel::host::AppService app;
  return BytecodeValue(app.getEnv(*name));
}

BytecodeValue AppBridge::handleAppSetEnv(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2) {
    throw std::runtime_error("app.setEnv() requires name and value");
  }
  const std::string *name = nullptr;
  const std::string *value = nullptr;
  if (!name || !value) {
    throw std::runtime_error("app.setEnv() requires string arguments");
  }
  havel::host::AppService app;
  return BytecodeValue(app.setEnv(*name, *value));
}

BytecodeValue
AppBridge::handleAppOpenUrl(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.openUrl() requires a URL");
  }
  const std::string *url = nullptr;
  if (!url) {
    throw std::runtime_error("app.openUrl() requires a string URL");
  }
  havel::host::AppService app;
  return BytecodeValue(app.openUrl(*url));
}

// Active window namespace implementations
BytecodeValue UIBridge::handleActiveGet(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  return handleWindowGetActive(args, ctx);
}

BytecodeValue
UIBridge::handleActiveTitle(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeNull();
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeNull();
  }
  return BytecodeValue(info.title);
}

BytecodeValue
UIBridge::handleActiveClass(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(
      havel::host::WindowService::getActiveWindowClassStatic());
}

BytecodeValue UIBridge::handleActiveExe(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(
      havel::host::WindowService::getActiveWindowProcessStatic());
}

BytecodeValue UIBridge::handleActivePid(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeInt(static_cast<int64_t>(0));
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeInt(static_cast<int64_t>(0));
  }
  return BytecodeValue::makeInt(static_cast<int64_t>(info.pid));
}

BytecodeValue
UIBridge::handleActiveClose(const std::vector<BytecodeValue> &args,
                            const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeBool(false);
  }
  winService.closeWindow(info.id);
  return BytecodeValue::makeBool(true);
}

BytecodeValue UIBridge::handleActiveMin(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeBool(false);
  }
  winService.minimizeWindow(info.id);
  return BytecodeValue::makeBool(true);
}

BytecodeValue UIBridge::handleActiveMax(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeBool(false);
  }
  winService.maximizeWindow(info.id);
  return BytecodeValue::makeBool(true);
}

BytecodeValue UIBridge::handleActiveHide(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeBool(false);
  }
  winService.hideWindow(info.id);
  return BytecodeValue::makeBool(true);
}

BytecodeValue UIBridge::handleActiveShow(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeBool(false);
  }
  winService.showWindow(info.id);
  return BytecodeValue::makeBool(true);
}

BytecodeValue UIBridge::handleActiveMove(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  int64_t x = 0, y = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    x = *v;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    y = *v;

  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeBool(false);
  }
  winService.moveWindow(info.id, static_cast<int>(x), static_cast<int>(y));
  return BytecodeValue::makeBool(true);
}

BytecodeValue
UIBridge::handleActiveResize(const std::vector<BytecodeValue> &args,
                             const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return BytecodeValue::makeBool(false);
  }
  int64_t w = 0, h = 0;
  if (auto *v = (args[0].isInt() ? &args[0] : nullptr))
    w = *v;
  if (auto *v = (args[1].isInt() ? &args[1] : nullptr))
    h = *v;

  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue::makeBool(false);
  }
  winService.resizeWindow(info.id, static_cast<int>(w), static_cast<int>(h));
  return BytecodeValue::makeBool(true);
}

} // namespace havel::compiler
