/*
 * AutomationService.cpp
 *
 * Automation service implementation.
 */
#include "AutomationService.hpp"
#include "core/automation/AutomationManager.hpp"
#include "core/IO.hpp"

namespace havel::host {

AutomationService::AutomationService(std::shared_ptr<IO> io) {
    m_manager = std::make_shared<havel::automation::AutomationManager>(io);
}

AutomationService::~AutomationService() {
}

bool AutomationService::createAutoClicker(const std::string& name, 
                                           const std::string& button,
                                           int intervalMs) {
    if (!m_manager) return false;
    
    auto task = m_manager->createAutoClicker(button, intervalMs);
    if (!task) return false;
    
    // Store task with name
    // Note: AutomationManager handles task storage internally
    return true;
}

bool AutomationService::createAutoRunner(const std::string& name,
                                          const std::string& direction,
                                          int intervalMs) {
    if (!m_manager) return false;
    
    auto task = m_manager->createAutoRunner(direction, intervalMs);
    if (!task) return false;
    
    return true;
}

bool AutomationService::createAutoKeyPresser(const std::string& name,
                                              const std::string& key,
                                              int intervalMs) {
    if (!m_manager) return false;
    
    auto task = m_manager->createAutoKeyPresser(key, intervalMs);
    if (!task) return false;
    
    return true;
}

bool AutomationService::hasTask(const std::string& name) const {
    return m_manager ? m_manager->hasTask(name) : false;
}

void AutomationService::removeTask(const std::string& name) {
    if (m_manager) m_manager->removeTask(name);
}

void AutomationService::stopAll() {
    if (m_manager) m_manager->stopAll();
}

std::vector<std::string> AutomationService::getTaskNames() const {
    // Note: AutomationManager doesn't expose task names directly
    // This would need to be added to AutomationManager
    return {};
}

} // namespace havel::host
