#include "HostBridge.hpp"

#include "core/HotkeyManager.hpp"
#include "core/IO.hpp"
#include "core/ModeManager.hpp"
#include "core/process/ProcessManager.hpp"
#include "window/WindowManager.hpp"

#include <stdexcept>

namespace havel::compiler {

namespace {
std::string requireStringArg(const std::vector<BytecodeValue> &args, size_t index,
                             const std::string &fn_name) {
  if (index >= args.size() || !std::holds_alternative<std::string>(args[index])) {
    throw std::runtime_error(fn_name + " expects string argument at index " +
                             std::to_string(index));
  }
  return std::get<std::string>(args[index]);
}
} // namespace

HostBridgeRegistry::HostBridgeRegistry(VM &vm, HostBridgeDependencies deps)
    : vm_(vm), deps_(std::move(deps)) {}

HostBridgeRegistry::~HostBridgeRegistry() {
  clear();
}

std::shared_ptr<HostBridgeRegistry>
createHostBridgeRegistry(VM &vm, HostBridgeDependencies deps) {
  return std::make_shared<HostBridgeRegistry>(vm, std::move(deps));
}

void HostBridgeRegistry::install(PipelineOptions &options) {
  const auto self = shared_from_this();

  options.host_functions["window.moveToNextMonitor"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleWindowMoveToNextMonitor(args);
      };

  options.host_functions["send"] = [self](const std::vector<BytecodeValue> &args) {
    return self->handleSend(args);
  };

  options.host_functions["hotkey.register"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleHotkeyRegister(args);
      };

  options.host_functions["mode.define"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleModeDefine(args);
      };

  options.host_functions["process.find"] =
      [self](const std::vector<BytecodeValue> &args) {
        return self->handleProcessFind(args);
      };
}

void HostBridgeRegistry::clear() {
  if (deps_.hotkey_manager) {
    for (const auto &[id, _] : hotkey_callback_roots_) {
      deps_.hotkey_manager->RemoveHotkey(static_cast<int>(id));
    }
  }
  hotkey_callback_roots_.clear();
  mode_callback_roots_.clear();
}

int64_t HostBridgeRegistry::pinLongLivedCallback(const BytecodeValue &value,
                                                 RootTable &table) {
  const int64_t id = next_callback_id_++;
  table.emplace(id, vm_.makeRoot(value));
  return id;
}

void HostBridgeRegistry::unpinLongLivedCallback(int64_t id, RootTable &table) {
  table.erase(id);
}

BytecodeValue HostBridgeRegistry::handleWindowMoveToNextMonitor(
    const std::vector<BytecodeValue> &args) {
  if (!args.empty()) {
    throw std::runtime_error("window.moveToNextMonitor expects 0 arguments");
  }
  if (!deps_.window_manager) {
    throw std::runtime_error("window manager unavailable");
  }
  (void)deps_.window_manager;
  havel::WindowManager::MoveWindowToNextMonitor();
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleSend(
    const std::vector<BytecodeValue> &args) {
  const auto text = requireStringArg(args, 0, "send");
  if (!deps_.io) {
    throw std::runtime_error("io unavailable");
  }
  deps_.io->Send(text.c_str());
  return BytecodeValue(nullptr);
}

BytecodeValue HostBridgeRegistry::handleHotkeyRegister(
    const std::vector<BytecodeValue> &args) {
  const auto key = requireStringArg(args, 0, "hotkey.register");
  if (args.size() < 2) {
    throw std::runtime_error("hotkey.register expects callback as second argument");
  }
  if (!deps_.hotkey_manager) {
    throw std::runtime_error("hotkey manager unavailable");
  }

  const int64_t id = pinLongLivedCallback(args[1], hotkey_callback_roots_);
  const bool ok = deps_.hotkey_manager->AddHotkey(
      key, [weak_self = weak_from_this(), id]() {
        if (auto self = weak_self.lock()) {
          if (self->hotkey_callback_roots_.find(id) ==
              self->hotkey_callback_roots_.end()) {
            return;
          }
          // Callback is pinned via VM::GCRoot; invocation wiring is a separate
          // execution-path feature.
        }
      },
      static_cast<int>(id));

  if (!ok) {
    unpinLongLivedCallback(id, hotkey_callback_roots_);
    return BytecodeValue(false);
  }
  return BytecodeValue(static_cast<int64_t>(id));
}

BytecodeValue HostBridgeRegistry::handleModeDefine(
    const std::vector<BytecodeValue> &args) {
  const auto mode_name = requireStringArg(args, 0, "mode.define");
  if (args.size() < 2) {
    throw std::runtime_error("mode.define expects callback as second argument");
  }
  if (!deps_.mode_manager) {
    throw std::runtime_error("mode manager unavailable");
  }

  const int64_t id = pinLongLivedCallback(args[1], mode_callback_roots_);
  havel::ModeManager::ModeDefinition mode;
  mode.name = mode_name;
  mode.onEnter = [weak_self = weak_from_this(), id]() {
    if (auto self = weak_self.lock()) {
      if (self->mode_callback_roots_.find(id) == self->mode_callback_roots_.end()) {
        return;
      }
      // Callback is pinned via VM::GCRoot; invocation wiring is a separate
      // execution-path feature.
    }
  };
  deps_.mode_manager->defineMode(std::move(mode));
  return BytecodeValue(static_cast<int64_t>(id));
}

BytecodeValue HostBridgeRegistry::handleProcessFind(
    const std::vector<BytecodeValue> &args) {
  const auto name = requireStringArg(args, 0, "process.find");
  auto matches = havel::ProcessManager::findProcesses(name);

  const auto result = vm_.createHostArray();
  for (const auto &entry : matches) {
    vm_.pushHostArrayValue(result, static_cast<int64_t>(entry.pid));
  }
  return BytecodeValue(result);
}

} // namespace havel::compiler
