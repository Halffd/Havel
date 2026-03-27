/*
 * ModularHostBridges.cpp - Modular bridge component implementations
 */
#include "ModularHostBridges.hpp"

#include "gui/ClipboardManager.hpp"
#include "host/async/AsyncService.hpp"
#include "host/audio/AudioService.hpp"
#include "host/automation/AutomationService.hpp"
#include "host/browser/BrowserService.hpp"
#include "host/brightness/BrightnessService.hpp"
#include "host/chunker/TextChunkerService.hpp"
#include "host/filesystem/FileSystemService.hpp"
#include "host/hotkey/HotkeyService.hpp"
#include "host/io/IOService.hpp"
#include "host/io/MapManagerService.hpp"
#include "host/process/ProcessService.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include "host/window/WindowService.hpp"
#include "host/window/AltTabService.hpp"
#include "host/media/MediaService.hpp"
#include "host/network/NetworkService.hpp"
#include "host/app/AppService.hpp"
#include "window/WindowManager.hpp"
#include "core/DisplayManager.hpp"
#include "core/ConfigManager.hpp"
#include "core/ModeManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/IO.hpp"
#include "media/AudioManager.hpp"

#include <QClipboard>
#include <fstream>
#include <sstream>
#include <chrono>
#include <atomic>
#include <deque>
#include <mutex>
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
  vm->setHostObjectField(obj, "__kind", BytecodeValue(kind));
  vm->setHostObjectField(obj, "__id", BytecodeValue(id));
  return obj;
}

std::optional<std::pair<std::string, std::string>>
extractHandle(const std::vector<BytecodeValue> &args, VM *vm, size_t index = 0) {
  if (!vm || index >= args.size() || !std::holds_alternative<ObjectRef>(args[index])) {
    return std::nullopt;
  }
  ObjectRef obj = std::get<ObjectRef>(args[index]);
  BytecodeValue kind = vm->getHostObjectField(obj, "__kind");
  BytecodeValue id = vm->getHostObjectField(obj, "__id");
  if (!std::holds_alternative<std::string>(kind) ||
      !std::holds_alternative<std::string>(id)) {
    return std::nullopt;
  }
  return std::make_pair(std::get<std::string>(kind), std::get<std::string>(id));
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
  options.host_functions["io.mouseMove"] = [ctx = ctx_](const auto &args) {
    return handleMouseMove(args, ctx);
  };
  options.host_functions["io.mouseClick"] = [ctx = ctx_](const auto &args) {
    return handleMouseClick(args, ctx);
  };
  options.host_functions["io.getMousePosition"] = [ctx = ctx_](const auto &args) {
    return handleGetMousePosition(args, ctx);
  };
}

BytecodeValue IOBridge::handleSend(const std::vector<BytecodeValue> &args,
                                   const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  if (auto *keys = std::get_if<std::string>(&args[0])) {
    return BytecodeValue(ioService.sendKeys(*keys));
  }
  return BytecodeValue(false);
}

BytecodeValue IOBridge::handleSendKey(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  if (auto *key = std::get_if<std::string>(&args[0])) {
    return BytecodeValue(ioService.sendKey(*key));
  }
  return BytecodeValue(false);
}

BytecodeValue IOBridge::handleMouseMove(const std::vector<BytecodeValue> &args,
                                        const HostContext *ctx) {
  if (args.size() < 2 || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  int64_t dx = 0, dy = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    dx = *v;
  else if (auto *v = std::get_if<double>(&args[0]))
    dx = static_cast<int64_t>(*v);
  if (auto *v = std::get_if<int64_t>(&args[1]))
    dy = *v;
  else if (auto *v = std::get_if<double>(&args[1]))
    dy = static_cast<int64_t>(*v);
  return BytecodeValue(ioService.mouseMove(static_cast<int>(dx), static_cast<int>(dy)));
}

BytecodeValue IOBridge::handleMouseClick(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty() || !ctx->io) {
    return BytecodeValue(false);
  }
  havel::host::IOService ioService(ctx->io);
  int64_t button = 1;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    button = *v;
  else if (auto *v = std::get_if<double>(&args[0]))
    button = static_cast<int64_t>(*v);
  return BytecodeValue(ioService.mouseClick(static_cast<int>(button)));
}

BytecodeValue IOBridge::handleGetMousePosition(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  if (!ctx->io) {
    return BytecodeValue(nullptr);
  }
  // TODO: Implement getMousePosition in IOService
  return BytecodeValue(nullptr);
}

// ============================================================================
// SystemBridge Implementation
// ============================================================================

void SystemBridge::install(PipelineOptions &options) {
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
}

BytecodeValue SystemBridge::handleFileRead(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("readFile() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("readFile() requires a string path");
  }
  havel::host::FileSystemService fs;
  std::string content = fs.readFile(*path);
  if (content.empty()) {
    return BytecodeValue(nullptr);
  }
  return BytecodeValue(content);
}

BytecodeValue SystemBridge::handleFileWrite(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("writeFile() requires path and content");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  const std::string *content = std::get_if<std::string>(&args[1]);
  if (!path || !content) {
    throw std::runtime_error("writeFile() requires string arguments");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue(fs.writeFile(*path, *content));
}

BytecodeValue SystemBridge::handleFileExists(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileExists() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("fileExists() requires a string path");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue(fs.exists(*path));
}

BytecodeValue SystemBridge::handleFileSize(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("fileSize() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("fileSize() requires a string path");
  }
  havel::host::FileSystemService fs;
  if (!fs.exists(*path)) {
    return BytecodeValue(static_cast<int64_t>(0));
  }
  return BytecodeValue(fs.getFileSize(*path));
}

BytecodeValue SystemBridge::handleFileDelete(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("deleteFile() requires a file path");
  }
  const std::string *path = std::get_if<std::string>(&args[0]);
  if (!path) {
    throw std::runtime_error("deleteFile() requires a string path");
  }
  havel::host::FileSystemService fs;
  return BytecodeValue(fs.deleteFile(*path));
}

BytecodeValue SystemBridge::handleProcessExecute(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("execute() requires a command");
  }
  const std::string *command = std::get_if<std::string>(&args[0]);
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
  return BytecodeValue(output.str());
}

BytecodeValue SystemBridge::handleProcessGetPid(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(havel::host::ProcessService::getCurrentPid()));
}

BytecodeValue SystemBridge::handleProcessGetPpid(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(havel::host::ProcessService::getParentPid()));
}

BytecodeValue SystemBridge::handleProcessFind(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("process.find() requires a process name");
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("process.find() requires a string argument");
  }
  auto pids = havel::host::ProcessService::findProcesses(*name);
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  auto arr = vm->createHostArray();
  for (int32_t pid : pids) {
    vm->pushHostArrayValue(arr, BytecodeValue(static_cast<int64_t>(pid)));
  }
  return BytecodeValue(arr);
}

