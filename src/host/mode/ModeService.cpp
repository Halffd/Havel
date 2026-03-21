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
    (void)conditionId;  // ModeManager uses AST expressions for conditions, not callbacks
    if (!m_vm || !m_manager) return;
    
    // Create mode definition with callbacks that invoke through VM
    havel::ModeManager::ModeDefinition mode;
    mode.name = name;
    // Note: conditionExpr is for AST expressions, not callbacks
    // Condition handling is done through mode.tick() checking CallbackId
    
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
