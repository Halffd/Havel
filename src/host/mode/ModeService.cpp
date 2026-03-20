/*
 * ModeService.cpp
 *
 * Pure C++ mode service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "ModeService.hpp"
#include "core/ModeManager.hpp"

namespace havel::host {

ModeService::ModeService(std::shared_ptr<havel::ModeManager> manager)
    : m_manager(manager) {}

// =========================================================================
// Mode queries
// =========================================================================

std::string ModeService::getCurrentMode() const {
    return m_manager ? m_manager->getCurrentMode() : "";
}

std::string ModeService::getPreviousMode() const {
    return m_manager ? m_manager->getPreviousMode() : "";
}

std::chrono::milliseconds ModeService::getModeTime(const std::string& modeName) const {
    if (!m_manager) return std::chrono::milliseconds(0);
    std::string name = modeName.empty() ? getCurrentMode() : modeName;
    return m_manager->getModeTime(name);
}

int ModeService::getModeTransitions(const std::string& modeName) const {
    if (!m_manager) return 0;
    std::string name = modeName.empty() ? getCurrentMode() : modeName;
    return m_manager->getModeTransitions(name);
}

std::vector<std::string> ModeService::getModeNames() const {
    if (!m_manager) return {};
    std::vector<std::string> names;
    const auto& modes = m_manager->getModes();
    names.reserve(modes.size());
    for (const auto& mode : modes) {
        names.push_back(mode.name);
    }
    return names;
}

// =========================================================================
// Mode control
// =========================================================================

void ModeService::setMode(const std::string& modeName) {
    if (m_manager) {
        m_manager->setMode(modeName);
    }
}

// =========================================================================
// Signal queries
// =========================================================================

bool ModeService::isSignalActive(const std::string& signalName) const {
    return m_manager && m_manager->isSignalActive(signalName);
}

} // namespace havel::host