BytecodeValue SystemBridge::handleProcessExists(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("process.exists() requires a process name or PID");
  }
  if (std::holds_alternative<int64_t>(args[0])) {
    int32_t pid = static_cast<int32_t>(std::get<int64_t>(args[0]));
    return BytecodeValue(havel::host::ProcessService::isProcessAlive(pid));
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("process.exists() requires a string or number");
  }
  return BytecodeValue(havel::host::ProcessService::processExists(*name));
}

BytecodeValue SystemBridge::handleProcessKill(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("process.kill() requires PID and signal");
  }
  int32_t pid = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    pid = static_cast<int32_t>(std::get<int64_t>(args[0]));
  } else {
    throw std::runtime_error("process.kill() requires a number PID");
  }
  const std::string *sig = std::get_if<std::string>(&args[1]);
  if (!sig) {
    throw std::runtime_error("process.kill() requires a string signal");
  }
  int signal_num = 15; // Default SIGTERM
  if (*sig == "SIGKILL" || *sig == "kill") signal_num = 9;
  else if (*sig == "SIGTERM" || *sig == "term") signal_num = 15;
  else if (*sig == "SIGHUP" || *sig == "hangup") signal_num = 1;
  else if (*sig == "SIGINT" || *sig == "int") signal_num = 2;
  return BytecodeValue(havel::host::ProcessService::sendSignal(pid, signal_num));
}

BytecodeValue SystemBridge::handleProcessNice(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("process.nice() requires PID and nice value");
  }
  int32_t pid = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    pid = static_cast<int32_t>(std::get<int64_t>(args[0]));
  } else {
    throw std::runtime_error("process.nice() requires a number PID");
  }
  int64_t nice = 0;
  if (std::holds_alternative<int64_t>(args[1])) {
    nice = std::get<int64_t>(args[1]);
  } else {
    throw std::runtime_error("process.nice() requires a number nice value");
  }
  // Nice range: -20 (highest priority) to 19 (lowest)
  if (nice < -20 || nice > 19) {
    throw std::runtime_error("process.nice() nice value must be between -20 and 19");
  }
  return BytecodeValue(havel::host::ProcessService::setNice(pid, static_cast<int>(nice)));
}

// ============================================================================
// UIBridge Implementation
// ============================================================================

void UIBridge::install(PipelineOptions &options) {
  options.host_functions["window.active"] = [ctx = ctx_](const auto &args) {
    return handleWindowGetActive(args, ctx);
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
  options.host_functions["window.moveToMonitor"] = [ctx = ctx_](const auto &args) {
    return handleWindowMoveToMonitor(args, ctx);
  };
  options.host_functions["window.moveToNextMonitor"] = [ctx = ctx_](const auto &args) {
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
  options.host_functions["screenshot.monitor"] = [ctx = ctx_](const auto &args) {
    return handleScreenshotMonitor(args, ctx);
  };
}

BytecodeValue UIBridge::handleWindowGetActive(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue(nullptr);
  }
  havel::host::WindowService winService(ctx->windowManager);
  auto info = winService.getActiveWindowInfo();
  if (!info.valid) {
    return BytecodeValue(nullptr);
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "id", BytecodeValue(static_cast<int64_t>(info.id)));
  vm->setHostObjectField(obj, "title", BytecodeValue(info.title));
  vm->setHostObjectField(obj, "class", BytecodeValue(info.windowClass));
  return BytecodeValue(obj);
}

BytecodeValue UIBridge::handleWindowClose(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.closeWindow(wid));
}

BytecodeValue UIBridge::handleWindowResize(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  int w = 0, h = 0;
  if (auto *v = std::get_if<int64_t>(&args[1]))
    w = static_cast<int>(*v);
  else if (auto *v = std::get_if<double>(&args[1]))
    w = static_cast<int>(*v);
  if (auto *v = std::get_if<int64_t>(&args[2]))
    h = static_cast<int>(*v);
  else if (auto *v = std::get_if<double>(&args[2]))
    h = static_cast<int>(*v);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.resizeWindow(wid, w, h));
}

BytecodeValue UIBridge::handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  if (args.size() < 2 || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  int monitor = 0;
  if (auto *v = std::get_if<int64_t>(&args[1]))
    monitor = static_cast<int>(*v);
  else if (auto *v = std::get_if<double>(&args[1]))
    monitor = static_cast<int>(*v);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindowToMonitor(wid, monitor));
}

BytecodeValue UIBridge::handleWindowMoveToNextMonitor(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  if (!ctx->windowManager) {
    return BytecodeValue(false);
  }
  // TODO: Implement move to next monitor
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindowToMonitor(0, 0));
}

BytecodeValue UIBridge::handleWindowMove(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.size() < 3 || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  int x = 0, y = 0;
  if (auto *v = std::get_if<int64_t>(&args[1]))
    x = static_cast<int>(*v);
  if (auto *v = std::get_if<int64_t>(&args[2]))
    y = static_cast<int>(*v);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.moveWindow(wid, x, y));
}

BytecodeValue UIBridge::handleWindowFocus(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.focusWindow(wid));
}

BytecodeValue UIBridge::handleWindowMinimize(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.minimizeWindow(wid));
}

BytecodeValue UIBridge::handleWindowMaximize(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  if (args.empty() || !ctx->windowManager) {
    return BytecodeValue(false);
  }
  uint64_t wid = 0;
  if (auto *v = std::get_if<int64_t>(&args[0]))
    wid = static_cast<uint64_t>(*v);
  else
    return BytecodeValue(false);
  havel::host::WindowService winService(ctx->windowManager);
  return BytecodeValue(winService.maximizeWindow(wid));
}

BytecodeValue UIBridge::handleClipboardGet(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return BytecodeValue(nullptr);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(nullptr);
  }
  return BytecodeValue(clipboard->text().toStdString());
}

