#pragma once

#include "Pipeline.hpp"
#include "VM.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {
class ModeManager;  // TEMPORARY for mode.define/mode.tick
namespace host {
class ServiceRegistry;
} // namespace host
} // namespace havel

namespace havel::compiler {

/**
 * HostBridgeDependencies - Dependencies for HostBridge
 * 
 * CRITICAL: HostBridge depends ONLY on ServiceRegistry.
 * All services are discovered through the registry.
 * 
 * Legacy dependencies have been removed - services MUST be registered
 * in ServiceRegistry before HostBridge is used.
 * 
 * TEMPORARY: ModeManager is still needed for mode.define/mode.tick
 * TODO: Refactor mode system to use ModeService with callback support
 */
struct HostBridgeDependencies {
  // Service registry (THE ONLY dependency for most operations)
  std::shared_ptr<host::ServiceRegistry> services;
  
  // TEMPORARY - ModeManager for complex mode operations
  // TODO: Remove when mode system is refactored
  class ModeManager* mode_manager = nullptr;
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
  BytecodeValue handleClipboardGet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleClipboardSet(const std::vector<BytecodeValue> &args);
  BytecodeValue handleClipboardClear(const std::vector<BytecodeValue> &args);

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
