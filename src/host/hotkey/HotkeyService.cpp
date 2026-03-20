/*
 * HotkeyService.cpp
 *
 * Pure C++ hotkey service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "core/HotkeyManager.hpp"
#include "core/io/KeyTap.hpp"
#include "HotkeyService.hpp"

namespace havel { namespace host {

HotkeyService::HotkeyService(std::shared_ptr<havel::HotkeyManager> manager)
    : m_manager(manager) {}

// =========================================================================
// Basic hotkey registration
// =========================================================================

bool HotkeyService::registerHotkey(const std::string& key, 
                                    std::function<void()> callback, 
                                    int id) {
    if (!m_manager) return false;
    return m_manager->AddHotkey(key, callback, id);
}

bool HotkeyService::removeHotkey(int id) {
    if (!m_manager) return false;
    return m_manager->RemoveHotkey(id);
}

bool HotkeyService::grabHotkey(int id) {
    if (!m_manager) return false;
    return m_manager->GrabHotkey(id);
}

bool HotkeyService::ungrabHotkey(int id) {
    if (!m_manager) return false;
    return m_manager->UngrabHotkey(id);
}

void HotkeyService::clearAllHotkeys() {
    if (m_manager) m_manager->clearAllHotkeys();
}

std::vector<HotkeyInfo> HotkeyService::getHotkeyList() const {
    std::vector<HotkeyInfo> result;
    if (!m_manager) return result;

    // Get regular hotkeys
    auto hotkeys = m_manager->getHotkeyList();
    for (const auto& hotkey : hotkeys) {
        HotkeyInfo info;
        info.id = hotkey.id;
        info.key = hotkey.alias;
        info.enabled = hotkey.enabled;
        info.type = "regular";
        result.push_back(info);
    }

    // Get conditional hotkeys
    auto conditional = m_manager->getConditionalHotkeyList();
    for (const auto& hotkey : conditional) {
        HotkeyInfo info;
        info.id = hotkey.id;
        info.key = hotkey.key;
        info.condition = hotkey.condition;
        info.enabled = hotkey.enabled;
        info.type = "conditional";
        result.push_back(info);
    }

    return result;
}

// =========================================================================
// Contextual hotkeys
// =========================================================================

int HotkeyService::addContextualHotkey(const std::string& key,
                                        const std::string& condition,
                                        std::function<void()> trueAction,
                                        std::function<void()> falseAction,
                                        int id) {
    if (!m_manager) return 0;

    if (falseAction) {
        return m_manager->AddContextualHotkey(
            key, condition, trueAction, falseAction, id);
    } else {
        return m_manager->AddContextualHotkey(
            key, condition, trueAction, nullptr, id);
    }
}

// =========================================================================
// Advanced KeyTap functionality
// =========================================================================

std::shared_ptr<KeyTap> HotkeyService::createKeyTap(
    const std::string& keyName,
    std::function<void()> onTap,
    std::variant<std::string, std::function<bool()>> tapCondition,
    std::variant<std::string, std::function<bool()>> comboCondition,
    std::function<void()> onCombo,
    bool grabDown,
    bool grabUp
) {
    if (!m_manager) return nullptr;

    // Create KeyTap with shared_ptr to HotkeyManager
    auto keyTap = std::make_shared<KeyTap>(
        m_manager,  // Pass shared_ptr directly
        keyName,
        onTap,
        tapCondition,
        comboCondition,
        onCombo,
        grabDown,
        grabUp
    );

    // Setup the KeyTap
    keyTap->setup();

    return keyTap;
}

// =========================================================================
// Utility operations
// =========================================================================

void HotkeyService::toggleFakeDesktopOverlay() {
    if (m_manager) m_manager->toggleFakeDesktopOverlay();
}

void HotkeyService::showBlackOverlay() {
    if (m_manager) m_manager->showBlackOverlay();
}

void HotkeyService::printActiveWindowInfo() {
    if (m_manager) m_manager->printActiveWindowInfo();
}

void HotkeyService::toggleWindowFocusTracking() {
    if (m_manager) m_manager->toggleWindowFocusTracking();
}

void HotkeyService::updateAllConditionalHotkeys() {
    if (m_manager) m_manager->updateAllConditionalHotkeys();
}

std::string HotkeyService::getCurrentMode() const {
    return m_manager ? m_manager->getMode() : "";
}

void HotkeyService::setCurrentMode(const std::string& mode) {
    if (m_manager) m_manager->setMode(mode);
}

} } // namespace havel::host