BytecodeValue UIBridge::handleClipboardSet(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  if (args.empty() || !ctx->clipboardManager) {
    return BytecodeValue(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(false);
  }
  if (auto *val = std::get_if<std::string>(&args[0])) {
    clipboard->setText(QString::fromStdString(*val));
    return BytecodeValue(true);
  }
  return BytecodeValue(false);
}

BytecodeValue UIBridge::handleClipboardClear(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)args;
  if (!ctx->clipboardManager) {
    return BytecodeValue(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return BytecodeValue(false);
  }
  clipboard->clear();
  return BytecodeValue(true);
}

BytecodeValue UIBridge::handleScreenshotFull(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::ScreenshotService service;
  auto data = service.captureFullDesktop();
  (void)data;
  return BytecodeValue(nullptr);
}

BytecodeValue UIBridge::handleScreenshotMonitor(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)ctx;
  int monitor = 0;
  if (!args.empty()) {
    if (auto *v = std::get_if<int64_t>(&args[0]))
      monitor = static_cast<int>(*v);
  }
  havel::host::ScreenshotService service;
  auto data = service.captureMonitor(monitor);
  (void)data;
  return BytecodeValue(nullptr);
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
  options.host_functions["mapmanager.getCurrentProfile"] = [ctx = ctx_](const auto &args) {
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

BytecodeValue InputBridge::handleHotkeyRegister(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  // Args: [hotkey_string, callback_closure]
  if (args.size() < 2) {
    return BytecodeValue(false);
  }
  
  if (!ctx || !ctx->hotkeyManager || !ctx->vm || !ctx->io) {
    return BytecodeValue(false);
  }
  
  // Get hotkey string
  std::string hotkeyStr;
  if (std::holds_alternative<std::string>(args[0])) {
    hotkeyStr = std::get<std::string>(args[0]);
  } else {
    return BytecodeValue(false);
  }
  
  // Register the closure as a callback
  CallbackId callbackId = ctx->vm->registerCallback(args[1]);
  
  // Get HotkeyExecutor for thread-safe execution
  auto *executor = ctx->io->GetHotkeyExecutor();
  
  // Register hotkey with callback
  bool success = ctx->hotkeyManager->AddHotkey(hotkeyStr, [ctx, callbackId, executor]() {
    // Submit callback to HotkeyExecutor for thread-safe execution
    if (executor) {
      executor->submit([ctx, callbackId]() {
        if (ctx && ctx->vm) {
          ctx->vm->invokeCallback(callbackId, {});
        }
      });
    } else {
      // Fallback: invoke directly (not thread-safe)
      if (ctx && ctx->vm) {
        ctx->vm->invokeCallback(callbackId, {});
      }
    }
  });
  
  return BytecodeValue(success);
}

BytecodeValue InputBridge::handleHotkeyTrigger(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue InputBridge::handleMapManagerMap(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue InputBridge::handleMapManagerGetCurrentProfile(const std::vector<BytecodeValue> &args,
                                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(std::string("default"));
}

BytecodeValue InputBridge::handleAltTabShow(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.show();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabHide(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.hide();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabToggle(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.toggle();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabNext(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.next();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabPrevious(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.previous();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabSelect(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AltTabService altTab;
  altTab.select();
  return BytecodeValue(true);
}

BytecodeValue InputBridge::handleAltTabGetWindows(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)args;
  havel::host::AltTabService altTab;
  auto windows = altTab.getWindows();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  auto arr = vm->createHostArray();
  for (const auto &win : windows) {
    auto winObj = vm->createHostObject();
    vm->setHostObjectField(winObj, "title", BytecodeValue(win.title));
    vm->setHostObjectField(winObj, "className", BytecodeValue(win.className));
    vm->setHostObjectField(winObj, "processName", BytecodeValue(win.processName));
    vm->setHostObjectField(winObj, "windowId", BytecodeValue(static_cast<int64_t>(win.windowId)));
    vm->setHostObjectField(winObj, "active", BytecodeValue(win.active));
    vm->pushHostArrayValue(arr, BytecodeValue(winObj));
  }
  return BytecodeValue(arr);
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
  options.host_functions["async.channel.close"] = [ctx = ctx_](const auto &args) {
    return handleChannelClose(args, ctx);
  };

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
    if (!handle.has_value()) return BytecodeValue(false);
    if (handle->first == "thread") return handleThreadPause(args, ctx);
    if (handle->first == "interval") return handleIntervalPause(args, ctx);
    return BytecodeValue(false);
  };
  options.host_functions["object.resume"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value()) return BytecodeValue(false);
    if (handle->first == "thread") return handleThreadResume(args, ctx);
    if (handle->first == "interval") return handleIntervalResume(args, ctx);
    return BytecodeValue(false);
  };
  options.host_functions["object.stop"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value()) return BytecodeValue(false);
    if (handle->first == "thread") return handleThreadStop(args, ctx);
    if (handle->first == "interval") return handleIntervalStop(args, ctx);
    return BytecodeValue(false);
  };
  options.host_functions["object.cancel"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value() || handle->first != "timeout") return BytecodeValue(false);
    return handleTimeoutCancel(args, ctx);
  };
  options.host_functions["object.running"] = [ctx = ctx_](const auto &args) {
    auto handle = extractHandle(args, static_cast<VM *>(ctx->vm));
    if (!handle.has_value()) return BytecodeValue(false);
    if (handle->first == "thread") return handleThreadRunning(args, ctx);
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
    return BytecodeValue(false);
  };
}

BytecodeValue AsyncBridge::handleSleep(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("sleep() requires milliseconds");
  }
  int64_t ms = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    ms = std::get<int64_t>(args[0]);
  } else if (std::holds_alternative<double>(args[0])) {
    ms = static_cast<int64_t>(std::get<double>(args[0]));
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
  return BytecodeValue(nullptr);
}

BytecodeValue AsyncBridge::handleTimeNow(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return BytecodeValue(static_cast<int64_t>(timestamp));
}

// Async task handlers
BytecodeValue AsyncBridge::handleAsyncRun(const std::vector<BytecodeValue> &args,
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

BytecodeValue AsyncBridge::handleAsyncAwait(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.await() requires a task ID");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.await() requires a string task ID");
  }
  
  std::string taskId = std::get<std::string>(args[0]);
  
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
  
  return BytecodeValue(false);
}

BytecodeValue AsyncBridge::handleAsyncCancel(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.cancel() requires a task ID");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.cancel() requires a string task ID");
  }
  
  std::string taskId = std::get<std::string>(args[0]);

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_tasks.find(taskId);
    if (it != g_async_tasks.end()) {
      it->second.cancelled = true;
      it->second.running = false;
      return BytecodeValue(true);
    }
  }

  if (ctx && ctx->asyncService) {
    bool cancelled = ctx->asyncService->cancel(taskId);
    return BytecodeValue(cancelled);
  }
  
  return BytecodeValue(false);
}

BytecodeValue AsyncBridge::handleAsyncIsRunning(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.isRunning() requires a task ID");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.isRunning() requires a string task ID");
  }
  
  std::string taskId = std::get<std::string>(args[0]);

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
  
  return BytecodeValue(false);
}

