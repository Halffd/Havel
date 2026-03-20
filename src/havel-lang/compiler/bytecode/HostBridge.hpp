#pragma once

#include "Pipeline.hpp"
#include "VM.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {
class WindowManager;
class IO;
class HotkeyManager;
class ModeManager;
class ProcessManager;

namespace host {
class IOService;
class HotkeyService;
class WindowService;
class ModeService;
} // namespace host
} // namespace havel

namespace havel::compiler {

struct HostBridgeDependencies {
  WindowManager *window_manager = nullptr;
  std::shared_ptr<IO> io;
  std::shared_ptr<HotkeyManager> hotkey_manager;
  std::shared_ptr<ModeManager> mode_manager;
  ProcessManager *process_manager = nullptr;
  
  // Service layer (preferred - decouples from core types)
  std::shared_ptr<host::IOService> io_service;
  std::shared_ptr<host::HotkeyService> hotkey_service;
  std::shared_ptr<host::WindowService> window_service;
  std::shared_ptr<host::ModeService> mode_service;
};

class HostBridgeRegistry
    : public std::enable_shared_from_this<HostBridgeRegistry> {
public:
  HostBridgeRegistry(VM &vm, HostBridgeDependencies deps);
  ~HostBridgeRegistry();

  void install(PipelineOptions &options);
  void clear();

private:
  using RootTable = std::unordered_map<int64_t, VM::GCRoot>;

  BytecodeValue handleWindowMoveToNextMonitor(
      const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowGetActive(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowMoveToMonitor(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowClose(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowResize(const std::vector<BytecodeValue> &args);
  BytecodeValue handleWindowOn(const std::vector<BytecodeValue> &args);
  BytecodeValue handleSend(const std::vector<BytecodeValue> &args);
  BytecodeValue handleSendKey(const std::vector<BytecodeValue> &args);
  BytecodeValue handleMouseMove(const std::vector<BytecodeValue> &args);
  BytecodeValue handleMouseClick(const std::vector<BytecodeValue> &args);
  BytecodeValue handleGetMousePosition(const std::vector<BytecodeValue> &args);
  BytecodeValue handleHotkeyRegister(const std::vector<BytecodeValue> &args);
  BytecodeValue handleHotkeyTrigger(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeDefine(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeSet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeTick(const std::vector<BytecodeValue> &args);
  BytecodeValue handleProcessFind(const std::vector<BytecodeValue> &args);

  int64_t pinLongLivedCallback(const BytecodeValue &value, RootTable &table);
  void unpinLongLivedCallback(int64_t id, RootTable &table);
  BytecodeValue invokePinnedCallback(RootTable &table, int64_t id,
                                     const std::vector<BytecodeValue> &args = {});

  struct ModeBinding {
    std::optional<int64_t> condition_id;
    std::optional<int64_t> enter_id;
    std::optional<int64_t> exit_id;
  };

  VM &vm_;
  HostBridgeDependencies deps_;
  int64_t next_callback_id_ = 1;
  RootTable hotkey_callback_roots_;
  RootTable mode_callback_roots_;
  RootTable window_event_callback_roots_;
  std::unordered_map<int64_t, std::string> hotkey_binding_keys_;
  std::vector<std::string> mode_definition_order_;
  std::unordered_map<std::string, ModeBinding> mode_bindings_;
};

std::shared_ptr<HostBridgeRegistry>
createHostBridgeRegistry(VM &vm, HostBridgeDependencies deps);

} // namespace havel::compiler
