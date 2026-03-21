/*
 * ModeService.cpp
 *
 * Pure C++ mode service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "ModeService.hpp"
#include "havel-lang/compiler/bytecode/VM.hpp"
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
    if (!m_manager || !m_vm) return;
    
    // Create mode definition with callbacks that invoke through VM
    havel::ModeManager::ModeDefinition mode;
    mode.name = name;
    
    // Condition callback - invokes through VM
    if (conditionId != compiler::INVALID_CALLBACK_ID) {
        mode.condition = [vm = m_vm, id = conditionId]() -> bool {
            try {
                auto result = vm->invokeCallback(id);
                if (std::holds_alternative<bool>(result)) {
                    return std::get<bool>(result);
                } else if (std::holds_alternative<int64_t>(result)) {
                    return std::get<int64_t>(result) != 0;
                } else if (std::holds_alternative<double>(result)) {
                    return std::get<double>(result) != 0.0;
                }
            } catch (...) {
                // Callback failed, assume false
            }
            return false;
        };
    }
    
    // Enter callback - invokes through VM
    if (enterId != compiler::INVALID_CALLBACK_ID) {
        mode.onEnter = [vm = m_vm, id = enterId]() {
            try {
                vm->invokeCallback(id);
            } catch (...) {
                // Callback failed, ignore
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