// Channel handlers
BytecodeValue AsyncBridge::handleChannelCreate(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.channel() requires a channel name");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.channel() requires a string name");
  }
  
  std::string name = std::get<std::string>(args[0]);

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto &channel = g_async_channels[name];
    channel.closed = false;
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->createChannel(name);
  }
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleChannelSend(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  if (args.size() < 2) {
    throw std::runtime_error("async.send() requires channel name and value");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.send() requires a string channel name");
  }
  
  std::string name = std::get<std::string>(args[0]);

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end() || it->second.closed) {
      return BytecodeValue(false);
    }
    it->second.queue.push_back(args[1]);
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->send(name, toString(args[1]));
  }
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleChannelReceive(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.receive() requires a channel name");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.receive() requires a string channel name");
  }
  
  std::string name = std::get<std::string>(args[0]);

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end() || it->second.queue.empty()) {
      return BytecodeValue(nullptr);
    }
    BytecodeValue value = it->second.queue.front();
    it->second.queue.pop_front();
    return value;
  }
}

BytecodeValue AsyncBridge::handleChannelTryReceive(const std::vector<BytecodeValue> &args,
                                                   const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.tryReceive() requires a channel name");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.tryReceive() requires a string channel name");
  }
  
  std::string name = std::get<std::string>(args[0]);

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end() || it->second.queue.empty()) {
      return BytecodeValue(nullptr);
    }
    BytecodeValue value = it->second.queue.front();
    it->second.queue.pop_front();
    return value;
  }
}

BytecodeValue AsyncBridge::handleChannelClose(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("async.channel.close() requires a channel name");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("async.channel.close() requires a string channel name");
  }
  
  std::string name = std::get<std::string>(args[0]);

  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_async_channels.find(name);
    if (it == g_async_channels.end()) {
      return BytecodeValue(false);
    }
    it->second.closed = true;
    it->second.queue.clear();
  }

  if (ctx && ctx->asyncService) {
    (void)ctx->asyncService->closeChannel(name);
  }
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleThreadCreate(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.empty()) {
    throw std::runtime_error("thread(fn) requires VM context and callback");
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  CallbackId callback = vm->registerCallback(args[0]);
  const std::string id = allocateTaskId();
  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_threads[id] = ThreadRecord{.callback = callback, .running = true, .paused = false};
  }
  return BytecodeValue(makeHandleObject(vm, "thread", id));
}

BytecodeValue AsyncBridge::handleThreadSend(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.size() < 2) {
    throw std::runtime_error("thread.send(handle, message) requires 2 args");
  }
  auto *vm = static_cast<VM *>(ctx->vm);
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue(false);
  }

  ThreadRecord record;
  {
    std::lock_guard<std::mutex> lock(g_async_mutex);
    auto it = g_threads.find(handle->second);
    if (it == g_threads.end() || !it->second.running || it->second.paused) {
      return BytecodeValue(false);
    }
    record = it->second;
  }

  {
    std::lock_guard<std::mutex> invoke_lock(g_vm_invoke_mutex);
    (void)vm->invokeCallback(record.callback, {args[1]});
  }
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleThreadPause(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end()) return BytecodeValue(false);
  it->second.paused = true;
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleThreadResume(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end()) return BytecodeValue(false);
  it->second.paused = false;
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleThreadStop(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  if (it == g_threads.end()) return BytecodeValue(false);
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  it->second.running = false;
  it->second.paused = false;
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleThreadRunning(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "thread") {
    return BytecodeValue(false);
  }
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_threads.find(handle->second);
  return BytecodeValue(it != g_threads.end() && it->second.running);
}

BytecodeValue AsyncBridge::handleIntervalCreate(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.size() < 2) {
    throw std::runtime_error("interval(ms, fn) requires delay and callback");
  }
  int64_t delay_ms = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    delay_ms = std::get<int64_t>(args[0]);
  } else if (std::holds_alternative<double>(args[0])) {
    delay_ms = static_cast<int64_t>(std::get<double>(args[0]));
  } else {
    throw std::runtime_error("interval delay must be number");
  }
  if (delay_ms < 1) delay_ms = 1;

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
    std::string task_id = ctx->asyncService->spawn([ctx, id, callback, delay_ms]() {
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

BytecodeValue AsyncBridge::handleIntervalPause(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval") return BytecodeValue(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end()) return BytecodeValue(false);
  it->second.paused = true;
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleIntervalResume(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval") return BytecodeValue(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end()) return BytecodeValue(false);
  it->second.paused = false;
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleIntervalStop(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "interval") return BytecodeValue(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end()) return BytecodeValue(false);
  it->second.running = false;
  if (ctx && ctx->asyncService && !it->second.task_id.empty()) {
    (void)ctx->asyncService->cancel(it->second.task_id);
  }
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  return BytecodeValue(true);
}

BytecodeValue AsyncBridge::handleTimeoutCreate(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  if (!ctx || !ctx->vm || args.size() < 2) {
    throw std::runtime_error("timeout(ms, fn) requires delay and callback");
  }
  int64_t delay_ms = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    delay_ms = std::get<int64_t>(args[0]);
  } else if (std::holds_alternative<double>(args[0])) {
    delay_ms = static_cast<int64_t>(std::get<double>(args[0]));
  } else {
    throw std::runtime_error("timeout delay must be number");
  }
  if (delay_ms < 1) delay_ms = 1;

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
    std::string task_id = ctx->asyncService->spawn([ctx, id, callback, delay_ms]() {
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
        } catch (...) {}
      }
    });
    std::lock_guard<std::mutex> lock(g_async_mutex);
    g_timers[id].task_id = task_id;
  }

  return BytecodeValue(makeHandleObject(vm, "timeout", id));
}

BytecodeValue AsyncBridge::handleTimeoutCancel(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  auto *vm = ctx && ctx->vm ? static_cast<VM *>(ctx->vm) : nullptr;
  auto handle = extractHandle(args, vm);
  if (!handle.has_value() || handle->first != "timeout") return BytecodeValue(false);
  std::lock_guard<std::mutex> lock(g_async_mutex);
  auto it = g_timers.find(handle->second);
  if (it == g_timers.end()) return BytecodeValue(false);
  it->second.running = false;
  if (ctx && ctx->asyncService && !it->second.task_id.empty()) {
    (void)ctx->asyncService->cancel(it->second.task_id);
  }
  if (vm) {
    vm->releaseCallback(it->second.callback);
  }
  return BytecodeValue(true);
}

// ============================================================================
// AutomationBridge Implementation
// ============================================================================

