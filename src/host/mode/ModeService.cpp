#include "ModeService.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "core/mode/ModeManager.hpp"

namespace havel::host {

ModeService::ModeService(havel::IHostAPI* hostAPI, havel::ModeManager* manager)
: m_hostAPI(hostAPI), m_manager(manager) {}

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
                                 compiler::CallbackId enterId,
                                 compiler::CallbackId exitId) {
    auto* vm = m_hostAPI ? m_hostAPI->GetVM() : nullptr;
    if (!vm || !m_manager) return;

    havel::ModeManager::ModeDefinition mode;
    mode.name = name;

    if (enterId != compiler::INVALID_CALLBACK_ID) {
        mode.onEnter = [vm, id = enterId]() {
            try {
                vm->invokeCallback(id);
            } catch (...) {
            }
        };
    }

    if (exitId != compiler::INVALID_CALLBACK_ID) {
        mode.onExit = [vm, id = exitId]() {
            try {
                vm->invokeCallback(id);
            } catch (...) {
            }
        };
    }

    m_manager->defineMode(std::move(mode));
}

} // namespace havel::host
