#pragma once

#include "Pipeline.hpp"
#include "VM.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace havel {
class WindowManager;
class IO;
class HotkeyManager;
class ModeManager;
class ProcessManager;
} // namespace havel

namespace havel::compiler {

struct HostBridgeDependencies {
  WindowManager *window_manager = nullptr;
  std::shared_ptr<IO> io;
  std::shared_ptr<HotkeyManager> hotkey_manager;
  std::shared_ptr<ModeManager> mode_manager;
  ProcessManager *process_manager = nullptr;
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
  BytecodeValue handleSend(const std::vector<BytecodeValue> &args);
  BytecodeValue handleHotkeyRegister(const std::vector<BytecodeValue> &args);
  BytecodeValue handleModeDefine(const std::vector<BytecodeValue> &args);
  BytecodeValue handleProcessFind(const std::vector<BytecodeValue> &args);

  int64_t pinLongLivedCallback(const BytecodeValue &value, RootTable &table);
  void unpinLongLivedCallback(int64_t id, RootTable &table);

  VM &vm_;
  HostBridgeDependencies deps_;
  int64_t next_callback_id_ = 1;
  RootTable hotkey_callback_roots_;
  RootTable mode_callback_roots_;
};

std::shared_ptr<HostBridgeRegistry>
createHostBridgeRegistry(VM &vm, HostBridgeDependencies deps);

} // namespace havel::compiler
