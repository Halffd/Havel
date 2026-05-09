/*
 * ModeService.cpp
 *
 * Pure C++ mode service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "ModeService.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "core/ModeManager.hpp"

namespace havel::host {

ModeService::ModeService(havel::compiler::VM* vm, havel::ModeManager* manager)
    : m_vm(vm), m_manager(manager) {}

std::string ModeService::getCurrentMode() const {
    return m_manager ? m_manager->getCurrentMode() : "";
}

std::string ModeService::getPreviousMode() const {
    return m_manager ? m_manager->getPreviousMode() : "";
}

void ModeService::setMode(const std::string& modeName) {
    if (m_manager) m_manager->setMode(modeName);
}

void ModeService::defineMode(const std::string& name,
                             compiler::CallbackId conditionId,
                             compiler::CallbackId enterId,
                             compiler::CallbackId exitId) {
  if (!m_vm || !m_manager) return;

  havel::ModeManager::ModeDefinition mode;
  mode.name = name;

  if (conditionId != compiler::INVALID_CALLBACK_ID) {
    mode.conditionCallback = [vm = m_vm, id = conditionId]() -> bool {
      try {
        auto result = vm->invokeCallback(id);
        return result.asBool();
      } catch (...) {
        return false;
      }
    };
  }

  if (enterId != compiler::INVALID_CALLBACK_ID) {
    mode.onEnter = [vm = m_vm, id = enterId]() {
      try {
        vm->invokeCallback(id);
      } catch (...) {
      }
    };
  }

  if (exitId != compiler::INVALID_CALLBACK_ID) {
    mode.onExit = [vm = m_vm, id = exitId]() {
      try {
        vm->invokeCallback(id);
      } catch (...) {
      }
    };
  }

  m_manager->defineMode(std::move(mode));
}
        };
    }

    // Exit callback - invokes through VM
    if (exitId != compiler::INVALID_CALLBACK_ID) {
        mode.onExit = [vm = m_vm, id = exitId]() {
            try {
                vm->invokeCallback(id);
            } catch (...) {
                // Callback failed, ignore
            }
        };
    }

    m_manager->defineMode(std::move(mode));
}

} // namespace havel::host