void AutomationBridge::install(PipelineOptions &options) {
  options.host_functions["automation.createAutoClicker"] = [ctx = ctx_](const auto &args) {
    return handleAutomationCreateAutoClicker(args, ctx);
  };
  options.host_functions["automation.createAutoRunner"] = [ctx = ctx_](const auto &args) {
    return handleAutomationCreateAutoRunner(args, ctx);
  };
  options.host_functions["automation.createAutoKeyPresser"] = [ctx = ctx_](const auto &args) {
    return handleAutomationCreateAutoKeyPresser(args, ctx);
  };
  options.host_functions["automation.hasTask"] = [ctx = ctx_](const auto &args) {
    return handleAutomationHasTask(args, ctx);
  };
  options.host_functions["automation.removeTask"] = [ctx = ctx_](const auto &args) {
    return handleAutomationRemoveTask(args, ctx);
  };
  options.host_functions["automation.stopAll"] = [ctx = ctx_](const auto &args) {
    return handleAutomationStopAll(args, ctx);
  };
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoClicker(const std::vector<BytecodeValue> &args,
                                                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoRunner(const std::vector<BytecodeValue> &args,
                                                                 const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationCreateAutoKeyPresser(const std::vector<BytecodeValue> &args,
                                                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationHasTask(const std::vector<BytecodeValue> &args,
                                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationRemoveTask(const std::vector<BytecodeValue> &args,
                                                           const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

BytecodeValue AutomationBridge::handleAutomationStopAll(const std::vector<BytecodeValue> &args,
                                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(false);
}

// ============================================================================
// BrowserBridge Implementation
// ============================================================================

void BrowserBridge::install(PipelineOptions &options) {
  options.host_functions["browser.connect"] = [ctx = ctx_](const auto &args) {
    return handleBrowserConnect(args, ctx);
  };
  options.host_functions["browser.connectFirefox"] = [ctx = ctx_](const auto &args) {
    return handleBrowserConnectFirefox(args, ctx);
  };
  options.host_functions["browser.disconnect"] = [ctx = ctx_](const auto &args) {
    return handleBrowserDisconnect(args, ctx);
  };
  options.host_functions["browser.isConnected"] = [ctx = ctx_](const auto &args) {
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

BytecodeValue BrowserBridge::handleBrowserConnect(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)ctx;
  std::string browserUrl = "http://localhost:9222";
  if (!args.empty()) {
    if (auto *s = std::get_if<std::string>(&args[0])) {
      browserUrl = *s;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.connect(browserUrl));
}

BytecodeValue BrowserBridge::handleBrowserConnectFirefox(const std::vector<BytecodeValue> &args,
                                                         const HostContext *ctx) {
  (void)ctx;
  int port = 2828;
  if (!args.empty()) {
    if (auto *v = std::get_if<int64_t>(&args[0])) {
      port = static_cast<int>(*v);
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.connectFirefox(port));
}

BytecodeValue BrowserBridge::handleBrowserDisconnect(const std::vector<BytecodeValue> &args,
                                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  browser.disconnect();
  return BytecodeValue(true);
}

BytecodeValue BrowserBridge::handleBrowserIsConnected(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.isConnected());
}

BytecodeValue BrowserBridge::handleBrowserOpen(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.open() requires a URL");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  if (!url) {
    throw std::runtime_error("browser.open() requires a string URL");
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.open(*url));
}

BytecodeValue BrowserBridge::handleBrowserNewTab(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)ctx;
  std::string url;
  if (!args.empty()) {
    if (auto *s = std::get_if<std::string>(&args[0])) {
      url = *s;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.newTab(url));
}

BytecodeValue BrowserBridge::handleBrowserGoto(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("browser.goto() requires a URL");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  if (!url) {
    throw std::runtime_error("browser.goto() requires a string URL");
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.gotoUrl(*url));
}

BytecodeValue BrowserBridge::handleBrowserBack(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.back());
}

BytecodeValue BrowserBridge::handleBrowserForward(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::BrowserService browser;
  return BytecodeValue(browser.forward());
}

BytecodeValue BrowserBridge::handleBrowserReload(const std::vector<BytecodeValue> &args,
                                                 const HostContext *ctx) {
  (void)ctx;
  bool ignoreCache = false;
  if (!args.empty()) {
    if (auto *b = std::get_if<bool>(&args[0])) {
      ignoreCache = *b;
    }
  }
  havel::host::BrowserService browser;
  return BytecodeValue(browser.reload(ignoreCache));
}

BytecodeValue BrowserBridge::handleBrowserListTabs(const std::vector<BytecodeValue> &args,
                                                   const HostContext *ctx) {
  (void)args;
  havel::host::BrowserService browser;
  auto tabs = browser.listTabs();
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  auto arr = vm->createHostArray();
  for (const auto &tab : tabs) {
    auto tabObj = vm->createHostObject();
    vm->setHostObjectField(tabObj, "id", BytecodeValue(static_cast<int64_t>(tab.id)));
    vm->setHostObjectField(tabObj, "title", BytecodeValue(tab.title));
    vm->setHostObjectField(tabObj, "url", BytecodeValue(tab.url));
    vm->setHostObjectField(tabObj, "type", BytecodeValue(tab.type));
    vm->pushHostArrayValue(arr, BytecodeValue(tabObj));
  }
  return BytecodeValue(arr);
}

// ============================================================================
// ToolsBridge Implementation
// ============================================================================

namespace {
  havel::host::TextChunkerService g_textChunker;
}

void ToolsBridge::install(PipelineOptions &options) {
  options.host_functions["textchunker.setText"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerSetText(args, ctx);
  };
  options.host_functions["textchunker.getText"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetText(args, ctx);
  };
  options.host_functions["textchunker.setChunkSize"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerSetChunkSize(args, ctx);
  };
  options.host_functions["textchunker.getTotalChunks"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetTotalChunks(args, ctx);
  };
  options.host_functions["textchunker.getCurrentChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetCurrentChunk(args, ctx);
  };
  options.host_functions["textchunker.setCurrentChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerSetCurrentChunk(args, ctx);
  };
  options.host_functions["textchunker.getChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetChunk(args, ctx);
  };
  options.host_functions["textchunker.getNextChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetNextChunk(args, ctx);
  };
  options.host_functions["textchunker.getPreviousChunk"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGetPreviousChunk(args, ctx);
  };
  options.host_functions["textchunker.goToFirst"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGoToFirst(args, ctx);
  };
  options.host_functions["textchunker.goToLast"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerGoToLast(args, ctx);
  };
  options.host_functions["textchunker.clear"] = [ctx = ctx_](const auto &args) {
    return handleTextChunkerClear(args, ctx);
  };
}

BytecodeValue ToolsBridge::handleTextChunkerSetText(const std::vector<BytecodeValue> &args,
                                                    const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setText() requires text");
  }
  const std::string *text = std::get_if<std::string>(&args[0]);
  if (!text) {
    throw std::runtime_error("textchunker.setText() requires a string");
  }
  g_textChunker.setText(*text);
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGetText(const std::vector<BytecodeValue> &args,
                                                    const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getText());
}

BytecodeValue ToolsBridge::handleTextChunkerSetChunkSize(const std::vector<BytecodeValue> &args,
                                                         const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setChunkSize() requires a size");
  }
  int64_t size = 20000;
  if (std::holds_alternative<int64_t>(args[0])) {
    size = std::get<int64_t>(args[0]);
  }
  g_textChunker.setChunkSize(static_cast<size_t>(size));
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGetTotalChunks(const std::vector<BytecodeValue> &args,
                                                           const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(g_textChunker.getTotalChunks()));
}

BytecodeValue ToolsBridge::handleTextChunkerGetCurrentChunk(const std::vector<BytecodeValue> &args,
                                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(static_cast<int64_t>(g_textChunker.getCurrentChunk()));
}

BytecodeValue ToolsBridge::handleTextChunkerSetCurrentChunk(const std::vector<BytecodeValue> &args,
                                                            const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.setCurrentChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    index = std::get<int64_t>(args[0]);
  }
  g_textChunker.setCurrentChunk(static_cast<int>(index));
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGetChunk(const std::vector<BytecodeValue> &args,
                                                     const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("textchunker.getChunk() requires a chunk index");
  }
  int64_t index = 0;
  if (std::holds_alternative<int64_t>(args[0])) {
    index = std::get<int64_t>(args[0]);
  }
  return BytecodeValue(g_textChunker.getChunk(static_cast<int>(index)));
}

BytecodeValue ToolsBridge::handleTextChunkerGetNextChunk(const std::vector<BytecodeValue> &args,
                                                         const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getNextChunk());
}

BytecodeValue ToolsBridge::handleTextChunkerGetPreviousChunk(const std::vector<BytecodeValue> &args,
                                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  return BytecodeValue(g_textChunker.getPreviousChunk());
}

BytecodeValue ToolsBridge::handleTextChunkerGoToFirst(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToFirst();
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerGoToLast(const std::vector<BytecodeValue> &args,
                                                     const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.goToLast();
  return BytecodeValue(true);
}

BytecodeValue ToolsBridge::handleTextChunkerClear(const std::vector<BytecodeValue> &args,
                                                  const HostContext *ctx) {
  (void)args;
  (void)ctx;
  g_textChunker.clear();
  return BytecodeValue(true);
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
  options.host_functions["media.getActivePlayer"] = [ctx = ctx_](const auto &args) {
    return handleMediaGetActivePlayer(args, ctx);
  };
  options.host_functions["media.setActivePlayer"] = [ctx = ctx_](const auto &args) {
    return handleMediaSetActivePlayer(args, ctx);
  };
  options.host_functions["media.getAvailablePlayers"] = [ctx = ctx_](const auto &args) {
    return handleMediaGetAvailablePlayers(args, ctx);
  };
}

BytecodeValue MediaBridge::handleMediaPlayPause(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.playPause();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaPlay(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.play();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaPause(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.pause();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaStop(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.stop();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaNext(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.next();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaPrevious(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    media.previous();
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaGetVolume(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    return BytecodeValue(media.getVolume());
  } catch (...) {
    return BytecodeValue(0.0);
  }
}

BytecodeValue MediaBridge::handleMediaSetVolume(const std::vector<BytecodeValue> &args,
                                                const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("media.setVolume() requires a volume value (0.0-1.0)");
  }
  double volume = 0.0;
  if (std::holds_alternative<double>(args[0])) {
    volume = std::get<double>(args[0]);
  } else if (std::holds_alternative<int64_t>(args[0])) {
    volume = static_cast<double>(std::get<int64_t>(args[0])) / 100.0;
  }
  try {
    havel::host::MediaService media;
    media.setVolume(volume);
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaGetActivePlayer(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  (void)args;
  try {
    havel::host::MediaService media;
    return BytecodeValue(media.getActivePlayer());
  } catch (...) {
    return BytecodeValue(std::string(""));
  }
}

BytecodeValue MediaBridge::handleMediaSetActivePlayer(const std::vector<BytecodeValue> &args,
                                                      const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("media.setActivePlayer() requires a player name");
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
  if (!name) {
    throw std::runtime_error("media.setActivePlayer() requires a string");
  }
  try {
    havel::host::MediaService media;
    media.setActivePlayer(*name);
    return BytecodeValue(true);
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue MediaBridge::handleMediaGetAvailablePlayers(const std::vector<BytecodeValue> &args,
                                                          const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) {
    return BytecodeValue(nullptr);
  }
  try {
    havel::host::MediaService media;
    auto players = media.getAvailablePlayers();
    auto arr = vm->createHostArray();
    for (const auto &player : players) {
      vm->pushHostArrayValue(arr, BytecodeValue(player));
    }
    return BytecodeValue(arr);
  } catch (...) {
    return BytecodeValue(nullptr);
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
  options.host_functions["network.getExternalIp"] = [ctx = ctx_](const auto &args) {
    return handleNetworkGetExternalIp(args, ctx);
  };
}

BytecodeValue NetworkBridge::handleNetworkGet(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)ctx;
  if (args.empty()) {
    throw std::runtime_error("http.get() requires a URL");
  }
  
  // Try to get string from variant
  std::string url;
  if (std::holds_alternative<std::string>(args[0])) {
    url = std::get<std::string>(args[0]);
  } else {
    throw std::runtime_error("http.get() requires a string URL, got type index " + std::to_string(args[0].index()));
  }
  
  int timeout_ms = 30000;
  if (args.size() > 1 && std::holds_alternative<int64_t>(args[1])) {
    timeout_ms = static_cast<int>(std::get<int64_t>(args[1]));
  }
  try {
    havel::host::NetworkService net;
    auto response = net.get(url, timeout_ms);
    if (response.success) {
      return BytecodeValue(response.body);
    } else {
      return BytecodeValue(nullptr);
    }
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("http.get() failed: ") + e.what());
  }
}

BytecodeValue NetworkBridge::handleNetworkPost(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("network.post() requires URL and data");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  const std::string *data = std::get_if<std::string>(&args[1]);
  if (!url || !data) {
    throw std::runtime_error("network.post() requires string arguments");
  }
  std::string content_type = "application/json";
  if (args.size() > 2 && std::holds_alternative<std::string>(args[2])) {
    content_type = std::get<std::string>(args[2]);
  }
  int timeout_ms = 30000;
  if (args.size() > 3 && std::holds_alternative<int64_t>(args[3])) {
    timeout_ms = static_cast<int>(std::get<int64_t>(args[3]));
  }
  try {
    havel::host::NetworkService net;
    auto response = net.post(*url, *data, content_type, timeout_ms);
    if (response.success) {
      return BytecodeValue(response.body);
    } else {
      return BytecodeValue(nullptr);
    }
  } catch (...) {
    return BytecodeValue(nullptr);
  }
}

BytecodeValue NetworkBridge::handleNetworkIsOnline(const std::vector<BytecodeValue> &args,
                                                   const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    havel::host::NetworkService net;
    return BytecodeValue(net.isOnline());
  } catch (...) {
    return BytecodeValue(false);
  }
}

BytecodeValue NetworkBridge::handleNetworkGetExternalIp(const std::vector<BytecodeValue> &args,
                                                        const HostContext *ctx) {
  (void)args;
  (void)ctx;
  try {
    havel::host::NetworkService net;
    return BytecodeValue(net.getExternalIp());
  } catch (...) {
    return BytecodeValue(std::string(""));
  }
}

BytecodeValue NetworkBridge::handleNetworkDownload(const std::vector<BytecodeValue> &args,
                                                   const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("http.download() requires URL and path");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  const std::string *path = std::get_if<std::string>(&args[1]);
  if (!url || !path) {
    throw std::runtime_error("http.download() requires string URL and path");
  }
  int timeout_ms = 30000;
  if (args.size() > 2 && std::holds_alternative<int64_t>(args[2])) {
    timeout_ms = static_cast<int>(std::get<int64_t>(args[2]));
  }
  try {
    havel::host::NetworkService net;
    bool success = net.download(*url, *path, timeout_ms);
    return BytecodeValue(success);
  } catch (...) {
    return BytecodeValue(false);
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
}

BytecodeValue AudioBridge::handleGetVolume(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue(1.0);  // Default volume
  }
  return BytecodeValue(ctx->audioManager->getVolume());
}

BytecodeValue AudioBridge::handleSetVolume(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue(false);
  }
  if (args.empty() || !std::holds_alternative<double>(args[0])) {
    throw std::runtime_error("audio.setVolume() requires a number");
  }
  double volume = std::get<double>(args[0]);
  return BytecodeValue(ctx->audioManager->setVolume(volume));
}

BytecodeValue AudioBridge::handleIsMuted(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue(false);
  }
  return BytecodeValue(ctx->audioManager->isMuted());
}

BytecodeValue AudioBridge::handleSetMute(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue(false);
  }
  if (args.empty() || !std::holds_alternative<bool>(args[0])) {
    throw std::runtime_error("audio.setMute() requires a boolean");
  }
  bool muted = std::get<bool>(args[0]);
  return BytecodeValue(ctx->audioManager->setMute(muted));
}

BytecodeValue AudioBridge::handleToggleMute(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->audioManager) {
    return BytecodeValue(false);
  }
  return BytecodeValue(ctx->audioManager->toggleMute());
}

// ============================================================================
// DisplayBridge Implementation
// ============================================================================

void DisplayBridge::install(PipelineOptions &options) {
  options.host_functions["display.getMonitors"] = [ctx = ctx_](const auto &args) {
    return handleGetMonitors(args, ctx);
  };
  options.host_functions["display.getPrimary"] = [ctx = ctx_](const auto &args) {
    return handleGetPrimary(args, ctx);
  };
  options.host_functions["display.getCount"] = [ctx = ctx_](const auto &args) {
    return handleGetCount(args, ctx);
  };
}

BytecodeValue DisplayBridge::handleGetMonitors(const std::vector<BytecodeValue> &args,
                                               const HostContext *ctx) {
  (void)args;
  if (!ctx) return BytecodeValue(nullptr);
  // Return array of monitor info objects
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) return BytecodeValue(nullptr);
  
  auto monitors = havel::DisplayManager::GetMonitors();
  auto arr = vm->createHostArray();
  
  for (const auto& mon : monitors) {
    auto obj = vm->createHostObject();
    vm->setHostObjectField(obj, "name", BytecodeValue(mon.name));
    vm->setHostObjectField(obj, "x", BytecodeValue(static_cast<int64_t>(mon.x)));
    vm->setHostObjectField(obj, "y", BytecodeValue(static_cast<int64_t>(mon.y)));
    vm->setHostObjectField(obj, "width", BytecodeValue(static_cast<int64_t>(mon.width)));
    vm->setHostObjectField(obj, "height", BytecodeValue(static_cast<int64_t>(mon.height)));
    vm->setHostObjectField(obj, "isPrimary", BytecodeValue(mon.isPrimary));
    vm->pushHostArrayValue(arr, BytecodeValue(obj));
  }
  
  return BytecodeValue(arr);
}

BytecodeValue DisplayBridge::handleGetPrimary(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  if (!ctx) return BytecodeValue(nullptr);
  auto *vm = static_cast<VM *>(ctx->vm);
  if (!vm) return BytecodeValue(nullptr);
  
  auto mon = havel::DisplayManager::GetPrimaryMonitor();
  auto obj = vm->createHostObject();
  vm->setHostObjectField(obj, "name", BytecodeValue(mon.name));
  vm->setHostObjectField(obj, "x", BytecodeValue(static_cast<int64_t>(mon.x)));
  vm->setHostObjectField(obj, "y", BytecodeValue(static_cast<int64_t>(mon.y)));
  vm->setHostObjectField(obj, "width", BytecodeValue(static_cast<int64_t>(mon.width)));
  vm->setHostObjectField(obj, "height", BytecodeValue(static_cast<int64_t>(mon.height)));
  vm->setHostObjectField(obj, "isPrimary", BytecodeValue(mon.isPrimary));
  
  return BytecodeValue(obj);
}

BytecodeValue DisplayBridge::handleGetCount(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto monitors = havel::DisplayManager::GetMonitors();
  return BytecodeValue(static_cast<int64_t>(monitors.size()));
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
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("config.get() key must be a string");
  }
  
  std::string key = std::get<std::string>(args[0]);
  auto& config = havel::Configs::Get();
  
  // Return value based on default type
  if (std::holds_alternative<std::string>(args[1])) {
    std::string def = std::get<std::string>(args[1]);
    return BytecodeValue(config.Get(key, def));
  } else if (std::holds_alternative<int64_t>(args[1])) {
    int64_t def = std::get<int64_t>(args[1]);
    return BytecodeValue(config.Get(key, def));
  } else if (std::holds_alternative<double>(args[1])) {
    double def = std::get<double>(args[1]);
    return BytecodeValue(config.Get(key, def));
  } else if (std::holds_alternative<bool>(args[1])) {
    bool def = std::get<bool>(args[1]);
    return BytecodeValue(config.Get(key, def));
  }
  
  return BytecodeValue(nullptr);
}

BytecodeValue ConfigBridge::handleSet(const std::vector<BytecodeValue> &args,
                                      const HostContext *ctx) {
  (void)ctx;
  if (args.size() < 2) {
    throw std::runtime_error("config.set() requires key and value");
  }
  
  if (!std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("config.set() key must be a string");
  }
  
  std::string key = std::get<std::string>(args[0]);
  auto& config = havel::Configs::Get();
  
  bool save = (args.size() > 2 && std::holds_alternative<bool>(args[2]) && std::get<bool>(args[2]));
  
  if (std::holds_alternative<std::string>(args[1])) {
    config.Set(key, std::get<std::string>(args[1]), save);
  } else if (std::holds_alternative<int64_t>(args[1])) {
    config.Set(key, std::get<int64_t>(args[1]), save);
  } else if (std::holds_alternative<double>(args[1])) {
    config.Set(key, std::get<double>(args[1]), save);
  } else if (std::holds_alternative<bool>(args[1])) {
    config.Set(key, std::get<bool>(args[1]), save);
  }
  
  return BytecodeValue(true);
}

BytecodeValue ConfigBridge::handleSave(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  auto& config = havel::Configs::Get();
  config.Save();
  return BytecodeValue(true);
}

// ============================================================================
// ModeBridge Implementation
// ============================================================================

void ModeBridge::install(PipelineOptions &options) {
  // mode() - returns current mode name (for comparisons like mode == "gaming")
  options.host_functions["mode"] = [ctx = ctx_](const auto &args) {
    (void)args;
    if (!ctx || !ctx->modeManager) {
      return BytecodeValue(std::string(""));
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
  // Args: name, priority, condition, enter, exit, onEnterFromMode, onEnterFrom, onExitToMode, onExitTo
  if (args.size() < 9) {
    return BytecodeValue(false);
  }
  
  if (!ctx || !ctx->modeManager || !ctx->vm) {
    return BytecodeValue(false);
  }
  
  // Get mode name
  std::string modeName;
  if (std::holds_alternative<std::string>(args[0])) {
    modeName = std::get<std::string>(args[0]);
  } else {
    return BytecodeValue(false);
  }
  
  // Get priority
  int priority = 0;
  if (std::holds_alternative<int64_t>(args[1])) {
    priority = static_cast<int>(std::get<int64_t>(args[1]));
  }
  
  // Condition is evaluated by ModeManager's update loop
  // For now, we'll store it and let ModeManager handle it
  // The condition expression needs to be evaluated periodically
  
  // Register callbacks for enter/exit
  // For now, we'll use placeholder implementations
  // Full implementation needs proper closure support
  
  info("Mode registered: {} with priority {}", modeName, priority);
  
  // TODO: Store condition expression and evaluate it in ModeManager update loop
  // TODO: Register enter/exit callbacks with proper closure support
  
  return BytecodeValue(true);
}

BytecodeValue ModeBridge::handleGetCurrent(const std::vector<BytecodeValue> &args,
                                           const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->modeManager) {
    return BytecodeValue(std::string(""));
  }
  return BytecodeValue(ctx->modeManager->getCurrentMode());
}

BytecodeValue ModeBridge::handleSet(const std::vector<BytecodeValue> &args,
                                    const HostContext *ctx) {
  if (!ctx || !ctx->modeManager) {
    return BytecodeValue(false);
  }
  if (args.empty() || !std::holds_alternative<std::string>(args[0])) {
    throw std::runtime_error("mode.set() requires a mode name string");
  }
  std::string modeName = std::get<std::string>(args[0]);
  ctx->modeManager->setMode(modeName);
  return BytecodeValue(true);
}

BytecodeValue ModeBridge::handleGetPrevious(const std::vector<BytecodeValue> &args,
                                            const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->modeManager) {
    return BytecodeValue(std::string(""));
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
  
  if (!std::holds_alternative<int64_t>(args[0])) {
    throw std::runtime_error("timer.after() delay must be an integer");
  }
  
  int64_t delay_ms = std::get<int64_t>(args[0]);
  
  // Simple implementation: just sleep
  // For callback support, use: sleep(delay_ms); callback()
  if (ctx && ctx->asyncService) {
    ctx->asyncService->sleep(static_cast<int>(delay_ms));
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }
  
  return BytecodeValue(nullptr);
}

BytecodeValue TimerBridge::handleEvery(const std::vector<BytecodeValue> &args,
                                       const HostContext *ctx) {
  (void)args;
  (void)ctx;
  // timer.every requires closure/callback support
  // For now, just return without doing anything
  // Users should use a while loop with sleep instead:
  // while (true) { body; sleep(interval); }
  throw std::runtime_error("timer.every() requires closure support - use while loop with sleep() instead");
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
}

BytecodeValue AppBridge::handleAppGetName(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getAppName());
}

BytecodeValue AppBridge::handleAppGetVersion(const std::vector<BytecodeValue> &args,
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

BytecodeValue AppBridge::handleAppGetHostname(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getHostname());
}

BytecodeValue AppBridge::handleAppGetUsername(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getUsername());
}

BytecodeValue AppBridge::handleAppGetHomeDir(const std::vector<BytecodeValue> &args,
                                             const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(app.getHomeDir());
}

BytecodeValue AppBridge::handleAppGetCpuCores(const std::vector<BytecodeValue> &args,
                                              const HostContext *ctx) {
  (void)args;
  (void)ctx;
  havel::host::AppService app;
  return BytecodeValue(static_cast<int64_t>(app.getCpuCores()));
}

BytecodeValue AppBridge::handleAppGetEnv(const std::vector<BytecodeValue> &args,
                                         const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.getEnv() requires a variable name");
  }
  const std::string *name = std::get_if<std::string>(&args[0]);
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
  const std::string *name = std::get_if<std::string>(&args[0]);
  const std::string *value = std::get_if<std::string>(&args[1]);
  if (!name || !value) {
    throw std::runtime_error("app.setEnv() requires string arguments");
  }
  havel::host::AppService app;
  return BytecodeValue(app.setEnv(*name, *value));
}

BytecodeValue AppBridge::handleAppOpenUrl(const std::vector<BytecodeValue> &args,
                                          const HostContext *ctx) {
  if (args.empty()) {
    throw std::runtime_error("app.openUrl() requires a URL");
  }
  const std::string *url = std::get_if<std::string>(&args[0]);
  if (!url) {
    throw std::runtime_error("app.openUrl() requires a string URL");
  }
  havel::host::AppService app;
  return BytecodeValue(app.openUrl(*url));
}

} // namespace havel::compiler
